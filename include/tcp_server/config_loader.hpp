#pragma once

#include <expected>
#include <string>
#include <string_view>

#include <tcp_server/config.hpp>

namespace tcp_server {

struct ConfigLoadError {
    enum class Code {
        FileOpenFailed,
        InvalidLine,
        UnknownKey,
        InvalidValue,
    };

    Code code{};
    std::string message{};
    std::size_t line{};  // 1-based, 0 when not applicable
};

// Loads config from a file and applies environment overrides.
//
// File format: UTF-8 text with `key=value` pairs (whitespace trimmed).
// Comments: lines starting with `#` or `;` are ignored.
//
// Environment overrides:
// - Prefix: `TCP_SERVER__`
// - Path separator: `__`
// - Example: `TCP_SERVER__listen__port=8080`
//
// No defaults are synthesized here; missing required values are handled by validation (milestone 9).
std::expected<ServerConfig, ConfigLoadError> load_config_from_file(
    std::string_view path);

}  // namespace tcp_server

