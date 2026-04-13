#include <tcp_server/net/send.hpp>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#else
#    include <cerrno>
#    include <sys/socket.h>
#endif

namespace tcp_server::net {
namespace {

[[nodiscard]] auto send_err(SendError::Code code, std::string msg, int native) -> SendError {
    return SendError{code, std::move(msg), native};
}

}  // namespace

auto flush_write_nonblocking(Connection& conn) -> std::expected<std::size_t, SendError> {
    auto& wb = conn.write_buffer();
    if (wb.empty()) {
        return 0;
    }
    if (!conn.socket().valid()) {
        return std::unexpected(send_err(SendError::Code::IoError, "connection socket is invalid", 0));
    }

#if defined(_WIN32)
    const int sent = ::send(
        static_cast<SOCKET>(conn.native_handle()),
        reinterpret_cast<const char*>(wb.data()),
        static_cast<int>(wb.size()),
        0);
    if (sent == SOCKET_ERROR) {
        const int err = ::WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
            return std::unexpected(send_err(SendError::Code::WouldBlock, "send would block", err));
        }
        return std::unexpected(send_err(SendError::Code::IoError, "send failed", err));
    }
    if (sent <= 0) {
        return std::unexpected(send_err(SendError::Code::IoError, "send returned non-positive", 0));
    }
    conn.consume_write(static_cast<std::size_t>(sent));
    return static_cast<std::size_t>(sent);
#else
#    if defined(MSG_NOSIGNAL)
    const int flags = MSG_NOSIGNAL;
#    else
    const int flags = 0;
#    endif

    ssize_t sent = 0;
    do {
        sent = ::send(
            static_cast<int>(conn.native_handle()),
            wb.data(),
            wb.size(),
            flags);
    } while (sent < 0 && errno == EINTR);

    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return std::unexpected(send_err(SendError::Code::WouldBlock, "send would block", errno));
        }
        return std::unexpected(send_err(SendError::Code::IoError, "send failed", errno));
    }
    if (sent == 0) {
        return std::unexpected(send_err(SendError::Code::IoError, "send returned 0", 0));
    }
    conn.consume_write(static_cast<std::size_t>(sent));
    return static_cast<std::size_t>(sent);
#endif
}

}  // namespace tcp_server::net
