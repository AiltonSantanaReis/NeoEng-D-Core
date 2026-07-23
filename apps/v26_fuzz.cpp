#include "neoeng/core/fixed_slot_allocator.hpp"
#include "neoeng/core/paged_segmented_pair_history.hpp"
#include "neoeng/core/uncertainty_lab.hpp"

#include <algorithm>
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
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

std::uint64_t allocator_fuzz(FixedSlotAllocatorMode mode, std::size_t iterations, std::uint64_t seed) {
    constexpr std::size_t capacity = 257U;
    FixedSlotAllocator allocator(capacity, mode);
    std::vector<std::uint8_t> used(capacity, 0U);
    std::vector<std::uint32_t> active;
    active.reserve(capacity);
    std::mt19937_64 rng(seed);
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        const bool acquire = active.empty() || (active.size() < capacity && (rng() & 3U) != 0U);
        if (acquire) {
            const std::uint32_t index = allocator.acquire();
            if (index >= capacity || used[index] != 0U || allocator.is_free(index)) {
                throw std::runtime_error("v0.26 allocator acquire invariant failed");
            }
            used[index] = 1U; active.push_back(index); mix_u64(hash, index);
        } else {
            const std::size_t position = static_cast<std::size_t>(rng() % active.size());
            const std::uint32_t index = active[position];
            allocator.release(index);
            if (!allocator.is_free(index)) throw std::runtime_error("v0.26 allocator release invariant failed");
            used[index] = 0U; active[position] = active.back(); active.pop_back(); mix_u64(hash, index ^ 0xFFFFFFFFU);
        }
        if (allocator.stats().used != active.size()) throw std::runtime_error("v0.26 allocator used-count mismatch");
    }
    for (const std::uint32_t index : active) allocator.release(index);
    if (allocator.free_count() != capacity) throw std::runtime_error("v0.26 allocator did not return to empty");
    return hash;
}

std::uint64_t history_fuzz(std::size_t iterations, std::uint64_t seed) {
    constexpr std::size_t bodies = 128U, base_pairs = 64U;
    const std::size_t pages = (bodies + 31U) / 32U;
    std::vector<NormalContact> contacts(base_pairs);
    std::vector<BroadphasePair> pairs(base_pairs);
    for (std::size_t index = 0U; index < base_pairs; ++index) {
        contacts[index] = {index * 2U, index * 2U + 1U, {kOne, 0}};
        pairs[index] = {index * 2U, index * 2U + 1U};
    }
    auto make = [&](FixedSlotAllocatorMode mode) {
        return PagedSegmentedPairHistory({
            .bodies = bodies, .maximum_contacts = base_pairs + 16U, .maximum_pairs = base_pairs + 16U,
            .maximum_pairs_per_segment = 64U, .history_capacity = 16U, .table_page_elements = 32U,
            .segment_generations = 4'096U, .spill_generations = 32U, .table_generations = 20U,
            .body_key_page_generations = pages * 20U, .segment_map_page_generations = pages * 24U,
            .allocation_mode = mode,
        });
    };
    auto next = make(FixedSlotAllocatorMode::NextFit);
    auto bitmap = make(FixedSlotAllocatorMode::HierarchicalBitmap);
    next.initialize(0U, contacts, pairs); bitmap.initialize(0U, contacts, pairs);
    std::mt19937_64 rng(seed);
    std::vector<BroadphasePair> out_next(base_pairs + 16U), out_bitmap(base_pairs + 16U);
    std::vector<std::size_t> dirty;
    std::vector<NormalContact> previous_contacts = contacts;
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (std::uint64_t frame = 1U; frame <= iterations; ++frame) {
        const std::size_t bridges = static_cast<std::size_t>(rng() % 9U);
        std::vector<NormalContact> current_contacts = contacts;
        std::vector<BroadphasePair> current_pairs = pairs;
        dirty.clear();
        for (std::size_t bridge = 0U; bridge < bridges; ++bridge) {
            const std::size_t island = static_cast<std::size_t>((rng() + bridge * 17U) % (base_pairs - 1U));
            const std::size_t first = island * 2U + 1U;
            current_contacts.push_back({first, first + 1U, {kOne, 0}});
            current_pairs.push_back({first, first + 1U});
            dirty.push_back(first); dirty.push_back(first + 1U);
        }
        std::sort(current_pairs.begin(), current_pairs.end());
        current_pairs.erase(std::unique(current_pairs.begin(), current_pairs.end()), current_pairs.end());
        // Contacts can contain duplicate bridge choices; canonicalize them as well.
        std::sort(current_contacts.begin(), current_contacts.end(), [](const NormalContact& a, const NormalContact& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });
        current_contacts.erase(std::unique(current_contacts.begin(), current_contacts.end(), [](const NormalContact& a, const NormalContact& b) {
            return a.first == b.first && a.second == b.second;
        }), current_contacts.end());
        const bool topology_changed = current_contacts.size() != previous_contacts.size()
            || !std::equal(current_contacts.begin(), current_contacts.end(), previous_contacts.begin(),
                [](const NormalContact& a, const NormalContact& b) {
                    return a.first == b.first && a.second == b.second
                        && a.normal.x == b.normal.x && a.normal.y == b.normal.y;
                });
        next.capture(frame, current_contacts, current_pairs, dirty, topology_changed, true, false);
        bitmap.capture(frame, current_contacts, current_pairs, dirty, topology_changed, true, false);
        previous_contacts = current_contacts;
        const std::size_t count_next = next.restore_pairs(frame, out_next);
        const std::size_t count_bitmap = bitmap.restore_pairs(frame, out_bitmap);
        if (count_next != count_bitmap || !std::equal(out_next.begin(), out_next.begin() + static_cast<std::ptrdiff_t>(count_next), out_bitmap.begin())
            || next.hash(frame) != bitmap.hash(frame)) {
            throw std::runtime_error("v0.26 allocator modes diverged in history fuzz");
        }
        mix_u64(hash, next.hash(frame));
    }
    return hash;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1 ? static_cast<std::size_t>(std::stoull(argv[1])) : 1'000U;
    const std::uint64_t next = allocator_fuzz(FixedSlotAllocatorMode::NextFit, iterations * 16U, 0x260001ULL);
    const std::uint64_t bitmap = allocator_fuzz(FixedSlotAllocatorMode::HierarchicalBitmap, iterations * 16U, 0x260002ULL);
    const std::uint64_t history = history_fuzz(iterations, 0x260003ULL);
    const UncertaintyLabResult uncertainty = run_uncertainty_lab({
        .steps = 60U, .monte_carlo_samples = 512U, .timing_repetitions = 2U,
        .full_affine_terms = 256U, .reduced_affine_terms = 10U, .seed = 0x260004ULL,
    });
    if (uncertainty.interval.empirical_violations != 0U || uncertainty.affine.empirical_violations != 0U
        || uncertainty.reduced_affine.empirical_violations != 0U
        || uncertainty.affine.maximum_terms > 256U || uncertainty.reduced_affine.maximum_terms > 10U
        || uncertainty.fixed_x_error > 1.0e-6 || uncertainty.fixed_v_error > 1.0e-6) {
        throw std::runtime_error("v0.26 uncertainty fuzz invariant failed");
    }
    std::uint64_t aggregate = 0xCBF29CE484222325ULL;
    mix_u64(aggregate, next); mix_u64(aggregate, bitmap); mix_u64(aggregate, history); mix_u64(aggregate, uncertainty.hash);
    std::cout << "v0.26 fuzz iterations=" << iterations << " aggregate=0x" << std::hex << std::uppercase
        << aggregate << std::dec << " next=0x" << std::hex << next << " bitmap=0x" << bitmap
        << " history=0x" << history << " uncertainty=0x" << uncertainty.hash << std::dec << '\n';
    return EXIT_SUCCESS;
}
