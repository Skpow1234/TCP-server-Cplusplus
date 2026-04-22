#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <tcp_server/net/socket.hpp>

namespace tcp_server::net {

/// Per-connection lifecycle aligned with read → parse → handle → write → close.
enum class ConnectionState : std::uint8_t {
    Closed = 0,
    Reading,
    Parsing,
    Handling,
    Writing,
    Closing,
};

/// Owns the client socket and bounded logical read/write buffers (grow as needed by upper layers).
class Connection {
public:
    explicit Connection(Socket&& socket);

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

    ~Connection() = default;

    [[nodiscard]] auto socket() -> Socket& { return socket_; }
    [[nodiscard]] auto socket() const -> const Socket& { return socket_; }

    [[nodiscard]] auto state() const -> ConnectionState { return state_; }
    void set_state(ConnectionState state);

    [[nodiscard]] auto read_buffer() -> std::vector<std::byte>& { return read_buf_; }
    [[nodiscard]] auto read_buffer() const -> const std::vector<std::byte>& { return read_buf_; }

    [[nodiscard]] auto write_buffer() -> std::vector<std::byte>& { return write_buf_; }
    [[nodiscard]] auto write_buffer() const -> const std::vector<std::byte>& { return write_buf_; }

    void append_read(std::span<const std::byte> data);
    void consume_read(std::size_t byte_count);
    void append_write(std::span<const std::byte> data);
    void consume_write(std::size_t byte_count);
    void clear_write();

    [[nodiscard]] auto native_handle() const -> NativeSocket { return socket_.native_handle(); }

private:
    Socket socket_;
    ConnectionState state_{ConnectionState::Reading};
    std::vector<std::byte> read_buf_;
    std::vector<std::byte> write_buf_;
};

}  // namespace tcp_server::net
