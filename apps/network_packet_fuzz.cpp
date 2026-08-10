#include "neoeng/core/network_security.hpp"
#include "neoeng/core/fuzz_cli.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

int main(int argc, char** argv) {
    using namespace neoeng::core;
    std::size_t iterations{};
    if (!parse_fuzz_iteration_count(
            argc, argv, 100'000U, 1'000'000U, "neoeng_network_packet_fuzz", iterations)) {
        return EXIT_FAILURE;
    }
    AuthenticationKey key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(0xA5U ^ index);
    }
    const NetworkSecurityLimits limits{
        .maximum_payload_bytes = 1'200U,
        .maximum_input_commands = 64U,
        .maximum_tracked_origins = 64U,
        .maximum_clock_skew_ms = 5'000U,
        .session_timeout_ms = 30'000U,
        .rate_limit_packets_per_second = 10'000U,
        .rate_limit_burst_packets = 10'000U,
    };
    NetworkSecurityGateway gateway(key, limits);
    std::mt19937_64 random(0x4E454F454E472D31ULL);
    std::uniform_int_distribution<std::size_t> size_distribution(0U, 2'048U);
    std::uniform_int_distribution<unsigned int> byte_distribution(0U, 255U);
    std::array<InputCommand, 64> commands{};
    std::uint64_t accepted{};
    std::uint64_t parsed{};
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        std::vector<std::uint8_t> datagram(size_distribution(random));
        for (std::uint8_t& byte : datagram) {
            byte = static_cast<std::uint8_t>(byte_distribution(random));
        }
        const PacketDecision decision = gateway.process(iteration % 64U, 1'000'000U, datagram);
        if (decision.accepted()) {
            ++accepted;
            if (parse_input_payload(decision.packet.payload, commands, limits).accepted()) {
                ++parsed;
            }
        }
    }
    std::cout << "{\n"
              << "  \"schema\": \"neoeng.dcore.network-fuzz.v1\",\n"
              << "  \"iterations\": " << iterations << ",\n"
              << "  \"accepted_authenticated_packets\": " << accepted << ",\n"
              << "  \"parsed_input_payloads\": " << parsed << ",\n"
              << "  \"status\": \"passed_without_exception\"\n"
              << "}\n";
    return EXIT_SUCCESS;
}
