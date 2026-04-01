#include <tcp_server/logging.hpp>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <string_view>

namespace tcp_server::logging {
namespace {

auto level_ref() -> std::atomic<LogLevel>& {
    static std::atomic<LogLevel> level{LogLevel::Info};
    return level;
}

auto sink_mutex_ref() -> std::mutex& {
    static std::mutex m;
    return m;
}

[[nodiscard]] constexpr auto level_name(LogLevel level) -> std::string_view {
    switch (level) {
        case LogLevel::Trace:
            return "trace";
        case LogLevel::Debug:
            return "debug";
        case LogLevel::Info:
            return "info";
        case LogLevel::Warn:
            return "warn";
        case LogLevel::Error:
            return "error";
    }
    return "unknown";
}

[[nodiscard]] auto needs_quotes(std::string_view s) -> bool {
    for (const char ch : s) {
        if (ch == ' ' || ch == '\t' || ch == '"' || ch == '=')
            return true;
    }
    return false;
}

void write_escaped(FILE* out, std::string_view s) {
    for (const char ch : s) {
        if (ch == '"' || ch == '\\') {
            std::fputc('\\', out);
        }
        std::fputc(ch, out);
    }
}

void write_kv(FILE* out, LogField field) {
    std::fputc(' ', out);
    std::fwrite(field.key.data(), 1, field.key.size(), out);
    std::fputc('=', out);
    if (needs_quotes(field.value)) {
        std::fputc('"', out);
        write_escaped(out, field.value);
        std::fputc('"', out);
    } else {
        std::fwrite(field.value.data(), 1, field.value.size(), out);
    }
}

}  // namespace

void configure(const LoggingConfig& cfg) {
    level_ref().store(cfg.level, std::memory_order_relaxed);
}

auto enabled(LogLevel level) -> bool {
    return static_cast<unsigned>(level) >= static_cast<unsigned>(level_ref().load(std::memory_order_relaxed));
}

void log(LogLevel level, std::string_view message, std::initializer_list<LogField> fields) {
    if (!enabled(level)) {
        return;
    }

    // Single sink for now: stderr. Guard writes to keep lines intact.
    std::lock_guard lock{sink_mutex_ref()};

    std::fwrite("level=", 1, 6, stderr);
    const auto name = level_name(level);
    std::fwrite(name.data(), 1, name.size(), stderr);

    std::fwrite(" msg=\"", 1, 6, stderr);
    write_escaped(stderr, message);
    std::fputc('"', stderr);

    for (const auto& f : fields) {
        if (f.key.empty()) {
            continue;
        }
        write_kv(stderr, f);
    }

    std::fputc('\n', stderr);
    std::fflush(stderr);
}

}  // namespace tcp_server::logging

