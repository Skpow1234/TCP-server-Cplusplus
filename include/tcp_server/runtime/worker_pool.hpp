#pragma once

#include <tcp_server/app/dispatcher.hpp>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <mutex>
#include <thread>
#include <vector>

namespace tcp_server::runtime {

/// When the inbound task deque is at `queue_capacity`.
enum class TaskQueueOverflowPolicy : std::uint8_t {
    /// `try_submit` returns `false` and leaves existing pending tasks unchanged.
    RejectNewTask,
    /// Drops the oldest pending task (front of queue), then enqueues the new task (never blocks I/O).
    DropOldestPendingTask,
};

/// When a worker finishes dispatching but the outbound result deque is full.
enum class ResultQueueOverflowPolicy : std::uint8_t {
    /// Worker waits until the I/O thread pops or `shutdown()` is requested.
    BlockWorkerUntilPopped,
    /// Removes the oldest completed `HandlerResult` to make room, then pushes the new one (I/O may never see dropped ids).
    DropOldestCompletedResult,
};

struct WorkerPoolOptions {
    TaskQueueOverflowPolicy task_queue_overflow{TaskQueueOverflowPolicy::RejectNewTask};
    ResultQueueOverflowPolicy result_queue_overflow{ResultQueueOverflowPolicy::BlockWorkerUntilPopped};
    /// When non-zero, caps the outbound result deque separately from the inbound `queue_capacity`.
    std::size_t result_queue_capacity_override{0};
};

/// Immutable request handed from the I/O thread to a worker (payload only; framing is I/O-side).
struct HandlerTask {
    std::uint64_t correlation_id{};
    std::vector<std::byte> payload{};
};

/// Result of running the dispatcher on a task; consumed on the I/O thread.
struct HandlerResult {
    std::uint64_t correlation_id{};
    std::expected<std::vector<std::byte>, app::DispatchError> outcome{};
};

/// Fixed-size thread pool with bounded inbound and outbound queues.
///
/// - `try_submit` never blocks on the I/O thread.
/// - Default task policy: reject when the inbound deque is full (`false` return).
/// - Optional task policy: drop the oldest pending task and accept the new one.
/// - Default result policy: workers block until space exists or `shutdown()`.
/// - Optional result policy: drop the oldest completed result to enqueue the newest (lossy).
/// - `dispatcher` must outlive the pool; it is invoked from worker threads only.
class WorkerPool {
public:
    /// `queue_capacity` is the inbound task deque cap unless `options.result_queue_capacity_override` is set
    /// (then that value caps only the outbound result deque).
    WorkerPool(
        std::size_t num_workers,
        std::size_t queue_capacity,
        app::RequestDispatcher& dispatcher,
        WorkerPoolOptions options = {});

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    WorkerPool(WorkerPool&&) = delete;
    WorkerPool& operator=(WorkerPool&&) = delete;

    ~WorkerPool();

    /// Non-blocking. `false` if stopped, or inbound queue is full under `RejectNewTask`.
    /// With `DropOldestPendingTask`, a full queue evicts the oldest pending task and returns `true`.
    [[nodiscard]] auto try_submit(HandlerTask&& task) -> bool;

    /// Non-blocking. `false` if no completed result is available.
    [[nodiscard]] auto try_pop_result(HandlerResult& out) -> bool;

    /// Stops workers and joins threads (idempotent).
    void shutdown();

private:
    void worker_loop();

    app::RequestDispatcher& dispatcher_;

    const WorkerPoolOptions options_;
    const std::size_t task_capacity_;
    const std::size_t result_capacity_;

    std::mutex task_mutex_{};
    std::condition_variable task_cv_{};
    std::deque<HandlerTask> tasks_{};

    std::mutex result_mutex_{};
    std::condition_variable result_cv_{};
    std::deque<HandlerResult> results_{};

    std::atomic<bool> stop_{false};
    std::vector<std::jthread> workers_{};
};

}  // namespace tcp_server::runtime
