#include <catch2/catch_test_macros.hpp>

#include <tcp_server/config.hpp>
#include <tcp_server/config_loader.hpp>

#include <fstream>
#include <string>

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

TEST_CASE("config loader: parses key=value file") {
    const auto path = "tcp_server_test_config.cfg";
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.is_open());
        out << "# comment\n";
        out << "listen.host=0.0.0.0\n";
        out << "listen.port=12345\n";
        out << "limits.max_connections=42\n";
        out << "limits.max_message_bytes=4096\n";
        out << "timeouts.idle_ms=1000\n";
        out << "runtime.worker_threads=7\n";
        out << "logging.level=warn\n";
        out << "metrics.enabled=true\n";
    }

    const auto loaded = tcp_server::load_config_from_file(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->listen.host == "0.0.0.0");
    REQUIRE(loaded->listen.port == 12345);
    REQUIRE(loaded->limits.max_connections == 42);
    REQUIRE(loaded->limits.max_message_bytes == 4096);
    REQUIRE(loaded->timeouts.idle_ms == 1000);
    REQUIRE(loaded->runtime.worker_threads == 7);
    REQUIRE(loaded->logging.level == tcp_server::LogLevel::Warn);
    REQUIRE(loaded->metrics.enabled);
}

