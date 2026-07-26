#include "neoeng/distributed_reference.hpp"

#include <chrono>

int main() {
    using namespace std::chrono_literals;
    auto [sender, receiver] =
        neoeng::distributed_reference::UdpReferenceEndpoint::make_loopback_pair();
    if (sender.enqueue({
            .message_kind = 9U,
            .channel_id = 10U,
            .frame = 11U,
            .payload = {12U},
        }) != neoeng::distributed_reference::TransportStatus::Accepted) {
        return 1;
    }
    if (sender.flush() != neoeng::distributed_reference::TransportStatus::Accepted) {
        return 2;
    }
    const auto result = receiver.receive(500ms);
    return result.accepted() && result.datagram.payload == std::vector<std::uint8_t>{12U}
        ? 0
        : 3;
}
