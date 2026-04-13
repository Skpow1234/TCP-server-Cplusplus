#include <catch2/catch_test_macros.hpp>

#include <tcp_server/app/dispatcher.hpp>
#include <tcp_server/protocol/frame_decoder.hpp>
#include <tcp_server/protocol/frame_encoder.hpp>

#include <memory>
#include <span>
#include <vector>

TEST_CASE("EchoDispatcher: empty payload") {
    tcp_server::app::EchoDispatcher echo{};
    const std::vector<std::byte> in{};
    const auto out = echo.dispatch(in);
    REQUIRE(out.has_value());
    REQUIRE(out->empty());
}

TEST_CASE("EchoDispatcher: copies bytes") {
    tcp_server::app::EchoDispatcher echo{};
    const std::vector<std::byte> in{std::byte{1}, std::byte{2}, std::byte{3}};
    const auto out = echo.dispatch(std::span<const std::byte>(in.data(), in.size()));
    REQUIRE(out.has_value());
    REQUIRE(*out == in);
    REQUIRE(out->data() != in.data());
}

TEST_CASE("RequestDispatcher: echo via interface") {
    std::unique_ptr<tcp_server::app::RequestDispatcher> d = std::make_unique<tcp_server::app::EchoDispatcher>();
    const std::vector<std::byte> in{std::byte{'x'}};
    const auto out = d->dispatch(in);
    REQUIRE(out.has_value());
    REQUIRE(out->size() == 1);
    REQUIRE((*out)[0] == std::byte{'x'});
}

TEST_CASE("EchoDispatcher: response can be framed and decoded") {
    tcp_server::app::EchoDispatcher echo{};
    const std::vector<std::byte> in{std::byte{'p'}, std::byte{'q'}};
    const auto response = echo.dispatch(in);
    REQUIRE(response.has_value());

    std::vector<std::byte> wire{};
    constexpr std::uint64_t k_max = 1024;
    REQUIRE(tcp_server::protocol::append_encoded_frame(wire, *response, k_max).has_value());

    const auto decoded = tcp_server::protocol::try_decode_frame(wire, k_max);
    REQUIRE(decoded.status == tcp_server::protocol::FrameDecodeResult::Status::Complete);
    REQUIRE(decoded.payload == in);
}
