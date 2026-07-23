#include "neoeng/core/versioned_island_pair_history.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace neoeng::core {
namespace {
constexpr std::size_t missing = std::numeric_limits<std::size_t>::max();
BroadphasePair canonical(BroadphasePair pair) {
    if (pair.second < pair.first) std::swap(pair.first, pair.second);
    if (pair.first == pair.second) throw std::invalid_argument("Island pair cannot be self-referential");
    return pair;
}
} // namespace

VersionedIslandPairHistory::VersionedIslandPairHistory(VersionedIslandPairHistoryConfig config)
    : config_(config), generations_per_island_(config.segment_generations_per_island == 0U
          ? config.history_capacity + 2U : config.segment_generations_per_island),
      parent_(config.bodies), root_to_island_(config.bodies, missing),
      body_island_(config.bodies, missing), contact_counts_(config.bodies),
      capacities_(config.bodies), storage_base_(config.bodies + 1U),
      tables_(config.history_capacity + 2U), snapshots_(config.history_capacity),
      dirty_islands_(config.bodies), staging_counts_(config.bodies),
      staging_base_(config.bodies + 1U), staging_write_(config.bodies + 1U),
      staged_local_generation_(config.bodies, no_generation) {
    if (config.bodies == 0U || config.history_capacity < 2U || generations_per_island_ < 2U
        || config.maximum_contacts > config.bodies * (config.bodies - 1U) / 2U) {
        throw std::invalid_argument("Versioned island pair history configuration is invalid");
    }
}

std::size_t VersionedIslandPairHistory::find_root(std::size_t body) noexcept {
    while (parent_[body] != body) {
        parent_[body] = parent_[parent_[body]];
        body = parent_[body];
    }
    return body;
}
void VersionedIslandPairHistory::unite(std::size_t first, std::size_t second) noexcept {
    first = find_root(first); second = find_root(second);
    if (first == second) return;
    if (second < first) std::swap(first, second);
    parent_[second] = first;
}
VersionedIslandPairHistory::Snapshot& VersionedIslandPairHistory::slot(std::uint64_t frame) noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}
const VersionedIslandPairHistory::Snapshot& VersionedIslandPairHistory::slot(std::uint64_t frame) const noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}
std::size_t VersionedIslandPairHistory::generation_index(
    std::size_t island, std::uint32_t local) const noexcept {
    return island * generations_per_island_ + local;
}
BroadphasePair* VersionedIslandPairHistory::generation_storage(
    std::size_t island, std::uint32_t local) noexcept {
    return storage_.data() + storage_base_[island]
        + static_cast<std::size_t>(local) * capacities_[island];
}
const BroadphasePair* VersionedIslandPairHistory::generation_storage(
    std::size_t island, std::uint32_t local) const noexcept {
    return storage_.data() + storage_base_[island]
        + static_cast<std::size_t>(local) * capacities_[island];
}

std::uint32_t VersionedIslandPairHistory::acquire_generation(std::size_t island) {
    for (std::uint32_t local = 0U; local < generations_per_island_; ++local) {
        Generation& generation = generations_[generation_index(island, local)];
        if (generation.refs == 0U) {
            generation.refs = staging_refcount;
            generation.count = 0U;
            return local;
        }
    }
    ++stats_.capacity_failures;
    throw std::length_error("No free island pair generation is available");
}

std::uint32_t VersionedIslandPairHistory::acquire_table() {
    for (std::uint32_t table = 0U; table < tables_.size(); ++table) {
        if (tables_[table].refs == 0U) {
            tables_[table].refs = staging_refcount;
            std::fill(tables_[table].generation.begin(), tables_[table].generation.end(), no_generation);
            return table;
        }
    }
    ++stats_.capacity_failures;
    throw std::length_error("No free island generation table is available");
}

void VersionedIslandPairHistory::release_table(std::uint32_t table) noexcept {
    if (table == no_generation) return;
    GenerationTable& item = tables_[table];
    if (item.refs == 0U || item.refs == staging_refcount) return;
    if (--item.refs != 0U) return;
    for (std::size_t island = 0U; island < island_count_; ++island) {
        const std::uint32_t local = item.generation[island];
        if (local == no_generation) continue;
        Generation& generation = generations_[generation_index(island, local)];
        if (generation.refs != 0U && generation.refs != staging_refcount) --generation.refs;
        item.generation[island] = no_generation;
    }
}

void VersionedIslandPairHistory::release_snapshot(Snapshot& snapshot) noexcept {
    if (snapshot.frame == empty_frame) return;
    release_table(snapshot.table);
    snapshot.frame = empty_frame;
    snapshot.pair_count = 0U;
    snapshot.table = no_generation;
}

void VersionedIslandPairHistory::release_staging() noexcept {
    for (std::size_t island = 0U; island < island_count_; ++island) {
        const std::uint32_t local = staged_local_generation_[island];
        if (local == no_generation) continue;
        Generation& generation = generations_[generation_index(island, local)];
        if (generation.refs == staging_refcount) {
            generation.refs = 0U;
            generation.count = 0U;
        }
        staged_local_generation_[island] = no_generation;
    }
    if (staging_table_ != no_generation) {
        tables_[staging_table_].refs = 0U;
        std::fill(tables_[staging_table_].generation.begin(),
            tables_[staging_table_].generation.end(), no_generation);
        staging_table_ = no_generation;
    }
}

void VersionedIslandPairHistory::build_topology(std::span<const NormalContact> contacts) {
    if (contacts.size() > config_.maximum_contacts) {
        throw std::length_error("Island pair history contact capacity exceeded");
    }
    std::iota(parent_.begin(), parent_.end(), 0U);
    std::fill(root_to_island_.begin(), root_to_island_.end(), missing);
    std::fill(contact_counts_.begin(), contact_counts_.end(), 0U);
    for (const NormalContact& contact : contacts) {
        if (contact.first >= config_.bodies || contact.second >= config_.bodies
            || contact.first == contact.second) {
            throw std::invalid_argument("Invalid contact in versioned island pair history");
        }
        unite(contact.first, contact.second);
    }
    island_count_ = 0U;
    for (std::size_t body = 0U; body < config_.bodies; ++body) {
        const std::size_t root = find_root(body);
        if (root_to_island_[root] == missing) root_to_island_[root] = island_count_++;
        body_island_[body] = root_to_island_[root];
    }
    for (const NormalContact& contact : contacts) ++contact_counts_[body_island_[contact.first]];
    storage_base_[0] = 0U;
    for (std::size_t island = 0U; island < island_count_; ++island) {
        capacities_[island] = std::max<std::size_t>(1U,
            contact_counts_[island] + config_.extra_pairs_per_island);
        const std::size_t amount = capacities_[island] * generations_per_island_;
        if (storage_base_[island] > std::numeric_limits<std::size_t>::max() - amount) {
            throw std::overflow_error("Versioned island pair history storage overflow");
        }
        storage_base_[island + 1U] = storage_base_[island] + amount;
    }
    generations_.assign(island_count_ * generations_per_island_, Generation{});
    storage_.assign(storage_base_[island_count_], BroadphasePair{});
    staging_pairs_.assign(std::accumulate(capacities_.begin(), capacities_.begin()
        + static_cast<std::ptrdiff_t>(island_count_), std::size_t{0}), BroadphasePair{});
    for (GenerationTable& table : tables_) {
        table.refs = 0U;
        table.generation.assign(island_count_, no_generation);
    }
    for (Snapshot& snapshot : snapshots_) snapshot = {};
    staging_table_ = no_generation;
    std::fill(staged_local_generation_.begin(), staged_local_generation_.end(), no_generation);
    stats_ = {};
    stats_.island_count = island_count_;
}

void VersionedIslandPairHistory::partition_pairs(
    std::span<const BroadphasePair> pairs,
    std::span<const std::uint8_t> dirty_islands) {
    std::fill(staging_counts_.begin(), staging_counts_.begin()
        + static_cast<std::ptrdiff_t>(island_count_), 0U);
    for (std::size_t island = 0U; island < island_count_; ++island) {
        if (dirty_islands[island] != 0U) staged_local_generation_[island] = acquire_generation(island);
    }
    try {
        for (const BroadphasePair& raw : pairs) {
            const BroadphasePair pair = canonical(raw);
            if (pair.first >= config_.bodies || pair.second >= config_.bodies) {
                throw std::invalid_argument("Pair body is outside versioned island history");
            }
            const std::size_t first_island = body_island_[pair.first];
            const std::size_t second_island = body_island_[pair.second];
            if (first_island != second_island) {
                ++stats_.cross_island_pairs;
                throw std::domain_error("Cross-island broadphase pair requires global fallback");
            }
            if (dirty_islands[first_island] == 0U) continue;
            if (++staging_counts_[first_island] > capacities_[first_island]) {
                ++stats_.capacity_failures;
                throw std::length_error("Island pair segment capacity exceeded");
            }
        }
        staging_base_[0] = 0U;
        for (std::size_t island = 0U; island < island_count_; ++island) {
            staging_base_[island + 1U] = staging_base_[island] + staging_counts_[island];
        }
        std::copy(staging_base_.begin(), staging_base_.begin()
            + static_cast<std::ptrdiff_t>(island_count_ + 1U), staging_write_.begin());
        for (const BroadphasePair& raw : pairs) {
            const BroadphasePair pair = canonical(raw);
            const std::size_t island = body_island_[pair.first];
            if (dirty_islands[island] != 0U) staging_pairs_[staging_write_[island]++] = pair;
        }
        for (std::size_t island = 0U; island < island_count_; ++island) {
            if (dirty_islands[island] == 0U) continue;
            const std::uint32_t local = staged_local_generation_[island];
            Generation& generation = generations_[generation_index(island, local)];
            generation.count = static_cast<std::uint32_t>(staging_counts_[island]);
            std::copy(staging_pairs_.begin() + static_cast<std::ptrdiff_t>(staging_base_[island]),
                staging_pairs_.begin() + static_cast<std::ptrdiff_t>(staging_base_[island + 1U]),
                generation_storage(island, local));
            ++stats_.segments_written;
            stats_.pairs_written += staging_counts_[island];
        }
    } catch (...) {
        release_staging();
        throw;
    }
}

void VersionedIslandPairHistory::initialize(
    std::uint64_t frame,
    std::span<const NormalContact> contacts,
    std::span<const BroadphasePair> pairs) {
    clear();
    build_topology(contacts);
    initialized_ = true;
    std::fill(dirty_islands_.begin(), dirty_islands_.begin()
        + static_cast<std::ptrdiff_t>(island_count_), std::uint8_t{1});
    staging_table_ = acquire_table();
    try {
        partition_pairs(pairs, std::span<const std::uint8_t>(dirty_islands_.data(), island_count_));
        GenerationTable& table = tables_[staging_table_];
        for (std::size_t island = 0U; island < island_count_; ++island) {
            const std::uint32_t local = staged_local_generation_[island];
            table.generation[island] = local;
            generations_[generation_index(island, local)].refs = 1U;
            staged_local_generation_[island] = no_generation;
        }
        table.refs = 1U;
        Snapshot& target = slot(frame);
        target = {.frame = frame, .pair_count = pairs.size(), .table = staging_table_};
        staging_table_ = no_generation;
    } catch (...) {
        release_staging();
        initialized_ = false;
        throw;
    }
    ++stats_.snapshots_captured;
    ++stats_.full_captures;
    stats_.live_pairs = pairs.size();
}

void VersionedIslandPairHistory::capture(
    std::uint64_t frame,
    std::span<const BroadphasePair> pairs,
    std::span<const std::size_t> dirty_bodies,
    bool force_full) {
    if (!initialized_) throw std::logic_error("Versioned island pair history is not initialized");
    const Snapshot& previous = slot(frame - 1U);
    if (previous.frame != frame - 1U) {
        throw std::invalid_argument("Versioned island pair history requires consecutive frames");
    }
    if (!force_full && dirty_bodies.empty() && pairs.size() != previous.pair_count) {
        throw std::invalid_argument("Pair-count change requires dirty islands or a full capture");
    }
    std::fill(dirty_islands_.begin(), dirty_islands_.begin()
        + static_cast<std::ptrdiff_t>(island_count_), force_full ? std::uint8_t{1} : std::uint8_t{0});
    if (!force_full) {
        for (const std::size_t body : dirty_bodies) {
            if (body >= config_.bodies) throw std::out_of_range("Dirty body is outside island history");
            dirty_islands_[body_island_[body]] = 1U;
        }
    }
    const bool any_dirty = std::any_of(dirty_islands_.begin(), dirty_islands_.begin()
        + static_cast<std::ptrdiff_t>(island_count_), [](std::uint8_t value) { return value != 0U; });
    Snapshot& target = slot(frame);
    if (!any_dirty) {
        if (target.frame != empty_frame) release_snapshot(target);
        target = {.frame = frame, .pair_count = pairs.size(), .table = previous.table};
        ++tables_[previous.table].refs;
        ++stats_.tables_shared;
        stats_.segments_reused += island_count_;
        ++stats_.snapshots_captured;
        ++stats_.incremental_captures;
        stats_.live_pairs = pairs.size();
        return;
    }

    staging_table_ = acquire_table();
    try {
        GenerationTable& table = tables_[staging_table_];
        table.generation = tables_[previous.table].generation;
        partition_pairs(pairs, std::span<const std::uint8_t>(dirty_islands_.data(), island_count_));
        for (std::size_t island = 0U; island < island_count_; ++island) {
            if (dirty_islands_[island] != 0U) {
                const std::uint32_t local = staged_local_generation_[island];
                table.generation[island] = local;
                generations_[generation_index(island, local)].refs = 1U;
                staged_local_generation_[island] = no_generation;
            } else {
                ++generations_[generation_index(island, table.generation[island])].refs;
                ++stats_.segments_reused;
            }
        }
        table.refs = 1U;
        if (target.frame != empty_frame) release_snapshot(target);
        target = {.frame = frame, .pair_count = pairs.size(), .table = staging_table_};
        staging_table_ = no_generation;
    } catch (...) {
        release_staging();
        throw;
    }
    ++stats_.tables_copied;
    ++stats_.snapshots_captured;
    if (force_full) ++stats_.full_captures; else ++stats_.incremental_captures;
    stats_.live_pairs = pairs.size();
}

std::size_t VersionedIslandPairHistory::restore(
    std::uint64_t frame,
    std::span<BroadphasePair> output) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Island pair snapshot is not retained");
    if (output.size() < snapshot.pair_count) throw std::length_error("Island pair restore buffer is too small");
    const GenerationTable& table = tables_[snapshot.table];
    std::size_t count = 0U;
    for (std::size_t island = 0U; island < island_count_; ++island) {
        const std::uint32_t local = table.generation[island];
        const Generation& generation = generations_[generation_index(island, local)];
        const BroadphasePair* source = generation_storage(island, local);
        std::copy(source, source + generation.count,
            output.begin() + static_cast<std::ptrdiff_t>(count));
        count += generation.count;
    }
    std::sort(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(count));
    return count;
}

void VersionedIslandPairHistory::truncate_after(std::uint64_t frame) {
    for (Snapshot& snapshot : snapshots_) {
        if (snapshot.frame != empty_frame && snapshot.frame > frame) release_snapshot(snapshot);
    }
}
void VersionedIslandPairHistory::clear() noexcept {
    if (initialized_) {
        for (Snapshot& snapshot : snapshots_) release_snapshot(snapshot);
        release_staging();
    }
    initialized_ = false;
}
bool VersionedIslandPairHistory::contains(std::uint64_t frame) const noexcept {
    return initialized_ && slot(frame).frame == frame;
}
std::size_t VersionedIslandPairHistory::island_of_body(std::size_t body) const {
    if (!initialized_ || body >= config_.bodies) throw std::out_of_range("Invalid island-history body");
    return body_island_[body];
}
std::size_t VersionedIslandPairHistory::pair_count(std::uint64_t frame) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Island pair snapshot is not retained");
    return snapshot.pair_count;
}
std::uint64_t VersionedIslandPairHistory::hash(std::uint64_t frame) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Island pair snapshot is not retained");
    const GenerationTable& table = tables_[snapshot.table];
    std::uint64_t value = 0xCBF29CE484222325ULL;
    const auto mix = [&value](std::uint64_t item) {
        for (unsigned byte = 0U; byte < 8U; ++byte) {
            value ^= (item >> (byte * 8U)) & 0xFFU;
            value *= 0x100000001B3ULL;
        }
    };
    mix(frame); mix(snapshot.pair_count); mix(island_count_);
    for (std::size_t island = 0U; island < island_count_; ++island) {
        mix(island);
        const std::uint32_t local = table.generation[island];
        const Generation& generation = generations_[generation_index(island, local)];
        mix(generation.count);
        const BroadphasePair* source = generation_storage(island, local);
        for (std::size_t index = 0U; index < generation.count; ++index) {
            mix(source[index].first); mix(source[index].second);
        }
    }
    return value;
}
std::size_t VersionedIslandPairHistory::reserved_bytes() const noexcept {
    std::size_t bytes = parent_.capacity() * sizeof(std::size_t)
        + root_to_island_.capacity() * sizeof(std::size_t)
        + body_island_.capacity() * sizeof(std::size_t)
        + contact_counts_.capacity() * sizeof(std::size_t)
        + capacities_.capacity() * sizeof(std::size_t)
        + storage_base_.capacity() * sizeof(std::size_t)
        + generations_.capacity() * sizeof(Generation)
        + storage_.capacity() * sizeof(BroadphasePair)
        + dirty_islands_.capacity() * sizeof(std::uint8_t)
        + staging_counts_.capacity() * sizeof(std::size_t)
        + (staging_base_.capacity() + staging_write_.capacity()) * sizeof(std::size_t)
        + staging_pairs_.capacity() * sizeof(BroadphasePair)
        + staged_local_generation_.capacity() * sizeof(std::uint32_t)
        + snapshots_.capacity() * sizeof(Snapshot);
    for (const GenerationTable& table : tables_) {
        bytes += table.generation.capacity() * sizeof(std::uint32_t);
    }
    return bytes;
}

} // namespace neoeng::core
