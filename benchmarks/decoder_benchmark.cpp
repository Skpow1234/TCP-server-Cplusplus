#include <benchmark/benchmark.h>

#include <tcp_server/protocol/frame_decoder.hpp>
#include <tcp_server/protocol/frame_encoder.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using tcp_server::protocol::append_encoded_frame;
using tcp_server::protocol::FrameDecodeResult;
using tcp_server::protocol::try_decode_frame;

// Large enough for all benchmark payload sizes (encoder/decoder cap at UINT32_MAX).
constexpr std::uint64_t k_max_payload_bytes = 1ULL << 20;

[[nodiscard]] auto make_wire(std::size_t payload_bytes) -> std::vector<std::byte> {
    std::vector<std::byte> payload(payload_bytes);
    for (std::size_t i = 0; i < payload_bytes; ++i) {
        payload[i] = static_cast<std::byte>(static_cast<unsigned char>(i & 0xFFU));
    }
    std::vector<std::byte> wire;
    wire.reserve(tcp_server::protocol::k_frame_length_field_bytes + payload_bytes);
    const auto ok = append_encoded_frame(wire, payload, k_max_payload_bytes);
    if (!ok.has_value()) {
        return {};
    }
    return wire;
}

[[nodiscard]] auto make_stream(std::size_t payload_bytes, int frame_count) -> std::vector<std::byte> {
    std::vector<std::byte> payload(payload_bytes);
    for (std::size_t i = 0; i < payload_bytes; ++i) {
        payload[i] = static_cast<std::byte>(static_cast<unsigned char>((i ^ 0x5AU) & 0xFFU));
    }
    std::vector<std::byte> wire;
    wire.reserve(static_cast<std::size_t>(frame_count)
        * (tcp_server::protocol::k_frame_length_field_bytes + payload_bytes));
    for (int f = 0; f < frame_count; ++f) {
        if (payload_bytes > 0) {
            payload[0] = static_cast<std::byte>(static_cast<unsigned char>(f & 0xFF));
        }
        if (!append_encoded_frame(wire, payload, k_max_payload_bytes).has_value()) {
            return {};
        }
    }
    return wire;
}

template <std::size_t PayloadBytes>
static void BM_decode_single_frame(benchmark::State& state) {
    const auto wire = make_wire(PayloadBytes);
    if (wire.size() < tcp_server::protocol::k_frame_length_field_bytes) {
        state.SkipWithError("encode failed");
        return;
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(wire.data());
        const auto r = try_decode_frame(wire, k_max_payload_bytes);
        if (r.status != FrameDecodeResult::Status::Complete) {
            state.SkipWithError("decode not complete");
            return;
        }
        benchmark::DoNotOptimize(r.payload.data());
        benchmark::DoNotOptimize(r.payload.size());
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations())
        * static_cast<std::int64_t>(wire.size()));
}

static void BM_decode_stream(benchmark::State& state) {
    const auto payload_bytes = static_cast<std::size_t>(state.range(0));
    const int frame_count = static_cast<int>(state.range(1));
    const auto wire = make_stream(payload_bytes, frame_count);
    if (wire.empty() && frame_count > 0) {
        state.SkipWithError("encode stream failed");
        return;
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(wire.data());
        std::size_t off = 0;
        int decoded = 0;
        while (off < wire.size()) {
            const auto r = try_decode_frame({wire.data() + off, wire.size() - off}, k_max_payload_bytes);
            if (r.status != FrameDecodeResult::Status::Complete) {
                state.SkipWithError("decode stream incomplete");
                return;
            }
            off += r.consumed_bytes;
            ++decoded;
        }
        if (decoded != frame_count) {
            state.SkipWithError("decode stream frame count mismatch");
            return;
        }
        benchmark::DoNotOptimize(decoded);
        benchmark::DoNotOptimize(off);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations())
        * static_cast<std::int64_t>(wire.size()));
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * frame_count);
}

}  // namespace

BENCHMARK_TEMPLATE(BM_decode_single_frame, 0)->Name("decode_single/payload=0");
BENCHMARK_TEMPLATE(BM_decode_single_frame, 1)->Name("decode_single/payload=1");
BENCHMARK_TEMPLATE(BM_decode_single_frame, 8)->Name("decode_single/payload=8");
BENCHMARK_TEMPLATE(BM_decode_single_frame, 64)->Name("decode_single/payload=64");
BENCHMARK_TEMPLATE(BM_decode_single_frame, 256)->Name("decode_single/payload=256");
BENCHMARK_TEMPLATE(BM_decode_single_frame, 1024)->Name("decode_single/payload=1024");
BENCHMARK_TEMPLATE(BM_decode_single_frame, 4096)->Name("decode_single/payload=4096");
BENCHMARK_TEMPLATE(BM_decode_single_frame, 16384)->Name("decode_single/payload=16384");
BENCHMARK_TEMPLATE(BM_decode_single_frame, 65535)->Name("decode_single/payload=65535");

BENCHMARK(BM_decode_stream)
    ->Name("decode_stream/frames=256")
    ->Args({0, 256})
    ->Args({1, 256})
    ->Args({64, 256})
    ->Args({1024, 256})
    ->Args({4096, 256})
    ->Args({1, 4096})
    ->Args({64, 4096});
