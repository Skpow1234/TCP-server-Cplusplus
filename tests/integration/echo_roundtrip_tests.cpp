#include <tcp_server/app/dispatcher.hpp>
#include <tcp_server/net/acceptor.hpp>
#include <tcp_server/net/listener.hpp>
#include <tcp_server/net/recv.hpp>
#include <tcp_server/net/select_poller.hpp>
#include <tcp_server/net/send.hpp>
#include <tcp_server/net/socket.hpp>
#include <tcp_server/protocol/frame_decoder.hpp>
#include <tcp_server/protocol/frame_encoder.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <cerrno>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

namespace {

constexpr int k_num_clients = 4;
constexpr std::uint64_t k_max_payload = 4096;
constexpr std::int32_t k_poll_ms = 50;

#if defined(_WIN32)
[[nodiscard]] auto accept_would_block(int err) -> bool {
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
}
#else
[[nodiscard]] auto accept_would_block(int err) -> bool {
    return err == EAGAIN || err == EWOULDBLOCK;
}
#endif

struct ConnSlot {
    tcp_server::net::Connection conn;
    bool awaiting_reply_flush{false};
};

enum class ReadOutcome : std::uint8_t {
    Ok,
    PeerClosed,
};

[[nodiscard]] auto find_slot_index(const std::vector<ConnSlot>& slots, tcp_server::net::NativeSocket h) -> std::size_t {
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (slots[i].conn.native_handle() == h) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

void remove_slot(std::vector<ConnSlot>& slots, std::size_t i, tcp_server::net::SelectPoller& poller) {
    (void)poller.erase(slots[i].conn.native_handle());
    if (i + 1 < slots.size()) {
        slots[i] = std::move(slots.back());
    }
    slots.pop_back();
}

void try_flush_writes(
    ConnSlot& slot,
    tcp_server::net::SelectPoller& poller,
    std::atomic<int>& completions) {
    auto& conn = slot.conn;
    for (;;) {
        const auto sent = tcp_server::net::flush_write_nonblocking(conn);
        if (!sent.has_value()) {
            if (sent.error().code == tcp_server::net::SendError::Code::WouldBlock) {
                (void)poller.upsert(conn.native_handle(), tcp_server::net::EventMask::Read | tcp_server::net::EventMask::Write);
                return;
            }
            assert(false && "flush_write_nonblocking failed");
        }
        if (*sent == 0) {
            break;
        }
    }
    if (conn.write_buffer().empty()) {
        if (slot.awaiting_reply_flush) {
            slot.awaiting_reply_flush = false;
            completions.fetch_add(1, std::memory_order_relaxed);
        }
        (void)poller.upsert(conn.native_handle(), tcp_server::net::EventMask::Read);
    } else {
        (void)poller.upsert(conn.native_handle(), tcp_server::net::EventMask::Read | tcp_server::net::EventMask::Write);
    }
}

auto process_connection_read(
    std::size_t slot_index,
    std::vector<ConnSlot>& slots,
    tcp_server::net::SelectPoller& poller,
    tcp_server::app::EchoDispatcher& echo,
    std::array<std::byte, 4096>& scratch,
    std::atomic<int>& completions) -> ReadOutcome {
    auto& slot = slots[slot_index];
    auto& conn = slot.conn;

    for (;;) {
        const auto got = tcp_server::net::receive_nonblocking(conn, scratch);
        if (!got.has_value()) {
            if (got.error().code == tcp_server::net::RecvError::Code::WouldBlock) {
                return ReadOutcome::Ok;
            }
            if (got.error().code == tcp_server::net::RecvError::Code::Closed) {
                conn.read_buffer().clear();
                conn.clear_write();
                remove_slot(slots, slot_index, poller);
                return ReadOutcome::PeerClosed;
            }
            assert(false && "receive_nonblocking failed");
        }

        for (;;) {
            const auto dec = tcp_server::protocol::try_decode_frame(conn.read_buffer(), k_max_payload);
            if (dec.status == tcp_server::protocol::FrameDecodeResult::Status::Incomplete) {
                break;
            }
            if (dec.status == tcp_server::protocol::FrameDecodeResult::Status::OversizedLength) {
                assert(false && "oversized frame in integration test");
            }

            const auto out = echo.dispatch(std::span<const std::byte>(dec.payload.data(), dec.payload.size()));
            assert(out.has_value());
            assert(tcp_server::protocol::append_encoded_frame(conn.write_buffer(), *out, k_max_payload).has_value());
            conn.consume_read(dec.consumed_bytes);

            slot.awaiting_reply_flush = true;
            (void)poller.upsert(conn.native_handle(), tcp_server::net::EventMask::Read | tcp_server::net::EventMask::Write);
            try_flush_writes(slot, poller, completions);
        }
    }
}

void run_server(
    std::atomic<std::uint16_t>& server_port,
    std::atomic<bool>& listening,
    std::atomic<bool>& stop,
    std::atomic<int>& completions) {
    auto bound = tcp_server::net::Listener::bind_and_listen("127.0.0.1", 0, 32);
    assert(bound.has_value());
    assert(bound->valid());

    const auto prt = tcp_server::net::local_port_v4(bound->socket());
    assert(prt.has_value());
    server_port.store(*prt, std::memory_order_release);

    assert(bound->socket().set_nonblocking(true).has_value());

    tcp_server::net::SelectPoller poller;
    tcp_server::net::Acceptor acceptor{std::move(*bound)};
    assert(acceptor.register_listener(poller).has_value());

    listening.store(true, std::memory_order_release);

    tcp_server::app::EchoDispatcher echo{};
    std::vector<ConnSlot> slots;
    std::array<std::byte, 4096> scratch{};

    tcp_server::net::Event events[32]{};
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::seconds(30);

    while (clock::now() < deadline) {
        if (stop.load(std::memory_order_acquire) && slots.empty()) {
            break;
        }

        const auto n = poller.wait(events, k_poll_ms);
        assert(n.has_value());
        const std::size_t count = *n;

        for (std::size_t ei = 0; ei < count; ++ei) {
            const auto& ev = events[ei];

            if (ev.socket == acceptor.listener_handle()) {
                if ((ev.mask & tcp_server::net::EventMask::Read) == tcp_server::net::EventMask::None) {
                    continue;
                }
                for (;;) {
                    auto accepted = acceptor.accept_and_register(poller);
                    if (!accepted.has_value()) {
                        assert(accept_would_block(accepted.error().native_error) && "accept failed unexpectedly");
                        break;
                    }
                    slots.push_back(ConnSlot{std::move(*accepted), false});
                }
                continue;
            }

            std::size_t idx = find_slot_index(slots, ev.socket);
            if (idx == static_cast<std::size_t>(-1)) {
                continue;
            }

            if ((ev.mask & tcp_server::net::EventMask::Read) != tcp_server::net::EventMask::None) {
                if (process_connection_read(idx, slots, poller, echo, scratch, completions) == ReadOutcome::PeerClosed) {
                    continue;
                }
                idx = find_slot_index(slots, ev.socket);
                if (idx == static_cast<std::size_t>(-1)) {
                    continue;
                }
            }

            if ((ev.mask & tcp_server::net::EventMask::Write) != tcp_server::net::EventMask::None) {
                const std::size_t widx = find_slot_index(slots, ev.socket);
                if (widx == static_cast<std::size_t>(-1)) {
                    continue;
                }
                try_flush_writes(slots[widx], poller, completions);
            }
        }
    }

    while (!slots.empty()) {
        (void)poller.erase(slots.back().conn.native_handle());
        slots.pop_back();
    }
    assert(acceptor.unregister_listener(poller).has_value());
}

#if defined(_WIN32)
using SysSocket = SOCKET;
void close_sock(SysSocket s) {
    ::closesocket(s);
}
#else
using SysSocket = int;
void close_sock(SysSocket s) {
    ::close(s);
}
#endif

void send_all(SysSocket s, const char* data, std::size_t len) {
    std::size_t off = 0;
    while (off < len) {
#if defined(_WIN32)
        const int n = ::send(s, data + off, static_cast<int>(len - off), 0);
        assert(n != SOCKET_ERROR && n > 0);
        off += static_cast<std::size_t>(n);
#else
        const ssize_t n = ::send(s, data + off, len - off, 0);
        assert(n > 0);
        off += static_cast<std::size_t>(n);
#endif
    }
}

void client_session(std::uint16_t port, int id) {
#if defined(_WIN32)
    const SysSocket s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(s != INVALID_SOCKET);
#else
    const SysSocket s = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(s >= 0);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);

    const std::string payload = "hello from client " + std::to_string(id);
    std::vector<std::byte> wire_out{};
    assert(tcp_server::protocol::append_encoded_frame(
               wire_out,
               std::as_bytes(std::span<const char>(payload.data(), payload.size())),
               k_max_payload)
               .has_value());

    send_all(s, reinterpret_cast<const char*>(wire_out.data()), wire_out.size());

    std::vector<std::byte> acc;
    acc.resize(4096);
    std::size_t total = 0;
    while (true) {
#if defined(_WIN32)
        const int n = ::recv(s, reinterpret_cast<char*>(acc.data() + total), static_cast<int>(acc.size() - total), 0);
        assert(n > 0);
        total += static_cast<std::size_t>(n);
#else
        const ssize_t n = ::recv(s, acc.data() + total, acc.size() - total, 0);
        assert(n > 0);
        total += static_cast<std::size_t>(n);
#endif
        const auto dec = tcp_server::protocol::try_decode_frame({acc.data(), total}, k_max_payload);
        if (dec.status == tcp_server::protocol::FrameDecodeResult::Status::Complete) {
            assert(dec.payload.size() == payload.size());
            for (std::size_t i = 0; i < payload.size(); ++i) {
                assert(dec.payload[i] == static_cast<std::byte>(static_cast<unsigned char>(payload[i])));
            }
            break;
        }
        assert(dec.status == tcp_server::protocol::FrameDecodeResult::Status::Incomplete);
        assert(total < acc.size());
    }

    close_sock(s);
}

}  // namespace

int main() {
    tcp_server::net::NetworkSession net;
    assert(net.ok());

    std::atomic<std::uint16_t> server_port{0};
    std::atomic<bool> listening{false};
    std::atomic<bool> stop{false};
    std::atomic<int> completions{0};

    std::thread server_thread([&] { run_server(server_port, listening, stop, completions); });

    while (!listening.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const std::uint16_t port = server_port.load(std::memory_order_acquire);
    assert(port != 0);

    std::vector<std::thread> clients;
    clients.reserve(k_num_clients);
    for (int i = 0; i < k_num_clients; ++i) {
        clients.emplace_back([port, i] { client_session(port, i); });
    }
    for (auto& t : clients) {
        t.join();
    }

    assert(completions.load(std::memory_order_relaxed) == k_num_clients);

    stop.store(true, std::memory_order_release);
    server_thread.join();
    return 0;
}
