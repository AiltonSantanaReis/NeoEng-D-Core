#include "neoeng/core/exact_oblique_tree_oracle.hpp"
#include "neoeng/core/segmented_dynamic_pair_history.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <vector>

using namespace neoeng::core;
namespace {
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 8> kNormals{{
    {kOne, 0}, {-kOne, 0}, {0, kOne}, {0, -kOne},
    {759250125, 759250125}, {-759250125, 759250125},
    {644245094, 858993459}, {-644245094, 858993459}
}};

void mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

std::vector<std::size_t> model_keys(
    std::size_t bodies, std::span<const NormalContact> contacts) {
    std::vector<std::size_t> parent(bodies);
    std::iota(parent.begin(), parent.end(), 0U);
    const auto root = [&parent](std::size_t body) {
        while (parent[body] != body) body = parent[body];
        return body;
    };
    for (const NormalContact& contact : contacts) {
        std::size_t first = root(contact.first);
        std::size_t second = root(contact.second);
        if (first == second) continue;
        if (second < first) std::swap(first, second);
        parent[second] = first;
    }
    std::vector<std::size_t> keys(bodies);
    for (std::size_t body = 0U; body < bodies; ++body) keys[body] = root(body);
    return keys;
}

bool segmented_history_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    constexpr std::size_t bodies = 16U;
    constexpr std::size_t capacity = 8U;
    constexpr std::size_t maximum_pairs = 24U;
    SegmentedDynamicPairHistory history({
        .bodies = bodies,
        .maximum_contacts = 15U,
        .maximum_pairs = maximum_pairs,
        .maximum_pairs_per_segment = maximum_pairs,
        .history_capacity = capacity,
        .segment_generations = 160U,
        .spill_generations = capacity + 2U,
        .table_generations = capacity + 2U,
    });
    std::array<std::vector<BroadphasePair>, capacity> pair_model;
    std::array<std::vector<std::size_t>, capacity> key_model;

    const auto make_topology = [&]() {
        std::vector<NormalContact> contacts;
        std::vector<std::size_t> parent(bodies);
        std::iota(parent.begin(), parent.end(), 0U);
        const auto root = [&parent](std::size_t body) {
            while (parent[body] != body) body = parent[body];
            return body;
        };
        const std::size_t desired = 4U + static_cast<std::size_t>(rng() % 9U);
        while (contacts.size() < desired) {
            std::size_t first = rng() % bodies;
            std::size_t second = rng() % bodies;
            if (first == second) continue;
            std::size_t first_root = root(first);
            std::size_t second_root = root(second);
            if (first_root == second_root) continue;
            if (second_root < first_root) std::swap(first_root, second_root);
            parent[second_root] = first_root;
            contacts.push_back({first, second, kNormals[rng() % kNormals.size()]});
        }
        std::shuffle(contacts.begin(), contacts.end(), rng);
        return contacts;
    };
    const auto make_pairs = [&]() {
        std::set<BroadphasePair> unique;
        const std::size_t count = 8U + static_cast<std::size_t>(rng() % 12U);
        while (unique.size() < count) {
            std::size_t first = rng() % bodies;
            std::size_t second = rng() % bodies;
            if (first == second) continue;
            if (second < first) std::swap(first, second);
            unique.insert({first, second});
        }
        return std::vector<BroadphasePair>(unique.begin(), unique.end());
    };

    auto contacts = make_topology();
    auto pairs = make_pairs();
    history.initialize(0U, contacts, pairs);
    pair_model[0] = pairs;
    key_model[0] = model_keys(bodies, contacts);
    for (std::uint64_t frame = 1U; frame <= 80U; ++frame) {
        contacts = make_topology();
        pairs = make_pairs();
        const std::size_t dirty = rng() % bodies;
        history.capture(frame, contacts, pairs, std::span<const std::size_t>(&dirty, 1U),
            true, false, false);
        const std::size_t model_slot = frame % capacity;
        pair_model[model_slot] = pairs;
        key_model[model_slot] = model_keys(bodies, contacts);
        std::vector<BroadphasePair> restored(maximum_pairs);
        std::vector<std::size_t> keys(bodies);
        restored.resize(history.restore_pairs(frame, restored));
        const std::size_t restored_islands = history.restore_body_island_keys(frame, keys);
        if (restored_islands == 0U) return false;
        if (restored != pair_model[model_slot] || keys != key_model[model_slot]) return false;
        std::vector<BroadphasePair> spill(maximum_pairs);
        spill.resize(history.restore_spill_pairs(frame, spill));
        for (const BroadphasePair pair : spill) {
            if (keys[pair.first] == keys[pair.second]) return false;
        }
    }
    // A complete no-change hint must share the immutable table in O(1).
    history.capture(81U, contacts, pairs, {}, false, true, true);
    if (history.stats().tables_shared == 0U) return false;
    history.truncate_after(80U);
    if (history.contains(81U) || !history.contains(80U)) return false;

    // Transactional capacity failure must not publish a partial frame.
    std::vector<BroadphasePair> overflow(maximum_pairs + 1U);
    bool failed = false;
    try {
        history.capture(81U, contacts, overflow, {}, false, true, false);
    } catch (const std::length_error&) {
        failed = true;
    }
    if (!failed || history.contains(81U) || !history.contains(80U)) return false;
    mix(aggregate, history.hash(80U));
    mix(aggregate, history.stats().segments_reused);
    return true;
}

std::uint64_t violation(
    std::span<const Fixed::rep> x,
    std::span<const Fixed::rep> y,
    std::span<const NormalContact> contacts) {
    std::uint64_t maximum = 0U;
    for (const NormalContact& contact : contacts) {
        const WideInteger value = static_cast<WideInteger>(contact.normal.x)
                * (static_cast<WideInteger>(x[contact.first]) - x[contact.second])
            + static_cast<WideInteger>(contact.normal.y)
                * (static_cast<WideInteger>(y[contact.first]) - y[contact.second]);
        if (value > 0) {
            maximum = std::max(maximum, value > std::numeric_limits<std::uint64_t>::max()
                ? std::numeric_limits<std::uint64_t>::max()
                : static_cast<std::uint64_t>(value));
        }
    }
    return maximum;
}



bool transactional_segment_failure_case(std::uint64_t& aggregate) {
    constexpr std::size_t bodies = 6U;
    const std::array<NormalContact, 3> contacts{{
        {0U, 1U, {kOne, 0}},
        {2U, 3U, {kOne, 0}},
        {3U, 4U, {kOne, 0}},
    }};
    const std::array<BroadphasePair, 2> initial{{{0U, 1U}, {2U, 3U}}};
    const std::array<BroadphasePair, 4> oversized{{
        {0U, 1U}, {2U, 3U}, {2U, 4U}, {3U, 4U}
    }};
    SegmentedDynamicPairHistory history({
        .bodies = bodies,
        .maximum_contacts = contacts.size(),
        .maximum_pairs = oversized.size(),
        .maximum_pairs_per_segment = 2U,
        .history_capacity = 4U,
        .segment_generations = 16U,
        .spill_generations = 4U,
        .table_generations = 6U,
    });
    history.initialize(0U, contacts, initial);
    const std::uint64_t before = history.hash(0U);
    bool failed = false;
    try {
        history.capture(1U, contacts, oversized, {}, false, true, false);
    } catch (const std::length_error&) {
        failed = true;
    }
    if (!failed || history.contains(1U) || !history.contains(0U) || history.hash(0U) != before) {
        return false;
    }
    std::array<BroadphasePair, 4> restored{};
    const std::size_t count = history.restore_pairs(0U, restored);
    if (count != initial.size()
        || !std::equal(initial.begin(), initial.end(), restored.begin())) {
        return false;
    }
    mix(aggregate, before);
    return true;
}

bool quantized_repair_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    const std::size_t bodies = 2U + static_cast<std::size_t>(rng() % 6U);
    std::vector<Fixed::rep> x(bodies), y(bodies);
    std::vector<std::uint32_t> masses(bodies);
    std::vector<NormalContact> contacts;
    contacts.reserve(bodies - 1U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        x[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 9U) - 4);
        y[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 9U) - 4);
        masses[body] = 1U + static_cast<std::uint32_t>(rng() % 7U);
        if (body != 0U) {
            contacts.push_back({static_cast<std::size_t>(rng() % body), body,
                kNormals[rng() % kNormals.size()]});
        }
    }
    const auto exact = solve_exact_oblique_tree_active_sets(x, y, masses, contacts,
        {.maximum_bodies = 7U, .maximum_contacts = 6U, .quantized_repair_radius = 1U});
    if (!exact.certified_continuous) return false;
    if (exact.repair_certified_neighbourhood) {
        if (!exact.repaired_quantized || exact.repaired_primal_violation_raw != 0U
            || violation(exact.repaired_velocity_x, exact.repaired_velocity_y, contacts) != 0U) {
            return false;
        }
    }
    auto reordered_contacts = contacts;
    std::shuffle(reordered_contacts.begin(), reordered_contacts.end(), rng);
    const auto reordered = solve_exact_oblique_tree_active_sets(x, y, masses, reordered_contacts,
        {.maximum_bodies = 7U, .maximum_contacts = 6U, .quantized_repair_radius = 1U});
    if (!reordered.certified_continuous
        || reordered.objective_numerator != exact.objective_numerator
        || reordered.objective_denominator != exact.objective_denominator
        || reordered.repair_certified_neighbourhood != exact.repair_certified_neighbourhood
        || reordered.repaired_velocity_x != exact.repaired_velocity_x
        || reordered.repaired_velocity_y != exact.repaired_velocity_y) {
        return false;
    }
    mix(aggregate, exact.hash);
    mix(aggregate, exact.rounded_primal_violation_raw);
    mix(aggregate, exact.repair_certified_neighbourhood ? 1U : 0U);
    return true;
}
} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1
        ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 5'000U;
    std::mt19937_64 rng(0x4E454F454E475632ULL);
    std::uint64_t aggregate = 0xCBF29CE484222325ULL;
    if (!transactional_segment_failure_case(aggregate)) {
        std::cerr << "v0.22 transactional segment failure test failed\n";
        return EXIT_FAILURE;
    }
    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        if (!segmented_history_case(rng, aggregate)) {
            std::cerr << "v0.22 segmented history fuzz failed at " << iteration << '\n';
            return EXIT_FAILURE;
        }
        if (!quantized_repair_case(rng, aggregate)) {
            std::cerr << "v0.22 quantized repair fuzz failed at " << iteration << '\n';
            return EXIT_FAILURE;
        }
    }
    std::cout << "v0.22 fuzz iterations=" << iterations << " aggregate=0x"
              << std::hex << std::uppercase << aggregate << std::dec << '\n';
    return EXIT_SUCCESS;
}
