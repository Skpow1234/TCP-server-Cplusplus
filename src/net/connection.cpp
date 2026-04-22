#include <tcp_server/net/connection.hpp>

#include <cassert>
#include <type_traits>

namespace tcp_server::net {
namespace {

[[nodiscard]] constexpr auto is_valid_connection_state(const ConnectionState s) noexcept -> bool {
    using U = std::underlying_type_t<ConnectionState>;
    const auto v = static_cast<U>(s);
    return v <= static_cast<U>(ConnectionState::Closing);
}

}  // namespace

Connection::Connection(Socket&& socket) : socket_(std::move(socket)) {}

void Connection::set_state(const ConnectionState state) {
    assert(is_valid_connection_state(state));
    state_ = state;
}

void Connection::append_read(std::span<const std::byte> data) {
    read_buf_.insert(read_buf_.end(), data.begin(), data.end());
}

void Connection::consume_read(std::size_t byte_count) {
    assert(byte_count <= read_buf_.size());
    if (byte_count >= read_buf_.size()) {
        read_buf_.clear();
        return;
    }
    read_buf_.erase(read_buf_.begin(), read_buf_.begin() + static_cast<std::ptrdiff_t>(byte_count));
}

void Connection::append_write(std::span<const std::byte> data) {
    write_buf_.insert(write_buf_.end(), data.begin(), data.end());
}

void Connection::consume_write(std::size_t byte_count) {
    assert(byte_count <= write_buf_.size());
    if (byte_count >= write_buf_.size()) {
        write_buf_.clear();
        return;
    }
    write_buf_.erase(write_buf_.begin(), write_buf_.begin() + static_cast<std::ptrdiff_t>(byte_count));
}

void Connection::clear_write() {
    write_buf_.clear();
}

}  // namespace tcp_server::net
