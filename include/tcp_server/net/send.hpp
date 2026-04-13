#pragma once

#include <cstddef>
#include <expected>
#include <string>

#include <tcp_server/net/connection.hpp>

namespace tcp_server::net {

struct SendError {
    enum class Code {
        WouldBlock,
        IoError,
    };

    Code code{};
    std::string message{};
    int native_error{};
};

/// Sends as much as possible from the front of `conn.write_buffer()` without blocking.
/// On success, removes the sent prefix from the write buffer (partial sends are handled).
/// Returns bytes sent (0 if the write buffer was already empty).
///
/// When `Code::WouldBlock` is returned, the caller should wait for a write-ready poll event
/// and call this again.
[[nodiscard]] auto flush_write_nonblocking(Connection& conn) -> std::expected<std::size_t, SendError>;

}  // namespace tcp_server::net
