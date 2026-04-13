#pragma once

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace tcp_server::app {

/// Recoverable handler failure (echo baseline does not emit errors).
struct DispatchError {
    enum class Code : std::uint8_t {
        Internal,
    };

    Code code{};
    std::string message{};
};

/// Maps one decoded request payload to a response payload. Framing is applied by the caller.
class RequestDispatcher {
public:
    RequestDispatcher() = default;
    virtual ~RequestDispatcher();

    RequestDispatcher(const RequestDispatcher&) = delete;
    RequestDispatcher& operator=(const RequestDispatcher&) = delete;
    RequestDispatcher(RequestDispatcher&&) = delete;
    RequestDispatcher& operator=(RequestDispatcher&&) = delete;

    [[nodiscard]] virtual auto dispatch(std::span<const std::byte> request_payload)
        -> std::expected<std::vector<std::byte>, DispatchError> = 0;
};

/// Returns the request bytes unchanged (echo baseline for milestone 25).
class EchoDispatcher final : public RequestDispatcher {
public:
    EchoDispatcher() = default;

    [[nodiscard]] auto dispatch(std::span<const std::byte> request_payload)
        -> std::expected<std::vector<std::byte>, DispatchError> override;
};

}  // namespace tcp_server::app
