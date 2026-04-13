#include <tcp_server/app/dispatcher.hpp>

namespace tcp_server::app {

RequestDispatcher::~RequestDispatcher() = default;

auto EchoDispatcher::dispatch(std::span<const std::byte> request_payload)
    -> std::expected<std::vector<std::byte>, DispatchError> {
    return std::vector<std::byte>(request_payload.begin(), request_payload.end());
}

}  // namespace tcp_server::app
