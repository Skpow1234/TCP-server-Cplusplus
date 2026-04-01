#include <tcp_server/net/accept.hpp>
#include <tcp_server/net/listener.hpp>
#include <tcp_server/net/socket.hpp>

#include <cassert>
#include <cstdint>
#include <thread>

#if defined(_WIN32)
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#else
#    include <arpa/inet.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

namespace {

auto connect_v4_loopback(std::uint16_t port) -> bool {
#if defined(_WIN32)
    const auto s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int rc = ::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    ::closesocket(s);
    return rc == 0;
#else
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const int rc = ::connect(s, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    ::close(s);
    return rc == 0;
#endif
}

}  // namespace

int main() {
    tcp_server::net::NetworkSession net;
    assert(net.ok());

    auto listener = tcp_server::net::Listener::bind_and_listen("127.0.0.1", 0, 16);
    assert(listener.has_value());
    assert(listener->valid());

    const auto port = tcp_server::net::local_port_v4(listener->socket());
    assert(port.has_value());
    assert(*port != 0);

    std::thread client([p = *port] {
        const bool ok = connect_v4_loopback(p);
        assert(ok);
    });

    const auto accepted = tcp_server::net::accept_one(listener->socket());
    assert(accepted.has_value());
    assert(accepted->valid());

    client.join();
    return 0;
}

