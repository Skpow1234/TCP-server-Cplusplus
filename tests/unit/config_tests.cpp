#include <catch2/catch_test_macros.hpp>

#include <tcp_server/config.hpp>
#include <tcp_server/config_loader.hpp>
#include <tcp_server/config_validator.hpp>
#include <tcp_server/logging.hpp>
#include <tcp_server/metrics.hpp>
#include <tcp_server/net/accept.hpp>
#include <tcp_server/net/acceptor.hpp>
#include <tcp_server/net/socket.hpp>
#include <tcp_server/net/listener.hpp>
#include <tcp_server/net/connection.hpp>
#include <tcp_server/net/select_poller.hpp>

#include <array>
#include <atomic>
#include <fstream>
#include <cstdlib>
#include <string>
#include <thread>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

namespace {

auto connect_v4_loopback(std::uint16_t port) -> bool {
#if defined(_WIN32)
    const auto s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int rc = ::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    ::closesocket(s);
    return rc == 0;
#else
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int rc = ::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    ::close(s);
    return rc == 0;
#endif
}

}  // namespace

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

    const auto validation_error = tcp_server::validate_config(*loaded);
    REQUIRE(!validation_error.has_value());
}

TEST_CASE("config validator: missing required values fails") {
    tcp_server::ServerConfig cfg{};
    cfg.listen.host = "127.0.0.1";
    cfg.listen.port = 0;  // missing
    cfg.limits.max_connections = 1;
    cfg.limits.max_message_bytes = 1;
    cfg.timeouts.idle_ms = 1;
    cfg.runtime.worker_threads = 1;

    const auto err = tcp_server::validate_config(cfg);
    REQUIRE(err.has_value());
    REQUIRE(err->code == tcp_server::ConfigValidateError::Code::MissingRequiredValue);
}

TEST_CASE("config loader: invalid line fails with line number") {
    const auto path = "tcp_server_test_config_invalid_line.cfg";
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.is_open());
        out << "listen.host 0.0.0.0\n";  // missing '='
    }

    const auto loaded = tcp_server::load_config_from_file(path);
    REQUIRE(!loaded.has_value());
    REQUIRE(loaded.error().code == tcp_server::ConfigLoadError::Code::InvalidLine);
    REQUIRE(loaded.error().line == 1);
}

TEST_CASE("config loader: unknown key fails") {
    const auto path = "tcp_server_test_config_unknown_key.cfg";
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.is_open());
        out << "listen.host=0.0.0.0\n";
        out << "bogus.key=123\n";
    }

    const auto loaded = tcp_server::load_config_from_file(path);
    REQUIRE(!loaded.has_value());
    REQUIRE(loaded.error().code == tcp_server::ConfigLoadError::Code::UnknownKey);
    REQUIRE(loaded.error().line == 2);
}

TEST_CASE("config loader: invalid value fails") {
    const auto path = "tcp_server_test_config_invalid_value.cfg";
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.is_open());
        out << "listen.port=not_a_number\n";
    }

    const auto loaded = tcp_server::load_config_from_file(path);
    REQUIRE(!loaded.has_value());
    REQUIRE(loaded.error().code == tcp_server::ConfigLoadError::Code::InvalidValue);
    REQUIRE(loaded.error().line == 1);
}

TEST_CASE("config loader: environment overrides apply") {
    const auto path = "tcp_server_test_config_env_override.cfg";
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.is_open());
        out << "listen.port=1000\n";
    }

    REQUIRE(_putenv("TCP_SERVER__listen__port=2000") == 0);
    const auto loaded = tcp_server::load_config_from_file(path);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->listen.port == 2000);
    REQUIRE(_putenv("TCP_SERVER__listen__port=") == 0);
}

TEST_CASE("config loader: invalid environment override fails") {
    const auto path = "tcp_server_test_config_env_override_invalid.cfg";
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.is_open());
        out << "listen.port=1000\n";
    }

    REQUIRE(_putenv("TCP_SERVER__listen__port=99999") == 0);
    const auto loaded = tcp_server::load_config_from_file(path);
    REQUIRE(!loaded.has_value());
    REQUIRE(loaded.error().code == tcp_server::ConfigLoadError::Code::InvalidValue);
    REQUIRE(loaded.error().line == 0);
    REQUIRE(_putenv("TCP_SERVER__listen__port=") == 0);
}

TEST_CASE("config validator: max_message_bytes guardrail triggers") {
    tcp_server::ServerConfig cfg{};
    cfg.listen.port = 1;
    cfg.limits.max_connections = 1;
    cfg.limits.max_message_bytes = (1024ULL * 1024ULL * 1024ULL) + 1;  // > 1 GiB
    cfg.timeouts.idle_ms = 1;
    cfg.runtime.worker_threads = 1;

    const auto err = tcp_server::validate_config(cfg);
    REQUIRE(err.has_value());
    REQUIRE(err->code == tcp_server::ConfigValidateError::Code::OutOfRange);
}

TEST_CASE("logging: level filtering works") {
    tcp_server::logging::configure(tcp_server::LoggingConfig{.level = tcp_server::LogLevel::Warn});
    REQUIRE(!tcp_server::logging::enabled(tcp_server::LogLevel::Info));
    REQUIRE(tcp_server::logging::enabled(tcp_server::LogLevel::Warn));
    REQUIRE(tcp_server::logging::enabled(tcp_server::LogLevel::Error));
}

TEST_CASE("logging: log call is safe") {
    tcp_server::logging::configure(tcp_server::LoggingConfig{.level = tcp_server::LogLevel::Info});
    tcp_server::logging::log(
        tcp_server::LogLevel::Info,
        "hello",
        {
            {"conn_id", "1"},
            {"event", "accept"},
            {"note", "value with spaces"},
        });
    REQUIRE(true);
}

TEST_CASE("metrics: disabled is no-op") {
    tcp_server::metrics::configure(tcp_server::MetricsConfig{.enabled = false});

    tcp_server::metrics::counter_add("accepts", 5);
    tcp_server::metrics::gauge_set("connections", 10);

    REQUIRE(!tcp_server::metrics::enabled());
    REQUIRE(tcp_server::metrics::counter_get("accepts") == 0);
    REQUIRE(tcp_server::metrics::gauge_get("connections") == 0);
}

TEST_CASE("metrics: enabled records counters and gauges") {
    tcp_server::metrics::configure(tcp_server::MetricsConfig{.enabled = true});

    tcp_server::metrics::counter_add("bytes_in", 3);
    tcp_server::metrics::counter_add("bytes_in", 7);
    tcp_server::metrics::gauge_set("connections", 12);

    REQUIRE(tcp_server::metrics::enabled());
    REQUIRE(tcp_server::metrics::counter_get("bytes_in") == 10);
    REQUIRE(tcp_server::metrics::gauge_get("connections") == 12);
}

TEST_CASE("net socket: RAII create and close works") {
    tcp_server::net::NetworkSession net;
    REQUIRE(net.ok());

    auto s = tcp_server::net::Socket::create_tcp_v4();
    REQUIRE(s.has_value());
    REQUIRE(s->valid());

    const auto closed = s->close();
    REQUIRE(closed.has_value());
    REQUIRE(!s->valid());
}

TEST_CASE("net listener: bind and listen works") {
    tcp_server::net::NetworkSession net;
    REQUIRE(net.ok());

    const auto listener = tcp_server::net::Listener::bind_and_listen("127.0.0.1", 0, 16);
    REQUIRE(listener.has_value());
    REQUIRE(listener->valid());
}

TEST_CASE("select poller: upsert/erase and timeout wait") {
    tcp_server::net::NetworkSession net;
    REQUIRE(net.ok());

    tcp_server::net::SelectPoller poller;

    auto s = tcp_server::net::Socket::create_tcp_v4();
    REQUIRE(s.has_value());
    REQUIRE(s->valid());

    REQUIRE(poller.upsert(s->native_handle(), tcp_server::net::EventMask::Read).has_value());

    tcp_server::net::Event events[8]{};
    const auto n = poller.wait(events, 0);
    REQUIRE(n.has_value());
    REQUIRE(*n <= 8);

    REQUIRE(poller.erase(s->native_handle()).has_value());
}

TEST_CASE("connection: buffers and state") {
    tcp_server::net::NetworkSession net;
    REQUIRE(net.ok());

    auto sock = tcp_server::net::Socket::create_tcp_v4();
    REQUIRE(sock.has_value());

    tcp_server::net::Connection conn{std::move(*sock)};
    REQUIRE(conn.state() == tcp_server::net::ConnectionState::Reading);
    REQUIRE(conn.read_buffer().empty());
    REQUIRE(conn.write_buffer().empty());

    const std::array<std::byte, 3> chunk{
        std::byte{1},
        std::byte{2},
        std::byte{3},
    };
    conn.append_read(chunk);
    REQUIRE(conn.read_buffer().size() == 3);

    conn.consume_read(1);
    REQUIRE(conn.read_buffer().size() == 2);

    conn.append_write(std::span<const std::byte>(chunk));
    REQUIRE(conn.write_buffer().size() == 3);
    conn.clear_write();
    REQUIRE(conn.write_buffer().empty());

    conn.set_state(tcp_server::net::ConnectionState::Writing);
    REQUIRE(conn.state() == tcp_server::net::ConnectionState::Writing);
}

TEST_CASE("acceptor: register listener and accept new connection into poller") {
    tcp_server::net::NetworkSession net;
    REQUIRE(net.ok());

    auto listener = tcp_server::net::Listener::bind_and_listen("127.0.0.1", 0, 16);
    REQUIRE(listener.has_value());

    const auto port = tcp_server::net::local_port_v4(listener->socket());
    REQUIRE(port.has_value());
    REQUIRE(*port != 0);

    tcp_server::net::SelectPoller poller;
    tcp_server::net::Acceptor acceptor{std::move(*listener)};
    REQUIRE(acceptor.register_listener(poller).has_value());

    std::atomic<bool> client_ok{false};
    std::thread client_thread([p = *port, &client_ok] {
        client_ok = connect_v4_loopback(p);
    });

    tcp_server::net::Event events[8]{};
    bool saw_listener_read = false;
    for (int attempt = 0; attempt < 100 && !saw_listener_read; ++attempt) {
        const auto n = poller.wait(events, 50);
        REQUIRE(n.has_value());
        for (std::size_t i = 0; i < *n; ++i) {
            if (events[i].socket == acceptor.listener_handle()
                && (events[i].mask & tcp_server::net::EventMask::Read) != tcp_server::net::EventMask::None) {
                saw_listener_read = true;
                break;
            }
        }
    }
    REQUIRE(saw_listener_read);

    const auto conn = acceptor.accept_and_register(poller);
    REQUIRE(conn.has_value());
    REQUIRE(conn->socket().valid());

    client_thread.join();
    REQUIRE(client_ok.load());

    REQUIRE(acceptor.unregister_listener(poller).has_value());
}

