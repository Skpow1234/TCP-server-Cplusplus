#include <tcp_server/net/socket.hpp>

#include <utility>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <cerrno>
#    include <cstring>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

namespace tcp_server::net {
namespace {

[[nodiscard]] auto make_error(SocketError::Code code, std::string msg, int native_error) -> SocketError {
    return SocketError{code, std::move(msg), native_error};
}

}  // namespace

NetworkSession::NetworkSession() {
#if defined(_WIN32)
    WSADATA wsa{};
    const int rc = ::WSAStartup(MAKEWORD(2, 2), &wsa);
    if (rc != 0) {
        ok_ = false;
        last_error_ = rc;
        return;
    }
    ok_ = true;
    last_error_ = 0;
#else
    ok_ = true;
    last_error_ = 0;
#endif
}

NetworkSession::~NetworkSession() {
#if defined(_WIN32)
    if (ok_) {
        (void)::WSACleanup();
    }
#endif
}

Socket::~Socket() {
    (void)close();
}

Socket::Socket(Socket&& other) noexcept : socket_(other.socket_) {
    other.socket_ = k_invalid_socket;
}

auto Socket::operator=(Socket&& other) noexcept -> Socket& {
    if (this == &other) {
        return *this;
    }
    (void)close();
    socket_ = other.socket_;
    other.socket_ = k_invalid_socket;
    return *this;
}

auto Socket::create_tcp_v4() -> std::expected<Socket, SocketError> {
#if defined(_WIN32)
    const auto s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return std::unexpected(make_error(SocketError::Code::CreateFailed, "socket() failed", ::WSAGetLastError()));
    }
    return Socket{static_cast<NativeSocket>(s)};
#else
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return std::unexpected(make_error(SocketError::Code::CreateFailed, "socket() failed", errno));
    }
    return Socket{s};
#endif
}

auto Socket::close() -> std::expected<void, SocketError> {
    if (socket_ == k_invalid_socket) {
        return {};
    }

#if defined(_WIN32)
    const auto s = static_cast<SOCKET>(socket_);
    socket_ = k_invalid_socket;
    if (::closesocket(s) != 0) {
        return std::unexpected(make_error(SocketError::Code::CloseFailed, "closesocket() failed", ::WSAGetLastError()));
    }
    return {};
#else
    const int s = socket_;
    socket_ = k_invalid_socket;
    if (::close(s) != 0) {
        return std::unexpected(make_error(SocketError::Code::CloseFailed, "close() failed", errno));
    }
    return {};
#endif
}

auto Socket::release() -> NativeSocket {
    auto out = socket_;
    socket_ = k_invalid_socket;
    return out;
}

void Socket::reset(NativeSocket s) {
    (void)close();
    socket_ = s;
}

}  // namespace tcp_server::net

