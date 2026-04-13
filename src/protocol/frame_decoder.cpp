#include <tcp_server/protocol/frame_decoder.hpp>

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

namespace tcp_server::protocol {
namespace {

[[nodiscard]] auto read_u32_be(const std::byte* p) -> std::uint32_t {
    std::uint32_t x{};
    std::memcpy(&x, p, sizeof(x));
    if constexpr (std::endian::native == std::endian::little) {
        return std::byteswap(x);
    }
    return x;
}

[[nodiscard]] auto effective_max_payload(std::uint64_t max_payload_bytes) -> std::uint64_t {
    return (std::min)(max_payload_bytes, static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()));
}

}  // namespace

auto try_decode_frame(std::span<const std::byte> buffer, std::uint64_t max_payload_bytes) -> FrameDecodeResult {
    if (buffer.size() < k_frame_length_field_bytes) {
        return FrameDecodeResult{.status = FrameDecodeResult::Status::Incomplete};
    }

    const std::uint64_t payload_len = read_u32_be(buffer.data());
    const std::uint64_t cap = effective_max_payload(max_payload_bytes);
    if (payload_len > cap) {
        return FrameDecodeResult{.status = FrameDecodeResult::Status::OversizedLength};
    }

    const std::size_t total = k_frame_length_field_bytes + static_cast<std::size_t>(payload_len);
    if (buffer.size() < total) {
        return FrameDecodeResult{.status = FrameDecodeResult::Status::Incomplete};
    }

    FrameDecodeResult out{};
    out.status = FrameDecodeResult::Status::Complete;
    out.consumed_bytes = total;
    if (payload_len > 0) {
        out.payload.resize(static_cast<std::size_t>(payload_len));
        const auto* src = buffer.data() + k_frame_length_field_bytes;
        std::copy_n(src, out.payload.size(), out.payload.begin());
    }
    return out;
}

}  // namespace tcp_server::protocol
