#include <catch2/catch_test_macros.hpp>

#include <tcp_server/protocol/frame_decoder.hpp>

#include <limits>
#include <vector>

namespace {

using tcp_server::protocol::FrameDecodeResult;
using tcp_server::protocol::try_decode_frame;

void append_be32(std::vector<std::byte>& out, std::uint32_t v) {
    out.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
    out.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(v & 0xFF));
}

void append_payload(std::vector<std::byte>& out, std::size_t n, std::byte fill) {
    out.insert(out.end(), n, fill);
}

void consume_front(std::vector<std::byte>& buf, std::size_t n) {
    buf.erase(buf.begin(), buf.begin() + n);
}

}  // namespace

TEST_CASE("frame decoder: empty input is incomplete") {
    const std::vector<std::byte> buf{};
    const auto r = try_decode_frame(buf, 1024);
    REQUIRE(r.status == FrameDecodeResult::Status::Incomplete);
    REQUIRE(r.consumed_bytes == 0);
    REQUIRE(r.payload.empty());
}

TEST_CASE("frame decoder: need more than length prefix") {
    std::vector<std::byte> buf{std::byte{0}, std::byte{0}, std::byte{0}};
    auto r = try_decode_frame(buf, 1024);
    REQUIRE(r.status == FrameDecodeResult::Status::Incomplete);

    buf.push_back(std::byte{0});
    r = try_decode_frame(buf, 1024);
    REQUIRE(r.status == FrameDecodeResult::Status::Complete);
    REQUIRE(r.consumed_bytes == 4);
    REQUIRE(r.payload.empty());
}

TEST_CASE("frame decoder: complete frame with payload") {
    std::vector<std::byte> buf{};
    append_be32(buf, 3);
    buf.push_back(std::byte{'a'});
    buf.push_back(std::byte{'b'});
    buf.push_back(std::byte{'c'});

    const auto r = try_decode_frame(buf, 1024);
    REQUIRE(r.status == FrameDecodeResult::Status::Complete);
    REQUIRE(r.consumed_bytes == 7);
    REQUIRE(r.payload.size() == 3);
    REQUIRE(r.payload[0] == std::byte{'a'});
    REQUIRE(r.payload[1] == std::byte{'b'});
    REQUIRE(r.payload[2] == std::byte{'c'});
}

TEST_CASE("frame decoder: incomplete when payload not fully buffered") {
    std::vector<std::byte> buf{};
    append_be32(buf, 4);
    buf.push_back(std::byte{1});
    buf.push_back(std::byte{2});

    const auto r = try_decode_frame(buf, 1024);
    REQUIRE(r.status == FrameDecodeResult::Status::Incomplete);
    REQUIRE(r.consumed_bytes == 0);
}

TEST_CASE("frame decoder: oversized length rejected without large allocation") {
    std::vector<std::byte> buf{};
    append_be32(buf, 9999);

    const auto r = try_decode_frame(buf, 100);
    REQUIRE(r.status == FrameDecodeResult::Status::OversizedLength);
    REQUIRE(r.consumed_bytes == 0);
    REQUIRE(r.payload.empty());
}

TEST_CASE("frame decoder: length at max boundary accepted when bytes present") {
    std::vector<std::byte> buf{};
    append_be32(buf, 2);
    buf.push_back(std::byte{0x10});
    buf.push_back(std::byte{0x20});

    const auto r = try_decode_frame(buf, 2);
    REQUIRE(r.status == FrameDecodeResult::Status::Complete);
    REQUIRE(r.consumed_bytes == 6);
    REQUIRE(r.payload.size() == 2);
}

// --- Milestone 23: normal / fragmented / oversized coverage ---

TEST_CASE("frame decoder normal: large payload within limit") {
    constexpr std::uint32_t k_len = 400;
    std::vector<std::byte> buf{};
    append_be32(buf, k_len);
    append_payload(buf, k_len, std::byte{0x7E});

    const auto r = try_decode_frame(buf, 512);
    REQUIRE(r.status == FrameDecodeResult::Status::Complete);
    REQUIRE(r.consumed_bytes == tcp_server::protocol::k_frame_length_field_bytes + k_len);
    REQUIRE(r.payload.size() == k_len);
    REQUIRE(r.payload.front() == std::byte{0x7E});
    REQUIRE(r.payload.back() == std::byte{0x7E});
}

TEST_CASE("frame decoder normal: explicit big-endian length field") {
    std::vector<std::byte> buf{
        std::byte{0},
        std::byte{0},
        std::byte{0},
        std::byte{5},
        std::byte{1},
        std::byte{2},
        std::byte{3},
        std::byte{4},
        std::byte{5},
    };
    const auto r = try_decode_frame(buf, 64);
    REQUIRE(r.status == FrameDecodeResult::Status::Complete);
    REQUIRE(r.consumed_bytes == 9);
    REQUIRE(r.payload.size() == 5);
    REQUIRE(r.payload[0] == std::byte{1});
    REQUIRE(r.payload[4] == std::byte{5});
}

TEST_CASE("frame decoder normal: two frames back-to-back") {
    std::vector<std::byte> buf{};
    append_be32(buf, 1);
    buf.push_back(std::byte{'x'});
    append_be32(buf, 2);
    buf.push_back(std::byte{'y'});
    buf.push_back(std::byte{'z'});

    const auto first = try_decode_frame(buf, 64);
    REQUIRE(first.status == FrameDecodeResult::Status::Complete);
    REQUIRE(first.consumed_bytes == 5);
    REQUIRE(first.payload.size() == 1);
    REQUIRE(first.payload[0] == std::byte{'x'});

    consume_front(buf, first.consumed_bytes);
    const auto second = try_decode_frame(buf, 64);
    REQUIRE(second.status == FrameDecodeResult::Status::Complete);
    REQUIRE(second.consumed_bytes == 6);
    REQUIRE(second.payload.size() == 2);
    REQUIRE(second.payload[0] == std::byte{'y'});
    REQUIRE(second.payload[1] == std::byte{'z'});
}

TEST_CASE("frame decoder fragmented: byte-at-a-time until one frame complete") {
    std::vector<std::byte> stream{};
    const std::vector<std::byte> full_frame = [] {
        std::vector<std::byte> f{};
        append_be32(f, 2);
        f.push_back(std::byte{'m'});
        f.push_back(std::byte{'n'});
        return f;
    }();

    FrameDecodeResult last{};
    for (std::size_t i = 0; i < full_frame.size(); ++i) {
        stream.push_back(full_frame[i]);
        last = try_decode_frame(stream, 64);
        if (i + 1 < full_frame.size()) {
            REQUIRE(last.status == FrameDecodeResult::Status::Incomplete);
            REQUIRE(last.consumed_bytes == 0);
        }
    }
    REQUIRE(last.status == FrameDecodeResult::Status::Complete);
    REQUIRE(last.consumed_bytes == full_frame.size());
    REQUIRE(last.payload.size() == 2);
    REQUIRE(last.payload[0] == std::byte{'m'});
    REQUIRE(last.payload[1] == std::byte{'n'});
}

TEST_CASE("frame decoder fragmented: length complete before payload arrives") {
    std::vector<std::byte> buf{};
    append_be32(buf, 3);
    buf.push_back(std::byte{'a'});
    auto r = try_decode_frame(buf, 64);
    REQUIRE(r.status == FrameDecodeResult::Status::Incomplete);

    buf.push_back(std::byte{'b'});
    r = try_decode_frame(buf, 64);
    REQUIRE(r.status == FrameDecodeResult::Status::Incomplete);

    buf.push_back(std::byte{'c'});
    r = try_decode_frame(buf, 64);
    REQUIRE(r.status == FrameDecodeResult::Status::Complete);
    REQUIRE(r.consumed_bytes == 7);
    REQUIRE(r.payload.size() == 3);
}

TEST_CASE("frame decoder fragmented: extra bytes after first frame stay in buffer") {
    std::vector<std::byte> buf{};
    append_be32(buf, 1);
    buf.push_back(std::byte{'p'});
    append_be32(buf, 1);
    buf.push_back(std::byte{'q'});

    const auto r = try_decode_frame(buf, 64);
    REQUIRE(r.status == FrameDecodeResult::Status::Complete);
    REQUIRE(r.consumed_bytes == 5);

    consume_front(buf, r.consumed_bytes);
    REQUIRE(buf.size() == 5);
    const auto r2 = try_decode_frame(buf, 64);
    REQUIRE(r2.status == FrameDecodeResult::Status::Complete);
    REQUIRE(r2.payload.size() == 1);
    REQUIRE(r2.payload[0] == std::byte{'q'});
}

TEST_CASE("frame decoder oversized: one over limit with only length prefix present") {
    std::vector<std::byte> buf{};
    append_be32(buf, 50);
    const auto r = try_decode_frame(buf, 49);
    REQUIRE(r.status == FrameDecodeResult::Status::OversizedLength);
    REQUIRE(r.consumed_bytes == 0);
}

TEST_CASE("frame decoder oversized: excess declared length despite partial payload in buffer") {
    std::vector<std::byte> buf{};
    append_be32(buf, 1000);
    append_payload(buf, 50, std::byte{0});
    const auto r = try_decode_frame(buf, 64);
    REQUIRE(r.status == FrameDecodeResult::Status::OversizedLength);
    REQUIRE(r.consumed_bytes == 0);
}

TEST_CASE("frame decoder oversized: max zero allows only empty payload") {
    std::vector<std::byte> empty_ok{};
    append_be32(empty_ok, 0);
    const auto ok = try_decode_frame(empty_ok, 0);
    REQUIRE(ok.status == FrameDecodeResult::Status::Complete);
    REQUIRE(ok.consumed_bytes == 4);
    REQUIRE(ok.payload.empty());

    std::vector<std::byte> non_empty{};
    append_be32(non_empty, 1);
    non_empty.push_back(std::byte{0});
    const auto bad = try_decode_frame(non_empty, 0);
    REQUIRE(bad.status == FrameDecodeResult::Status::OversizedLength);
}

TEST_CASE("frame decoder oversized: huge u32 length rejected when cap is small") {
    std::vector<std::byte> buf{};
    append_be32(buf, std::numeric_limits<std::uint32_t>::max());
    const auto r = try_decode_frame(buf, 1024);
    REQUIRE(r.status == FrameDecodeResult::Status::OversizedLength);
    REQUIRE(r.consumed_bytes == 0);
}

TEST_CASE("frame decoder oversized: at wire limit vs policy cap") {
    std::vector<std::byte> buf{};
    append_be32(buf, 10);
    append_payload(buf, 10, std::byte{0x55});
    const auto r = try_decode_frame(buf, 10);
    REQUIRE(r.status == FrameDecodeResult::Status::Complete);
    REQUIRE(r.payload.size() == 10);
}
