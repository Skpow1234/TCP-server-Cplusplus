#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

#include <tcp_server/net/socket.hpp>

namespace tcp_server::net {

struct PollError {
    std::string message{};
    int native_error{};  // platform-specific error code when available
};

enum class EventMask : std::uint8_t {
    None = 0,
    Read = 1u << 0,
    Write = 1u << 1,
    Error = 1u << 2,
};

[[nodiscard]] constexpr auto operator|(EventMask a, EventMask b) -> EventMask {
    return static_cast<EventMask>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}

[[nodiscard]] constexpr auto operator&(EventMask a, EventMask b) -> EventMask {
    return static_cast<EventMask>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}

struct Event {
    NativeSocket socket{k_invalid_socket};
    EventMask mask{EventMask::None};
};

// Interface for platform-specific polling backends (select/poll/epoll/kqueue/IOCP).
//
// Invariants:
// - `socket` must refer to a non-owning handle; ownership stays with connection/acceptor.
// - Backends must not block indefinitely when `timeout_ms` is finite.
class Poller {
public:
    virtual ~Poller() = default;

    Poller() = default;
    Poller(const Poller&) = delete;
    Poller& operator=(const Poller&) = delete;
    Poller(Poller&&) = delete;
    Poller& operator=(Poller&&) = delete;

    // Register or update interest for a socket.
    virtual auto upsert(NativeSocket socket, EventMask interest) -> std::expected<void, PollError> = 0;

    // Remove a socket from polling.
    virtual auto erase(NativeSocket socket) -> std::expected<void, PollError> = 0;

    // Wait for events and write them into `out_events` (up to its size).
    // Returns number of events written.
    virtual auto wait(std::span<Event> out_events, std::int32_t timeout_ms)
        -> std::expected<std::size_t, PollError> = 0;
};

}  // namespace tcp_server::net

