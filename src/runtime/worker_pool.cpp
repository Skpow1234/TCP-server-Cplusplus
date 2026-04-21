#include <tcp_server/runtime/worker_pool.hpp>

#include <span>
#include <stdexcept>
#include <utility>

namespace tcp_server::runtime {

WorkerPool::WorkerPool(
    std::size_t num_workers,
    std::size_t queue_capacity,
    app::RequestDispatcher& dispatcher,
    WorkerPoolOptions options)
    : dispatcher_(dispatcher)
    , options_(options)
    , task_capacity_(queue_capacity)
    , result_capacity_(
          options.result_queue_capacity_override != 0 ? options.result_queue_capacity_override : queue_capacity) {
    if (num_workers == 0) {
        throw std::invalid_argument("WorkerPool: num_workers must be >= 1");
    }
    if (queue_capacity == 0) {
        throw std::invalid_argument("WorkerPool: queue_capacity must be >= 1");
    }
    if (result_capacity_ == 0) {
        throw std::invalid_argument("WorkerPool: effective result queue capacity must be >= 1");
    }

    workers_.reserve(num_workers);
    for (std::size_t i = 0; i < num_workers; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

WorkerPool::~WorkerPool() {
    shutdown();
}

void WorkerPool::shutdown() {
    const bool already = stop_.exchange(true, std::memory_order_acq_rel);
    if (already) {
        return;
    }
    task_cv_.notify_all();
    result_cv_.notify_all();
    workers_.clear();
}

auto WorkerPool::try_submit(HandlerTask&& task) -> bool {
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        if (stop_.load(std::memory_order_acquire)) {
            return false;
        }
        if (tasks_.size() >= task_capacity_) {
            if (options_.task_queue_overflow == TaskQueueOverflowPolicy::DropOldestPendingTask) {
                if (!tasks_.empty()) {
                    tasks_.pop_front();
                }
            } else {
                return false;
            }
        }
        tasks_.push_back(std::move(task));
    }
    task_cv_.notify_one();
    return true;
}

auto WorkerPool::try_pop_result(HandlerResult& out) -> bool {
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        if (results_.empty()) {
            return false;
        }
        out = std::move(results_.front());
        results_.pop_front();
    }
    result_cv_.notify_all();
    return true;
}

void WorkerPool::worker_loop() {
    for (;;) {
        HandlerTask task{};
        {
            std::unique_lock<std::mutex> lock(task_mutex_);
            task_cv_.wait(lock, [this] {
                return stop_.load(std::memory_order_acquire) || !tasks_.empty();
            });
            if (tasks_.empty()) {
                if (stop_.load(std::memory_order_acquire)) {
                    return;
                }
                continue;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        task_cv_.notify_all();

        std::expected<std::vector<std::byte>, app::DispatchError> outcome;
        try {
            outcome = dispatcher_.dispatch(std::span<const std::byte>(task.payload.data(), task.payload.size()));
        } catch (const std::exception& ex) {
            outcome = std::unexpected(app::DispatchError{
                .code = app::DispatchError::Code::Internal,
                .message = ex.what(),
            });
        } catch (...) {
            outcome = std::unexpected(app::DispatchError{
                .code = app::DispatchError::Code::Internal,
                .message = "unknown exception in dispatcher",
            });
        }

        HandlerResult done{.correlation_id = task.correlation_id, .outcome = std::move(outcome)};

        {
            std::unique_lock<std::mutex> lock(result_mutex_);
            if (options_.result_queue_overflow == ResultQueueOverflowPolicy::DropOldestCompletedResult) {
                while (results_.size() >= result_capacity_ && !results_.empty()) {
                    results_.pop_front();
                }
                if (stop_.load(std::memory_order_acquire)) {
                    return;
                }
                results_.push_back(std::move(done));
            } else {
                result_cv_.wait(lock, [this] {
                    return stop_.load(std::memory_order_acquire) || results_.size() < result_capacity_;
                });
                if (stop_.load(std::memory_order_acquire)) {
                    return;
                }
                results_.push_back(std::move(done));
            }
        }
        result_cv_.notify_one();
    }
}

}  // namespace tcp_server::runtime
