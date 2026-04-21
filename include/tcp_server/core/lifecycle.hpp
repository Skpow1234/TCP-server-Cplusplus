#pragma once

#include <atomic>
#include <cstddef>

namespace tcp_server::core {

/// Monotonic server lifecycle (invariant: `Running -> Draining -> Stopped`).
enum class ShutdownPhase : std::uint8_t {
    Running = 0,
    Draining,
    Stopped,
};

/// Coordinates graceful shutdown: stop accepting, drain connections, then stop the loop.
///
/// Typical integration (single I/O thread unless noted):
/// 1. On shutdown request (signal/admin thread): call `request_shutdown()` (thread-safe).
/// 2. I/O thread observes `phase() == Draining`, unregisters the listener (stop accept), and keeps
///    polling until outbound data is flushed and connections close.
/// 3. When the listener is stopped, there are no active connections, and no pending writes remain,
///    call `try_advance_to_stopped_if_drained(...)` each tick until `phase() == Stopped`, then exit.
class ShutdownCoordinator {
public:
    ShutdownCoordinator() = default;

    ShutdownCoordinator(const ShutdownCoordinator&) = delete;
    ShutdownCoordinator& operator=(const ShutdownCoordinator&) = delete;
    ShutdownCoordinator(ShutdownCoordinator&&) = delete;
    ShutdownCoordinator& operator=(ShutdownCoordinator&&) = delete;

    ~ShutdownCoordinator() = default;

    /// Thread-safe: first call moves `Running -> Draining`; later calls are no-ops until `Stopped`.
    void request_shutdown();

    [[nodiscard]] auto phase() const -> ShutdownPhase;

    [[nodiscard]] auto should_accept_new_connections() const -> bool;

    /// While not `Stopped`, the event loop may continue multiplexing existing sockets.
    [[nodiscard]] auto should_poll_network() const -> bool;

    /// From `Draining` only: when accept is already stopped and all connections and writes are
    /// drained, moves to `Stopped`.
    void try_advance_to_stopped_if_drained(
        bool listener_stopped,
        std::size_t active_connection_count,
        bool any_connection_has_pending_writes);

private:
    std::atomic<ShutdownPhase> phase_{ShutdownPhase::Running};
};

}  // namespace tcp_server::core
