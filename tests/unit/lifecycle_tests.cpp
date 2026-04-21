#include <catch2/catch_test_macros.hpp>

#include <tcp_server/core/lifecycle.hpp>

#include <thread>
#include <vector>

using tcp_server::core::ShutdownCoordinator;
using tcp_server::core::ShutdownPhase;

TEST_CASE("lifecycle: starts running and accepts") {
    ShutdownCoordinator c{};
    REQUIRE(c.phase() == ShutdownPhase::Running);
    REQUIRE(c.should_accept_new_connections());
    REQUIRE(c.should_poll_network());
}

TEST_CASE("lifecycle: request_shutdown moves to draining") {
    ShutdownCoordinator c{};
    c.request_shutdown();
    REQUIRE(c.phase() == ShutdownPhase::Draining);
    REQUIRE_FALSE(c.should_accept_new_connections());
    REQUIRE(c.should_poll_network());
}

TEST_CASE("lifecycle: shutdown is monotonic") {
    ShutdownCoordinator c{};
    c.request_shutdown();
    c.request_shutdown();
    REQUIRE(c.phase() == ShutdownPhase::Draining);
    c.try_advance_to_stopped_if_drained(true, 0, false);
    REQUIRE(c.phase() == ShutdownPhase::Stopped);
    c.request_shutdown();
    REQUIRE(c.phase() == ShutdownPhase::Stopped);
}

TEST_CASE("lifecycle: cannot stop from running without draining") {
    ShutdownCoordinator c{};
    c.try_advance_to_stopped_if_drained(true, 0, false);
    REQUIRE(c.phase() == ShutdownPhase::Running);
}

TEST_CASE("lifecycle: draining waits for listener and connections") {
    ShutdownCoordinator c{};
    c.request_shutdown();

    c.try_advance_to_stopped_if_drained(false, 0, false);
    REQUIRE(c.phase() == ShutdownPhase::Draining);

    c.try_advance_to_stopped_if_drained(true, 1, false);
    REQUIRE(c.phase() == ShutdownPhase::Draining);

    c.try_advance_to_stopped_if_drained(true, 0, true);
    REQUIRE(c.phase() == ShutdownPhase::Draining);

    c.try_advance_to_stopped_if_drained(true, 0, false);
    REQUIRE(c.phase() == ShutdownPhase::Stopped);
    REQUIRE_FALSE(c.should_poll_network());
}

TEST_CASE("lifecycle: concurrent request_shutdown is safe") {
    ShutdownCoordinator c{};
    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&c] { c.request_shutdown(); });
    }
    for (auto& t : threads) {
        t.join();
    }
    REQUIRE(c.phase() == ShutdownPhase::Draining);
}
