#include "neoeng/core/fixed_raa_microkernel.hpp"
#include "neoeng/core/fixed_slot_allocator.hpp"
#include "neoeng/core/paged_segmented_pair_history.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace neoeng::core;
constexpr std::int32_t kOne = 1 << 30;

void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) { hash ^= (value >> (byte * 8U)) & 0xFFU; hash *= 0x100000001B3ULL; }
}

std::uint64_t allocator_equivalence(std::size_t iterations, std::uint64_t seed) {
    constexpr std::size_t capacity = 521U;
    std::array<FixedSlotAllocator, 3U> allocators{{
        FixedSlotAllocator(capacity, FixedSlotAllocatorMode::NextFit),
        FixedSlotAllocator(capacity, FixedSlotAllocatorMode::HierarchicalBitmap),
        FixedSlotAllocator(capacity, FixedSlotAllocatorMode::Adaptive),
    }};
    std::array<std::vector<std::uint32_t>, 3U> active;
    for (auto& values : active) values.reserve(capacity);
    std::mt19937_64 rng(seed);
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        const bool acquire = active[0].empty() || (active[0].size() < capacity && (rng() & 3U) != 0U);
        if (acquire) {
            for (std::size_t mode = 0U; mode < allocators.size(); ++mode) active[mode].push_back(allocators[mode].acquire());
        } else {
            const std::size_t position = static_cast<std::size_t>(rng() % active[0].size());
            for (std::size_t mode = 0U; mode < allocators.size(); ++mode) {
                allocators[mode].release(active[mode][position]);
                active[mode][position] = active[mode].back(); active[mode].pop_back();
            }
        }
        for (std::size_t mode = 0U; mode < allocators.size(); ++mode) {
            if (allocators[mode].stats().used != active[mode].size()) throw std::runtime_error("v0.27 allocator accounting mismatch");
        }
        mix_u64(hash, active[0].size());
    }
    const auto& adaptive = allocators[2].stats();
    if (adaptive.next_fit_dispatches == 0U || adaptive.bitmap_dispatches == 0U) {
        throw std::runtime_error("v0.27 adaptive allocator did not exercise both paths");
    }
    mix_u64(hash, adaptive.next_fit_dispatches); mix_u64(hash, adaptive.bitmap_dispatches);
    return hash;
}

std::uint64_t history_equivalence(std::size_t iterations, std::uint64_t seed) {
    constexpr std::size_t bodies = 256U, base_pairs = 128U;
    const std::size_t pages = (bodies + 31U) / 32U;
    std::vector<NormalContact> base_contacts(base_pairs);
    std::vector<BroadphasePair> base_pair_list(base_pairs);
    for (std::size_t index = 0U; index < base_pairs; ++index) {
        base_contacts[index] = {index * 2U, index * 2U + 1U, {kOne, 0}};
        base_pair_list[index] = {index * 2U, index * 2U + 1U};
    }
    auto make = [&](FixedSlotAllocatorMode mode) {
        return PagedSegmentedPairHistory({
            .bodies = bodies, .maximum_contacts = base_pairs + 32U, .maximum_pairs = base_pairs + 32U,
            .maximum_pairs_per_segment = 64U, .history_capacity = 16U, .table_page_elements = 32U,
            .segment_generations = 8'192U, .spill_generations = 64U, .table_generations = 20U,
            .body_key_page_generations = pages * 24U, .segment_map_page_generations = pages * 32U,
            .allocation_mode = mode,
        });
    };
    auto next = make(FixedSlotAllocatorMode::NextFit);
    auto bitmap = make(FixedSlotAllocatorMode::HierarchicalBitmap);
    auto adaptive = make(FixedSlotAllocatorMode::Adaptive);
    next.initialize(0U, base_contacts, base_pair_list);
    bitmap.initialize(0U, base_contacts, base_pair_list);
    adaptive.initialize(0U, base_contacts, base_pair_list);
    std::mt19937_64 rng(seed);
    std::vector<BroadphasePair> out_a(base_pairs + 32U), out_b(base_pairs + 32U), out_c(base_pairs + 32U);
    std::vector<NormalContact> previous = base_contacts;
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (std::uint64_t frame = 1U; frame <= iterations; ++frame) {
        auto contacts = base_contacts;
        auto pairs = base_pair_list;
        std::vector<std::size_t> dirty;
        const std::size_t bridges = static_cast<std::size_t>(rng() % 17U);
        for (std::size_t bridge = 0U; bridge < bridges; ++bridge) {
            const std::size_t island = static_cast<std::size_t>((rng() + bridge * 19U) % (base_pairs - 1U));
            const std::size_t first = island * 2U + 1U;
            contacts.push_back({first, first + 1U, {kOne, 0}}); pairs.push_back({first, first + 1U});
            dirty.push_back(first); dirty.push_back(first + 1U);
        }
        std::sort(contacts.begin(), contacts.end()); contacts.erase(std::unique(contacts.begin(), contacts.end()), contacts.end());
        std::sort(pairs.begin(), pairs.end()); pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
        const bool changed = contacts != previous;
        for (auto* history : {&next, &bitmap, &adaptive}) history->capture(frame, contacts, pairs, dirty, changed, true, false);
        previous = contacts;
        const auto ca = next.restore_pairs(frame, out_a), cb = bitmap.restore_pairs(frame, out_b), cc = adaptive.restore_pairs(frame, out_c);
        if (ca != cb || ca != cc || !std::equal(out_a.begin(), out_a.begin() + static_cast<std::ptrdiff_t>(ca), out_b.begin())
            || !std::equal(out_a.begin(), out_a.begin() + static_cast<std::ptrdiff_t>(ca), out_c.begin())
            || next.hash(frame) != bitmap.hash(frame) || next.hash(frame) != adaptive.hash(frame)) {
            throw std::runtime_error("v0.27 history allocator policy changed semantics");
        }
        mix_u64(hash, next.hash(frame));
    }
    return hash;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1 ? static_cast<std::size_t>(std::stoull(argv[1])) : 1'000U;
    const std::uint64_t allocator = allocator_equivalence(iterations * 32U, 0x270001ULL);
    const std::uint64_t history = history_equivalence(iterations, 0x270002ULL);
    const auto raa8 = run_fixed_raa_microkernel({
        .bodies = 128U, .steps = 8U, .maximum_terms = 8U,
        .monte_carlo_samples = 512U, .timing_repetitions = 2U, .seed = 0x270003ULL,
    });
    const auto raa12 = run_fixed_raa_microkernel({
        .bodies = 128U, .steps = 8U, .maximum_terms = 12U,
        .monte_carlo_samples = 512U, .timing_repetitions = 2U, .seed = 0x270003ULL,
    });
    const auto repeat = run_fixed_raa_microkernel({
        .bodies = 128U, .steps = 8U, .maximum_terms = 12U,
        .monte_carlo_samples = 512U, .timing_repetitions = 2U, .seed = 0x270003ULL,
    });
    if (raa8.empirical_violations != 0U || raa12.empirical_violations != 0U
        || raa8.maximum_terms > 8U || raa12.maximum_terms > 12U || raa12.hash != repeat.hash) {
        throw std::runtime_error("v0.27 fixed RAA invariant failed");
    }
    std::uint64_t aggregate = 0xCBF29CE484222325ULL;
    for (const std::uint64_t value : {allocator, history, raa8.hash, raa12.hash}) mix_u64(aggregate, value);
    std::cout << "v0.27 fuzz iterations=" << iterations << " aggregate=0x" << std::hex << std::uppercase << aggregate
        << " allocator=0x" << allocator << " history=0x" << history << " raa8=0x" << raa8.hash
        << " raa12=0x" << raa12.hash << std::dec << '\n';
    return EXIT_SUCCESS;
}
