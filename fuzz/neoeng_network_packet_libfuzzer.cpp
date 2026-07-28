#include "neoeng/core/network_security.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(
    const std::uint8_t* data,
    std::size_t size) {
    using namespace neoeng::core;
    AuthenticationKey key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(0xA5U ^ index);
    }
    const NetworkSecurityLimits limits{
        .maximum_payload_bytes = 1'200U,
        .maximum_input_commands = 64U,
        .maximum_tracked_origins = 4U,
        .maximum_clock_skew_ms = 5'000U,
        .session_timeout_ms = 30'000U,
        .rate_limit_packets_per_second = 1'000U,
        .rate_limit_burst_packets = 1'000U,
    };
    NetworkSecurityGateway gateway(key, limits);
    const std::span<const std::uint8_t> datagram{
        data == nullptr ? reinterpret_cast<const std::uint8_t*>("") : data,
        size,
    };
    const PacketDecision decision = gateway.process(
        size == 0U ? 1U : static_cast<OriginId>(data[0]) + 1U,
        1'000'000U,
        datagram);
    if (decision.accepted()) {
        std::array<InputCommand, 64> commands{};
        static_cast<void>(
            parse_input_payload(decision.packet.payload, commands, limits));
    }
    return 0;
}
