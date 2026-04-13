#include <tcp_server/net/recv.hpp>

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

[[nodiscard]] auto recv_err(RecvError::Code code, std::string msg, int native) -> RecvError {
    return RecvError{code, std::move(msg), native};
}

}  // namespace

auto receive_nonblocking(Connection& conn, std::span<std::byte> scratch) -> std::expected<std::size_t, RecvError> {
    if (scratch.empty()) {
        return std::unexpected(recv_err(RecvError::Code::InvalidArgument, "scratch buffer is empty", 0));
    }
    if (!conn.socket().valid()) {
        return std::unexpected(recv_err(RecvError::Code::IoError, "connection socket is invalid", 0));
    }

#if defined(_WIN32)
    const int received = ::recv(
        static_cast<SOCKET>(conn.native_handle()),
        reinterpret_cast<char*>(scratch.data()),
        static_cast<int>(scratch.size()),
        0);
    if (received == SOCKET_ERROR) {
        const int err = ::WSAGetLastError();
        if (err == WSAEWOULDBLOCK || err == WSAEINPROGRESS) {
            return std::unexpected(recv_err(RecvError::Code::WouldBlock, "recv would block", err));
        }
        return std::unexpected(recv_err(RecvError::Code::IoError, "recv failed", err));
    }
    if (received == 0) {
        return std::unexpected(recv_err(RecvError::Code::Closed, "peer closed connection", 0));
    }
    const auto chunk = scratch.first(static_cast<std::size_t>(received));
    conn.append_read(chunk);
    return chunk.size();
#else
    ssize_t received = 0;
    do {
        received = ::recv(
            static_cast<int>(conn.native_handle()),
            scratch.data(),
            scratch.size(),
            0);
    } while (received < 0 && errno == EINTR);

    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return std::unexpected(recv_err(RecvError::Code::WouldBlock, "recv would block", errno));
        }
        return std::unexpected(recv_err(RecvError::Code::IoError, "recv failed", errno));
    }
    if (received == 0) {
        return std::unexpected(recv_err(RecvError::Code::Closed, "peer closed connection", 0));
    }
    const auto chunk = scratch.first(static_cast<std::size_t>(received));
    conn.append_read(chunk);
    return chunk.size();
#endif
}

}  // namespace tcp_server::net
