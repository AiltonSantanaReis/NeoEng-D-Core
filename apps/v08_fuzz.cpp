#include "neoeng/core/hash.hpp"
#include "neoeng/core/interactive_rollback.hpp"
#include "neoeng/core/temporal_contact.hpp"

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
        const Fixed::rep x = static_cast<Fixed::rep>(generator.bounded(121U)) - 60;
        const Fixed::rep y = static_cast<Fixed::rep>(generator.bounded(121U)) - 60;
        const Fixed::rep vx = static_cast<Fixed::rep>(generator.bounded(25U)) - 12;
        const Fixed::rep vy = static_cast<Fixed::rep>(generator.bounded(25U)) - 12;
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(index + 1U),
            .position = {Fixed::from_ratio(x, 4), Fixed::from_ratio(y, 4)},
            .velocity = {Fixed::from_ratio(vx, 3), Fixed::from_ratio(vy, 3)},
        });
    }
    return world;
}

[[nodiscard]] std::vector<InputCommand> random_inputs(
    Generator& generator,
    std::size_t body_count) {
    std::vector<InputCommand> inputs;
    const std::size_t count = generator.bounded(3U);
    inputs.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const Fixed::rep ax = static_cast<Fixed::rep>(generator.bounded(401U)) - 200;
        const Fixed::rep ay = static_cast<Fixed::rep>(generator.bounded(401U)) - 200;
        inputs.push_back(InputCommand{
            .entity = static_cast<EntityId>(generator.bounded(body_count) + 1U),
            .acceleration = {Fixed::from_ratio(ax, 7), Fixed::from_ratio(ay, 9)},
        });
    }
    return inputs;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1
        ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
        : 5'000U;
    Generator generator(0x8F47A11CE0DDF00DULL);
    std::uint64_t aggregate = 0U;
    ContactSolverConfig contacts{
        .half_extent = Fixed::from_ratio(1, 2),
        .cell_size = Fixed::from_integer(2),
        .position_iterations = 4U,
        .max_cells_per_body = 64U,
        .broadphase_mode = ContactBroadphaseMode::SweptCellRaster,
    };
    const TemporalBroadphaseConfig temporal{
        .horizon_frames = 8U,
        .incremental_body_limit = 64U,
    };
    const ComponentStepOptions options{.kernel_mode = FixedKernelMode::Scalar};

    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        const std::size_t body_count = 2U + generator.bounded(23U);
        WorldState world = random_world(generator, body_count);
        ComponentWorldState reference_state = make_component_world(world, 8U);
        ComponentWorldState temporal_state = reference_state;
        DeterministicActiveSet reference_active = DeterministicActiveSet::from_world(world);
        DeterministicActiveSet temporal_active = reference_active;
        TemporalBroadphaseState cache = make_temporal_broadphase(
            temporal_state, contacts, temporal);
        PersistentManifoldState manifold{};

        for (std::size_t frame = 0U; frame < 4U; ++frame) {
            const std::vector<InputCommand> inputs = random_inputs(generator, body_count);
            ComponentStepResult reference_integrated = step_component_active(
                reference_state, reference_active, inputs, options);
            std::vector<SweptContact> reference_contacts = brute_force_swept_aabb_contacts(
                reference_state, reference_integrated.state, contacts.half_extent);
            ContactStepResult reference = solve_component_contact_constraints(
                reference_state, std::move(reference_integrated),
                std::move(reference_contacts), contacts);
            TemporalContactStepResult candidate = step_component_contacts_temporal(
                temporal_state, temporal_active, cache, manifold,
                inputs, contacts, temporal, options);
            if (reference.state.materialize() != candidate.contact.state.materialize()
                || reference.active != candidate.contact.active
                || reference.contacts != candidate.contact.contacts) {
                std::cerr << "Temporal contact mismatch at iteration " << iteration
                          << ", frame " << frame
                          << ", bodies=" << body_count
                          << ", inputs=" << inputs.size()
                          << ", reference_contacts=" << reference.contacts.size()
                          << ", temporal_contacts=" << candidate.contact.contacts.size()
                          << ", cached_pairs=" << candidate.broadphase.pairs().size()
                          << ", reference_hash=" << hash_hex(stable_hash(reference.state.materialize()))
                          << ", temporal_hash=" << hash_hex(stable_hash(candidate.contact.state.materialize()))
                          << '\n';
                for (const SweptContact& contact : reference.contacts) {
                    std::cerr << " reference_contact " << contact.first << '-' << contact.second
                              << " axis=" << static_cast<int>(contact.axis)
                              << " toi=" << contact.toi.raw()
                              << " initial=" << contact.initial_overlap
                              << " final=" << contact.final_overlap << '\n';
                }
                for (const SweptContact& contact : candidate.contact.contacts) {
                    std::cerr << " temporal_contact " << contact.first << '-' << contact.second
                              << " axis=" << static_cast<int>(contact.axis)
                              << " toi=" << contact.toi.raw()
                              << " initial=" << contact.initial_overlap
                              << " final=" << contact.final_overlap << '\n';
                }
                std::cerr << " temporal builds=" << candidate.temporal_stats.pair_cache_builds
                          << " updates=" << candidate.temporal_stats.pair_cache_incremental_updates
                          << " reuses=" << candidate.temporal_stats.pair_cache_reuses
                          << " escaped=" << candidate.temporal_stats.escaped_bodies
                          << " tested=" << candidate.temporal_stats.fat_bounds_tested << '\n';
                for (const InputCommand& input : inputs) {
                    std::cerr << " input entity=" << input.entity
                              << " ax=" << input.acceleration.x.raw()
                              << " ay=" << input.acceleration.y.raw() << '\n';
                }
                for (std::size_t index = 0U; index < reference_state.body_count(); ++index) {
                    const Body body = reference_state.body_at(index);
                    std::cerr << " body " << index << " id=" << body.id
                              << " px=" << body.position.x.raw()
                              << " py=" << body.position.y.raw()
                              << " vx=" << body.velocity.x.raw()
                              << " vy=" << body.velocity.y.raw() << '\n';
                }
                return EXIT_FAILURE;
            }
            if (!temporal_cache_is_conservative(
                    candidate.contact.state, candidate.broadphase, contacts)) {
                std::cerr << "Temporal cache lost a current overlap at iteration "
                          << iteration << ", frame " << frame << '\n';
                return EXIT_FAILURE;
            }
            reference_state = std::move(reference.state);
            reference_active = std::move(reference.active);
            temporal_state = std::move(candidate.contact.state);
            temporal_active = std::move(candidate.contact.active);
            cache = std::move(candidate.broadphase);
            manifold = std::move(candidate.manifold);
        }
        aggregate ^= stable_hash(temporal_state.materialize())
            + static_cast<std::uint64_t>(iteration * 0x9E37U + manifold.contacts.size());

        if ((iteration % 41U) == 0U) {
            InteractiveRollbackConfig temporal_config;
            temporal_config.capacity = 24U;
            temporal_config.page_size = 8U;
            temporal_config.contacts = contacts;
            temporal_config.temporal = temporal;
            temporal_config.use_temporal_cache = true;
            temporal_config.step_options = options;
            InteractiveRollbackConfig reference_config = temporal_config;
            reference_config.use_temporal_cache = false;

            InteractiveRollbackEngine rollback(world, temporal_config);
            InteractiveRollbackEngine authoritative(world, reference_config);
            std::vector<std::vector<InputCommand>> history(10U);
            for (std::size_t frame = 0U; frame < history.size(); ++frame) {
                history[frame] = random_inputs(generator, body_count);
                rollback.advance(history[frame]);
            }
            history[3U] = random_inputs(generator, body_count);
            for (const auto& frame_inputs : history) authoritative.advance(frame_inputs);
            const std::size_t resimulated = rollback.correct_input_and_resimulate(
                3U, history[3U]);
            if (resimulated != 7U
                || rollback.materialized_state() != authoritative.materialized_state()) {
                std::cerr << "Temporal rollback mismatch at iteration " << iteration << '\n';
                return EXIT_FAILURE;
            }
            aggregate ^= stable_hash(rollback.materialized_state());
        }
    }

    std::cout << "v0.8 fuzz passed: iterations=" << iterations
              << ", aggregate=" << hash_hex(aggregate) << '\n';
    return EXIT_SUCCESS;
}
