#include <catch2/catch_test_macros.hpp>

#include <tcp_server/net/connection.hpp>
#include <tcp_server/net/socket.hpp>
#include <tcp_server/protocol/frame_decoder.hpp>
#include <tcp_server/protocol/frame_encoder.hpp>

#include <span>
#include <string>
#include <vector>

namespace {

using tcp_server::protocol::FrameEncodeError;
using tcp_server::protocol::append_encoded_frame;
using tcp_server::protocol::try_decode_frame;

}  // namespace

TEST_CASE("frame encoder: empty payload") {
    std::vector<std::byte> out{};
    const auto ok = append_encoded_frame(out, {}, 1024);
    REQUIRE(ok.has_value());
    REQUIRE(out.size() == 4);
    REQUIRE(out[0] == std::byte{0});
    REQUIRE(out[1] == std::byte{0});
    REQUIRE(out[2] == std::byte{0});
    REQUIRE(out[3] == std::byte{0});
}

TEST_CASE("frame encoder: rejects payload over max") {
    std::vector<std::byte> out{};
    std::vector<std::byte> payload(10, std::byte{1});
    const auto err = append_encoded_frame(out, payload, 9);
    REQUIRE(!err.has_value());
    REQUIRE(err.error().code == FrameEncodeError::Code::PayloadTooLarge);
    REQUIRE(out.empty());
}

TEST_CASE("frame encoder: append preserves existing outbound bytes") {
    std::vector<std::byte> out{std::byte{0xAA}, std::byte{0xBB}};
    std::vector<std::byte> payload{std::byte{'h'}, std::byte{'i'}};
    REQUIRE(append_encoded_frame(out, payload, 64).has_value());
    REQUIRE(out.size() == 2 + 4 + 2);
    REQUIRE(out[0] == std::byte{0xAA});
    REQUIRE(out[1] == std::byte{0xBB});
    REQUIRE(out[2] == std::byte{0});
    REQUIRE(out[3] == std::byte{0});
    REQUIRE(out[4] == std::byte{0});
    REQUIRE(out[5] == std::byte{2});
    REQUIRE(out[6] == std::byte{'h'});
    REQUIRE(out[7] == std::byte{'i'});
}

TEST_CASE("frame encoder: roundtrip with decoder") {
    std::vector<std::byte> wire{};
    const std::string text = "roundtrip";
    const auto bytes = std::as_bytes(std::span<const char>(text.data(), text.size()));
    REQUIRE(append_encoded_frame(wire, bytes, 1024).has_value());

    const auto dec = try_decode_frame(wire, 1024);
    REQUIRE(dec.status == tcp_server::protocol::FrameDecodeResult::Status::Complete);
    REQUIRE(dec.consumed_bytes == wire.size());
    REQUIRE(dec.payload.size() == text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        REQUIRE(dec.payload[i] == static_cast<std::byte>(static_cast<unsigned char>(text[i])));
    }
}

TEST_CASE("frame encoder: two frames back-to-back decode sequentially") {
    std::vector<std::byte> wire{};
    const std::vector<std::byte> f1{std::byte{'a'}};
    const std::vector<std::byte> f2{std::byte{'b'}, std::byte{'c'}};
    REQUIRE(append_encoded_frame(wire, f1, 64).has_value());
    REQUIRE(append_encoded_frame(wire, f2, 64).has_value());

    auto first = try_decode_frame(wire, 64);
    REQUIRE(first.status == tcp_server::protocol::FrameDecodeResult::Status::Complete);
    REQUIRE(first.payload.size() == 1);
    REQUIRE(first.payload[0] == std::byte{'a'});

    wire.erase(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(first.consumed_bytes));
    auto second = try_decode_frame(wire, 64);
    REQUIRE(second.status == tcp_server::protocol::FrameDecodeResult::Status::Complete);
    REQUIRE(second.payload.size() == 2);
    REQUIRE(second.payload[0] == std::byte{'b'});
    REQUIRE(second.payload[1] == std::byte{'c'});
}

TEST_CASE("frame encoder: append to connection write buffer") {
    tcp_server::net::NetworkSession net;
    REQUIRE(net.ok());
    auto sock = tcp_server::net::Socket::create_tcp_v4();
    REQUIRE(sock.has_value());
    tcp_server::net::Connection conn{std::move(*sock)};

    const std::vector<std::byte> payload{std::byte{0xDE}, std::byte{0xAD}};
    REQUIRE(append_encoded_frame(conn, payload, 64).has_value());

    const auto& wb = conn.write_buffer();
    REQUIRE(wb.size() == 6);
    auto dec = try_decode_frame(wb, 64);
    REQUIRE(dec.status == tcp_server::protocol::FrameDecodeResult::Status::Complete);
    REQUIRE(dec.payload == payload);
}
