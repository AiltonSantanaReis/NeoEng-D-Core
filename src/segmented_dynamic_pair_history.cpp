#include "neoeng/core/segmented_dynamic_pair_history.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace neoeng::core {
namespace {
constexpr std::size_t missing = std::numeric_limits<std::size_t>::max();

BroadphasePair canonical_pair(BroadphasePair pair) {
    if (pair.second < pair.first) std::swap(pair.first, pair.second);
    if (pair.first == pair.second) {
        throw std::invalid_argument("Segmented pair cannot be self-referential");
    }
    return pair;
}

void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}
} // namespace

SegmentedDynamicPairHistory::SegmentedDynamicPairHistory(
    SegmentedDynamicPairHistoryConfig config)
    : config_(config),
      segment_generation_count_(config.segment_generations == 0U
          ? config.bodies + config.history_capacity + 2U : config.segment_generations),
      spill_generation_count_(config.spill_generations == 0U
          ? config.history_capacity + 2U : config.spill_generations),
      table_generation_count_(config.table_generations == 0U
          ? config.history_capacity + 2U : config.table_generations),
      segment_generations_(segment_generation_count_),
      spill_generations_(spill_generation_count_), tables_(table_generation_count_),
      segment_payload_(segment_generation_count_ * config.maximum_pairs_per_segment),
      spill_payload_(spill_generation_count_ * config.maximum_pairs),
      table_body_keys_(table_generation_count_ * config.bodies, missing),
      table_segments_(table_generation_count_ * config.bodies, no_generation),
      snapshots_(config.history_capacity), parent_(config.bodies),
      current_body_keys_(config.bodies, missing), dirty_keys_(config.bodies),
      keyed_pairs_(config.maximum_pairs), spill_pairs_(config.maximum_pairs),
      staged_segments_(config.bodies, no_generation) {
    if (config.bodies == 0U || config.history_capacity < 2U
        || config.maximum_pairs == 0U || config.maximum_pairs_per_segment == 0U
        || segment_generation_count_ < 2U || spill_generation_count_ < 2U
        || table_generation_count_ < 2U
        || config.maximum_contacts > config.bodies * (config.bodies - 1U) / 2U) {
        throw std::invalid_argument("Segmented dynamic pair history configuration is invalid");
    }
}

SegmentedDynamicPairHistory::Snapshot& SegmentedDynamicPairHistory::slot(
    std::uint64_t frame) noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}
const SegmentedDynamicPairHistory::Snapshot& SegmentedDynamicPairHistory::slot(
    std::uint64_t frame) const noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}

std::size_t SegmentedDynamicPairHistory::find_root(std::size_t body) noexcept {
    while (parent_[body] != body) {
        parent_[body] = parent_[parent_[body]];
        body = parent_[body];
    }
    return body;
}
void SegmentedDynamicPairHistory::unite(std::size_t first, std::size_t second) noexcept {
    first = find_root(first);
    second = find_root(second);
    if (first == second) return;
    if (second < first) std::swap(first, second);
    parent_[second] = first;
}

std::size_t* SegmentedDynamicPairHistory::table_body_keys(std::uint32_t table) noexcept {
    return table_body_keys_.data() + static_cast<std::size_t>(table) * config_.bodies;
}
const std::size_t* SegmentedDynamicPairHistory::table_body_keys(std::uint32_t table) const noexcept {
    return table_body_keys_.data() + static_cast<std::size_t>(table) * config_.bodies;
}
std::uint32_t* SegmentedDynamicPairHistory::table_segments(std::uint32_t table) noexcept {
    return table_segments_.data() + static_cast<std::size_t>(table) * config_.bodies;
}
const std::uint32_t* SegmentedDynamicPairHistory::table_segments(std::uint32_t table) const noexcept {
    return table_segments_.data() + static_cast<std::size_t>(table) * config_.bodies;
}
BroadphasePair* SegmentedDynamicPairHistory::segment_storage(std::uint32_t generation) noexcept {
    return segment_payload_.data()
        + static_cast<std::size_t>(generation) * config_.maximum_pairs_per_segment;
}
const BroadphasePair* SegmentedDynamicPairHistory::segment_storage(
    std::uint32_t generation) const noexcept {
    return segment_payload_.data()
        + static_cast<std::size_t>(generation) * config_.maximum_pairs_per_segment;
}
BroadphasePair* SegmentedDynamicPairHistory::spill_storage(std::uint32_t generation) noexcept {
    return spill_payload_.data() + static_cast<std::size_t>(generation) * config_.maximum_pairs;
}
const BroadphasePair* SegmentedDynamicPairHistory::spill_storage(
    std::uint32_t generation) const noexcept {
    return spill_payload_.data() + static_cast<std::size_t>(generation) * config_.maximum_pairs;
}

std::uint32_t SegmentedDynamicPairHistory::acquire_table() {
    for (std::uint32_t index = 0U; index < tables_.size(); ++index) {
        if (tables_[index].refs == 0U) {
            tables_[index] = {};
            tables_[index].refs = staging_refcount;
            tables_[index].spill_generation = no_generation;
            std::fill(table_body_keys(index), table_body_keys(index) + config_.bodies, missing);
            std::fill(table_segments(index), table_segments(index) + config_.bodies, no_generation);
            return index;
        }
    }
    ++stats_.capacity_failures;
    throw std::length_error("No free segmented pair table is available");
}
std::uint32_t SegmentedDynamicPairHistory::acquire_segment() {
    for (std::uint32_t index = 0U; index < segment_generations_.size(); ++index) {
        if (segment_generations_[index].refs == 0U) {
            segment_generations_[index] = {.refs = staging_refcount};
            return index;
        }
    }
    ++stats_.capacity_failures;
    throw std::length_error("No free island segment generation is available");
}
std::uint32_t SegmentedDynamicPairHistory::acquire_spill() {
    for (std::uint32_t index = 0U; index < spill_generations_.size(); ++index) {
        if (spill_generations_[index].refs == 0U) {
            spill_generations_[index] = {.refs = staging_refcount};
            return index;
        }
    }
    ++stats_.capacity_failures;
    throw std::length_error("No free spill generation is available");
}

void SegmentedDynamicPairHistory::release_table(std::uint32_t table) noexcept {
    if (table == no_generation) return;
    TableGeneration& item = tables_[table];
    if (item.refs == 0U || item.refs == staging_refcount) return;
    if (--item.refs != 0U) return;
    const std::uint32_t* segments = table_segments(table);
    for (std::size_t key = 0U; key < config_.bodies; ++key) {
        const std::uint32_t generation = segments[key];
        if (generation == no_generation) continue;
        SegmentGeneration& segment = segment_generations_[generation];
        if (segment.refs != 0U && segment.refs != staging_refcount && --segment.refs == 0U) {
            segment = {};
        }
    }
    if (item.spill_generation != no_generation) {
        SpillGeneration& spill = spill_generations_[item.spill_generation];
        if (spill.refs != 0U && spill.refs != staging_refcount && --spill.refs == 0U) spill = {};
    }
    item = {};
    item.spill_generation = no_generation;
}
void SegmentedDynamicPairHistory::release_snapshot(Snapshot& snapshot) noexcept {
    if (snapshot.frame == empty_frame) return;
    release_table(snapshot.table);
    snapshot = {};
}
void SegmentedDynamicPairHistory::release_staging() noexcept {
    for (std::size_t index = 0U; index < staged_segment_count_; ++index) {
        const std::uint32_t generation = staged_segments_[index];
        if (generation != no_generation) segment_generations_[generation] = {};
        staged_segments_[index] = no_generation;
    }
    staged_segment_count_ = 0U;
    if (staging_spill_ != no_generation) {
        spill_generations_[staging_spill_] = {};
        staging_spill_ = no_generation;
    }
    if (staging_table_ != no_generation) {
        tables_[staging_table_] = {};
        tables_[staging_table_].spill_generation = no_generation;
        staging_table_ = no_generation;
    }
}

std::uint64_t SegmentedDynamicPairHistory::topology_signature(
    std::span<const NormalContact> contacts) const noexcept {
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

void SegmentedDynamicPairHistory::build_topology(
    std::span<const NormalContact> contacts) {
    if (contacts.size() > config_.maximum_contacts) {
        ++stats_.capacity_failures;
        throw std::length_error("Segmented history contact capacity exceeded");
    }
    std::iota(parent_.begin(), parent_.end(), 0U);
    for (const NormalContact& contact : contacts) {
        if (contact.first >= config_.bodies || contact.second >= config_.bodies
            || contact.first == contact.second || (contact.normal.x == 0 && contact.normal.y == 0)) {
            throw std::invalid_argument("Invalid contact in segmented topology");
        }
        unite(contact.first, contact.second);
    }
    current_island_count_ = 0U;
    for (std::size_t body = 0U; body < config_.bodies; ++body) {
        current_body_keys_[body] = find_root(body);
        if (current_body_keys_[body] == body) ++current_island_count_;
    }
    current_topology_signature_ = topology_signature(contacts);
}

void SegmentedDynamicPairHistory::partition_pairs(
    std::span<const BroadphasePair> pairs) {
    if (pairs.size() > config_.maximum_pairs) {
        ++stats_.capacity_failures;
        throw std::length_error("Segmented history pair capacity exceeded");
    }
    keyed_pair_count_ = 0U;
    spill_pair_count_ = 0U;
    for (const BroadphasePair raw : pairs) {
        const BroadphasePair pair = canonical_pair(raw);
        if (pair.second >= config_.bodies) {
            throw std::out_of_range("Segmented pair body is outside capacity");
        }
        const std::size_t first_key = current_body_keys_[pair.first];
        const std::size_t second_key = current_body_keys_[pair.second];
        if (first_key == second_key) {
            keyed_pairs_[keyed_pair_count_++] = {.key = first_key, .pair = pair};
        } else {
            spill_pairs_[spill_pair_count_++] = pair;
        }
    }
    auto keyed_begin = keyed_pairs_.begin();
    std::sort(keyed_begin, keyed_begin + static_cast<std::ptrdiff_t>(keyed_pair_count_));
    auto spill_begin = spill_pairs_.begin();
    std::sort(spill_begin, spill_begin + static_cast<std::ptrdiff_t>(spill_pair_count_));
    if (std::adjacent_find(spill_begin, spill_begin + static_cast<std::ptrdiff_t>(spill_pair_count_))
        != spill_begin + static_cast<std::ptrdiff_t>(spill_pair_count_)) {
        throw std::invalid_argument("Segmented spill list contains a duplicate");
    }
    for (std::size_t index = 1U; index < keyed_pair_count_; ++index) {
        if (keyed_pairs_[index - 1U].key == keyed_pairs_[index].key
            && !(keyed_pairs_[index - 1U].pair < keyed_pairs_[index].pair)) {
            throw std::invalid_argument("Segmented island pair list contains a duplicate");
        }
    }
}

bool SegmentedDynamicPairHistory::same_segment(
    std::uint32_t generation,
    std::span<const KeyedPair> run) const noexcept {
    if (generation == no_generation) return run.empty();
    const SegmentGeneration& item = segment_generations_[generation];
    if (item.count != run.size()) return false;
    const BroadphasePair* payload = segment_storage(generation);
    for (std::size_t index = 0U; index < run.size(); ++index) {
        if (!(payload[index] == run[index].pair)) return false;
    }
    return true;
}
bool SegmentedDynamicPairHistory::same_spill(
    std::uint32_t generation,
    std::span<const BroadphasePair> pairs) const noexcept {
    if (generation == no_generation) return pairs.empty();
    const SpillGeneration& item = spill_generations_[generation];
    if (item.count != pairs.size()) return false;
    return std::equal(pairs.begin(), pairs.end(), spill_storage(generation));
}

void SegmentedDynamicPairHistory::initialize(
    std::uint64_t frame,
    std::span<const NormalContact> contacts,
    std::span<const BroadphasePair> pairs) {
    clear();
    capture(frame, contacts, pairs, {}, true, true, false);
    initialized_ = true;
}

void SegmentedDynamicPairHistory::capture(
    std::uint64_t frame,
    std::span<const NormalContact> contacts,
    std::span<const BroadphasePair> pairs,
    std::span<const std::size_t> dirty_bodies,
    bool topology_changed,
    bool topology_hint_complete,
    bool pair_hint_complete) {
    if (initialized_ && frame == 0U) {
        throw std::invalid_argument("Segmented history frame must advance");
    }
    const Snapshot* previous_snapshot = nullptr;
    const TableGeneration* previous_table = nullptr;
    const std::uint32_t* previous_segments = nullptr;
    const std::size_t* previous_body_keys = nullptr;
    if (frame > 0U && contains(frame - 1U)) {
        previous_snapshot = &slot(frame - 1U);
        previous_table = &tables_[previous_snapshot->table];
        previous_segments = table_segments(previous_snapshot->table);
        previous_body_keys = table_body_keys(previous_snapshot->table);
    }

    build_topology(contacts);
    bool topology_equal = previous_table != nullptr
        && previous_table->topology_signature == current_topology_signature_;
    if (topology_equal) {
        topology_equal = std::equal(current_body_keys_.begin(), current_body_keys_.end(),
            previous_body_keys);
    }
    if (topology_hint_complete && topology_changed == topology_equal) {
        throw std::invalid_argument("Complete topology hint contradicts computed topology");
    }
    if (!topology_equal) ++stats_.topology_changes;

    if (previous_table != nullptr && topology_equal && topology_hint_complete
        && !topology_changed && pair_hint_complete && dirty_bodies.empty()) {
        Snapshot& destination = slot(frame);
        if (destination.frame != empty_frame) release_snapshot(destination);
        ++tables_[previous_snapshot->table].refs;
        destination = {.frame = frame, .table = previous_snapshot->table};
        ++stats_.snapshots_captured;
        ++stats_.tables_shared;
        stats_.live_pairs = previous_table->pair_count;
        stats_.live_islands = previous_table->island_count;
        stats_.live_spill_pairs = previous_table->spill_generation == no_generation ? 0U
            : spill_generations_[previous_table->spill_generation].count;
        initialized_ = true;
        return;
    }

    partition_pairs(pairs);
    std::fill(dirty_keys_.begin(), dirty_keys_.end(), pair_hint_complete ? 0U : 1U);
    for (const std::size_t body : dirty_bodies) {
        if (body >= config_.bodies) throw std::out_of_range("Dirty body is outside segmented history");
        dirty_keys_[current_body_keys_[body]] = 1U;
    }
    if (!topology_equal) {
        for (std::size_t body = 0U; body < config_.bodies; ++body) {
            if (current_body_keys_[body] == body) dirty_keys_[body] = 1U;
        }
    }

    try {
        staging_table_ = acquire_table();
        TableGeneration& table = tables_[staging_table_];
        table.island_count = static_cast<std::uint32_t>(current_island_count_);
        table.topology_signature = current_topology_signature_;
        table.pair_count = pairs.size();
        std::copy(current_body_keys_.begin(), current_body_keys_.end(),
            table_body_keys(staging_table_));
        std::uint32_t* output_segments = table_segments(staging_table_);

        std::size_t run_begin = 0U;
        while (run_begin < keyed_pair_count_) {
            const std::size_t key = keyed_pairs_[run_begin].key;
            std::size_t run_end = run_begin + 1U;
            while (run_end < keyed_pair_count_ && keyed_pairs_[run_end].key == key) ++run_end;
            const std::span<const KeyedPair> run(keyed_pairs_.data() + run_begin, run_end - run_begin);
            if (run.size() > config_.maximum_pairs_per_segment) {
                ++stats_.capacity_failures;
                throw std::length_error("Island pair segment capacity exceeded");
            }
            std::uint32_t previous_generation = no_generation;
            if (topology_equal) previous_generation = previous_segments[key];
            bool reuse = previous_generation != no_generation;
            if (reuse && (!pair_hint_complete || dirty_keys_[key] != 0U)) {
                reuse = same_segment(previous_generation, run);
            }
            if (reuse) {
                output_segments[key] = previous_generation;
                ++stats_.segments_reused;
            } else {
                const std::uint32_t generation = acquire_segment();
                staged_segments_[staged_segment_count_++] = generation;
                SegmentGeneration& segment = segment_generations_[generation];
                segment.key = static_cast<std::uint32_t>(key);
                segment.count = static_cast<std::uint32_t>(run.size());
                BroadphasePair* payload = segment_storage(generation);
                for (std::size_t index = 0U; index < run.size(); ++index) payload[index] = run[index].pair;
                output_segments[key] = generation;
                ++stats_.segments_written;
            }
            run_begin = run_end;
        }

        // Preserve explicit empty segments as no_generation. Previous non-empty segments
        // disappear when an island has no pair in the new snapshot.
        const std::span<const BroadphasePair> spill(spill_pairs_.data(), spill_pair_count_);
        std::uint32_t previous_spill = no_generation;
        if (topology_equal && previous_table != nullptr) previous_spill = previous_table->spill_generation;
        if (same_spill(previous_spill, spill)) {
            table.spill_generation = previous_spill;
            ++stats_.spill_reused;
        } else if (!spill.empty()) {
            staging_spill_ = acquire_spill();
            SpillGeneration& item = spill_generations_[staging_spill_];
            item.count = static_cast<std::uint32_t>(spill.size());
            std::copy(spill.begin(), spill.end(), spill_storage(staging_spill_));
            table.spill_generation = staging_spill_;
            ++stats_.spill_written;
        }

        // Commit references only after all validation and payload writes succeeded.
        for (std::size_t key = 0U; key < config_.bodies; ++key) {
            const std::uint32_t generation = output_segments[key];
            if (generation == no_generation) continue;
            SegmentGeneration& item = segment_generations_[generation];
            if (item.refs == staging_refcount) item.refs = 1U;
            else ++item.refs;
        }
        if (table.spill_generation != no_generation) {
            SpillGeneration& spill_item = spill_generations_[table.spill_generation];
            if (spill_item.refs == staging_refcount) spill_item.refs = 1U;
            else ++spill_item.refs;
        }
        table.refs = 1U;

        Snapshot& destination = slot(frame);
        if (destination.frame != empty_frame) release_snapshot(destination);
        destination = {.frame = frame, .table = staging_table_};
        staging_table_ = no_generation;
        staging_spill_ = no_generation;
        for (std::size_t index = 0U; index < staged_segment_count_; ++index) {
            staged_segments_[index] = no_generation;
        }
        staged_segment_count_ = 0U;
        ++stats_.snapshots_captured;
        ++stats_.tables_written;
        stats_.live_pairs = pairs.size();
        stats_.live_islands = current_island_count_;
        stats_.live_spill_pairs = spill_pair_count_;
        initialized_ = true;
    } catch (...) {
        release_staging();
        throw;
    }
}

bool SegmentedDynamicPairHistory::contains(std::uint64_t frame) const noexcept {
    return !snapshots_.empty() && slot(frame).frame == frame;
}
std::size_t SegmentedDynamicPairHistory::pair_count(std::uint64_t frame) const {
    if (!contains(frame)) throw std::out_of_range("Segmented pair frame is unavailable");
    return tables_[slot(frame).table].pair_count;
}
std::size_t SegmentedDynamicPairHistory::island_count(std::uint64_t frame) const {
    if (!contains(frame)) throw std::out_of_range("Segmented pair frame is unavailable");
    return tables_[slot(frame).table].island_count;
}
std::size_t SegmentedDynamicPairHistory::spill_pair_count(std::uint64_t frame) const {
    if (!contains(frame)) throw std::out_of_range("Segmented pair frame is unavailable");
    const TableGeneration& table = tables_[slot(frame).table];
    return table.spill_generation == no_generation ? 0U
        : spill_generations_[table.spill_generation].count;
}

std::size_t SegmentedDynamicPairHistory::restore_pairs(
    std::uint64_t frame,
    std::span<BroadphasePair> output) const {
    if (!contains(frame)) throw std::out_of_range("Segmented pair frame is unavailable");
    const TableGeneration& table = tables_[slot(frame).table];
    if (output.size() < table.pair_count) throw std::length_error("Segmented pair output is too small");
    const std::uint32_t* segments = table_segments(slot(frame).table);
    std::size_t count = 0U;
    for (std::size_t key = 0U; key < config_.bodies; ++key) {
        const std::uint32_t generation = segments[key];
        if (generation == no_generation) continue;
        const SegmentGeneration& item = segment_generations_[generation];
        std::copy(segment_storage(generation), segment_storage(generation) + item.count,
            output.begin() + static_cast<std::ptrdiff_t>(count));
        count += item.count;
    }
    if (table.spill_generation != no_generation) {
        const SpillGeneration& spill = spill_generations_[table.spill_generation];
        std::copy(spill_storage(table.spill_generation),
            spill_storage(table.spill_generation) + spill.count,
            output.begin() + static_cast<std::ptrdiff_t>(count));
        count += spill.count;
    }
    std::sort(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(count));
    return count;
}
std::size_t SegmentedDynamicPairHistory::restore_spill_pairs(
    std::uint64_t frame,
    std::span<BroadphasePair> output) const {
    if (!contains(frame)) throw std::out_of_range("Segmented pair frame is unavailable");
    const TableGeneration& table = tables_[slot(frame).table];
    if (table.spill_generation == no_generation) return 0U;
    const SpillGeneration& spill = spill_generations_[table.spill_generation];
    if (output.size() < spill.count) throw std::length_error("Segmented spill output is too small");
    std::copy(spill_storage(table.spill_generation),
        spill_storage(table.spill_generation) + spill.count, output.begin());
    return spill.count;
}
std::size_t SegmentedDynamicPairHistory::restore_body_island_keys(
    std::uint64_t frame,
    std::span<std::size_t> output) const {
    if (!contains(frame)) throw std::out_of_range("Segmented pair frame is unavailable");
    if (output.size() < config_.bodies) throw std::length_error("Segmented topology output is too small");
    const TableGeneration& table = tables_[slot(frame).table];
    std::copy(table_body_keys(slot(frame).table),
        table_body_keys(slot(frame).table) + config_.bodies, output.begin());
    return table.island_count;
}

void SegmentedDynamicPairHistory::truncate_after(std::uint64_t frame) {
    for (Snapshot& snapshot : snapshots_) {
        if (snapshot.frame != empty_frame && snapshot.frame > frame) release_snapshot(snapshot);
    }
}
void SegmentedDynamicPairHistory::clear() noexcept {
    for (Snapshot& snapshot : snapshots_) release_snapshot(snapshot);
    release_staging();
    std::fill(segment_generations_.begin(), segment_generations_.end(), SegmentGeneration{});
    std::fill(spill_generations_.begin(), spill_generations_.end(), SpillGeneration{});
    std::fill(tables_.begin(), tables_.end(), TableGeneration{});
    for (TableGeneration& table : tables_) table.spill_generation = no_generation;
    stats_ = {};
    initialized_ = false;
}

std::uint64_t SegmentedDynamicPairHistory::hash(std::uint64_t frame) const {
    if (!contains(frame)) throw std::out_of_range("Segmented pair frame is unavailable");
    const Snapshot& snapshot = slot(frame);
    const TableGeneration& table = tables_[snapshot.table];
    std::uint64_t value = 0xCBF29CE484222325ULL;
    mix_u64(value, frame);
    mix_u64(value, table.topology_signature);
    mix_u64(value, table.island_count);
    const std::uint32_t* segments = table_segments(snapshot.table);
    for (std::size_t key = 0U; key < config_.bodies; ++key) {
        const std::uint32_t generation = segments[key];
        if (generation == no_generation) continue;
        const SegmentGeneration& item = segment_generations_[generation];
        mix_u64(value, key);
        mix_u64(value, item.count);
        const BroadphasePair* payload = segment_storage(generation);
        for (std::size_t index = 0U; index < item.count; ++index) {
            mix_u64(value, payload[index].first);
            mix_u64(value, payload[index].second);
        }
    }
    if (table.spill_generation != no_generation) {
        const SpillGeneration& spill = spill_generations_[table.spill_generation];
        mix_u64(value, std::numeric_limits<std::uint64_t>::max());
        mix_u64(value, spill.count);
        const BroadphasePair* payload = spill_storage(table.spill_generation);
        for (std::size_t index = 0U; index < spill.count; ++index) {
            mix_u64(value, payload[index].first);
            mix_u64(value, payload[index].second);
        }
    }
    return value;
}

std::size_t SegmentedDynamicPairHistory::reserved_bytes() const noexcept {
    return segment_generations_.capacity() * sizeof(SegmentGeneration)
        + spill_generations_.capacity() * sizeof(SpillGeneration)
        + tables_.capacity() * sizeof(TableGeneration)
        + segment_payload_.capacity() * sizeof(BroadphasePair)
        + spill_payload_.capacity() * sizeof(BroadphasePair)
        + table_body_keys_.capacity() * sizeof(std::size_t)
        + table_segments_.capacity() * sizeof(std::uint32_t)
        + snapshots_.capacity() * sizeof(Snapshot)
        + (parent_.capacity() + current_body_keys_.capacity()) * sizeof(std::size_t)
        + dirty_keys_.capacity() * sizeof(std::uint8_t)
        + keyed_pairs_.capacity() * sizeof(KeyedPair)
        + spill_pairs_.capacity() * sizeof(BroadphasePair)
        + staged_segments_.capacity() * sizeof(std::uint32_t);
}

} // namespace neoeng::core
