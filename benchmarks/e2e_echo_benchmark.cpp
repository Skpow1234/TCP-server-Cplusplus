// End-to-end echo latency: in-process TCP server (same stack as integration echo test)
// with configurable concurrent clients. Uses manual wall-clock timing (UseManualTime).
//
// Examples:
//   ./benchmark_e2e_echo --benchmark_filter=EchoConcurrent --benchmark_min_time=0.2s
//   ./benchmark_e2e_echo --benchmark_filter=EchoSinglePipelined
//
// Args (EchoConcurrent): {concurrent_clients, round_trips_per_client, payload_bytes}

#include <benchmark/benchmark.h>

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

namespace e2e_echo_benchmark {

constexpr std::uint64_t k_max_payload = 1ULL << 20;
constexpr std::int32_t k_poll_ms = 50;
constexpr int k_accept_backlog = 128;

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

void try_flush_writes(ConnSlot& slot, tcp_server::net::SelectPoller& poller, std::atomic<int>& completions) {
    auto& conn = slot.conn;
    for (;;) {
        const auto sent = tcp_server::net::flush_write_nonblocking(conn);
        if (!sent.has_value()) {
            if (sent.error().code == tcp_server::net::SendError::Code::WouldBlock) {
                (void)poller.upsert(conn.native_handle(), tcp_server::net::EventMask::Read | tcp_server::net::EventMask::Write);
                return;
            }
            std::abort();
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
    std::array<std::byte, 16384>& scratch,
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
            std::abort();
        }

        for (;;) {
            const auto dec = tcp_server::protocol::try_decode_frame(conn.read_buffer(), k_max_payload);
            if (dec.status == tcp_server::protocol::FrameDecodeResult::Status::Incomplete) {
                break;
            }
            if (dec.status == tcp_server::protocol::FrameDecodeResult::Status::OversizedLength) {
                std::abort();
            }

            const auto out = echo.dispatch(std::span<const std::byte>(dec.payload.data(), dec.payload.size()));
            if (!out.has_value()) {
                std::abort();
            }
            if (!tcp_server::protocol::append_encoded_frame(conn.write_buffer(), *out, k_max_payload).has_value()) {
                std::abort();
            }
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
    auto bound = tcp_server::net::Listener::bind_and_listen("127.0.0.1", 0, k_accept_backlog);
    if (!bound.has_value() || !bound->valid()) {
        std::abort();
    }

    const auto prt = tcp_server::net::local_port_v4(bound->socket());
    if (!prt.has_value()) {
        std::abort();
    }
    server_port.store(*prt, std::memory_order_release);

    if (!bound->socket().set_nonblocking(true).has_value()) {
        std::abort();
    }

    tcp_server::net::SelectPoller poller;
    tcp_server::net::Acceptor acceptor{std::move(*bound)};
    if (!acceptor.register_listener(poller).has_value()) {
        std::abort();
    }

    listening.store(true, std::memory_order_release);

    tcp_server::app::EchoDispatcher echo{};
    std::vector<ConnSlot> slots;
    std::array<std::byte, 16384> scratch{};

    tcp_server::net::Event events[64]{};
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::minutes(5);

    while (clock::now() < deadline) {
        if (stop.load(std::memory_order_acquire) && slots.empty()) {
            break;
        }

        const auto n = poller.wait(events, k_poll_ms);
        if (!n.has_value()) {
            std::abort();
        }
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
                        if (!accept_would_block(accepted.error().native_error)) {
                            std::abort();
                        }
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
    if (!acceptor.unregister_listener(poller).has_value()) {
        std::abort();
    }
}

#if defined(_WIN32)
using SysSocket = SOCKET;
inline void close_sock(SysSocket s) {
    ::closesocket(s);
}
#else
using SysSocket = int;
inline void close_sock(SysSocket s) {
    ::close(s);
}
#endif

void send_all(SysSocket s, const void* data, std::size_t len) {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t off = 0;
    while (off < len) {
#if defined(_WIN32)
        const int n = ::send(s, bytes + off, static_cast<int>(len - off), 0);
        if (n == SOCKET_ERROR || n <= 0) {
            std::abort();
        }
        off += static_cast<std::size_t>(n);
#else
        const ssize_t n = ::send(s, bytes + off, len - off, 0);
        if (n <= 0) {
            std::abort();
        }
        off += static_cast<std::size_t>(n);
#endif
    }
}

[[nodiscard]] auto connect_loopback(std::uint16_t port) -> SysSocket {
#if defined(_WIN32)
    const SysSocket sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::abort();
    }
#else
    const SysSocket sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::abort();
    }
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::abort();
    }
    return sock;
}

[[nodiscard]] auto make_payload(std::size_t payload_bytes, int client_id) -> std::vector<std::byte> {
    std::vector<std::byte> p(payload_bytes);
    for (std::size_t i = 0; i < payload_bytes; ++i) {
        p[i] = static_cast<std::byte>(static_cast<unsigned char>((static_cast<int>(i) ^ client_id) & 0xFF));
    }
    return p;
}

void recv_one_echo_frame(SysSocket s, std::span<const std::byte> expected_payload) {
    std::vector<std::byte> acc;
    acc.resize(65536 + expected_payload.size());
    std::size_t total = 0;
    while (true) {
#if defined(_WIN32)
        const int n = ::recv(
            s,
            reinterpret_cast<char*>(acc.data() + total),
            static_cast<int>(acc.size() - total),
            0);
        if (n <= 0) {
            std::abort();
        }
        total += static_cast<std::size_t>(n);
#else
        const ssize_t n = ::recv(s, acc.data() + total, acc.size() - total, 0);
        if (n <= 0) {
            std::abort();
        }
        total += static_cast<std::size_t>(n);
#endif
        const auto dec = tcp_server::protocol::try_decode_frame({acc.data(), total}, k_max_payload);
        if (dec.status == tcp_server::protocol::FrameDecodeResult::Status::Complete) {
            if (dec.payload.size() != expected_payload.size()) {
                std::abort();
            }
            for (std::size_t i = 0; i < expected_payload.size(); ++i) {
                if (dec.payload[i] != expected_payload[i]) {
                    std::abort();
                }
            }
            return;
        }
        if (dec.status != tcp_server::protocol::FrameDecodeResult::Status::Incomplete) {
            std::abort();
        }
        if (total >= acc.size()) {
            std::abort();
        }
    }
}

void client_worker(std::uint16_t port, int client_id, int round_trips, std::size_t payload_bytes) {
    const auto payload = make_payload(payload_bytes, client_id);
    const SysSocket s = connect_loopback(port);
    for (int r = 0; r < round_trips; ++r) {
        std::vector<std::byte> wire_out;
        if (!tcp_server::protocol::append_encoded_frame(wire_out, payload, k_max_payload).has_value()) {
            std::abort();
        }
        send_all(s, wire_out.data(), wire_out.size());
        recv_one_echo_frame(s, payload);
    }
    close_sock(s);
}

}  // namespace e2e_echo_benchmark

struct EchoE2EFixture : benchmark::Fixture {
    tcp_server::net::NetworkSession net_{};
    std::atomic<std::uint16_t> server_port_{0};
    std::atomic<bool> listening_{false};
    std::atomic<bool> stop_{false};
    std::atomic<int> completions_{0};
    std::thread server_{};

    void SetUp(const benchmark::State&) override {
        if (!net_.ok()) {
            std::abort();
        }
        server_port_.store(0, std::memory_order_relaxed);
        listening_.store(false, std::memory_order_relaxed);
        stop_.store(false, std::memory_order_relaxed);
        completions_.store(0, std::memory_order_relaxed);

        server_ = std::thread{[this] {
            e2e_echo_benchmark::run_server(server_port_, listening_, stop_, completions_);
        }};

        while (!listening_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (server_port_.load(std::memory_order_acquire) == 0) {
            std::abort();
        }
    }

    void TearDown(const benchmark::State&) override {
        stop_.store(true, std::memory_order_release);
        if (server_.joinable()) {
            server_.join();
        }
    }
};

BENCHMARK_DEFINE_F(EchoE2EFixture, EchoConcurrent)(benchmark::State& state) {
    const int concurrency = static_cast<int>(state.range(0));
    const int rtts = static_cast<int>(state.range(1));
    const auto payload_bytes = static_cast<std::size_t>(state.range(2));
    if (concurrency <= 0 || rtts <= 0) {
        state.SkipWithError("invalid args");
        return;
    }

    const std::uint16_t port = server_port_.load(std::memory_order_acquire);
    const int expected_completions = concurrency * rtts;

    for (auto _ : state) {
        completions_.store(0, std::memory_order_relaxed);

        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::thread> workers;
        workers.reserve(static_cast<std::size_t>(concurrency));
        for (int i = 0; i < concurrency; ++i) {
            workers.emplace_back([port, i, rtts, payload_bytes] {
                e2e_echo_benchmark::client_worker(port, i, rtts, payload_bytes);
            });
        }
        for (auto& w : workers) {
            w.join();
        }
        const auto t1 = std::chrono::steady_clock::now();

        const auto sync_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (completions_.load(std::memory_order_acquire) != expected_completions
               && std::chrono::steady_clock::now() < sync_deadline) {
            std::this_thread::yield();
        }
        if (completions_.load(std::memory_order_acquire) != expected_completions) {
            state.SkipWithError("completion count mismatch");
            return;
        }

        state.SetIterationTime(std::chrono::duration<double>(t1 - t0).count());
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * expected_completions);
}
BENCHMARK_REGISTER_F(EchoE2EFixture, EchoConcurrent)
    ->Name("EchoConcurrent/wall_clients_rtts_payloadB")
    ->Args({1, 1, 64})
    ->Args({1, 32, 64})
    ->Args({4, 4, 64})
    ->Args({8, 2, 64})
    ->Args({16, 1, 64})
    ->Args({4, 1, 4096})
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);

BENCHMARK_DEFINE_F(EchoE2EFixture, EchoSinglePipelined)(benchmark::State& state) {
    const int rtts = static_cast<int>(state.range(0));
    const auto payload_bytes = static_cast<std::size_t>(state.range(1));
    if (rtts <= 0) {
        state.SkipWithError("invalid args");
        return;
    }

    const std::uint16_t port = server_port_.load(std::memory_order_acquire);

    for (auto _ : state) {
        completions_.store(0, std::memory_order_relaxed);

        const auto t0 = std::chrono::steady_clock::now();
        e2e_echo_benchmark::client_worker(port, 0, rtts, payload_bytes);
        const auto t1 = std::chrono::steady_clock::now();

        const auto sync_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (completions_.load(std::memory_order_acquire) != rtts
               && std::chrono::steady_clock::now() < sync_deadline) {
            std::this_thread::yield();
        }
        if (completions_.load(std::memory_order_acquire) != rtts) {
            state.SkipWithError("completion count mismatch");
            return;
        }

        state.SetIterationTime(std::chrono::duration<double>(t1 - t0).count());
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * rtts);
}
BENCHMARK_REGISTER_F(EchoE2EFixture, EchoSinglePipelined)
    ->Name("EchoSinglePipelined/wall_rtts_payloadB")
    ->Args({1, 64})
    ->Args({32, 64})
    ->Args({128, 64})
    ->Args({32, 4096})
    ->UseManualTime()
    ->Unit(benchmark::kMillisecond);
