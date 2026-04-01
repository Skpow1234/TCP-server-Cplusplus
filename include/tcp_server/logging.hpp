#pragma once

#include <initializer_list>
#include <string_view>

#include <tcp_server/config.hpp>

namespace tcp_server {

struct LogField {
    std::string_view key;
    std::string_view value;
};

// Minimal structured logging facade.
//
// Format (single line):
//   level=<level> msg="<msg>" key=value key2="value with spaces"
//
// Thread-safe. Intended for low overhead in hot paths (no allocations required if
// caller provides string_views with sufficient lifetime).
namespace logging {

void configure(const LoggingConfig& cfg);

[[nodiscard]] auto enabled(LogLevel level) -> bool;

void log(LogLevel level, std::string_view message, std::initializer_list<LogField> fields = {});

}  // namespace logging
}  // namespace tcp_server

