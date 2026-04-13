#pragma once

#include <cstdint>
#include <expected>
#include <string>

namespace tcp_server::net {

struct SocketError {
    enum class Code {
        NotInitialized,
        CreateFailed,
        CloseFailed,
        NonBlockingFailed,
    };

    Code code{};
    std::string message{};
    int native_error{};  // platform-specific error code when available
};

#if defined(_WIN32)
using NativeSocket = std::uintptr_t;  // SOCKET is an integer/pointer-sized type
constexpr NativeSocket k_invalid_socket = static_cast<NativeSocket>(~0ULL);  // INVALID_SOCKET
#else
using NativeSocket = int;
constexpr NativeSocket k_invalid_socket = -1;
#endif

// Windows-only: initializes Winsock (WSAStartup/WSACleanup).
// On non-Windows platforms, it's a no-op.
class NetworkSession {
public:
    NetworkSession();
    ~NetworkSession();

    NetworkSession(const NetworkSession&) = delete;
    NetworkSession& operator=(const NetworkSession&) = delete;
    NetworkSession(NetworkSession&&) = delete;
    NetworkSession& operator=(NetworkSession&&) = delete;

    [[nodiscard]] auto ok() const -> bool { return ok_; }
    [[nodiscard]] auto last_error() const -> int { return last_error_; }

private:
    bool ok_{false};
    int last_error_{0};
};

// RAII-owned socket handle (closes on destruction).
class Socket {
public:
    Socket() = default;
    explicit Socket(NativeSocket s) : socket_(s) {}
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    [[nodiscard]] static auto create_tcp_v4() -> std::expected<Socket, SocketError>;

    [[nodiscard]] auto valid() const -> bool { return socket_ != k_invalid_socket; }
    [[nodiscard]] auto native_handle() const -> NativeSocket { return socket_; }

    [[nodiscard]] auto close() -> std::expected<void, SocketError>;

    [[nodiscard]] auto set_nonblocking(bool enable) -> std::expected<void, SocketError>;

    [[nodiscard]] auto release() -> NativeSocket;
    void reset(NativeSocket s = k_invalid_socket);

private:
    NativeSocket socket_{k_invalid_socket};
};

}  // namespace tcp_server::net

