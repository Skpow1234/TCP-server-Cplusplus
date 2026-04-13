#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace tcp_server::protocol {

/// Big-endian u32 length prefix (payload only), then `length` bytes of payload.
inline constexpr std::size_t k_frame_length_field_bytes = 4;

struct FrameDecodeResult {
    enum class Status : std::uint8_t {
        /// Fewer than 4 bytes, or fewer than 4 + length bytes available.
        Incomplete,
        /// Extracted one frame; `consumed_bytes` is 4 + payload size.
        Complete,
        /// Declared payload length exceeds `max_payload_bytes`; `consumed_bytes` is 0.
        OversizedLength,
    };

    Status status{Status::Incomplete};
    std::size_t consumed_bytes{};
    std::vector<std::byte> payload{};
};

/// Attempts to decode a single frame from the front of `buffer`.
///
/// Does not allocate the declared payload size until the length is validated and
/// the full frame is present. On `OversizedLength`, the caller should treat the
/// connection as failed and discard buffered input.
[[nodiscard]] auto try_decode_frame(std::span<const std::byte> buffer, std::uint64_t max_payload_bytes)
    -> FrameDecodeResult;

}  // namespace tcp_server::protocol
