#pragma once

#include <cstdint>
#include <string_view>

#include <tcp_server/config.hpp>

namespace tcp_server {

namespace metrics {

// Minimal metrics facade.
//
// - When disabled, calls are cheap and have no side effects.
// - When enabled, counters and gauges are stored in-memory (initial backend).
// - Metric names are expected to have stable lifetime (string literals recommended).

void configure(const MetricsConfig& cfg);

[[nodiscard]] auto enabled() -> bool;

void counter_add(std::string_view name, std::uint64_t delta = 1);
void gauge_set(std::string_view name, std::int64_t value);

// Test/diagnostic helpers (stable API can evolve later).
[[nodiscard]] auto counter_get(std::string_view name) -> std::uint64_t;
[[nodiscard]] auto gauge_get(std::string_view name) -> std::int64_t;

}  // namespace metrics
}  // namespace tcp_server

