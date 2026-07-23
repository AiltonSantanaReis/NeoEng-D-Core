#include "neoeng/core/hash.hpp"
#include "neoeng/core/interactive_rollback.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

namespace {
using namespace neoeng::core;

[[nodiscard]] WorldState random_chain(std::mt19937_64& generator, std::size_t count) {
    std::uniform_int_distribution<int> extra(1, 64);
    const Fixed::rep base = static_cast<Fixed::rep>(count + static_cast<std::size_t>(extra(generator)));
    WorldState world;
    world.bodies.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const Fixed::rep numerator = base - static_cast<Fixed::rep>(index);
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(index + 1U),
            .position = {Fixed::from_integer(static_cast<Fixed::rep>(index)), {}},
            .velocity = {Fixed::from_ratio(numerator, 64), {}},
        });
    }
    return world;
}

[[nodiscard]] InteractiveRollbackConfig make_config(bool warm) {
    InteractiveRollbackConfig config;
    config.capacity = 24U;
    config.page_size = 64U;
    config.contacts.connected_solver_mode = ConnectedContactSolverMode::ChainIsotonic;
    config.contacts.enable_chain_warm_start = warm;
    config.contacts.max_cells_per_body = 64U;
    config.temporal = TemporalBroadphaseConfig{
        .horizon_frames = 8U,
        .incremental_body_limit = 128U,
    };
    config.use_temporal_cache = true;
    config.step_options = ComponentStepOptions{.kernel_mode = FixedKernelMode::Scalar};
    return config;
}

[[nodiscard]] WideInteger sum_velocity_x(const WorldState& state) {
    WideInteger sum = 0;
    for (const Body& body : state.bodies) sum += body.velocity.x.raw();
    return sum;
}

[[nodiscard]] bool chain_invariants(const WorldState& state) {
    const Fixed::rep diameter = Fixed::from_integer(1).raw();
    for (std::size_t index = 1U; index < state.bodies.size(); ++index) {
        const Body& previous = state.bodies[index - 1U];
        const Body& current = state.bodies[index];
        const WideInteger separation = static_cast<WideInteger>(current.position.x.raw())
            - previous.position.x.raw();
        if (separation < diameter) return false;
        if (previous.velocity.x > current.velocity.x) return false;
        if (previous.position.y != current.position.y) return false;
    }
    return true;
}

[[nodiscard]] bool projection_kkt(const WorldState& before, const WorldState& after) {
    if (before.bodies.size() != after.bodies.size()) return false;
    WideInteger velocity_prefix = 0;
    WideInteger position_prefix = 0;
    Fixed::rep previous_velocity = std::numeric_limits<Fixed::rep>::min();
    Fixed::rep previous_shifted_position = std::numeric_limits<Fixed::rep>::min();
    for (std::size_t index = 0U; index < before.bodies.size(); ++index) {
        const Fixed::rep input_velocity = before.bodies[index].velocity.x.raw();
        const Fixed::rep output_velocity = after.bodies[index].velocity.x.raw();
        velocity_prefix += static_cast<WideInteger>(input_velocity) - output_velocity;
        if (velocity_prefix < 0 || output_velocity < previous_velocity) return false;
        previous_velocity = output_velocity;

        const Fixed predicted = before.bodies[index].position.x
            + before.bodies[index].velocity.x * kSimulationDelta;
        const WideInteger offset = static_cast<WideInteger>(index)
            * Fixed::from_integer(1).raw();
        const Fixed::rep input_shifted = static_cast<Fixed::rep>(
            static_cast<WideInteger>(predicted.raw()) - offset);
        const Fixed::rep output_shifted = static_cast<Fixed::rep>(
            static_cast<WideInteger>(after.bodies[index].position.x.raw()) - offset);
        position_prefix += static_cast<WideInteger>(input_shifted) - output_shifted;
        if (position_prefix < 0 || output_shifted < previous_shifted_position) return false;
        previous_shifted_position = output_shifted;
    }
    const WideInteger bound = static_cast<WideInteger>(before.bodies.size());
    return velocity_prefix >= 0 && velocity_prefix < bound
        && position_prefix >= 0 && position_prefix < bound;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1
        ? static_cast<std::size_t>(std::stoull(argv[1])) : 5'000U;
    std::mt19937_64 generator(0x4E454F454E475639ULL);
    std::uniform_int_distribution<std::size_t> count_distribution(3U, 64U);
    std::uint64_t aggregate = 0U;

    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        const WorldState initial = random_chain(generator, count_distribution(generator));
        InteractiveRollbackEngine cold(initial, make_config(false));
        InteractiveRollbackEngine warm(initial, make_config(true));
        WorldState previous_state = initial;
        for (std::size_t frame = 0U; frame < 8U; ++frame) {
            cold.advance({});
            warm.advance({});
            const WorldState cold_state = cold.materialized_state();
            const WorldState warm_state = warm.materialized_state();
            if (cold_state != warm_state) {
                std::cerr << "cold/warm divergence at iteration " << iteration
                          << ", frame " << frame << '\n';
                return EXIT_FAILURE;
            }
            if (!chain_invariants(warm_state) || !projection_kkt(previous_state, warm_state)) {
                std::cerr << "chain invariant/KKT failure at iteration " << iteration
                          << ", frame " << frame << '\n';
                return EXIT_FAILURE;
            }
            previous_state = warm_state;
        }
        const WorldState final_state = warm.materialized_state();
        WideInteger momentum_error = sum_velocity_x(final_state) - sum_velocity_x(initial);
        if (momentum_error < 0) momentum_error = -momentum_error;
        if (momentum_error >= static_cast<WideInteger>(initial.bodies.size())) {
            std::cerr << "momentum rounding bound exceeded at iteration " << iteration << '\n';
            return EXIT_FAILURE;
        }
        const InteractiveRollbackStats before = warm.stats();
        const std::size_t depth = warm.correct_input_and_resimulate(0U, {});
        if (depth != 8U || warm.materialized_state() != final_state) {
            std::cerr << "rollback/manifold restoration failure at iteration "
                      << iteration << '\n';
            return EXIT_FAILURE;
        }
        const InteractiveRollbackStats after = warm.stats();
        if (after.cumulative_solver.chain_solver_accepts
                <= before.cumulative_solver.chain_solver_accepts
            || after.cumulative_solver.warm_start_accepts
                <= before.cumulative_solver.warm_start_accepts) {
            std::cerr << "chain or warm-start path was not exercised at iteration "
                      << iteration << " before_chain=" << before.cumulative_solver.chain_solver_accepts
                      << " after_chain=" << after.cumulative_solver.chain_solver_accepts
                      << " before_warm=" << before.cumulative_solver.warm_start_accepts
                      << " after_warm=" << after.cumulative_solver.warm_start_accepts << '\n';
            return EXIT_FAILURE;
        }
        aggregate ^= stable_hash(final_state)
            + static_cast<std::uint64_t>(iteration * 0x9E3779B1U)
            + after.cumulative_solver.warm_start_accepts;
    }

    std::cout << "v0.9 fuzz passed: iterations=" << iterations
              << ", aggregate=" << hash_hex(aggregate) << '\n';
    return EXIT_SUCCESS;
}
