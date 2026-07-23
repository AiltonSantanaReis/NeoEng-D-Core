#include "neoeng/core/fixed_raa_active_island_shadow.hpp"

#include "neoeng/core/component_world.hpp"
#include "neoeng/core/contact_solver.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace neoeng::core {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::uint64_t kHashOffset = 0xCBF29CE484222325ULL;
constexpr std::uint64_t kHashPrime = 0x100000001B3ULL;

void mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= kHashPrime;
    }
}

[[nodiscard]] std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t value = state;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] Fixed milli(std::int64_t value) {
    return Fixed::from_ratio(value, 1'000);
}

[[nodiscard]] std::uint64_t world_hash(const ComponentWorldState& state) {
    std::uint64_t hash = kHashOffset;
    mix(hash, state.frame());
    mix(hash, state.body_count());
    for (std::size_t index = 0U; index < state.body_count(); ++index) {
        mix(hash, state.entity_at(index));
        mix(hash, static_cast<std::uint64_t>(state.position_x_at(index).raw()));
        mix(hash, static_cast<std::uint64_t>(state.position_y_at(index).raw()));
        mix(hash, static_cast<std::uint64_t>(state.velocity_x_at(index).raw()));
        mix(hash, static_cast<std::uint64_t>(state.velocity_y_at(index).raw()));
    }
    return hash;
}


[[nodiscard]] bool same_dirty(const DirtySet& first, const DirtySet& second) noexcept {
    if (first.entity_count() != second.entity_count()
        || first.changed_count() != second.changed_count()
        || first.words().size() != second.words().size()) {
        return false;
    }
    for (std::size_t index = 0U; index < first.entity_count(); ++index) {
        if (first.mask(index) != second.mask(index)) return false;
    }
    return true;
}

[[nodiscard]] std::size_t topology_index(IslandTopology topology) noexcept {
    return static_cast<std::size_t>(topology);
}

void add_body(WorldState& world, Fixed x, Fixed y, std::uint64_t& rng) {
    const auto velocity_component = [&rng]() {
        return milli(static_cast<std::int64_t>(splitmix64(rng) % 41U) - 20);
    };
    world.bodies.push_back(Body{
        .id = static_cast<EntityId>(world.bodies.size() + 1U),
        .position = {x, y},
        .velocity = {velocity_component(), velocity_component()},
    });
}

[[nodiscard]] std::pair<Fixed, Fixed> cluster_origin(std::size_t cluster) {
    const std::size_t column = cluster % 12U;
    const std::size_t row = cluster / 12U;
    return {
        Fixed::from_integer(static_cast<Fixed::rep>(column * 8U)),
        Fixed::from_integer(static_cast<Fixed::rep>(row * 8U)),
    };
}

[[nodiscard]] WorldState make_world(
    std::size_t groups_per_topology, std::uint64_t seed) {
    WorldState world;
    std::size_t cluster = 0U;
    std::uint64_t rng = seed;
    const Fixed near = milli(950);
    const Fixed grid = milli(900);

    for (std::size_t group = 0U; group < groups_per_topology; ++group) {
        const auto [x, y] = cluster_origin(cluster++);
        add_body(world, x, y, rng);
        add_body(world, x + near, y, rng);
    }
    for (std::size_t group = 0U; group < groups_per_topology; ++group) {
        const auto [x, y] = cluster_origin(cluster++);
        for (std::size_t index = 0U; index < 6U; ++index) {
            add_body(world, x + near * Fixed::from_integer(
                static_cast<Fixed::rep>(index)), y, rng);
        }
    }
    for (std::size_t group = 0U; group < groups_per_topology; ++group) {
        const auto [x, y] = cluster_origin(cluster++);
        add_body(world, x, y, rng);
        add_body(world, x - near, y - near, rng);
        add_body(world, x - near, y + near, rng);
        add_body(world, x + near, y - near, rng);
        add_body(world, x + near, y + near, rng);
    }
    for (std::size_t group = 0U; group < groups_per_topology; ++group) {
        const auto [x, y] = cluster_origin(cluster++);
        add_body(world, x, y + near, rng);
        add_body(world, x + near, y, rng);
        add_body(world, x, y - near, rng);
        add_body(world, x - near, y, rng);
    }
    for (std::size_t group = 0U; group < groups_per_topology; ++group) {
        const auto [x, y] = cluster_origin(cluster++);
        for (std::size_t row = 0U; row < 3U; ++row) {
            for (std::size_t column = 0U; column < 3U; ++column) {
                add_body(world,
                    x + grid * Fixed::from_integer(static_cast<Fixed::rep>(column)),
                    y + grid * Fixed::from_integer(static_cast<Fixed::rep>(row)), rng);
            }
        }
    }
    return world;
}

[[nodiscard]] Fixed axis_position(
    const ComponentWorldState& state, std::size_t body, ContactAxis axis) {
    return axis == ContactAxis::X
        ? state.position_x_at(body) : state.position_y_at(body);
}

[[nodiscard]] Fixed axis_velocity(
    const ComponentWorldState& state, std::size_t body, ContactAxis axis) {
    return axis == ContactAxis::X
        ? state.velocity_x_at(body) : state.velocity_y_at(body);
}

[[nodiscard]] FixedRaaSelectiveContactInput make_shadow_input(
    const ComponentWorldState& state,
    const SweptContact& contact,
    std::size_t frame,
    std::size_t contact_index,
    std::uint64_t seed) {
    std::size_t first = contact.first;
    std::size_t second = contact.second;
    Fixed first_position = axis_position(state, first, contact.axis);
    Fixed second_position = axis_position(state, second, contact.axis);
    if (second_position < first_position
        || (second_position == first_position && second < first)) {
        std::swap(first, second);
        std::swap(first_position, second_position);
    }
    std::uint64_t rng = seed
        ^ (static_cast<std::uint64_t>(frame) * 0xD6E8FEB86659FD93ULL)
        ^ (static_cast<std::uint64_t>(contact_index) * 0xA0761D6478BD642FULL)
        ^ (static_cast<std::uint64_t>(first) << 32U)
        ^ static_cast<std::uint64_t>(second);
    const auto position_radius = [&rng]() {
        return milli(1 + static_cast<std::int64_t>(splitmix64(rng) % 5U));
    };
    const auto velocity_radius = [&rng]() {
        return milli(1 + static_cast<std::int64_t>(splitmix64(rng) % 4U));
    };
    return {
        .x1_center = first_position,
        .x2_center = second_position,
        .v1_center = axis_velocity(state, first, contact.axis),
        .v2_center = axis_velocity(state, second, contact.axis),
        .x1_radius = position_radius(),
        .x2_radius = position_radius(),
        .v1_radius = velocity_radius(),
        .v2_radius = velocity_radius(),
    };
}

[[nodiscard]] FixedRaaTimingDistribution distribution(std::vector<double> values) {
    if (values.empty()) return {};
    std::sort(values.begin(), values.end());
    const auto at = [&values](double fraction) {
        return values[static_cast<std::size_t>(
            fraction * static_cast<double>(values.size() - 1U))];
    };
    return {
        .p50_us = at(0.50),
        .p95_us = at(0.95),
        .p99_us = at(0.99),
        .maximum_us = values.back(),
    };
}

} // namespace

FixedRaaActiveIslandShadowResult run_fixed_raa_active_island_shadow(
    const FixedRaaActiveIslandShadowConfig& config) {
    if (config.groups_per_topology == 0U || config.frames == 0U
        || config.raa_steps == 0U || config.maximum_terms < 2U
        || config.maximum_terms > 16U || config.page_size == 0U) {
        throw std::invalid_argument("Active-island shadow configuration is invalid");
    }

    const WorldState initial = make_world(config.groups_per_topology, config.seed);
    ComponentWorldState current = make_component_world(initial, config.page_size);
    DeterministicActiveSet active = DeterministicActiveSet::from_world(initial);
    ContactSolverConfig contact_config;
    contact_config.connected_solver_mode = ConnectedContactSolverMode::Auto;
    ContactIslandWorkspace workspace(initial.bodies.size(), initial.bodies.size() * 16U);

    FixedRaaActiveIslandShadowResult result{};
    result.bodies = initial.bodies.size();
    result.initial_world_hash = world_hash(current);
    result.island_layout_hash = kHashOffset;
    result.aggregate_hash = kHashOffset;
    std::vector<double> classifier_timings;
    std::vector<double> full_raa_timings;
    std::vector<double> selective_kernel_timings;
    std::vector<double> selective_total_timings;
    std::vector<double> timings;
    classifier_timings.reserve(config.frames);
    full_raa_timings.reserve(config.frames);
    selective_kernel_timings.reserve(config.frames);
    selective_total_timings.reserve(config.frames);
    timings.reserve(config.frames);

    for (std::size_t frame = 0U; frame < config.frames; ++frame) {
        const ComponentStepResult integrated = step_component_active(
            current, active, {}, ComponentStepOptions{.kernel_mode = FixedKernelMode::Scalar});
        const std::vector<SweptContact> contacts = swept_aabb_contacts(
            current, integrated.state, contact_config);
        const ContactStepResult direct = solve_component_contact_constraints(
            current, integrated, contacts, contact_config);

        if (contacts.empty()) {
            ++result.zero_contact_frames;
        } else {
            workspace.classify(current.body_count(), contacts);
            std::vector<FixedRaaSelectiveContactInput> inputs;
            inputs.reserve(contacts.size());
            for (std::size_t index = 0U; index < contacts.size(); ++index) {
                inputs.push_back(make_shadow_input(
                    current, contacts[index], frame, index, config.seed));
            }

            const auto begin = Clock::now();
            const FixedRaaSelectiveBatchResult shadow =
                evaluate_fixed_raa_selective_contacts(
                    inputs, config.raa_steps, config.maximum_terms);
            const auto end = Clock::now();
            timings.push_back(std::chrono::duration<double, std::micro>(
                end - begin).count());
            classifier_timings.push_back(shadow.classifier_us);
            full_raa_timings.push_back(shadow.full_raa_us);
            selective_kernel_timings.push_back(shadow.selective_kernel_us);
            selective_total_timings.push_back(shadow.selective_total_us);

            result.oracle_vulnerable += shadow.oracle_vulnerable;
            result.selected += shadow.selected;
            result.true_positives += shadow.true_positives;
            result.false_positives += shadow.false_positives;
            result.false_negatives += shadow.false_negatives;
            result.true_negatives += shadow.true_negatives;
            result.center_mismatches += shadow.center_mismatches;
            result.selected_state_mismatches += shadow.selected_state_mismatches;
            result.contacts_observed += contacts.size();

            const auto islands = workspace.islands();
            const auto order = workspace.contact_order();
            result.islands_observed += islands.size();
            for (std::size_t island_index = 0U; island_index < islands.size(); ++island_index) {
                const ContactIslandDescriptor& island = islands[island_index];
                FixedRaaActiveIslandRecord record{
                    .frame = frame,
                    .island_index = island_index,
                    .topology = island.topology,
                    .bodies = island.body_count,
                    .contacts = island.contact_count,
                    .contact_hash = kHashOffset,
                    .oracle_mask_hash = kHashOffset,
                    .classifier_mask_hash = kHashOffset,
                };
                for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
                    const std::size_t ordered = island.contact_begin + offset;
                    const std::size_t index = order[ordered];
                    const bool oracle = shadow.oracle_mask[index] != 0U;
                    const bool selected = shadow.classifier_mask[index] != 0U;
                    record.oracle_vulnerable += oracle ? 1U : 0U;
                    record.selected += selected ? 1U : 0U;
                    record.false_positives += !oracle && selected ? 1U : 0U;
                    record.false_negatives += oracle && !selected ? 1U : 0U;
                    mix(record.contact_hash, index);
                    mix(record.contact_hash, contacts[index].first);
                    mix(record.contact_hash, contacts[index].second);
                    mix(record.contact_hash, static_cast<std::uint64_t>(contacts[index].axis));
                    mix(record.oracle_mask_hash, oracle ? 1U : 0U);
                    mix(record.classifier_mask_hash, selected ? 1U : 0U);
                }
                result.vulnerable_islands += record.oracle_vulnerable != 0U ? 1U : 0U;
                result.selected_islands += record.selected != 0U ? 1U : 0U;
                result.maximum_island_bodies = std::max(
                    result.maximum_island_bodies, record.bodies);
                result.maximum_island_contacts = std::max(
                    result.maximum_island_contacts, record.contacts);
                ++result.topology_counts[topology_index(record.topology)];
                for (const std::uint64_t value : {
                         static_cast<std::uint64_t>(record.frame),
                         static_cast<std::uint64_t>(record.island_index),
                         static_cast<std::uint64_t>(record.topology),
                         static_cast<std::uint64_t>(record.bodies),
                         static_cast<std::uint64_t>(record.contacts),
                         static_cast<std::uint64_t>(record.oracle_vulnerable),
                         static_cast<std::uint64_t>(record.selected),
                         record.contact_hash, record.oracle_mask_hash,
                         record.classifier_mask_hash}) {
                    mix(result.island_layout_hash, value);
                }
                result.islands.push_back(record);
            }
            for (const std::uint64_t value : {shadow.corpus_hash,
                     shadow.oracle_mask_hash, shadow.classifier_mask_hash,
                     shadow.full_center_hash, shadow.selective_center_hash,
                     shadow.full_state_hash, shadow.selective_state_hash}) {
                mix(result.aggregate_hash, value);
            }
        }

        const ContactStepResult observed = solve_component_contact_constraints(
            current, integrated, contacts, contact_config);
        if (direct.state.materialize() != observed.state.materialize()
            || direct.active != observed.active
            || !same_dirty(direct.dirty, observed.dirty)
            || direct.contacts != observed.contacts) {
            ++result.authoritative_state_mismatches;
        }
        mix(result.aggregate_hash, world_hash(direct.state));
        current = direct.state;
        active = direct.active;
        ++result.frames_processed;
    }

    result.final_world_hash = world_hash(current);
    result.classifier_timing = distribution(std::move(classifier_timings));
    result.full_raa_timing = distribution(std::move(full_raa_timings));
    result.selective_kernel_timing = distribution(std::move(selective_kernel_timings));
    result.selective_total_timing = distribution(std::move(selective_total_timings));
    result.shadow_timing = distribution(std::move(timings));
    mix(result.aggregate_hash, result.initial_world_hash);
    mix(result.aggregate_hash, result.final_world_hash);
    mix(result.aggregate_hash, result.island_layout_hash);
    mix(result.aggregate_hash, result.false_negatives);
    mix(result.aggregate_hash, result.authoritative_state_mismatches);
    return result;
}

} // namespace neoeng::core
