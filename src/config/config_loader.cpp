#include <tcp_server/config_loader.hpp>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace tcp_server {
namespace {

constexpr std::string_view k_env_prefix = "TCP_SERVER__";

[[nodiscard]] auto trim(std::string_view s) -> std::string_view {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

[[nodiscard]] auto parse_kv(std::string_view line) -> std::optional<std::pair<std::string_view, std::string_view>> {
    const auto pos = line.find('=');
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    auto key = trim(line.substr(0, pos));
    auto value = trim(line.substr(pos + 1));
    if (key.empty()) {
        return std::nullopt;
    }
    return std::pair{key, value};
}

template <class UInt>
[[nodiscard]] auto parse_uint(std::string_view s) -> std::optional<UInt> {
    UInt out{};
    const auto* begin = s.data();
    const auto* end = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(begin, end, out);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return out;
}

[[nodiscard]] auto parse_bool(std::string_view s) -> std::optional<bool> {
    if (s == "1" || s == "true" || s == "TRUE" || s == "True") {
        return true;
    }
    if (s == "0" || s == "false" || s == "FALSE" || s == "False") {
        return false;
    }
    return std::nullopt;
}

[[nodiscard]] auto parse_log_level(std::string_view s) -> std::optional<LogLevel> {
    if (s == "trace" || s == "TRACE" || s == "Trace") {
        return LogLevel::Trace;
    }
    if (s == "debug" || s == "DEBUG" || s == "Debug") {
        return LogLevel::Debug;
    }
    if (s == "info" || s == "INFO" || s == "Info") {
        return LogLevel::Info;
    }
    if (s == "warn" || s == "WARN" || s == "Warn" || s == "warning" || s == "WARNING" || s == "Warning") {
        return LogLevel::Warn;
    }
    if (s == "error" || s == "ERROR" || s == "Error") {
        return LogLevel::Error;
    }
    return std::nullopt;
}

[[nodiscard]] auto apply_key_value(ServerConfig& cfg, std::string_view key, std::string_view value)
    -> std::optional<ConfigLoadError> {
    if (key == "listen.host") {
        cfg.listen.host = std::string(value);
        return std::nullopt;
    }
    if (key == "listen.port") {
        const auto parsed = parse_uint<std::uint32_t>(value);
        if (!parsed || *parsed > 65535u) {
            return ConfigLoadError{ConfigLoadError::Code::InvalidValue, "listen.port must be 0..65535", 0};
        }
        cfg.listen.port = static_cast<std::uint16_t>(*parsed);
        return std::nullopt;
    }
    if (key == "limits.max_connections") {
        const auto parsed = parse_uint<std::uint32_t>(value);
        if (!parsed) {
            return ConfigLoadError{ConfigLoadError::Code::InvalidValue, "limits.max_connections must be an integer", 0};
        }
        cfg.limits.max_connections = *parsed;
        return std::nullopt;
    }
    if (key == "limits.max_message_bytes") {
        const auto parsed = parse_uint<std::uint64_t>(value);
        if (!parsed) {
            return ConfigLoadError{
                ConfigLoadError::Code::InvalidValue, "limits.max_message_bytes must be an integer", 0};
        }
        cfg.limits.max_message_bytes = *parsed;
        return std::nullopt;
    }
    if (key == "timeouts.idle_ms") {
        const auto parsed = parse_uint<std::uint32_t>(value);
        if (!parsed) {
            return ConfigLoadError{ConfigLoadError::Code::InvalidValue, "timeouts.idle_ms must be an integer", 0};
        }
        cfg.timeouts.idle_ms = *parsed;
        return std::nullopt;
    }
    if (key == "runtime.worker_threads") {
        const auto parsed = parse_uint<std::uint32_t>(value);
        if (!parsed) {
            return ConfigLoadError{
                ConfigLoadError::Code::InvalidValue, "runtime.worker_threads must be an integer", 0};
        }
        cfg.runtime.worker_threads = *parsed;
        return std::nullopt;
    }
    if (key == "logging.level") {
        const auto parsed = parse_log_level(value);
        if (!parsed) {
            return ConfigLoadError{ConfigLoadError::Code::InvalidValue, "logging.level is not recognized", 0};
        }
        cfg.logging.level = *parsed;
        return std::nullopt;
    }
    if (key == "metrics.enabled") {
        const auto parsed = parse_bool(value);
        if (!parsed) {
            return ConfigLoadError{ConfigLoadError::Code::InvalidValue, "metrics.enabled must be true/false", 0};
        }
        cfg.metrics.enabled = *parsed;
        return std::nullopt;
    }

    return ConfigLoadError{ConfigLoadError::Code::UnknownKey, "unknown key: " + std::string(key), 0};
}

}  // namespace

auto load_config_from_file(std::string_view path) -> std::expected<ServerConfig, ConfigLoadError> {
    ServerConfig cfg{};

    std::ifstream in{std::filesystem::path(std::string(path))};
    if (!in.is_open()) {
        return std::unexpected(ConfigLoadError{
            ConfigLoadError::Code::FileOpenFailed, "failed to open config file: " + std::string(path), 0});
    }

    std::string line;
    std::size_t line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        auto sv = trim(line);
        if (sv.empty()) {
            continue;
        }
        if (sv.front() == '#' || sv.front() == ';') {
            continue;
        }

        const auto kv = parse_kv(sv);
        if (!kv) {
            return std::unexpected(ConfigLoadError{
                ConfigLoadError::Code::InvalidLine, "invalid line (expected key=value)", line_no});
        }

        if (auto err = apply_key_value(cfg, kv->first, kv->second)) {
            err->line = line_no;
            return std::unexpected(*err);
        }
    }

    // Environment overrides (best-effort scan of known keys, no hardcoded defaults).
    // We intentionally only read the keys we support rather than iterating the process environment.
    struct EnvKey {
        std::string_view env;
        std::string_view key;
    };
    constexpr EnvKey env_keys[] = {
        {"TCP_SERVER__listen__host", "listen.host"},
        {"TCP_SERVER__listen__port", "listen.port"},
        {"TCP_SERVER__limits__max_connections", "limits.max_connections"},
        {"TCP_SERVER__limits__max_message_bytes", "limits.max_message_bytes"},
        {"TCP_SERVER__timeouts__idle_ms", "timeouts.idle_ms"},
        {"TCP_SERVER__runtime__worker_threads", "runtime.worker_threads"},
        {"TCP_SERVER__logging__level", "logging.level"},
        {"TCP_SERVER__metrics__enabled", "metrics.enabled"},
    };

    for (const auto& ek : env_keys) {
        if (const char* v = std::getenv(std::string(ek.env).c_str()); v != nullptr) {
            if (auto err = apply_key_value(cfg, ek.key, v)) {
                err->line = 0;
                err->message = "env " + std::string(ek.env) + ": " + err->message;
                return std::unexpected(*err);
            }
        }
    }

    return cfg;
}

}  // namespace tcp_server
