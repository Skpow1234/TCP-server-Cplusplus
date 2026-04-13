#pragma once

#include <tcp_server/protocol/frame_decoder.hpp>

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace tcp_server::net {
class Connection;
}

namespace tcp_server::protocol {

struct FrameEncodeError {
    enum class Code : std::uint8_t {
        PayloadTooLarge,
    };

    Code code{};
    std::string message{};
};

/// Appends one length-prefixed frame to `out` (big-endian u32 payload length, then payload).
/// Reserves `out.capacity()` growth up-front to avoid repeated reallocations for this frame.
/// Fails when `payload.size()` exceeds `max_payload_bytes` (capped by `UINT32_MAX` like the decoder).
[[nodiscard]] auto append_encoded_frame(
    std::vector<std::byte>& out,
    std::span<const std::byte> payload,
    std::uint64_t max_payload_bytes) -> std::expected<void, FrameEncodeError>;

/// Same as the vector overload, appending to `conn.write_buffer()`.
[[nodiscard]] auto append_encoded_frame(
    net::Connection& conn,
    std::span<const std::byte> payload,
    std::uint64_t max_payload_bytes) -> std::expected<void, FrameEncodeError>;

}  // namespace tcp_server::protocol
