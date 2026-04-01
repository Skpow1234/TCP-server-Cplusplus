#pragma once

#include <cstdint>
#include <string>

namespace tcp_server {

/// TCP listen endpoint (host is resolved at bind time; empty host means default).
struct ListenConfig {
    std::string host;
    std::uint16_t port{};
};

struct LimitsConfig {
    std::uint32_t max_connections{};
    std::uint64_t max_message_bytes{};
};

struct TimeoutsConfig {
    std::uint32_t idle_ms{};
};

struct RuntimeConfig {
    std::uint32_t worker_threads{};
};

enum class LogLevel : std::uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
};

struct LoggingConfig {
    LogLevel level{LogLevel::Info};
};

struct MetricsConfig {
    bool enabled{false};
};

/// Full server configuration snapshot (immutable after load + validate in later milestones).
struct ServerConfig {
    ListenConfig listen{};
    LimitsConfig limits{};
    TimeoutsConfig timeouts{};
    RuntimeConfig runtime{};
    LoggingConfig logging{};
    MetricsConfig metrics{};
};

}  // namespace tcp_server
