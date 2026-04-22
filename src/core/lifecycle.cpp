#include <tcp_server/core/lifecycle.hpp>

namespace tcp_server::core {

void ShutdownCoordinator::request_shutdown() {
    for (;;) {
        auto expected_phase = phase_.load(std::memory_order_acquire);
        if (expected_phase != ShutdownPhase::Running) {
            return;
        }
        if (phase_.compare_exchange_weak(
                expected_phase,
                ShutdownPhase::Draining,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }
    }
}

auto ShutdownCoordinator::phase() const -> ShutdownPhase {
    return phase_.load(std::memory_order_acquire);
}

auto ShutdownCoordinator::should_accept_new_connections() const -> bool {
    return phase() == ShutdownPhase::Running;
}

auto ShutdownCoordinator::should_poll_network() const -> bool {
    return phase() != ShutdownPhase::Stopped;
}

void ShutdownCoordinator::try_advance_to_stopped_if_drained(
    const bool listener_stopped,
    const std::size_t active_connection_count,
    const bool any_connection_has_pending_writes) {
    if (phase_.load(std::memory_order_acquire) != ShutdownPhase::Draining) {
        return;
    }
    if (!listener_stopped || active_connection_count > 0 || any_connection_has_pending_writes) {
        return;
    }
    ShutdownPhase expected = ShutdownPhase::Draining;
    (void)phase_.compare_exchange_strong(
        expected,
        ShutdownPhase::Stopped,
        std::memory_order_acq_rel,
        std::memory_order_acquire);
}

}  // namespace tcp_server::core
