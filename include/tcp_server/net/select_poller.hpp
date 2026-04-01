#pragma once

#include <expected>
#include <unordered_map>

#include <tcp_server/net/event_loop.hpp>

namespace tcp_server::net {

// First polling backend: select().
// Portable and simple, good as a baseline; not intended for very high fd counts.
class SelectPoller final : public Poller {
public:
    SelectPoller() = default;
    ~SelectPoller() override = default;

    auto upsert(NativeSocket socket, EventMask interest) -> std::expected<void, PollError> override;
    auto erase(NativeSocket socket) -> std::expected<void, PollError> override;
    auto wait(std::span<Event> out_events, std::int32_t timeout_ms)
        -> std::expected<std::size_t, PollError> override;

private:
    std::unordered_map<NativeSocket, EventMask> interest_;
};

}  // namespace tcp_server::net

