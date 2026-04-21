#include <tcp_server/net/idle_timeout_manager.hpp>

namespace tcp_server::net {

IdleTimeoutManager::IdleTimeoutManager(std::chrono::milliseconds idle_after) : idle_after_(idle_after) {}

void IdleTimeoutManager::register_connection(const NativeSocket socket, const time_point now) {
    last_activity_[socket] = now;
}

void IdleTimeoutManager::unregister_connection(const NativeSocket socket) {
    (void)last_activity_.erase(socket);
}

void IdleTimeoutManager::touch_activity(const NativeSocket socket, const time_point now) {
    const auto it = last_activity_.find(socket);
    if (it != last_activity_.end()) {
        it->second = now;
    }
}

auto IdleTimeoutManager::poll_expired(const time_point now) -> std::vector<NativeSocket> {
    if (idle_after_.count() == 0) {
        return {};
    }

    std::vector<NativeSocket> expired;
    for (auto it = last_activity_.begin(); it != last_activity_.end();) {
        if (now - it->second >= idle_after_) {
            expired.push_back(it->first);
            it = last_activity_.erase(it);
        } else {
            ++it;
        }
    }
    return expired;
}

}  // namespace tcp_server::net
