#include "neoeng/core/island_pair_cache.hpp"
#include "neoeng/core/paged_atomic_temporal_physics.hpp"
#include "neoeng/core/small_oblique_grid_oracle.hpp"
#include "neoeng/core/weighted_tree_projection.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

namespace {
using namespace neoeng::core;
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 3> kNormals{{
    {kOne, 0}, {759'250'125, 759'250'125}, {644'245'094, 858'993'459}
}};

void mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

PagedAtomicTemporalConfig small_config(std::size_t bodies, std::size_t contacts) {
    const AtomicTemporalPhysicsConfig physics{
        .bodies = bodies, .contacts = contacts, .maximum_candidate_pairs = contacts * 3U + 16U,
        .history_capacity = 16U, .horizon_frames = 8U,
        .maximum_velocity_mutations = 2U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U, .half_extent = Fixed::from_ratio(1, 2),
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
    };
    return {.physics = physics, .history = {
        .bodies = bodies, .contacts = contacts, .maximum_candidate_pairs = physics.maximum_candidate_pairs,
        .history_capacity = 16U, .page_elements = 32U,
        .maximum_position_dirty_pages_per_frame = 4U,
        .maximum_velocity_dirty_pages_per_frame = 4U,
        .maximum_mass_dirty_pages_per_frame = 2U,
        .maximum_contact_dirty_pages_per_frame = 2U,
        .full_position_generations = 2U, .full_velocity_generations = 3U,
        .full_contact_generations = 3U, .maximum_cache_generations = 4U}};
}

bool direct_restore_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    constexpr std::size_t bodies = 64U, contacts_count = 16U;
    std::vector<Fixed::rep> px(bodies), py(bodies), vx(bodies), vy(bodies);
    std::vector<std::uint32_t> masses(bodies);
    std::vector<NormalContact> contacts;
    contacts.reserve(contacts_count);
    for (std::size_t body = 0U; body < bodies; ++body) {
        px[body] = Fixed::from_integer(static_cast<Fixed::rep>((body % 8U) * 4U)).raw();
        py[body] = Fixed::from_integer(static_cast<Fixed::rep>((body / 8U) * 4U)).raw();
        masses[body] = 1U + static_cast<std::uint32_t>(rng() % 8U);
    }
    for (std::size_t pair = 0U; pair < contacts_count; ++pair) {
        const std::size_t first = pair * 2U, second = first + 1U;
        const NormalQ30 normal = kNormals[pair % kNormals.size()];
        contacts.push_back({first, second, normal});
        px[second] = px[first] + Fixed::from_ratio(3, 4).raw();
        vx[first] = Fixed::from_ratio(1, 32).raw();
        vx[second] = -vx[first];
    }
    PagedAtomicTemporalPhysicsEngine rollback(small_config(bodies, contacts_count));
    PagedAtomicTemporalPhysicsEngine clean(small_config(bodies, contacts_count));
    rollback.initialize(px, py, vx, vy, masses, contacts);
    clean.initialize(px, py, vx, vy, masses, contacts);
    std::array<VelocityMutation, 12> original{}, corrected{};
    for (std::size_t index = 0U; index < original.size(); ++index) {
        const std::size_t body = static_cast<std::size_t>(rng() % (contacts_count * 2U));
        original[index] = {body,
            static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 401U) - 200),
            static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 401U) - 200)};
        corrected[index] = original[index];
    }
    corrected[5].delta_x += 17;
    corrected[5].delta_y -= 11;
    for (std::uint64_t frame = 1U; frame <= original.size(); ++frame) {
        rollback.set_input(frame, {.velocity = std::span<const VelocityMutation>(&original[frame - 1U], 1U)});
        clean.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
    }
    rollback.simulate_to(original.size());
    clean.simulate_to(original.size());
    rollback.correct_and_resimulate(6U,
        {.velocity = std::span<const VelocityMutation>(&corrected[5], 1U)}, original.size());
    if (!rollback.equivalent_to(clean)) return false;
    mix(aggregate, rollback.hash());
    mix(aggregate, rollback.history_live_payload_bytes());
    return true;
}

bool island_cache_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    const std::size_t bodies = 8U + static_cast<std::size_t>(rng() % 57U);
    std::vector<NormalContact> contacts;
    contacts.reserve(bodies - 1U);
    for (std::size_t body = 1U; body < bodies; ++body) {
        if ((rng() & 3U) == 0U) continue;
        const std::size_t parent = static_cast<std::size_t>(rng() % body);
        contacts.push_back({parent, body, {kOne, 0}});
    }
    IslandPairCache first({.bodies = bodies, .maximum_contacts = bodies - 1U,
                           .extra_pairs_per_island = 2U});
    IslandPairCache second({.bodies = bodies, .maximum_contacts = bodies - 1U,
                            .extra_pairs_per_island = 2U});
    first.initialize(contacts); second.initialize(contacts);
    if (first.hash() != second.hash() || first.island_count() != second.island_count()) return false;
    const std::size_t body = static_cast<std::size_t>(rng() % bodies);
    const std::size_t island = first.island_of_body(body);
    const auto existing = first.pairs_for_island(island);
    std::vector<BroadphasePair> copy(existing.begin(), existing.end());
    first.replace_island_pairs(island, copy);
    second.replace_pairs_for_body(body, copy);
    if (first.hash() != second.hash()) return false;
    for (const NormalContact& contact : contacts) {
        if (first.island_of_body(contact.first) != first.island_of_body(contact.second)) return false;
    }
    mix(aggregate, first.hash());
    mix(aggregate, first.stats().island_rebuilds);
    return true;
}

bool warm_tree_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    const std::size_t bodies = 2U + static_cast<std::size_t>(rng() % 62U);
    std::vector<Fixed::rep> source_x(bodies), source_y(bodies), cold_x, cold_y, warm_x, warm_y;
    std::vector<std::uint32_t> masses(bodies);
    std::vector<DirectedTreeEdge> edges; edges.reserve(bodies - 1U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        source_x[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 20'001U) - 10'000);
        source_y[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 20'001U) - 10'000);
        masses[body] = 1U + static_cast<std::uint32_t>(rng() % 16U);
        if (body != 0U) edges.push_back({static_cast<std::size_t>(rng() % body), body});
    }
    WeightedTreeScratch cold_scratch(bodies), warm_scratch(bodies);
    for (std::size_t frame = 0U; frame < 4U; ++frame) {
        source_x[rng() % bodies] += static_cast<Fixed::rep>(
            static_cast<std::int64_t>(rng() % 7U) - 3);
        cold_x = source_x; cold_y = source_y; warm_x = source_x; warm_y = source_y;
        const auto cold = project_weighted_tree_common_normal_inplace(
            cold_x, cold_y, masses, edges, {kOne, 0},
            {.maximum_active_set_iterations = 4096U, .feasibility_tolerance_raw = 4U,
             .stationarity_tolerance_raw = 16U, .use_warm_start = false}, cold_scratch);
        const auto warm = project_weighted_tree_common_normal_inplace(
            warm_x, warm_y, masses, edges, {kOne, 0},
            {.maximum_active_set_iterations = 4096U, .feasibility_tolerance_raw = 4U,
             .stationarity_tolerance_raw = 16U, .use_warm_start = true}, warm_scratch);
        if (!cold.residuals.certified || !warm.residuals.certified) return false;
        // Warm is never authoritative unless it is exactly cold. A mismatch is
        // valid experimental evidence, but the authoritative hash remains cold.
        for (Fixed::rep value : cold_x) mix(aggregate, static_cast<std::uint64_t>(value));
        mix(aggregate, cold_x == warm_x && cold_y == warm_y ? 1U : 0U);
    }
    return true;
}

bool oracle_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    std::array<Fixed::rep, 3> input_x{}, input_y{};
    std::array<std::uint32_t, 3> masses{};
    for (std::size_t body = 0U; body < 3U; ++body) {
        input_x[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 5U) - 2);
        input_y[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 5U) - 2);
        masses[body] = 1U + static_cast<std::uint32_t>(rng() % 3U);
    }
    std::array<NormalContact, 2> contacts{{
        {0U, 1U, kNormals[1]}, {1U, 2U, kNormals[2]},
    }};
    const auto first = solve_small_oblique_grid_oracle(input_x, input_y, masses, contacts,
        {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = 3U});
    std::reverse(contacts.begin(), contacts.end());
    const auto second = solve_small_oblique_grid_oracle(input_x, input_y, masses, contacts,
        {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = 3U});
    if (!first.feasible || !second.feasible || first.objective != second.objective
        || first.velocity_x != second.velocity_x || first.velocity_y != second.velocity_y) return false;
    mix(aggregate, first.objective);
    for (Fixed::rep value : first.velocity_x) mix(aggregate, static_cast<std::uint64_t>(value));
    for (Fixed::rep value : first.velocity_y) mix(aggregate, static_cast<std::uint64_t>(value));
    return true;
}
} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1
        ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 5'000U;
    std::mt19937_64 rng(0x4E454F454E475631ULL);
    std::uint64_t aggregate = 0xCBF29CE484222325ULL;
    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        if (!direct_restore_case(rng, aggregate)
            || !island_cache_case(rng, aggregate)
            || !warm_tree_case(rng, aggregate)
            || !oracle_case(rng, aggregate)) {
            std::cerr << "v0.19 fuzz failed at iteration " << iteration << '\n';
            return EXIT_FAILURE;
        }
    }
    std::cout << "v0.19 fuzz iterations=" << iterations << " aggregate=0x"
              << std::hex << std::uppercase << aggregate << std::dec << '\n';
    return EXIT_SUCCESS;
}
