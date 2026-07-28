#include "neoeng/dcore_replica_adapter.hpp"
#include "neoeng/distributed_reference.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>

namespace {

using namespace std::chrono_literals;

[[nodiscard]] neoeng::core::WorldState initial_world() {
    return {
        .frame = 0U,
        .bodies = {
            {
                .id = 1U,
                .position = {},
                .velocity = {
                    neoeng::core::Fixed::from_integer(1),
                    neoeng::core::Fixed::from_integer(0),
                },
            },
        },
    };
}

[[nodiscard]] neoeng::core::InputCommand command_for(std::uint64_t frame) {
    return {
        .entity = 1U,
        .acceleration = {
            neoeng::core::Fixed::from_raw(
                static_cast<neoeng::core::Fixed::rep>(frame % 11U) - 5),
            neoeng::core::Fixed::from_raw(
                static_cast<neoeng::core::Fixed::rep>(frame % 7U) - 3),
        },
    };
}

} // namespace

int main() {
    try {
        constexpr std::uint64_t frame_count = 4'096U;
        constexpr std::uint64_t divergence_frame = 1'024U;
        neoeng::distributed_reference::DCoreReplicaAdapter authority(
            initial_world(), frame_count + 1U);
        neoeng::distributed_reference::DCoreReplicaAdapter follower(
            initial_world(), frame_count + 1U);
        for (std::uint64_t frame = 0U; frame < frame_count; ++frame) {
            const auto command = command_for(frame);
            authority.advance(std::span<const neoeng::core::InputCommand>{&command, 1U});
            if (frame == divergence_frame) {
                const neoeng::core::InputCommand wrong{
                    .entity = 1U,
                    .acceleration = {
                        neoeng::core::Fixed::from_integer(3),
                        neoeng::core::Fixed::from_integer(-2),
                    },
                };
                follower.advance(
                    std::span<const neoeng::core::InputCommand>{&wrong, 1U});
            } else {
                follower.advance(
                    std::span<const neoeng::core::InputCommand>{&command, 1U});
            }
        }

        neoeng::distributed_reference::TwoInstanceCoordinator coordinator(
            authority, follower);
        const auto before = coordinator.compare();
        const auto diagnosis = authority.diagnose_against(follower, 0xC5010U);
        auto [sender, receiver] =
            neoeng::distributed_reference::UdpReferenceEndpoint::make_loopback_pair();
        const auto after = coordinator.reconcile(
            divergence_frame, sender, receiver, 1s);

        std::cout << "{\n"
                  << "  \"schema\": \"neoeng.dcore.distributed-reference-result.v1\",\n"
                  << "  \"transport\": \"udp-loopback\",\n"
                  << "  \"instances\": 2,\n"
                  << "  \"frames\": " << frame_count << ",\n"
                  << "  \"divergence_frame\": " << divergence_frame << ",\n"
                  << "  \"before\": \""
                  << neoeng::distributed_reference::to_string(before.status) << "\",\n"
                  << "  \"localized\": "
                  << (diagnosis.divergent
                          && !diagnosis.first_divergent_component.empty()
                      ? "true" : "false")
                  << ",\n"
                  << "  \"first_divergent_component\": \""
                  << diagnosis.first_divergent_component << "\",\n"
                  << "  \"after\": \""
                  << neoeng::distributed_reference::to_string(after.status) << "\",\n"
                  << "  \"canonical_state_equal\": "
                  << (authority.state() == follower.state() ? "true" : "false") << "\n"
                  << "}\n";
        return before.status == neoeng::distributed_reference::CoordinationStatus::Divergent
                && diagnosis.divergent
                && !diagnosis.first_divergent_component.empty()
                && after.status == neoeng::distributed_reference::CoordinationStatus::Converged
                && authority.state() == follower.state()
            ? 0
            : 1;
    } catch (const std::exception& error) {
        std::cerr << "distributed_reference_probe_error=" << error.what() << '\n';
        return 2;
    }
}
