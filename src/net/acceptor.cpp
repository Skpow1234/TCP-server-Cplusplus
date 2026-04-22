#include <tcp_server/net/acceptor.hpp>

#include <memory>
#include <string>
#include <string_view>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <cerrno>
#    include <cstring>
#    include <netdb.h>
#    include <sys/socket.h>
#endif

namespace tcp_server::net {
namespace {

[[nodiscard]] auto make_err(ListenerError::Code code, std::string msg, int native_error) -> ListenerError {
    return ListenerError{code, std::move(msg), native_error};
}

struct AddrInfoDeleter {
    void operator()(addrinfo* p) const noexcept {
        if (p != nullptr) {
            ::freeaddrinfo(p);
        }
    }
};

}  // namespace

auto Listener::bind_and_listen(std::string_view host, std::uint16_t port, int backlog)
    -> std::expected<Listener, ListenerError> {
    if (backlog <= 0) {
        return std::unexpected(make_err(ListenerError::Code::ListenFailed, "backlog must be > 0", 0));
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    const auto host_str = std::string(host);
    const auto port_str = std::to_string(port);

    addrinfo* raw = nullptr;
    const int gai_rc = ::getaddrinfo(
        host.empty() ? nullptr : host_str.c_str(),
        port_str.c_str(),
        &hints,
        &raw);
    std::unique_ptr<addrinfo, AddrInfoDeleter> res{raw};
    if (gai_rc != 0) {
#if defined(_WIN32)
        return std::unexpected(make_err(ListenerError::Code::ResolveFailed, "getaddrinfo() failed", gai_rc));
#else
        return std::unexpected(make_err(ListenerError::Code::ResolveFailed, ::gai_strerror(gai_rc), gai_rc));
#endif
    }

    for (auto* ai = res.get(); ai != nullptr; ai = ai->ai_next) {
#if defined(_WIN32)
        const auto s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == INVALID_SOCKET) {
            continue;
        }
        Socket sock(static_cast<NativeSocket>(s));

        BOOL yes = TRUE;
        if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes)) != 0) {
            const auto err = ::WSAGetLastError();
            return std::unexpected(make_err(ListenerError::Code::SetSockOptFailed, "setsockopt(SO_REUSEADDR) failed", err));
        }

        if (::bind(s, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) != 0) {
            (void)sock.close();
            continue;
        }
        if (::listen(s, backlog) != 0) {
            const auto err = ::WSAGetLastError();
            return std::unexpected(make_err(ListenerError::Code::ListenFailed, "listen() failed", err));
        }
        return Listener{std::move(sock)};
#else
        const int s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s < 0) {
            continue;
        }
        Socket sock{s};

        int yes = 1;
        if (::setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
            const int err = errno;
            return std::unexpected(make_err(ListenerError::Code::SetSockOptFailed, "setsockopt(SO_REUSEADDR) failed", err));
        }

        if (::bind(s, ai->ai_addr, ai->ai_addrlen) != 0) {
            [[maybe_unused]] const auto close_result = sock.close();
            continue;
        }
        if (::listen(s, backlog) != 0) {
            const int err = errno;
            return std::unexpected(make_err(ListenerError::Code::ListenFailed, "listen() failed", err));
        }
        return Listener{std::move(sock)};
#endif
    }

#if defined(_WIN32)
    return std::unexpected(make_err(ListenerError::Code::BindFailed, "bind() failed for all resolved addresses", ::WSAGetLastError()));
#else
    return std::unexpected(make_err(ListenerError::Code::BindFailed, "bind() failed for all resolved addresses", errno));
#endif
}

Acceptor::Acceptor(Listener listener) : listener_(std::move(listener)) {}

auto Acceptor::register_listener(Poller& poller) -> std::expected<void, PollError> {
    return poller.upsert(listener_handle(), EventMask::Read);
}

auto Acceptor::unregister_listener(Poller& poller) -> std::expected<void, PollError> {
    return poller.erase(listener_handle());
}

auto Acceptor::accept_and_register(Poller& poller) -> std::expected<Connection, AcceptError> {
    auto accepted = accept_one(listener_.socket());
    if (!accepted) {
        return std::unexpected(accepted.error());
    }
    Connection conn{std::move(*accepted)};
    if (auto nb = conn.socket().set_nonblocking(true); !nb) {
        return std::unexpected(AcceptError{
            "failed to set non-blocking mode on accepted socket: " + nb.error().message,
            nb.error().native_error,
        });
    }
    if (auto reg = poller.upsert(conn.native_handle(), EventMask::Read); !reg) {
        return std::unexpected(AcceptError{
            "failed to register accepted socket: " + reg.error().message,
            reg.error().native_error,
        });
    }
    return conn;
}

}  // namespace tcp_server::net

