#include "neoeng/core/oblique_tree_grid_dp.hpp"
#include "neoeng/core/small_oblique_grid_oracle.hpp"
#include "neoeng/core/versioned_island_pair_history.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

namespace {
using namespace neoeng::core;
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 4> kNormals{{
    {kOne, 0}, {0, kOne}, {759'250'125, 759'250'125}, {644'245'094, 858'993'459}
}};

void mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

bool versioned_history_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    constexpr std::size_t bodies = 32U;
    std::vector<NormalContact> contacts;
    std::vector<BroadphasePair> pairs;
    for (std::size_t pair = 0U; pair < bodies / 2U; ++pair) {
        contacts.push_back({pair * 2U, pair * 2U + 1U, {kOne, 0}});
        pairs.push_back({pair * 2U, pair * 2U + 1U});
    }
    VersionedIslandPairHistory history({
        .bodies = bodies, .maximum_contacts = contacts.size(),
        .history_capacity = 8U, .extra_pairs_per_island = 1U});
    history.initialize(0U, contacts, pairs);
    std::array<std::vector<BroadphasePair>, 8> model;
    model[0] = pairs;
    for (std::uint64_t frame = 1U; frame <= 20U; ++frame) {
        const std::size_t island = static_cast<std::size_t>(rng() % contacts.size());
        const BroadphasePair target{island * 2U, island * 2U + 1U};
        const auto it = std::lower_bound(pairs.begin(), pairs.end(), target);
        if (it != pairs.end() && *it == target) pairs.erase(it);
        else pairs.insert(it, target);
        const std::size_t dirty = target.first;
        history.capture(frame, pairs, std::span<const std::size_t>(&dirty, 1U));
        model[static_cast<std::size_t>(frame % model.size())] = pairs;
        std::vector<BroadphasePair> restored(contacts.size() + 1U);
        const std::size_t count = history.restore(frame, restored);
        restored.resize(count);
        if (restored != pairs) return false;
    }
    history.truncate_after(17U);
    if (history.contains(18U)) return false;
    const std::size_t dirty = 0U;
    history.capture(18U, model[17U % model.size()], std::span<const std::size_t>(&dirty, 1U));
    std::vector<BroadphasePair> restored(contacts.size() + 1U);
    const std::size_t count = history.restore(18U, restored);
    restored.resize(count);
    if (restored != model[17U % model.size()]) return false;

    // A cross-island pair must fail transactionally and leave frame 18 retained.
    const std::vector<BroadphasePair> invalid{{0U, 3U}};
    bool failed = false;
    try { history.capture(19U, invalid, std::span<const std::size_t>(&dirty, 1U), true); }
    catch (const std::domain_error&) { failed = true; }
    if (!failed || !history.contains(18U) || history.contains(19U)) return false;
    mix(aggregate, history.hash(18U));
    mix(aggregate, history.stats().segments_reused);
    return true;
}

bool grid_dp_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    std::array<Fixed::rep, 3> input_x{}, input_y{};
    std::array<std::uint32_t, 3> masses{};
    for (std::size_t body = 0U; body < 3U; ++body) {
        input_x[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 5U) - 2);
        input_y[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 5U) - 2);
        masses[body] = 1U + static_cast<std::uint32_t>(rng() % 3U);
    }
    std::array<NormalContact, 2> contacts{{
        {0U, 1U, kNormals[2]}, {1U, 2U, kNormals[3]},
    }};
    ObliqueTreeGridScratch scratch(3U, 25U);
    const auto dp = solve_oblique_tree_grid_dp(input_x, input_y, masses, contacts,
        {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = 3U,
         .maximum_grid_states = 25U}, scratch);
    const auto brute = solve_small_oblique_grid_oracle(input_x, input_y, masses, contacts,
        {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = 3U});
    if (!dp.certified_on_grid || !brute.feasible || dp.objective != brute.objective) {
        std::cerr << "grid mismatch cert=" << dp.certified_on_grid << " brute=" << brute.feasible
                  << " dp_obj=" << dp.objective << " brute_obj=" << brute.objective << '\n';
        return false;
    }
    std::reverse(contacts.begin(), contacts.end());
    const auto reordered = solve_oblique_tree_grid_dp(input_x, input_y, masses, contacts,
        {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = 3U,
         .maximum_grid_states = 25U}, scratch);
    if (!reordered.certified_on_grid || reordered.objective != dp.objective
        || reordered.velocity_x != dp.velocity_x || reordered.velocity_y != dp.velocity_y) {
        std::cerr << "reorder mismatch cert=" << reordered.certified_on_grid
                  << " obj=" << reordered.objective << " expected=" << dp.objective
                  << " vx_equal=" << (reordered.velocity_x == dp.velocity_x)
                  << " vy_equal=" << (reordered.velocity_y == dp.velocity_y) << '\n';
        return false;
    }
    mix(aggregate, dp.objective);
    for (Fixed::rep value : dp.velocity_x) mix(aggregate, static_cast<std::uint64_t>(value));
    for (Fixed::rep value : dp.velocity_y) mix(aggregate, static_cast<std::uint64_t>(value));
    return true;
}

bool large_tree_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    const std::size_t bodies = 32U + static_cast<std::size_t>(rng() % 96U);
    std::vector<Fixed::rep> x(bodies), y(bodies);
    std::vector<std::uint32_t> masses(bodies);
    std::vector<NormalContact> contacts;
    contacts.reserve(bodies - 1U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        x[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 5U) - 2);
        y[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 5U) - 2);
        masses[body] = 1U + static_cast<std::uint32_t>(rng() % 7U);
        if (body != 0U) {
            const std::size_t parent = static_cast<std::size_t>(rng() % body);
            contacts.push_back({parent, body, kNormals[rng() % kNormals.size()]});
        }
    }
    ObliqueTreeGridScratch scratch(bodies, 25U);
    const auto result = solve_oblique_tree_grid_dp(x, y, masses, contacts,
        {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = bodies,
         .maximum_grid_states = 25U}, scratch);
    if (!result.certified_on_grid || result.primal_violation_raw != 0U) return false;
    mix(aggregate, result.objective);
    mix(aggregate, result.state_pairs_tested);
    return true;
}
} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1
        ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 5'000U;
    std::mt19937_64 rng(0x4E454F454E475632ULL);
    std::uint64_t aggregate = 0xCBF29CE484222325ULL;
    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        if (!versioned_history_case(rng, aggregate)) {
            std::cerr << "v0.20 history fuzz failed at iteration " << iteration << '\n'; return EXIT_FAILURE;
        }
        if (!grid_dp_case(rng, aggregate)) {
            std::cerr << "v0.20 grid fuzz failed at iteration " << iteration << '\n'; return EXIT_FAILURE;
        }
        if (!large_tree_case(rng, aggregate)) {
            std::cerr << "v0.20 large-tree fuzz failed at iteration " << iteration << '\n'; return EXIT_FAILURE;
        }
    }
    std::cout << "v0.20 fuzz iterations=" << iterations << " aggregate=0x"
              << std::hex << std::uppercase << aggregate << std::dec << '\n';
    return EXIT_SUCCESS;
}
