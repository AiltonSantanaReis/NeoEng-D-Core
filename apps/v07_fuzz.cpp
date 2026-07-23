#include "neoeng/core/contact_solver.hpp"
#include "neoeng/core/hash.hpp"
#include "neoeng/core/interactive_rollback.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <vector>

namespace {

using namespace neoeng::core;

class Generator final {
public:
    explicit Generator(std::uint64_t seed) : state_(seed) {}
    [[nodiscard]] std::uint64_t next() noexcept {
        state_ ^= state_ << 13U;
        state_ ^= state_ >> 7U;
        state_ ^= state_ << 17U;
        return state_;
    }
    [[nodiscard]] std::size_t bounded(std::size_t bound) noexcept {
        return static_cast<std::size_t>(next() % bound);
    }
private:
    std::uint64_t state_;
};

[[nodiscard]] WorldState random_world(Generator& generator, std::size_t body_count) {
    WorldState world;
    world.bodies.reserve(body_count);
    for (std::size_t index = 0U; index < body_count; ++index) {
        const auto x = static_cast<Fixed::rep>(generator.bounded(81U)) - 40;
        const auto y = static_cast<Fixed::rep>(generator.bounded(81U)) - 40;
        const auto vx = static_cast<Fixed::rep>(generator.bounded(41U)) - 20;
        const auto vy = static_cast<Fixed::rep>(generator.bounded(41U)) - 20;
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(index + 1U),
            .position = {Fixed::from_ratio(x, 2), Fixed::from_ratio(y, 2)},
            .velocity = {Fixed::from_ratio(vx, 3), Fixed::from_ratio(vy, 3)},
        });
    }
    return world;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1
        ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
        : 5'000U;
    Generator generator(0x7A11C0FFEE123456ULL);
    std::uint64_t aggregate = 0U;
    ContactSolverConfig config{
        .half_extent = Fixed::from_ratio(1, 2),
        .cell_size = Fixed::from_integer(2),
        .position_iterations = 4U,
        .max_cells_per_body = 64U,
    };

    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        WorldState world = random_world(generator, 24U);
        ComponentWorldState current = make_component_world(world, 8U);
        const DeterministicActiveSet active = DeterministicActiveSet::from_world(world);
        const ComponentStepResult predicted = step_component_active(current, active, {});
        config.broadphase_mode = ContactBroadphaseMode::SweptCellRaster;
        const auto raster = swept_aabb_contacts(current, predicted.state, config);
        config.broadphase_mode = ContactBroadphaseMode::CenterGridExpansion;
        const auto center = swept_aabb_contacts(current, predicted.state, config);
        const auto brute = brute_force_swept_aabb_contacts(
            current, predicted.state, config.half_extent);
        if (raster != brute || center != brute) {
            std::cerr << "Swept broadphase mismatch at iteration " << iteration << '\n';
            return EXIT_FAILURE;
        }

        config.broadphase_mode = ContactBroadphaseMode::SweptCellRaster;
        const ContactStepResult first = step_component_contacts(current, active, {}, config);
        const ContactStepResult second = step_component_contacts(current, active, {}, config);
        if (first.state.materialize() != second.state.materialize()
            || first.active != second.active
            || first.contacts != second.contacts) {
            std::cerr << "Contact determinism mismatch at iteration " << iteration << '\n';
            return EXIT_FAILURE;
        }
        aggregate ^= stable_hash(first.state.materialize())
            + static_cast<std::uint64_t>(first.contacts.size() * 131U + iteration);

        if ((iteration % 97U) == 0U) {
            InteractiveRollbackConfig rollback_config;
            rollback_config.capacity = 24U;
            rollback_config.page_size = 8U;
            rollback_config.arena_bytes_per_epoch = 4096U;
            rollback_config.contacts = config;
            InteractiveRollbackEngine rollback(world, rollback_config);
            InteractiveRollbackEngine fresh(world, rollback_config);
            std::vector<std::vector<InputCommand>> inputs(10U);
            for (std::size_t frame = 0U; frame < inputs.size(); ++frame) {
                if ((frame % 3U) == 1U) {
                    inputs[frame].push_back(InputCommand{
                        .entity = static_cast<EntityId>(generator.bounded(world.bodies.size()) + 1U),
                        .acceleration = {
                            Fixed::from_ratio(static_cast<Fixed::rep>(generator.bounded(7U)) - 3, 11),
                            Fixed::from_ratio(static_cast<Fixed::rep>(generator.bounded(7U)) - 3, 13),
                        },
                    });
                }
                rollback.advance(inputs[frame]);
            }
            const InputCommand correction{
                .entity = static_cast<EntityId>(generator.bounded(world.bodies.size()) + 1U),
                .acceleration = {Fixed::from_ratio(1, 7), Fixed::from_ratio(-1, 9)},
            };
            inputs[3U] = {correction};
            for (const auto& frame_inputs : inputs) fresh.advance(frame_inputs);
            const std::size_t resimulated = rollback.correct_input_and_resimulate(
                3U, std::span<const InputCommand>(&correction, 1U));
            if (resimulated != 7U) {
                std::cerr << "Unexpected rollback depth at iteration " << iteration << '\n';
                return EXIT_FAILURE;
            }
            if (rollback.materialized_state() != fresh.materialized_state()) {
                std::cerr << "Interactive rollback mismatch at iteration " << iteration << '\n';
                return EXIT_FAILURE;
            }
            aggregate ^= stable_hash(rollback.materialized_state());
        }
    }

    std::cout << "v0.7 contact fuzz passed " << iterations
              << " iterations; hash=0x" << std::hex << std::uppercase
              << aggregate << '\n';
    return EXIT_SUCCESS;
}
