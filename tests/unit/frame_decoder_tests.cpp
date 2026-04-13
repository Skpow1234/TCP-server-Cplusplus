#include <catch2/catch_test_macros.hpp>

#include <tcp_server/protocol/frame_decoder.hpp>

#include <vector>

namespace {

void append_be32(std::vector<std::byte>& out, std::uint32_t v) {
    out.push_back(static_cast<std::byte>((v >> 24) & 0xFF));
    out.push_back(static_cast<std::byte>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::byte>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(v & 0xFF));
}

}  // namespace

TEST_CASE("frame decoder: empty input is incomplete") {
    const std::vector<std::byte> buf{};
    const auto r = tcp_server::protocol::try_decode_frame(buf, 1024);
    REQUIRE(r.status == tcp_server::protocol::FrameDecodeResult::Status::Incomplete);
    REQUIRE(r.consumed_bytes == 0);
    REQUIRE(r.payload.empty());
}

TEST_CASE("frame decoder: need more than length prefix") {
    std::vector<std::byte> buf{std::byte{0}, std::byte{0}, std::byte{0}};
    auto r = tcp_server::protocol::try_decode_frame(buf, 1024);
    REQUIRE(r.status == tcp_server::protocol::FrameDecodeResult::Status::Incomplete);

    buf.push_back(std::byte{0});
    r = tcp_server::protocol::try_decode_frame(buf, 1024);
    REQUIRE(r.status == tcp_server::protocol::FrameDecodeResult::Status::Complete);
    REQUIRE(r.consumed_bytes == 4);
    REQUIRE(r.payload.empty());
}

TEST_CASE("frame decoder: complete frame with payload") {
    std::vector<std::byte> buf{};
    append_be32(buf, 3);
    buf.push_back(std::byte{'a'});
    buf.push_back(std::byte{'b'});
    buf.push_back(std::byte{'c'});

    const auto r = tcp_server::protocol::try_decode_frame(buf, 1024);
    REQUIRE(r.status == tcp_server::protocol::FrameDecodeResult::Status::Complete);
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

    const auto r = tcp_server::protocol::try_decode_frame(buf, 1024);
    REQUIRE(r.status == tcp_server::protocol::FrameDecodeResult::Status::Incomplete);
    REQUIRE(r.consumed_bytes == 0);
}

TEST_CASE("frame decoder: oversized length rejected without large allocation") {
    std::vector<std::byte> buf{};
    append_be32(buf, 9999);

    const auto r = tcp_server::protocol::try_decode_frame(buf, 100);
    REQUIRE(r.status == tcp_server::protocol::FrameDecodeResult::Status::OversizedLength);
    REQUIRE(r.consumed_bytes == 0);
    REQUIRE(r.payload.empty());
}

TEST_CASE("frame decoder: length at max boundary accepted when bytes present") {
    std::vector<std::byte> buf{};
    append_be32(buf, 2);
    buf.push_back(std::byte{0x10});
    buf.push_back(std::byte{0x20});

    const auto r = tcp_server::protocol::try_decode_frame(buf, 2);
    REQUIRE(r.status == tcp_server::protocol::FrameDecodeResult::Status::Complete);
    REQUIRE(r.consumed_bytes == 6);
    REQUIRE(r.payload.size() == 2);
}
