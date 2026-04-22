#include <tcp_server/protocol/frame_encoder.hpp>

#include <tcp_server/net/connection.hpp>

#include <algorithm>
#include <cassert>
#include <limits>

namespace tcp_server::protocol {
namespace {

[[nodiscard]] auto effective_max_payload(std::uint64_t max_payload_bytes) -> std::uint64_t {
    return (std::min)(max_payload_bytes, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
}

void push_be32(std::vector<std::byte>& out, std::uint32_t v) {
    out.push_back(static_cast<std::byte>((v >> 24U) & 0xFFU));
    out.push_back(static_cast<std::byte>((v >> 16U) & 0xFFU));
    out.push_back(static_cast<std::byte>((v >> 8U) & 0xFFU));
    out.push_back(static_cast<std::byte>(v & 0xFFU));
}

}  // namespace

auto append_encoded_frame(
    std::vector<std::byte>& out,
    std::span<const std::byte> payload,
    std::uint64_t max_payload_bytes) -> std::expected<void, FrameEncodeError> {
    const std::uint64_t cap = effective_max_payload(max_payload_bytes);
    if (payload.size() > cap) {
        return std::unexpected(FrameEncodeError{
            FrameEncodeError::Code::PayloadTooLarge,
            "payload exceeds configured max message size",
        });
    }

    const auto len = static_cast<std::uint32_t>(payload.size());
    const auto extra = k_frame_length_field_bytes + payload.size();
    if (out.capacity() < out.size() + extra) {
        out.reserve(out.size() + extra);
    }
    const std::size_t before = out.size();
    push_be32(out, len);
    out.insert(out.end(), payload.begin(), payload.end());
    assert(out.size() == before + k_frame_length_field_bytes + payload.size());
    return {};
}

auto append_encoded_frame(
    net::Connection& conn,
    std::span<const std::byte> payload,
    std::uint64_t max_payload_bytes) -> std::expected<void, FrameEncodeError> {
    return append_encoded_frame(conn.write_buffer(), payload, max_payload_bytes);
}

}  // namespace tcp_server::protocol
