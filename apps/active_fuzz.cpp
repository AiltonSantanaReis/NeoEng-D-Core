#include "neoeng/core/active_world.hpp"
#include "neoeng/core/component_world.hpp"
#include "neoeng/core/hash.hpp"
#include "neoeng/core/radix_world.hpp"
#include "neoeng/core/simulation.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

using namespace neoeng::core;

namespace {

[[nodiscard]] std::uint64_t next_random(std::uint64_t& state) noexcept {
    state = state * 2'862'933'555'777'941'757ULL + 3'037'000'493ULL;
    return state;
}

[[nodiscard]] std::size_t iteration_count() {
    const char* value = std::getenv("NEOENG_ACTIVE_FUZZ_ITERATIONS");
    if (value == nullptr) return 20'000U;
    try {
        const unsigned long parsed = std::stoul(value);
        return parsed == 0UL ? 1U : static_cast<std::size_t>(parsed);
    } catch (...) {
        return 20'000U;
    }
}

[[nodiscard]] WorldState make_world(std::size_t count) {
    WorldState state;
    state.bodies.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        state.bodies.push_back(Body{
            .id = static_cast<EntityId>(index * 2U + 1U),
            .position = {
                Fixed::from_ratio(static_cast<Fixed::rep>(index % 29U), 13),
                Fixed::from_ratio(-static_cast<Fixed::rep>(index % 31U), 17)},
            .velocity = {},
        });
    }
    return state;
}

} // namespace

int main() {
    try {
        WorldState authoritative = make_world(257U);
        ImmutableWorldState binary = make_immutable_world(authoritative, 32U);
        ComponentWorldState component = make_component_world(authoritative, 32U);
        std::array<RadixWorldState, 3> radix{
            make_radix_world(authoritative, 32U, 16U),
            make_radix_world(authoritative, 32U, 32U),
            make_radix_world(authoritative, 32U, 64U),
        };
        DeterministicActiveSet binary_active;
        DeterministicActiveSet component_active;
        std::array<DeterministicActiveSet, 3> radix_active{};
        std::uint64_t random = 0x51A7E5EED1234ABFULL;

        const std::size_t iterations = iteration_count();
        for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
            const std::size_t command_count = static_cast<std::size_t>(next_random(random) % 5U);
            std::vector<InputCommand> inputs;
            inputs.reserve(command_count);
            for (std::size_t command = 0U; command < command_count; ++command) {
                const std::uint64_t sample = next_random(random);
                const std::size_t index = static_cast<std::size_t>(sample % 257U);
                const auto x = static_cast<Fixed::rep>((sample >> 11U) % 7U) - 3;
                const auto y = static_cast<Fixed::rep>((sample >> 19U) % 7U) - 3;
                inputs.push_back(InputCommand{
                    .entity = static_cast<EntityId>(index * 2U + 1U),
                    .acceleration = {Fixed::from_ratio(x, 5), Fixed::from_ratio(y, 7)},
                });
            }
            // Include unknown entity IDs to preserve the legacy ignore semantics.
            if ((iteration % 97U) == 0U) {
                inputs.push_back(InputCommand{
                    .entity = std::numeric_limits<EntityId>::max(),
                    .acceleration = {Fixed::from_integer(1), Fixed::from_integer(-1)},
                });
            }

            authoritative = step(authoritative, inputs);
            auto binary_result = step_immutable_active(binary, binary_active, inputs);
            binary = std::move(binary_result.state);
            binary_active = std::move(binary_result.active);
            auto component_result = step_component_active(component, component_active, inputs);
            component = std::move(component_result.state);
            component_active = std::move(component_result.active);
            for (std::size_t index = 0U; index < radix.size(); ++index) {
                auto result = step_radix_active(radix[index], radix_active[index], inputs);
                radix[index] = std::move(result.state);
                radix_active[index] = std::move(result.active);
            }

            if (binary.materialize() != authoritative
                || component.materialize() != authoritative) {
                std::cerr << "Active fuzz divergence at iteration " << iteration << '\n';
                return EXIT_FAILURE;
            }
            if (binary_active != component_active) {
                std::cerr << "Active-set divergence at iteration " << iteration << '\n';
                return EXIT_FAILURE;
            }
            for (std::size_t index = 0U; index < radix.size(); ++index) {
                if (radix[index].materialize() != authoritative
                    || radix_active[index] != binary_active) {
                    std::cerr << "Radix fuzz divergence at iteration " << iteration
                              << " fanout index " << index << '\n';
                    return EXIT_FAILURE;
                }
            }
        }
        std::cout << "Active-layout model fuzz passed " << iterations
                  << " iterations; hash=" << hash_hex(stable_hash(authoritative)) << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Active fuzz exception: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
