#include <catch2/catch_test_macros.hpp>

#include <tcp_server/config.hpp>

TEST_CASE("smoke: test framework runs") {
    REQUIRE(true);
}

TEST_CASE("config schema: typed fields are constructible") {
    const tcp_server::ServerConfig cfg{
        .listen =
            {
                .host = "127.0.0.1",
                .port = 8080,
            },
        .limits =
            {
                .max_connections = 128,
                .max_message_bytes = 1024 * 1024,
            },
        .timeouts =
            {
                .idle_ms = 60'000,
            },
        .runtime =
            {
                .worker_threads = 4,
            },
        .logging =
            {
                .level = tcp_server::LogLevel::Info,
            },
        .metrics =
            {
                .enabled = true,
            },
    };

    REQUIRE(cfg.listen.host == "127.0.0.1");
    REQUIRE(cfg.listen.port == 8080);
    REQUIRE(cfg.limits.max_connections == 128);
    REQUIRE(cfg.limits.max_message_bytes == 1024 * 1024);
    REQUIRE(cfg.timeouts.idle_ms == 60'000);
    REQUIRE(cfg.runtime.worker_threads == 4);
    REQUIRE(cfg.logging.level == tcp_server::LogLevel::Info);
    REQUIRE(cfg.metrics.enabled);
}

