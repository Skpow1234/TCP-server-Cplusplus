#pragma once

#include <optional>
#include <string>

#include <tcp_server/config.hpp>

namespace tcp_server {

struct ConfigValidateError {
    enum class Code {
        MissingRequiredValue,
        OutOfRange,
        InvalidCombination,
    };

    Code code{};
    std::string message{};
};

// Returns nullopt on success; otherwise a structured validation error.
//
// This is intentionally separate from loading so we can validate configs that come
// from any source (file, env, tests, future formats).
[[nodiscard]] auto validate_config(const ServerConfig& cfg) -> std::optional<ConfigValidateError>;

}  // namespace tcp_server

