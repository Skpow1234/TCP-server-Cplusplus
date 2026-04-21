#pragma once

#include <chrono>
#include <unordered_map>
#include <vector>

#include <tcp_server/net/socket.hpp>

namespace tcp_server::net {

/// Tracks per-socket last activity and selects idle sockets to evict (single-threaded use: event loop).
///
/// - `idle_after` of zero disables eviction: `poll_expired` always returns an empty vector.
/// - Call `register_connection` when a socket is accepted, `touch_activity` after successful read/write
///   (or when application progress is made), `unregister_connection` when the socket is closed.
/// - `poll_expired(now)` removes expired entries from the registry and returns their handles for the
///   caller to close and erase from the poller.
class IdleTimeoutManager {
public:
    using clock = std::chrono::steady_clock;
    using time_point = clock::time_point;

    explicit IdleTimeoutManager(std::chrono::milliseconds idle_after);

    IdleTimeoutManager(const IdleTimeoutManager&) = delete;
    IdleTimeoutManager& operator=(const IdleTimeoutManager&) = delete;
    IdleTimeoutManager(IdleTimeoutManager&&) = delete;
    IdleTimeoutManager& operator=(IdleTimeoutManager&&) = delete;

    ~IdleTimeoutManager() = default;

    [[nodiscard]] auto idle_after() const -> std::chrono::milliseconds { return idle_after_; }

    void register_connection(NativeSocket socket, time_point now);
    void unregister_connection(NativeSocket socket);
    /// No-op if `socket` is not registered.
    void touch_activity(NativeSocket socket, time_point now);

    /// Returns handles whose idle duration is at least `idle_after()`; removes them from tracking.
    [[nodiscard]] auto poll_expired(time_point now) -> std::vector<NativeSocket>;

private:
    std::chrono::milliseconds idle_after_{};
    // Single-threaded owner (event loop): no mutex.
    std::unordered_map<NativeSocket, time_point> last_activity_{};
};

}  // namespace tcp_server::net
