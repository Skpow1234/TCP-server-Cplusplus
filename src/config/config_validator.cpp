#include <tcp_server/config_validator.hpp>

#include <string>

namespace tcp_server {
namespace {

[[nodiscard]] auto missing(std::string msg) -> ConfigValidateError {
    return ConfigValidateError{ConfigValidateError::Code::MissingRequiredValue, std::move(msg)};
}

[[nodiscard]] auto out_of_range(std::string msg) -> ConfigValidateError {
    return ConfigValidateError{ConfigValidateError::Code::OutOfRange, std::move(msg)};
}

}  // namespace

auto validate_config(const ServerConfig& cfg) -> std::optional<ConfigValidateError> {
    // listen
    if (cfg.listen.port == 0) {
        return missing("listen.port is required and must be > 0");
    }

    // limits
    if (cfg.limits.max_connections == 0) {
        return missing("limits.max_connections is required and must be > 0");
    }
    if (cfg.limits.max_message_bytes == 0) {
        return missing("limits.max_message_bytes is required and must be > 0");
    }

    // timeouts
    if (cfg.timeouts.idle_ms == 0) {
        return missing("timeouts.idle_ms is required and must be > 0");
    }

    // runtime
    if (cfg.runtime.worker_threads == 0) {
        return missing("runtime.worker_threads is required and must be > 0");
    }

    // basic guardrails (non-hardcoded, just sanity constraints)
    constexpr auto k_max_reasonable_message_bytes = 1024ULL * 1024ULL * 1024ULL;  // 1 GiB
    if (cfg.limits.max_message_bytes > k_max_reasonable_message_bytes) {
        return out_of_range("limits.max_message_bytes is unreasonably large (> 1 GiB)");
    }

    return std::nullopt;
}

}  // namespace tcp_server
