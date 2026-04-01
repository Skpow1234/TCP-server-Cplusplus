#include <tcp_server/net/select_poller.hpp>

#include <cstdint>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#else
#    include <cerrno>
#    include <sys/select.h>
#    include <sys/time.h>
#endif

namespace tcp_server::net {
namespace {

[[nodiscard]] auto make_err(const char* msg, int native_error) -> PollError {
    return PollError{msg, native_error};
}

[[nodiscard]] auto has(EventMask mask, EventMask bit) -> bool {
    return (mask & bit) != EventMask::None;
}

}  // namespace

auto SelectPoller::upsert(NativeSocket socket, EventMask interest) -> std::expected<void, PollError> {
    if (socket == k_invalid_socket) {
        return std::unexpected(make_err("invalid socket", 0));
    }
    interest_[socket] = interest;
    return {};
}

auto SelectPoller::erase(NativeSocket socket) -> std::expected<void, PollError> {
    (void)interest_.erase(socket);
    return {};
}

auto SelectPoller::wait(std::span<Event> out_events, std::int32_t timeout_ms)
    -> std::expected<std::size_t, PollError> {
    if (out_events.empty()) {
        return std::unexpected(make_err("out_events must not be empty", 0));
    }

    fd_set readfds{};
    fd_set writefds{};
    fd_set exceptfds{};
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    NativeSocket max_fd = 0;
    for (const auto& [sock, mask] : interest_) {
        if (has(mask, EventMask::Read)) {
            FD_SET(static_cast<SOCKET>(sock), &readfds);
        }
        if (has(mask, EventMask::Write)) {
            FD_SET(static_cast<SOCKET>(sock), &writefds);
        }
        if (has(mask, EventMask::Error)) {
            FD_SET(static_cast<SOCKET>(sock), &exceptfds);
        }
        if (sock > max_fd) {
            max_fd = sock;
        }
    }

    timeval tv{};
    timeval* tvp = nullptr;
    if (timeout_ms >= 0) {
        tv.tv_sec = static_cast<long>(timeout_ms / 1000);
        tv.tv_usec = static_cast<long>((timeout_ms % 1000) * 1000);
        tvp = &tv;
    }

#if defined(_WIN32)
    const int rc = ::select(0, &readfds, &writefds, &exceptfds, tvp);
    if (rc == SOCKET_ERROR) {
        return std::unexpected(make_err("select() failed", ::WSAGetLastError()));
    }
#else
    const int rc = ::select(static_cast<int>(max_fd + 1), &readfds, &writefds, &exceptfds, tvp);
    if (rc < 0) {
        return std::unexpected(make_err("select() failed", errno));
    }
#endif

    if (rc == 0) {
        return static_cast<std::size_t>(0);
    }

    std::size_t written = 0;
    for (const auto& [sock, _mask] : interest_) {
        if (written >= out_events.size()) {
            break;
        }
        EventMask fired = EventMask::None;
        if (FD_ISSET(static_cast<SOCKET>(sock), &readfds)) {
            fired = fired | EventMask::Read;
        }
        if (FD_ISSET(static_cast<SOCKET>(sock), &writefds)) {
            fired = fired | EventMask::Write;
        }
        if (FD_ISSET(static_cast<SOCKET>(sock), &exceptfds)) {
            fired = fired | EventMask::Error;
        }
        if (fired != EventMask::None) {
            out_events[written++] = Event{sock, fired};
        }
    }

    return written;
}

}  // namespace tcp_server::net

