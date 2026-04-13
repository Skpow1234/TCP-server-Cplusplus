#pragma once

#include <expected>

#include <tcp_server/net/accept.hpp>
#include <tcp_server/net/connection.hpp>
#include <tcp_server/net/event_loop.hpp>
#include <tcp_server/net/listener.hpp>

namespace tcp_server::net {

/// Accepts connections from a bound `Listener` and registers new client sockets on a `Poller`.
class Acceptor {
public:
    explicit Acceptor(Listener listener);

    Acceptor(const Acceptor&) = delete;
    Acceptor& operator=(const Acceptor&) = delete;
    Acceptor(Acceptor&&) noexcept = default;
    Acceptor& operator=(Acceptor&&) noexcept = default;

    ~Acceptor() = default;

    [[nodiscard]] auto listener_socket() const -> const Socket& { return listener_.socket(); }
    [[nodiscard]] auto listener_handle() const -> NativeSocket { return listener_.socket().native_handle(); }

    /// Subscribe the listening socket for read readiness (incoming connections).
    auto register_listener(Poller& poller) -> std::expected<void, PollError>;

    /// Remove the listening socket from the poller.
    auto unregister_listener(Poller& poller) -> std::expected<void, PollError>;

    /// Accept one pending connection and register it for read on `poller`.
    auto accept_and_register(Poller& poller) -> std::expected<Connection, AcceptError>;

private:
    Listener listener_{};
};

}  // namespace tcp_server::net
