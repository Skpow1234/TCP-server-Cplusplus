#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include <tcp_server/net/socket.hpp>

namespace tcp_server::net {

struct AcceptError {
    std::string message{};
    int native_error{};  // platform-specific error code when available
};

// Accepts a single connection from a listening socket.
[[nodiscard]] auto accept_one(const Socket& listener) -> std::expected<Socket, AcceptError>;

// Returns the locally-bound TCP port of the listening socket.
[[nodiscard]] auto local_port_v4(const Socket& listener) -> std::expected<std::uint16_t, AcceptError>;

}  // namespace tcp_server::net

