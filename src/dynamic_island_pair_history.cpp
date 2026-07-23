#include "neoeng/core/dynamic_island_pair_history.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace neoeng::core {
namespace {
constexpr std::size_t missing = std::numeric_limits<std::size_t>::max();

BroadphasePair canonical_pair(BroadphasePair pair) {
    if (pair.second < pair.first) std::swap(pair.first, pair.second);
    if (pair.first == pair.second) throw std::invalid_argument("Dynamic island pair cannot be self-referential");
    return pair;
}

void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}
} // namespace

DynamicIslandPairHistory::DynamicIslandPairHistory(DynamicIslandPairHistoryConfig config)
    : config_(config),
      pair_generation_count_(config.pair_generations == 0U
          ? config.history_capacity + 2U : config.pair_generations),
      topology_generation_count_(config.topology_generations == 0U
          ? config.history_capacity + 2U : config.topology_generations),
      pair_generations_(pair_generation_count_),
      topology_generations_(topology_generation_count_),
      pair_storage_(pair_generation_count_ * config.maximum_pairs),
      spill_storage_(pair_generation_count_ * config.maximum_pairs),
      topology_storage_(topology_generation_count_ * config.bodies),
      snapshots_(config.history_capacity), parent_(config.bodies),
      root_to_island_(config.bodies, missing), staging_pairs_(config.maximum_pairs) {
    if (config.bodies == 0U || config.history_capacity < 2U
        || config.maximum_pairs == 0U || pair_generation_count_ < 2U
        || topology_generation_count_ < 2U
        || config.maximum_contacts > config.bodies * (config.bodies - 1U) / 2U) {
        throw std::invalid_argument("Dynamic island pair history configuration is invalid");
    }
}

DynamicIslandPairHistory::Snapshot& DynamicIslandPairHistory::slot(std::uint64_t frame) noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}
const DynamicIslandPairHistory::Snapshot& DynamicIslandPairHistory::slot(std::uint64_t frame) const noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}
BroadphasePair* DynamicIslandPairHistory::pair_storage(std::uint32_t generation) noexcept {
    return pair_storage_.data() + static_cast<std::size_t>(generation) * config_.maximum_pairs;
}
const BroadphasePair* DynamicIslandPairHistory::pair_storage(std::uint32_t generation) const noexcept {
    return pair_storage_.data() + static_cast<std::size_t>(generation) * config_.maximum_pairs;
}
std::size_t* DynamicIslandPairHistory::topology_storage(std::uint32_t generation) noexcept {
    return topology_storage_.data() + static_cast<std::size_t>(generation) * config_.bodies;
}
const std::size_t* DynamicIslandPairHistory::topology_storage(std::uint32_t generation) const noexcept {
    return topology_storage_.data() + static_cast<std::size_t>(generation) * config_.bodies;
}

std::size_t DynamicIslandPairHistory::find_root(std::size_t body) noexcept {
    while (parent_[body] != body) {
        parent_[body] = parent_[parent_[body]];
        body = parent_[body];
    }
    return body;
}
void DynamicIslandPairHistory::unite(std::size_t first, std::size_t second) noexcept {
    first = find_root(first);
    second = find_root(second);
    if (first == second) return;
    if (second < first) std::swap(first, second);
    parent_[second] = first;
}

std::uint32_t DynamicIslandPairHistory::acquire_pair_generation() {
    for (std::uint32_t generation = 0U; generation < pair_generations_.size(); ++generation) {
        if (pair_generations_[generation].refs == 0U) {
            pair_generations_[generation] = {.refs = staging_refcount};
            return generation;
        }
    }
    ++stats_.capacity_failures;
    throw std::length_error("No free authoritative pair generation is available");
}
std::uint32_t DynamicIslandPairHistory::acquire_topology_generation() {
    for (std::uint32_t generation = 0U; generation < topology_generations_.size(); ++generation) {
        if (topology_generations_[generation].refs == 0U) {
            topology_generations_[generation] = {.refs = staging_refcount};
            return generation;
        }
    }
    ++stats_.capacity_failures;
    throw std::length_error("No free island topology generation is available");
}
void DynamicIslandPairHistory::release_pair_generation(std::uint32_t generation) noexcept {
    if (generation == no_generation) return;
    PairGeneration& item = pair_generations_[generation];
    if (item.refs == 0U || item.refs == staging_refcount) return;
    if (--item.refs == 0U) item = {};
}
void DynamicIslandPairHistory::release_topology_generation(std::uint32_t generation) noexcept {
    if (generation == no_generation) return;
    TopologyGeneration& item = topology_generations_[generation];
    if (item.refs == 0U || item.refs == staging_refcount) return;
    if (--item.refs == 0U) item = {};
}
void DynamicIslandPairHistory::release_snapshot(Snapshot& snapshot) noexcept {
    if (snapshot.frame == empty_frame) return;
    release_pair_generation(snapshot.pair_generation);
    release_topology_generation(snapshot.topology_generation);
    snapshot = {};
}
void DynamicIslandPairHistory::release_staging() noexcept {
    if (staging_pair_ != no_generation) {
        pair_generations_[staging_pair_] = {};
        staging_pair_ = no_generation;
    }
    if (staging_topology_ != no_generation) {
        topology_generations_[staging_topology_] = {};
        staging_topology_ = no_generation;
    }
}

std::uint64_t DynamicIslandPairHistory::topology_signature(
    std::span<const NormalContact> contacts) const noexcept {
    // Connectivity signature: independent of contact order, orientation and normal.
    // The history topology partitions pairs by connected components; contact physics is
    // versioned by the physical snapshot and does not belong in this signature.
    std::uint64_t sum = 0x9E3779B97F4A7C15ULL + contacts.size();
    std::uint64_t xor_value = 0xD6E8FEB86659FD93ULL ^ config_.bodies;
    for (const NormalContact& contact : contacts) {
        const std::uint64_t first = std::min(contact.first, contact.second);
        const std::uint64_t second = std::max(contact.first, contact.second);
        std::uint64_t item = first * 0x9E3779B185EBCA87ULL
            ^ second * 0xC2B2AE3D27D4EB4FULL;
        item ^= item >> 30U; item *= 0xBF58476D1CE4E5B9ULL;
        item ^= item >> 27U; item *= 0x94D049BB133111EBULL;
        item ^= item >> 31U;
        sum += item;
        xor_value ^= item + 0x9E3779B97F4A7C15ULL + (item << 6U) + (item >> 2U);
    }
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    mix_u64(hash, config_.bodies);
    mix_u64(hash, contacts.size());
    mix_u64(hash, sum);
    mix_u64(hash, xor_value);
    return hash;
}

void DynamicIslandPairHistory::build_topology(
    std::span<const NormalContact> contacts,
    std::uint32_t topology_generation) {
    if (contacts.size() > config_.maximum_contacts) {
        ++stats_.capacity_failures;
        throw std::length_error("Dynamic island contact capacity exceeded");
    }
    std::iota(parent_.begin(), parent_.end(), 0U);
    std::fill(root_to_island_.begin(), root_to_island_.end(), missing);
    for (const NormalContact& contact : contacts) {
        if (contact.first >= config_.bodies || contact.second >= config_.bodies
            || contact.first == contact.second || (contact.normal.x == 0 && contact.normal.y == 0)) {
            throw std::invalid_argument("Invalid contact in dynamic island topology");
        }
        unite(contact.first, contact.second);
    }
    std::size_t islands = 0U;
    std::size_t* output = topology_storage(topology_generation);
    for (std::size_t body = 0U; body < config_.bodies; ++body) {
        const std::size_t root = find_root(body);
        if (root_to_island_[root] == missing) root_to_island_[root] = islands++;
        output[body] = root_to_island_[root];
    }
    topology_generations_[topology_generation].island_count = static_cast<std::uint32_t>(islands);
    topology_generations_[topology_generation].signature = topology_signature(contacts);
}

void DynamicIslandPairHistory::write_pairs(
    std::span<const BroadphasePair> pairs,
    std::uint32_t topology_generation,
    std::uint32_t pair_generation) {
    if (pairs.size() > config_.maximum_pairs) {
        ++stats_.capacity_failures;
        throw std::length_error("Authoritative pair capacity exceeded");
    }
    const std::size_t* islands = topology_storage(topology_generation);
    std::size_t count = 0U;
    std::size_t spill = 0U;
    BroadphasePair* spill_output = spill_storage_.data()
        + static_cast<std::size_t>(pair_generation) * config_.maximum_pairs;
    for (const BroadphasePair raw : pairs) {
        BroadphasePair pair = canonical_pair(raw);
        if (pair.second >= config_.bodies) throw std::out_of_range("Authoritative pair body is outside capacity");
        staging_pairs_[count++] = pair;
        if (islands[pair.first] != islands[pair.second]) spill_output[spill++] = pair;
    }
    std::sort(staging_pairs_.begin(), staging_pairs_.begin() + static_cast<std::ptrdiff_t>(count));
    const auto duplicate = std::adjacent_find(staging_pairs_.begin(),
        staging_pairs_.begin() + static_cast<std::ptrdiff_t>(count));
    if (duplicate != staging_pairs_.begin() + static_cast<std::ptrdiff_t>(count)) {
        throw std::invalid_argument("Authoritative pair list contains a duplicate");
    }
    std::copy(staging_pairs_.begin(), staging_pairs_.begin() + static_cast<std::ptrdiff_t>(count),
        pair_storage(pair_generation));
    std::sort(spill_output, spill_output + static_cast<std::ptrdiff_t>(spill));
    pair_generations_[pair_generation].count = static_cast<std::uint32_t>(count);
    pair_generations_[pair_generation].spill_count = static_cast<std::uint32_t>(spill);
}

void DynamicIslandPairHistory::initialize(
    std::uint64_t frame,
    std::span<const NormalContact> contacts,
    std::span<const BroadphasePair> pairs) {
    clear();
    staging_topology_ = acquire_topology_generation();
    staging_pair_ = acquire_pair_generation();
    try {
        build_topology(contacts, staging_topology_);
        write_pairs(pairs, staging_topology_, staging_pair_);
        Snapshot& target = slot(frame);
        target = {.frame = frame, .pair_generation = staging_pair_,
            .topology_generation = staging_topology_};
        pair_generations_[staging_pair_].refs = 1U;
        topology_generations_[staging_topology_].refs = 1U;
        staging_pair_ = staging_topology_ = no_generation;
        initialized_ = true;
    } catch (...) {
        release_staging();
        throw;
    }
    ++stats_.snapshots_captured;
    ++stats_.pair_generations_written;
    ++stats_.topology_generations_written;
    stats_.live_pairs = pairs.size();
    stats_.live_islands = island_count(frame);
    stats_.live_spill_pairs = spill_pair_count(frame);
    stats_.spill_pairs_written += stats_.live_spill_pairs;
}

void DynamicIslandPairHistory::capture(
    std::uint64_t frame,
    std::span<const NormalContact> contacts,
    std::span<const BroadphasePair> pairs,
    std::span<const std::size_t> dirty_bodies,
    bool topology_changed,
    bool topology_hint_complete) {
    if (!initialized_) throw std::logic_error("Dynamic island pair history is not initialized");
    const Snapshot& previous = slot(frame - 1U);
    if (previous.frame != frame - 1U) {
        throw std::invalid_argument("Dynamic island pair history requires consecutive frames");
    }
    if (!topology_hint_complete || topology_changed) {
        const std::uint64_t signature = topology_signature(contacts);
        const bool topology_differs = signature
            != topology_generations_[previous.topology_generation].signature;
        if (topology_hint_complete && !topology_changed && topology_differs) {
            throw std::invalid_argument("Topology changed despite a complete unchanged hint");
        }
        topology_changed = topology_changed || topology_differs;
    }
    for (const std::size_t body : dirty_bodies) {
        if (body >= config_.bodies) throw std::out_of_range("Dirty body is outside authoritative pair history");
    }
    const bool pair_dirty = topology_changed || !dirty_bodies.empty()
        || pairs.size() != pair_generations_[previous.pair_generation].count;

    std::uint32_t next_topology = previous.topology_generation;
    std::uint32_t next_pairs = previous.pair_generation;
    if (topology_changed) staging_topology_ = acquire_topology_generation();
    if (pair_dirty) staging_pair_ = acquire_pair_generation();
    try {
        if (topology_changed) {
            build_topology(contacts, staging_topology_);
            next_topology = staging_topology_;
        }
        if (pair_dirty) {
            write_pairs(pairs, next_topology, staging_pair_);
            next_pairs = staging_pair_;
        }
        Snapshot& target = slot(frame);
        if (target.frame != empty_frame) release_snapshot(target);
        target = {.frame = frame, .pair_generation = next_pairs,
            .topology_generation = next_topology};
        if (pair_dirty) {
            pair_generations_[next_pairs].refs = 1U;
            staging_pair_ = no_generation;
            ++stats_.pair_generations_written;
        } else {
            ++pair_generations_[next_pairs].refs;
            ++stats_.pair_generations_shared;
        }
        if (topology_changed) {
            topology_generations_[next_topology].refs = 1U;
            staging_topology_ = no_generation;
            ++stats_.topology_generations_written;
            ++stats_.topology_changes;
        } else {
            ++topology_generations_[next_topology].refs;
            ++stats_.topology_generations_shared;
        }
    } catch (...) {
        release_staging();
        throw;
    }
    ++stats_.snapshots_captured;
    stats_.spill_pairs_written += pair_dirty ? pair_generations_[next_pairs].spill_count : 0U;
    stats_.live_pairs = pairs.size();
    stats_.live_islands = topology_generations_[next_topology].island_count;
    stats_.live_spill_pairs = pair_generations_[next_pairs].spill_count;
}

std::size_t DynamicIslandPairHistory::restore_pairs(
    std::uint64_t frame,
    std::span<BroadphasePair> output) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Authoritative pair snapshot is not retained");
    const PairGeneration& generation = pair_generations_[snapshot.pair_generation];
    if (output.size() < generation.count) throw std::length_error("Authoritative pair restore buffer is too small");
    std::copy(pair_storage(snapshot.pair_generation),
        pair_storage(snapshot.pair_generation) + generation.count, output.begin());
    return generation.count;
}
std::size_t DynamicIslandPairHistory::restore_spill_pairs(
    std::uint64_t frame,
    std::span<BroadphasePair> output) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Authoritative pair snapshot is not retained");
    const PairGeneration& generation = pair_generations_[snapshot.pair_generation];
    if (output.size() < generation.spill_count) throw std::length_error("Spill restore buffer is too small");
    const BroadphasePair* source = spill_storage_.data()
        + static_cast<std::size_t>(snapshot.pair_generation) * config_.maximum_pairs;
    std::copy(source, source + generation.spill_count, output.begin());
    return generation.spill_count;
}

std::size_t DynamicIslandPairHistory::restore_body_islands(
    std::uint64_t frame,
    std::span<std::size_t> output) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Island topology snapshot is not retained");
    if (output.size() < config_.bodies) throw std::length_error("Island topology restore buffer is too small");
    std::copy(topology_storage(snapshot.topology_generation),
        topology_storage(snapshot.topology_generation) + config_.bodies, output.begin());
    return topology_generations_[snapshot.topology_generation].island_count;
}

void DynamicIslandPairHistory::truncate_after(std::uint64_t frame) {
    for (Snapshot& snapshot : snapshots_) {
        if (snapshot.frame != empty_frame && snapshot.frame > frame) release_snapshot(snapshot);
    }
}
void DynamicIslandPairHistory::clear() noexcept {
    if (initialized_) {
        for (Snapshot& snapshot : snapshots_) release_snapshot(snapshot);
    }
    release_staging();
    initialized_ = false;
    stats_ = {};
}
bool DynamicIslandPairHistory::contains(std::uint64_t frame) const noexcept {
    return initialized_ && slot(frame).frame == frame;
}
std::size_t DynamicIslandPairHistory::pair_count(std::uint64_t frame) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Authoritative pair snapshot is not retained");
    return pair_generations_[snapshot.pair_generation].count;
}
std::size_t DynamicIslandPairHistory::island_count(std::uint64_t frame) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Island topology snapshot is not retained");
    return topology_generations_[snapshot.topology_generation].island_count;
}
std::size_t DynamicIslandPairHistory::spill_pair_count(std::uint64_t frame) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Authoritative pair snapshot is not retained");
    return pair_generations_[snapshot.pair_generation].spill_count;
}
std::uint64_t DynamicIslandPairHistory::hash(std::uint64_t frame) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Authoritative pair snapshot is not retained");
    const PairGeneration& pairs = pair_generations_[snapshot.pair_generation];
    const TopologyGeneration& topology = topology_generations_[snapshot.topology_generation];
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    mix_u64(hash, frame);
    mix_u64(hash, pairs.count);
    mix_u64(hash, pairs.spill_count);
    mix_u64(hash, topology.island_count);
    mix_u64(hash, topology.signature);
    const std::size_t* islands = topology_storage(snapshot.topology_generation);
    for (std::size_t body = 0U; body < config_.bodies; ++body) mix_u64(hash, islands[body]);
    const BroadphasePair* values = pair_storage(snapshot.pair_generation);
    for (std::size_t index = 0U; index < pairs.count; ++index) {
        mix_u64(hash, values[index].first);
        mix_u64(hash, values[index].second);
    }
    return hash;
}
std::size_t DynamicIslandPairHistory::reserved_bytes() const noexcept {
    return pair_generations_.capacity() * sizeof(PairGeneration)
        + topology_generations_.capacity() * sizeof(TopologyGeneration)
        + pair_storage_.capacity() * sizeof(BroadphasePair)
        + spill_storage_.capacity() * sizeof(BroadphasePair)
        + topology_storage_.capacity() * sizeof(std::size_t)
        + snapshots_.capacity() * sizeof(Snapshot)
        + parent_.capacity() * sizeof(std::size_t)
        + root_to_island_.capacity() * sizeof(std::size_t)
        + staging_pairs_.capacity() * sizeof(BroadphasePair);
}

} // namespace neoeng::core
