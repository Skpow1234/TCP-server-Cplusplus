// Stress tests intended for ThreadSanitizer (no sleeps for correctness; heavy contention).
// Run:  ctest --preset mingw-tsan
//   or: bash cmake/scripts/run-sanitizer-tests.sh mingw-tsan

#include <catch2/catch_test_macros.hpp>

#include <tcp_server/app/dispatcher.hpp>
#include <tcp_server/core/lifecycle.hpp>
#include <tcp_server/runtime/worker_pool.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {

using tcp_server::app::EchoDispatcher;
using tcp_server::core::ShutdownCoordinator;
using tcp_server::core::ShutdownPhase;
using tcp_server::runtime::HandlerResult;
using tcp_server::runtime::HandlerTask;
using tcp_server::runtime::WorkerPool;

constexpr int k_stress_producers = 8;
constexpr int k_tasks_per_producer = 200;
constexpr int k_stress_total = k_stress_producers * k_tasks_per_producer;

}  // namespace

TEST_CASE("stress: worker pool multi-producer single consumer", "[stress][tsan]") {
    EchoDispatcher echo{};
    WorkerPool pool(4, 4096, echo);

    std::atomic<int> popped{0};

    std::thread consumer([&pool, &popped] {
        HandlerResult r{};
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::seconds(120);
        while (popped.load(std::memory_order_acquire) < k_stress_total && clock::now() < deadline) {
            if (pool.try_pop_result(r)) {
                REQUIRE(r.outcome.has_value());
                popped.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
        REQUIRE(popped.load(std::memory_order_acquire) == k_stress_total);
    });

    std::vector<std::thread> producers;
    producers.reserve(static_cast<std::size_t>(k_stress_producers));
    for (int p = 0; p < k_stress_producers; ++p) {
        producers.emplace_back([&pool, p] {
            for (int i = 0; i < k_tasks_per_producer; ++i) {
                const auto cid = static_cast<std::uint64_t>(p * k_tasks_per_producer + i + 1);
                while (true) {
                    HandlerTask t{
                        .correlation_id = cid,
                        .payload = {static_cast<std::byte>(static_cast<unsigned char>(p))},
                    };
                    if (pool.try_submit(std::move(t))) {
                        break;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }
    consumer.join();

    REQUIRE(popped.load(std::memory_order_acquire) == k_stress_total);
}

TEST_CASE("stress: worker pool multi-producer dual consumer", "[stress][tsan]") {
    EchoDispatcher echo{};
    WorkerPool pool(4, 4096, echo);

    std::atomic<int> popped{0};

    auto consumer_fn = [&pool, &popped] {
        HandlerResult r{};
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::seconds(120);
        while (popped.load(std::memory_order_acquire) < k_stress_total && clock::now() < deadline) {
            if (pool.try_pop_result(r)) {
                REQUIRE(r.outcome.has_value());
                popped.fetch_add(1, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();
            }
        }
    };

    std::thread c1{consumer_fn};
    std::thread c2{consumer_fn};

    std::vector<std::thread> producers;
    producers.reserve(static_cast<std::size_t>(k_stress_producers));
    for (int p = 0; p < k_stress_producers; ++p) {
        producers.emplace_back([&pool, p] {
            for (int i = 0; i < k_tasks_per_producer; ++i) {
                const auto cid = static_cast<std::uint64_t>(p * k_tasks_per_producer + i + 1);
                while (true) {
                    HandlerTask t{
                        .correlation_id = cid,
                        .payload = {static_cast<std::byte>(static_cast<unsigned char>(p ^ 1))},
                    };
                    if (pool.try_submit(std::move(t))) {
                        break;
                    }
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }
    c1.join();
    c2.join();

    REQUIRE(popped.load(std::memory_order_acquire) == k_stress_total);
}

TEST_CASE("stress: shutdown coordinator concurrent advance", "[stress][tsan]") {
    ShutdownCoordinator lifecycle{};
    lifecycle.request_shutdown();

    constexpr int k_threads = 6;
    constexpr int k_iters = 8000;
    std::vector<std::thread> threads;
    threads.reserve(k_threads);
    for (int ti = 0; ti < k_threads; ++ti) {
        threads.emplace_back([&lifecycle] {
            for (int i = 0; i < k_iters; ++i) {
                lifecycle.try_advance_to_stopped_if_drained(true, 0, false);
                if (lifecycle.phase() == ShutdownPhase::Stopped) {
                    break;
                }
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    REQUIRE(lifecycle.phase() == ShutdownPhase::Stopped);
}
