#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string>

#include <tcp_server/net/connection.hpp>

namespace tcp_server::net {

struct RecvError {
    enum class Code {
        WouldBlock,
        Closed,
        IoError,
        InvalidArgument,
    };

    Code code{};
    std::string message{};
    int native_error{};
};

/// Reads available bytes without blocking (socket must be non-blocking).
/// Appends into `conn.read_buffer()` and returns how many bytes were appended.
[[nodiscard]] auto receive_nonblocking(Connection& conn, std::span<std::byte> scratch)
    -> std::expected<std::size_t, RecvError>;

}  // namespace tcp_server::net
