#include <tcp_server/metrics.hpp>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace tcp_server::metrics {
namespace {

auto enabled_flag() -> std::atomic<bool>& {
    static std::atomic<bool> enabled{false};
    return enabled;
}

auto lock_ref() -> std::mutex& {
    static std::mutex lock;
    return lock;
}

auto counters_ref() -> std::unordered_map<std::string, std::uint64_t>& {
    static std::unordered_map<std::string, std::uint64_t> counters;
    return counters;
}

auto gauges_ref() -> std::unordered_map<std::string, std::int64_t>& {
    static std::unordered_map<std::string, std::int64_t> gauges;
    return gauges;
}

[[nodiscard]] auto to_key(std::string_view name) -> std::string {
    return std::string(name);
}

}  // namespace

void configure(const MetricsConfig& cfg) {
    enabled_flag().store(cfg.enabled, std::memory_order_relaxed);
}

auto enabled() -> bool {
    return enabled_flag().load(std::memory_order_relaxed);
}

struct CounterAddArgs {
    std::string_view name;
    std::uint64_t delta;
};

struct GaugeSetArgs {
    std::string_view name;
    std::int64_t value;
};

void counter_add(std::string_view name, std::uint64_t delta) {
    if (!enabled()) {
        return;
    }
    std::lock_guard lock{lock_ref()};
    counters_ref()[to_key(name)] += delta;
}

void gauge_set(std::string_view name, std::int64_t value) {
    if (!enabled()) {
        return;
    }
    std::lock_guard lock{lock_ref()};
    gauges_ref()[to_key(name)] = value;
}

auto counter_get(std::string_view name) -> std::uint64_t {
    std::lock_guard lock{lock_ref()};
    const auto it = counters_ref().find(to_key(name));
    return it == counters_ref().end() ? 0ULL : it->second;
}

auto gauge_get(std::string_view name) -> std::int64_t {
    std::lock_guard lock{lock_ref()};
    const auto it = gauges_ref().find(to_key(name));
    return it == gauges_ref().end() ? 0 : it->second;
}

}  // namespace tcp_server::metrics

