#include "neoeng/core/island_pair_cache.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace neoeng::core {
namespace {
constexpr std::size_t kMissing = std::numeric_limits<std::size_t>::max();

BroadphasePair canonical_pair(BroadphasePair pair) {
    if (pair.second < pair.first) std::swap(pair.first, pair.second);
    if (pair.first == pair.second) throw std::invalid_argument("Island pair cannot be self-referential");
    return pair;
}
}

IslandPairCache::IslandPairCache(IslandPairCacheConfig config)
    : config_(config), parent_(config.bodies), root_to_island_(config.bodies, kMissing),
      body_island_(config.bodies, kMissing), contact_counts_(config.bodies),
      offsets_(config.bodies + 1U), capacities_(config.bodies), counts_(config.bodies) {
    if (config.bodies == 0U || config.maximum_contacts > config.bodies * (config.bodies - 1U) / 2U) {
        throw std::invalid_argument("Island pair cache configuration is invalid");
    }
}

std::size_t IslandPairCache::find_root(std::size_t body) noexcept {
    while (parent_[body] != body) {
        parent_[body] = parent_[parent_[body]];
        body = parent_[body];
    }
    return body;
}

void IslandPairCache::unite(std::size_t first, std::size_t second) noexcept {
    first = find_root(first);
    second = find_root(second);
    if (first == second) return;
    if (second < first) std::swap(first, second);
    parent_[second] = first;
}

void IslandPairCache::initialize(std::span<const NormalContact> contacts) {
    if (contacts.size() > config_.maximum_contacts) {
        throw std::length_error("Island pair cache contact capacity exceeded");
    }
    std::iota(parent_.begin(), parent_.end(), 0U);
    std::fill(root_to_island_.begin(), root_to_island_.end(), kMissing);
    std::fill(contact_counts_.begin(), contact_counts_.end(), 0U);
    for (const NormalContact& contact : contacts) {
        if (contact.first >= config_.bodies || contact.second >= config_.bodies
            || contact.first == contact.second) {
            throw std::invalid_argument("Invalid contact in island pair cache");
        }
        unite(contact.first, contact.second);
    }
    island_count_ = 0U;
    for (std::size_t body = 0U; body < config_.bodies; ++body) {
        const std::size_t root = find_root(body);
        if (root_to_island_[root] == kMissing) root_to_island_[root] = island_count_++;
        body_island_[body] = root_to_island_[root];
    }
    for (const NormalContact& contact : contacts) {
        ++contact_counts_[body_island_[contact.first]];
    }
    offsets_[0] = 0U;
    for (std::size_t island = 0U; island < island_count_; ++island) {
        capacities_[island] = std::max<std::size_t>(1U,
            contact_counts_[island] + config_.extra_pairs_per_island);
        if (offsets_[island] > std::numeric_limits<std::size_t>::max() - capacities_[island]) {
            throw std::overflow_error("Island pair cache storage size overflow");
        }
        offsets_[island + 1U] = offsets_[island] + capacities_[island];
        counts_[island] = 0U;
    }
    storage_.assign(offsets_[island_count_], BroadphasePair{});

    // Seed each island with the canonical contact pairs. Contacts are sorted per
    // physical segment so future replacement can be compared deterministically.
    for (const NormalContact& contact : contacts) {
        const std::size_t island = body_island_[contact.first];
        storage_[offsets_[island] + counts_[island]++] = canonical_pair({contact.first, contact.second});
    }
    for (std::size_t island = 0U; island < island_count_; ++island) {
        auto begin = storage_.begin() + static_cast<std::ptrdiff_t>(offsets_[island]);
        std::sort(begin, begin + static_cast<std::ptrdiff_t>(counts_[island]));
    }
    stats_ = {};
    stats_.full_rebuilds = 1U;
    stats_.island_count = island_count_;
    stats_.live_pairs = contacts.size();
    stats_.pairs_written = contacts.size();
}

void IslandPairCache::replace_island_pairs(
    std::size_t island, std::span<const BroadphasePair> pairs) {
    if (island >= island_count_) throw std::out_of_range("Island pair cache island is invalid");
    if (pairs.size() > capacities_[island]) {
        ++stats_.capacity_failures;
        throw std::length_error("Island pair segment capacity exceeded");
    }
    BroadphasePair previous{};
    bool have_previous = false;
    const std::size_t base = offsets_[island];
    for (std::size_t index = 0U; index < pairs.size(); ++index) {
        const BroadphasePair pair = canonical_pair(pairs[index]);
        if (pair.first >= config_.bodies || pair.second >= config_.bodies
            || body_island_[pair.first] != island || body_island_[pair.second] != island) {
            throw std::invalid_argument("Pair does not belong to target island");
        }
        if (have_previous && !(previous < pair)) {
            throw std::invalid_argument("Island pairs must be strictly sorted and unique");
        }
        storage_[base + index] = pair;
        previous = pair;
        have_previous = true;
    }
    stats_.live_pairs -= counts_[island];
    counts_[island] = pairs.size();
    stats_.live_pairs += counts_[island];
    ++stats_.island_rebuilds;
    stats_.pairs_written += pairs.size();
}

void IslandPairCache::replace_pairs_for_body(
    std::size_t body, std::span<const BroadphasePair> pairs) {
    replace_island_pairs(island_of_body(body), pairs);
}

std::size_t IslandPairCache::island_of_body(std::size_t body) const {
    if (body >= config_.bodies || island_count_ == 0U) {
        throw std::out_of_range("Island pair cache body is invalid");
    }
    return body_island_[body];
}

std::span<const BroadphasePair> IslandPairCache::pairs_for_island(std::size_t island) const {
    if (island >= island_count_) throw std::out_of_range("Island pair cache island is invalid");
    return std::span<const BroadphasePair>(storage_.data() + offsets_[island], counts_[island]);
}

std::size_t IslandPairCache::reserved_bytes() const noexcept {
    return (parent_.capacity() + root_to_island_.capacity() + body_island_.capacity()
        + contact_counts_.capacity() + offsets_.capacity() + capacities_.capacity()
        + counts_.capacity()) * sizeof(std::size_t)
        + storage_.capacity() * sizeof(BroadphasePair);
}

std::uint64_t IslandPairCache::hash() const noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (unsigned byte = 0U; byte < 8U; ++byte) {
            hash ^= (value >> (byte * 8U)) & 0xFFU;
            hash *= 0x100000001B3ULL;
        }
    };
    mix(island_count_);
    for (std::size_t island = 0U; island < island_count_; ++island) {
        mix(island); mix(counts_[island]);
        for (const BroadphasePair& pair : pairs_for_island(island)) {
            mix(pair.first); mix(pair.second);
        }
    }
    return hash;
}

} // namespace neoeng::core
