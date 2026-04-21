#include <catch2/catch_test_macros.hpp>

#include <tcp_server/net/idle_timeout_manager.hpp>

#include <cstdint>

namespace {

using tcp_server::net::IdleTimeoutManager;
using tcp_server::net::NativeSocket;

constexpr NativeSocket k_sock_a = static_cast<NativeSocket>(101);
constexpr NativeSocket k_sock_b = static_cast<NativeSocket>(102);

}  // namespace

TEST_CASE("idle timeout: disabled when idle_after is zero") {
    IdleTimeoutManager mgr{std::chrono::milliseconds{0}};
    const auto t0 = IdleTimeoutManager::clock::now();
    mgr.register_connection(k_sock_a, t0);
    const auto expired = mgr.poll_expired(t0 + std::chrono::hours(24));
    REQUIRE(expired.empty());
}

TEST_CASE("idle timeout: not expired before threshold") {
    IdleTimeoutManager mgr{std::chrono::milliseconds{100}};
    const auto t0 = IdleTimeoutManager::clock::now();
    mgr.register_connection(k_sock_a, t0);
    mgr.touch_activity(k_sock_a, t0 + std::chrono::milliseconds{40});
    REQUIRE(mgr.poll_expired(t0 + std::chrono::milliseconds{120}).empty());
}

TEST_CASE("idle timeout: expires after idle window from last activity") {
    IdleTimeoutManager mgr{std::chrono::milliseconds{100}};
    const auto t0 = IdleTimeoutManager::clock::now();
    mgr.register_connection(k_sock_a, t0);
    mgr.touch_activity(k_sock_a, t0 + std::chrono::milliseconds{50});
    const auto expired = mgr.poll_expired(t0 + std::chrono::milliseconds{160});
    REQUIRE(expired.size() == 1);
    REQUIRE(expired[0] == k_sock_a);
    REQUIRE(mgr.poll_expired(t0 + std::chrono::hours(1)).empty());
}

TEST_CASE("idle timeout: unregister prevents expiry") {
    IdleTimeoutManager mgr{std::chrono::milliseconds{10}};
    const auto t0 = IdleTimeoutManager::clock::now();
    mgr.register_connection(k_sock_a, t0);
    mgr.unregister_connection(k_sock_a);
    REQUIRE(mgr.poll_expired(t0 + std::chrono::milliseconds{1000}).empty());
}

TEST_CASE("idle timeout: touch on unknown socket is no-op") {
    IdleTimeoutManager mgr{std::chrono::milliseconds{10}};
    const auto t0 = IdleTimeoutManager::clock::now();
    mgr.touch_activity(k_sock_b, t0);
    mgr.register_connection(k_sock_a, t0);
    REQUIRE(mgr.poll_expired(t0 + std::chrono::milliseconds{1000}).size() == 1);
}

TEST_CASE("idle timeout: multiple sockets expire independently") {
    IdleTimeoutManager mgr{std::chrono::milliseconds{100}};
    const auto t0 = IdleTimeoutManager::clock::now();
    mgr.register_connection(k_sock_a, t0);
    mgr.register_connection(k_sock_b, t0 + std::chrono::milliseconds{80});

    auto first = mgr.poll_expired(t0 + std::chrono::milliseconds{110});
    REQUIRE(first.size() == 1);
    REQUIRE(first[0] == k_sock_a);

    auto second = mgr.poll_expired(t0 + std::chrono::milliseconds{200});
    REQUIRE(second.size() == 1);
    REQUIRE(second[0] == k_sock_b);
}
