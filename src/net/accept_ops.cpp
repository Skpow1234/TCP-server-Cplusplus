#include <tcp_server/net/accept.hpp>

#include <cstdint>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <cerrno>
#    include <netinet/in.h>
#    include <sys/socket.h>
#endif

namespace tcp_server::net {
namespace {

[[nodiscard]] auto make_err(const char* msg, int native_error) -> AcceptError {
    return AcceptError{msg, native_error};
}

}  // namespace

auto accept_one(const Socket& listener) -> std::expected<Socket, AcceptError> {
    if (!listener.valid()) {
        return std::unexpected(make_err("listener socket is invalid", 0));
    }

#if defined(_WIN32)
    const auto s = ::accept(static_cast<SOCKET>(listener.native_handle()), nullptr, nullptr);
    if (s == INVALID_SOCKET) {
        return std::unexpected(make_err("accept() failed", ::WSAGetLastError()));
    }
    return Socket{static_cast<NativeSocket>(s)};
#else
    const int s = ::accept(listener.native_handle(), nullptr, nullptr);
    if (s < 0) {
        return std::unexpected(make_err("accept() failed", errno));
    }
    return Socket{s};
#endif
}

auto local_port_v4(const Socket& listener) -> std::expected<std::uint16_t, AcceptError> {
    if (!listener.valid()) {
        return std::unexpected(make_err("listener socket is invalid", 0));
    }

#if defined(_WIN32)
    sockaddr_in addr{};
    int len = sizeof(addr);
    if (::getsockname(static_cast<SOCKET>(listener.native_handle()), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return std::unexpected(make_err("getsockname() failed", ::WSAGetLastError()));
    }
    return static_cast<std::uint16_t>(ntohs(addr.sin_port));
#else
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (::getsockname(listener.native_handle(), reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        return std::unexpected(make_err("getsockname() failed", errno));
    }
    return static_cast<std::uint16_t>(ntohs(addr.sin_port));
#endif
}

}  // namespace tcp_server::net
