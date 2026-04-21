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
/// - `try_submit` never blocks: returns `false` when the inbound queue is full (I/O thread stays non-blocking).
/// - Workers block when the outbound queue is full until the I/O thread `try_pop_result`s.
/// - `dispatcher` must outlive the pool; it is invoked from worker threads only.
class WorkerPool {
public:
    /// `queue_capacity` applies separately to inbound and outbound bounded deques.
    WorkerPool(std::size_t num_workers, std::size_t queue_capacity, app::RequestDispatcher& dispatcher);

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;
    WorkerPool(WorkerPool&&) = delete;
    WorkerPool& operator=(WorkerPool&&) = delete;

    ~WorkerPool();

    /// Non-blocking. `false` if stopped, full inbound queue, or invalid construction.
    [[nodiscard]] auto try_submit(HandlerTask&& task) -> bool;

    /// Non-blocking. `false` if no completed result is available.
    [[nodiscard]] auto try_pop_result(HandlerResult& out) -> bool;

    /// Stops workers and joins threads (idempotent).
    void shutdown();

private:
    void worker_loop();

    app::RequestDispatcher& dispatcher_;

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
