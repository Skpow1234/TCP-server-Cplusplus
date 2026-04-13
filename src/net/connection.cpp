#include <tcp_server/net/connection.hpp>

namespace tcp_server::net {

Connection::Connection(Socket&& socket) : socket_(std::move(socket)) {}

void Connection::append_read(std::span<const std::byte> data) {
    read_buf_.insert(read_buf_.end(), data.begin(), data.end());
}

void Connection::consume_read(std::size_t byte_count) {
    if (byte_count >= read_buf_.size()) {
        read_buf_.clear();
        return;
    }
    read_buf_.erase(read_buf_.begin(), read_buf_.begin() + static_cast<std::ptrdiff_t>(byte_count));
}

void Connection::append_write(std::span<const std::byte> data) {
    write_buf_.insert(write_buf_.end(), data.begin(), data.end());
}

void Connection::clear_write() {
    write_buf_.clear();
}

}  // namespace tcp_server::net
