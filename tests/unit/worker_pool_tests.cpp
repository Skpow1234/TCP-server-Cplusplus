#include <catch2/catch_test_macros.hpp>

#include <tcp_server/app/dispatcher.hpp>
#include <tcp_server/runtime/worker_pool.hpp>

#include <chrono>
#include <set>
#include <span>
#include <thread>
#include <vector>

namespace {

class SlowEchoDispatcher final : public tcp_server::app::RequestDispatcher {
public:
    explicit SlowEchoDispatcher(std::chrono::milliseconds delay) : delay_(delay) {}

    [[nodiscard]] auto dispatch(std::span<const std::byte> request_payload)
        -> std::expected<std::vector<std::byte>, tcp_server::app::DispatchError> override {
        std::this_thread::sleep_for(delay_);
        return std::vector<std::byte>(request_payload.begin(), request_payload.end());
    }

private:
    std::chrono::milliseconds delay_;
};

bool drain_until_count(tcp_server::runtime::WorkerPool& pool, int want) {
    int got = 0;
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::seconds(5);
    while (clock::now() < deadline && got < want) {
        tcp_server::runtime::HandlerResult r{};
        if (pool.try_pop_result(r)) {
            ++got;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return got == want;
}

}  // namespace

TEST_CASE("worker pool: rejects invalid construction") {
    tcp_server::app::EchoDispatcher echo{};
    REQUIRE_THROWS_AS((void)tcp_server::runtime::WorkerPool(0, 4, echo), std::invalid_argument);
    REQUIRE_THROWS_AS((void)tcp_server::runtime::WorkerPool(1, 0, echo), std::invalid_argument);
}

TEST_CASE("worker pool: echo tasks roundtrip") {
    tcp_server::app::EchoDispatcher echo{};
    tcp_server::runtime::WorkerPool pool(2, 16, echo);

    constexpr int k_tasks = 12;
    for (int i = 0; i < k_tasks; ++i) {
        std::vector<std::byte> payload{static_cast<std::byte>(i)};
        REQUIRE(pool.try_submit(tcp_server::runtime::HandlerTask{
            .correlation_id = static_cast<std::uint64_t>(i),
            .payload = std::move(payload),
        }));
    }

    std::set<std::uint64_t> seen{};
    for (int n = 0; n < k_tasks; ++n) {
        tcp_server::runtime::HandlerResult r{};
        bool got = false;
        using clock = std::chrono::steady_clock;
        const auto deadline = clock::now() + std::chrono::seconds(5);
        while (clock::now() < deadline && !got) {
            got = pool.try_pop_result(r);
            if (!got) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        REQUIRE(got);
        REQUIRE(r.outcome.has_value());
        REQUIRE(r.outcome->size() == 1);
        REQUIRE((*r.outcome)[0] == static_cast<std::byte>(static_cast<int>(r.correlation_id)));
        REQUIRE(seen.insert(r.correlation_id).second);
    }
    REQUIRE(seen.size() == static_cast<std::size_t>(k_tasks));
}

TEST_CASE("worker pool: try_submit fails when inbound queue is full") {
    SlowEchoDispatcher slow{std::chrono::milliseconds(60)};
    tcp_server::runtime::WorkerPool pool(1, 2, slow);

    REQUIRE(pool.try_submit(tcp_server::runtime::HandlerTask{.correlation_id = 1, .payload = {std::byte{'a'}}}));
    REQUIRE(pool.try_submit(tcp_server::runtime::HandlerTask{.correlation_id = 2, .payload = {std::byte{'b'}}}));
    REQUIRE_FALSE(pool.try_submit(tcp_server::runtime::HandlerTask{.correlation_id = 3, .payload = {std::byte{'c'}}}));

    REQUIRE(drain_until_count(pool, 2));

    REQUIRE(pool.try_submit(tcp_server::runtime::HandlerTask{.correlation_id = 3, .payload = {std::byte{'c'}}}));
    REQUIRE(drain_until_count(pool, 1));
}
