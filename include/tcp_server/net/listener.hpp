#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include <tcp_server/net/socket.hpp>

namespace tcp_server::net {

struct ListenerError {
    enum class Code {
        ResolveFailed,
        CreateFailed,
        SetSockOptFailed,
        BindFailed,
        ListenFailed,
    };

    Code code{};
    std::string message{};
    int native_error{};  // platform-specific error code when available
};

class Listener {
public:
    Listener() = default;
    explicit Listener(Socket s) : socket_(std::move(s)) {}

    [[nodiscard]] auto valid() const -> bool { return socket_.valid(); }
    [[nodiscard]] auto socket() -> Socket& { return socket_; }
    [[nodiscard]] auto socket() const -> const Socket& { return socket_; }

    // Creates, binds, and starts listening.
    //
    // - `host` may be empty to indicate "any".
    // - `port` may be 0 to request an ephemeral port.
    // - `backlog` is caller-provided (no hardcoded default).
    [[nodiscard]] static auto bind_and_listen(
        std::string_view host,
        std::uint16_t port,
        int backlog) -> std::expected<Listener, ListenerError>;

private:
    Socket socket_{};
};

}  // namespace tcp_server::net

