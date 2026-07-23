#include "neoeng/core/paged_segmented_pair_history.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace neoeng::core {
namespace {
constexpr std::size_t missing = std::numeric_limits<std::size_t>::max();

BroadphasePair canonical_pair(BroadphasePair pair) {
    if (pair.second < pair.first) std::swap(pair.first, pair.second);
    if (pair.first == pair.second) throw std::invalid_argument("Paged segmented pair cannot be self-referential");
    return pair;
}

void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}
} // namespace

PagedSegmentedPairHistory::PagedSegmentedPairHistory(PagedSegmentedPairHistoryConfig config)
    : config_(config),
      page_count_((config.bodies + config.table_page_elements - 1U) / config.table_page_elements),
      segment_generation_count_(config.segment_generations == 0U
          ? config.maximum_pairs + config.history_capacity + 2U : config.segment_generations),
      spill_generation_count_(config.spill_generations == 0U
          ? config.history_capacity + 2U : config.spill_generations),
      table_generation_count_(config.table_generations == 0U
          ? config.history_capacity + 2U : config.table_generations),
      body_page_generation_count_(config.body_key_page_generations == 0U
          ? page_count_ + config.history_capacity + 2U : config.body_key_page_generations),
      segment_page_generation_count_(config.segment_map_page_generations == 0U
          ? page_count_ + config.history_capacity + 2U : config.segment_map_page_generations),
      segment_generations_(segment_generation_count_), spill_generations_(spill_generation_count_),
      body_page_generations_(body_page_generation_count_),
      segment_page_generations_(segment_page_generation_count_), tables_(table_generation_count_),
      segment_payload_(segment_generation_count_ * config.maximum_pairs_per_segment),
      spill_payload_(spill_generation_count_ * config.maximum_pairs),
      body_page_payload_(body_page_generation_count_ * config.table_page_elements, missing),
      segment_page_payload_(segment_page_generation_count_ * config.table_page_elements, no_generation),
      table_body_page_ids_(table_generation_count_ * page_count_, no_generation),
      table_segment_page_ids_(table_generation_count_ * page_count_, no_generation),
      snapshots_(config.history_capacity), parent_(config.bodies),
      current_body_keys_(config.bodies, missing), current_segments_(config.bodies, no_generation),
      dirty_keys_(config.bodies), keyed_pairs_(config.maximum_pairs), spill_pairs_(config.maximum_pairs),
      staged_segments_(config.maximum_pairs + config.history_capacity + 2U, no_generation),
      staged_body_pages_(page_count_, no_generation), staged_segment_pages_(page_count_, no_generation),
      restore_heap_(config.bodies + 1U),
      table_allocator_(table_generation_count_, config.allocation_mode),
      segment_allocator_(segment_generation_count_, config.allocation_mode),
      spill_allocator_(spill_generation_count_, config.allocation_mode),
      body_page_allocator_(body_page_generation_count_, config.allocation_mode),
      segment_page_allocator_(segment_page_generation_count_, config.allocation_mode) {
    if (config.bodies == 0U || config.history_capacity < 2U || config.table_page_elements == 0U
        || config.maximum_pairs == 0U || config.maximum_pairs_per_segment == 0U
        || page_count_ == 0U || segment_generation_count_ < 2U || spill_generation_count_ < 2U
        || table_generation_count_ < 2U || body_page_generation_count_ < page_count_ + 1U
        || segment_page_generation_count_ < page_count_ + 1U
        || config.maximum_contacts > config.bodies * (config.bodies - 1U) / 2U) {
        throw std::invalid_argument("Paged segmented pair history configuration is invalid");
    }
    for (TableGeneration& table : tables_) table.spill_generation = no_generation;
}

PagedSegmentedPairHistory::Snapshot& PagedSegmentedPairHistory::slot(std::uint64_t frame) noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}
const PagedSegmentedPairHistory::Snapshot& PagedSegmentedPairHistory::slot(std::uint64_t frame) const noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}

std::size_t PagedSegmentedPairHistory::find_root(std::size_t body) noexcept {
    while (parent_[body] != body) {
        parent_[body] = parent_[parent_[body]];
        body = parent_[body];
    }
    return body;
}
void PagedSegmentedPairHistory::unite(std::size_t first, std::size_t second) noexcept {
    first = find_root(first); second = find_root(second);
    if (first == second) return;
    if (second < first) std::swap(first, second);
    parent_[second] = first;
}

std::uint32_t* PagedSegmentedPairHistory::table_body_pages(std::uint32_t table) noexcept {
    return table_body_page_ids_.data() + static_cast<std::size_t>(table) * page_count_;
}
const std::uint32_t* PagedSegmentedPairHistory::table_body_pages(std::uint32_t table) const noexcept {
    return table_body_page_ids_.data() + static_cast<std::size_t>(table) * page_count_;
}
std::uint32_t* PagedSegmentedPairHistory::table_segment_pages(std::uint32_t table) noexcept {
    return table_segment_page_ids_.data() + static_cast<std::size_t>(table) * page_count_;
}
const std::uint32_t* PagedSegmentedPairHistory::table_segment_pages(std::uint32_t table) const noexcept {
    return table_segment_page_ids_.data() + static_cast<std::size_t>(table) * page_count_;
}
std::size_t* PagedSegmentedPairHistory::body_page_storage(std::uint32_t generation) noexcept {
    return body_page_payload_.data() + static_cast<std::size_t>(generation) * config_.table_page_elements;
}
const std::size_t* PagedSegmentedPairHistory::body_page_storage(std::uint32_t generation) const noexcept {
    return body_page_payload_.data() + static_cast<std::size_t>(generation) * config_.table_page_elements;
}
std::uint32_t* PagedSegmentedPairHistory::segment_page_storage(std::uint32_t generation) noexcept {
    return segment_page_payload_.data() + static_cast<std::size_t>(generation) * config_.table_page_elements;
}
const std::uint32_t* PagedSegmentedPairHistory::segment_page_storage(std::uint32_t generation) const noexcept {
    return segment_page_payload_.data() + static_cast<std::size_t>(generation) * config_.table_page_elements;
}
BroadphasePair* PagedSegmentedPairHistory::segment_storage(std::uint32_t generation) noexcept {
    return segment_payload_.data() + static_cast<std::size_t>(generation) * config_.maximum_pairs_per_segment;
}
const BroadphasePair* PagedSegmentedPairHistory::segment_storage(std::uint32_t generation) const noexcept {
    return segment_payload_.data() + static_cast<std::size_t>(generation) * config_.maximum_pairs_per_segment;
}
BroadphasePair* PagedSegmentedPairHistory::spill_storage(std::uint32_t generation) noexcept {
    return spill_payload_.data() + static_cast<std::size_t>(generation) * config_.maximum_pairs;
}
const BroadphasePair* PagedSegmentedPairHistory::spill_storage(std::uint32_t generation) const noexcept {
    return spill_payload_.data() + static_cast<std::size_t>(generation) * config_.maximum_pairs;
}

std::uint32_t PagedSegmentedPairHistory::acquire_table() {
    try {
        const std::uint32_t index = table_allocator_.acquire();
        if (tables_[index].refs != 0U) throw std::logic_error("Paged segmented table allocator diverged from refs");
        tables_[index] = {};
        tables_[index].refs = staging_refcount;
        tables_[index].spill_generation = no_generation;
        std::fill(table_body_pages(index), table_body_pages(index) + page_count_, no_generation);
        std::fill(table_segment_pages(index), table_segment_pages(index) + page_count_, no_generation);
        return index;
    } catch (const std::length_error&) {
        ++stats_.capacity_failures;
        throw std::length_error("No free paged segmented table is available");
    }
}
std::uint32_t PagedSegmentedPairHistory::acquire_segment() {
    try {
        const std::uint32_t index = segment_allocator_.acquire();
        if (segment_generations_[index].refs != 0U) throw std::logic_error("Paged segmented segment allocator diverged from refs");
        segment_generations_[index] = {.refs = staging_refcount};
        return index;
    } catch (const std::length_error&) {
        ++stats_.capacity_failures;
        throw std::length_error("No free paged segmented pair generation is available");
    }
}
std::uint32_t PagedSegmentedPairHistory::acquire_spill() {
    try {
        const std::uint32_t index = spill_allocator_.acquire();
        if (spill_generations_[index].refs != 0U) throw std::logic_error("Paged segmented spill allocator diverged from refs");
        spill_generations_[index] = {.refs = staging_refcount};
        return index;
    } catch (const std::length_error&) {
        ++stats_.capacity_failures;
        throw std::length_error("No free paged segmented spill generation is available");
    }
}
std::uint32_t PagedSegmentedPairHistory::acquire_body_page() {
    try {
        const std::uint32_t index = body_page_allocator_.acquire();
        if (body_page_generations_[index].refs != 0U) throw std::logic_error("Paged segmented body-page allocator diverged from refs");
        body_page_generations_[index].refs = staging_refcount;
        std::fill(body_page_storage(index), body_page_storage(index) + config_.table_page_elements, missing);
        return index;
    } catch (const std::length_error&) {
        ++stats_.capacity_failures;
        throw std::length_error("No free body-key page generation is available");
    }
}
std::uint32_t PagedSegmentedPairHistory::acquire_segment_page() {
    try {
        const std::uint32_t index = segment_page_allocator_.acquire();
        if (segment_page_generations_[index].refs != 0U) throw std::logic_error("Paged segmented segment-page allocator diverged from refs");
        segment_page_generations_[index].refs = staging_refcount;
        std::fill(segment_page_storage(index), segment_page_storage(index) + config_.table_page_elements,
            no_generation);
        return index;
    } catch (const std::length_error&) {
        ++stats_.capacity_failures;
        throw std::length_error("No free segment-map page generation is available");
    }
}

void PagedSegmentedPairHistory::release_body_page(std::uint32_t page) noexcept {
    if (page == no_generation) return;
    BodyPageGeneration& item = body_page_generations_[page];
    if (item.refs == 0U || item.refs == staging_refcount) return;
    if (--item.refs == 0U) {
        item = {};
        body_page_allocator_.release(page);
    }
}
void PagedSegmentedPairHistory::release_segment_page(std::uint32_t page) noexcept {
    if (page == no_generation) return;
    SegmentPageGeneration& item = segment_page_generations_[page];
    if (item.refs == 0U || item.refs == staging_refcount) return;
    if (--item.refs != 0U) return;
    const std::uint32_t* payload = segment_page_storage(page);
    for (std::size_t offset = 0U; offset < config_.table_page_elements; ++offset) {
        const std::uint32_t generation = payload[offset];
        if (generation == no_generation) continue;
        SegmentGeneration& segment = segment_generations_[generation];
        if (segment.refs != 0U && segment.refs != staging_refcount && --segment.refs == 0U) {
            segment = {};
            segment_allocator_.release(generation);
        }
    }
    item = {};
    segment_page_allocator_.release(page);
}
void PagedSegmentedPairHistory::release_table(std::uint32_t table) noexcept {
    if (table == no_generation) return;
    TableGeneration& item = tables_[table];
    if (item.refs == 0U || item.refs == staging_refcount) return;
    if (--item.refs != 0U) return;
    const std::uint32_t* body_pages = table_body_pages(table);
    const std::uint32_t* segment_pages = table_segment_pages(table);
    for (std::size_t page = 0U; page < page_count_; ++page) {
        release_body_page(body_pages[page]);
        release_segment_page(segment_pages[page]);
    }
    if (item.spill_generation != no_generation) {
        SpillGeneration& spill = spill_generations_[item.spill_generation];
        if (spill.refs != 0U && spill.refs != staging_refcount && --spill.refs == 0U) {
            spill = {};
            spill_allocator_.release(item.spill_generation);
        }
    }
    item = {};
    item.spill_generation = no_generation;
    table_allocator_.release(table);
}
void PagedSegmentedPairHistory::release_snapshot(Snapshot& snapshot) noexcept {
    if (snapshot.frame == empty_frame) return;
    release_table(snapshot.table);
    snapshot = {};
}
void PagedSegmentedPairHistory::release_staging() noexcept {
    for (std::size_t index = 0U; index < staged_segment_count_; ++index) {
        const std::uint32_t generation = staged_segments_[index];
        if (generation != no_generation && segment_generations_[generation].refs == staging_refcount) {
            segment_generations_[generation] = {};
            segment_allocator_.release(generation);
        }
        staged_segments_[index] = no_generation;
    }
    staged_segment_count_ = 0U;
    for (std::size_t index = 0U; index < staged_body_page_count_; ++index) {
        const std::uint32_t generation = staged_body_pages_[index];
        if (generation != no_generation && body_page_generations_[generation].refs == staging_refcount) {
            body_page_generations_[generation] = {};
            body_page_allocator_.release(generation);
        }
        staged_body_pages_[index] = no_generation;
    }
    staged_body_page_count_ = 0U;
    for (std::size_t index = 0U; index < staged_segment_page_count_; ++index) {
        const std::uint32_t generation = staged_segment_pages_[index];
        if (generation != no_generation && segment_page_generations_[generation].refs == staging_refcount) {
            segment_page_generations_[generation] = {};
            segment_page_allocator_.release(generation);
        }
        staged_segment_pages_[index] = no_generation;
    }
    staged_segment_page_count_ = 0U;
    if (staging_spill_ != no_generation) {
        if (spill_generations_[staging_spill_].refs == staging_refcount) {
            spill_generations_[staging_spill_] = {};
            spill_allocator_.release(staging_spill_);
        }
        staging_spill_ = no_generation;
    }
    if (staging_table_ != no_generation) {
        tables_[staging_table_] = {};
        tables_[staging_table_].spill_generation = no_generation;
        table_allocator_.release(staging_table_);
        staging_table_ = no_generation;
    }
}

std::uint64_t PagedSegmentedPairHistory::topology_signature(
    std::span<const NormalContact> contacts) const noexcept {
    std::uint64_t sum = 0x9E3779B97F4A7C15ULL + contacts.size();
    std::uint64_t xor_value = 0xD6E8FEB86659FD93ULL ^ config_.bodies;
    for (const NormalContact& contact : contacts) {
        const std::uint64_t first = std::min(contact.first, contact.second);
        const std::uint64_t second = std::max(contact.first, contact.second);
        std::uint64_t item = first * 0x9E3779B185EBCA87ULL ^ second * 0xC2B2AE3D27D4EB4FULL;
        item ^= item >> 30U; item *= 0xBF58476D1CE4E5B9ULL;
        item ^= item >> 27U; item *= 0x94D049BB133111EBULL;
        item ^= item >> 31U;
        sum += item;
        xor_value ^= item + 0x9E3779B97F4A7C15ULL + (item << 6U) + (item >> 2U);
    }
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    mix_u64(hash, config_.bodies); mix_u64(hash, contacts.size()); mix_u64(hash, sum); mix_u64(hash, xor_value);
    return hash;
}
void PagedSegmentedPairHistory::build_topology(std::span<const NormalContact> contacts) {
    if (contacts.size() > config_.maximum_contacts) {
        ++stats_.capacity_failures;
        throw std::length_error("Paged segmented contact capacity exceeded");
    }
    std::iota(parent_.begin(), parent_.end(), 0U);
    for (const NormalContact& contact : contacts) {
        if (contact.first >= config_.bodies || contact.second >= config_.bodies
            || contact.first == contact.second || (contact.normal.x == 0 && contact.normal.y == 0)) {
            throw std::invalid_argument("Invalid contact in paged segmented topology");
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
void PagedSegmentedPairHistory::partition_pairs(std::span<const BroadphasePair> pairs) {
    if (pairs.size() > config_.maximum_pairs) {
        ++stats_.capacity_failures;
        throw std::length_error("Paged segmented pair capacity exceeded");
    }
    keyed_pair_count_ = 0U; spill_pair_count_ = 0U;
    for (const BroadphasePair raw : pairs) {
        const BroadphasePair pair = canonical_pair(raw);
        if (pair.second >= config_.bodies) throw std::out_of_range("Paged segmented pair body is outside capacity");
        const std::size_t first_key = current_body_keys_[pair.first];
        const std::size_t second_key = current_body_keys_[pair.second];
        if (first_key == second_key) keyed_pairs_[keyed_pair_count_++] = {.key = first_key, .pair = pair};
        else spill_pairs_[spill_pair_count_++] = pair;
    }
    std::sort(keyed_pairs_.begin(), keyed_pairs_.begin() + static_cast<std::ptrdiff_t>(keyed_pair_count_));
    std::sort(spill_pairs_.begin(), spill_pairs_.begin() + static_cast<std::ptrdiff_t>(spill_pair_count_));
    if (std::adjacent_find(spill_pairs_.begin(), spill_pairs_.begin() + static_cast<std::ptrdiff_t>(spill_pair_count_))
        != spill_pairs_.begin() + static_cast<std::ptrdiff_t>(spill_pair_count_)) {
        throw std::invalid_argument("Paged segmented spill contains duplicate pairs");
    }
    for (std::size_t index = 1U; index < keyed_pair_count_; ++index) {
        if (keyed_pairs_[index - 1U].key == keyed_pairs_[index].key
            && !(keyed_pairs_[index - 1U].pair < keyed_pairs_[index].pair)) {
            throw std::invalid_argument("Paged segmented island contains duplicate pairs");
        }
    }
}
bool PagedSegmentedPairHistory::same_segment(
    std::uint32_t generation, std::span<const KeyedPair> run) const noexcept {
    if (generation == no_generation) return run.empty();
    const SegmentGeneration& item = segment_generations_[generation];
    if (item.count != run.size()) return false;
    const BroadphasePair* payload = segment_storage(generation);
    for (std::size_t index = 0U; index < run.size(); ++index) if (!(payload[index] == run[index].pair)) return false;
    return true;
}
bool PagedSegmentedPairHistory::same_spill(
    std::uint32_t generation, std::span<const BroadphasePair> pairs) const noexcept {
    if (generation == no_generation) return pairs.empty();
    const SpillGeneration& item = spill_generations_[generation];
    return item.count == pairs.size() && std::equal(pairs.begin(), pairs.end(), spill_storage(generation));
}

bool PagedSegmentedPairHistory::body_page_equals(std::uint32_t generation, std::size_t page) const noexcept {
    if (generation == no_generation) return false;
    const std::size_t begin = page * config_.table_page_elements;
    const std::size_t count = std::min(config_.table_page_elements, config_.bodies - begin);
    return std::equal(current_body_keys_.begin() + static_cast<std::ptrdiff_t>(begin),
        current_body_keys_.begin() + static_cast<std::ptrdiff_t>(begin + count), body_page_storage(generation));
}
bool PagedSegmentedPairHistory::segment_page_equals(std::uint32_t generation, std::size_t page) const noexcept {
    if (generation == no_generation) return false;
    const std::size_t begin = page * config_.table_page_elements;
    const std::size_t count = std::min(config_.table_page_elements, config_.bodies - begin);
    return std::equal(current_segments_.begin() + static_cast<std::ptrdiff_t>(begin),
        current_segments_.begin() + static_cast<std::ptrdiff_t>(begin + count), segment_page_storage(generation));
}
std::uint32_t PagedSegmentedPairHistory::body_page_at(
    const TableGeneration&, std::uint32_t table_index, std::size_t page) const noexcept {
    return table_body_pages(table_index)[page];
}
std::uint32_t PagedSegmentedPairHistory::segment_page_at(
    const TableGeneration&, std::uint32_t table_index, std::size_t page) const noexcept {
    return table_segment_pages(table_index)[page];
}

void PagedSegmentedPairHistory::initialize(
    std::uint64_t frame, std::span<const NormalContact> contacts, std::span<const BroadphasePair> pairs) {
    clear();
    capture(frame, contacts, pairs, {}, true, true, false);
    initialized_ = true;
}

void PagedSegmentedPairHistory::capture(
    std::uint64_t frame,
    std::span<const NormalContact> contacts,
    std::span<const BroadphasePair> pairs,
    std::span<const std::size_t> dirty_bodies,
    bool topology_changed,
    bool topology_hint_complete,
    bool pair_hint_complete) {
    if (initialized_ && frame == 0U) throw std::invalid_argument("Paged segmented frame must advance");
    const Snapshot* previous_snapshot = nullptr;
    const TableGeneration* previous_table = nullptr;
    if (frame > 0U && contains(frame - 1U)) {
        previous_snapshot = &slot(frame - 1U);
        previous_table = &tables_[previous_snapshot->table];
    }

    // A complete unchanged hint is a producer-side proof. Share the immutable
    // table before rebuilding union-find or hashing all contacts.
    if (previous_table != nullptr && topology_hint_complete && !topology_changed
        && pair_hint_complete && dirty_bodies.empty()) {
        Snapshot& destination = slot(frame);
        if (destination.frame != empty_frame) release_snapshot(destination);
        ++tables_[previous_snapshot->table].refs;
        destination = {.frame = frame, .table = previous_snapshot->table};
        ++stats_.snapshots_captured; ++stats_.tables_shared;
        stats_.live_pairs = previous_table->pair_count;
        stats_.live_islands = previous_table->island_count;
        stats_.live_spill_pairs = previous_table->spill_generation == no_generation ? 0U
            : spill_generations_[previous_table->spill_generation].count;
        initialized_ = true;
        return;
    }

    build_topology(contacts);
    bool topology_equal = previous_table != nullptr
        && previous_table->topology_signature == current_topology_signature_;
    if (topology_equal) {
        for (std::size_t page = 0U; page < page_count_ && topology_equal; ++page) {
            topology_equal = body_page_equals(body_page_at(*previous_table, previous_snapshot->table, page), page);
        }
    }
    if (topology_hint_complete && topology_changed == topology_equal) {
        throw std::invalid_argument("Complete paged topology hint contradicts computed topology");
    }
    if (!topology_equal) ++stats_.topology_changes;

    if (previous_table != nullptr && topology_equal && topology_hint_complete && !topology_changed
        && pair_hint_complete && dirty_bodies.empty()) {
        Snapshot& destination = slot(frame);
        if (destination.frame != empty_frame) release_snapshot(destination);
        ++tables_[previous_snapshot->table].refs;
        destination = {.frame = frame, .table = previous_snapshot->table};
        ++stats_.snapshots_captured; ++stats_.tables_shared;
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
        if (body >= config_.bodies) throw std::out_of_range("Dirty body is outside paged segmented history");
        dirty_keys_[current_body_keys_[body]] = 1U;
    }
    if (!topology_equal) {
        for (std::size_t body = 0U; body < config_.bodies; ++body) {
            if (current_body_keys_[body] == body) dirty_keys_[body] = 1U;
        }
    }
    std::fill(current_segments_.begin(), current_segments_.end(), no_generation);

    try {
        // Resolve one immutable pair payload per island first.
        std::size_t run_begin = 0U;
        while (run_begin < keyed_pair_count_) {
            const std::size_t key = keyed_pairs_[run_begin].key;
            std::size_t run_end = run_begin + 1U;
            while (run_end < keyed_pair_count_ && keyed_pairs_[run_end].key == key) ++run_end;
            const std::span<const KeyedPair> run(keyed_pairs_.data() + run_begin, run_end - run_begin);
            if (run.size() > config_.maximum_pairs_per_segment) {
                ++stats_.capacity_failures;
                throw std::length_error("Paged island pair segment capacity exceeded");
            }
            std::uint32_t previous_generation = no_generation;
            if (previous_table != nullptr) {
                const std::size_t page = key / config_.table_page_elements;
                const std::size_t offset = key % config_.table_page_elements;
                const std::uint32_t map_page = table_segment_pages(previous_snapshot->table)[page];
                previous_generation = segment_page_storage(map_page)[offset];
            }
            bool reuse = previous_generation != no_generation;
            if (reuse && (!pair_hint_complete || dirty_keys_[key] != 0U)) reuse = same_segment(previous_generation, run);
            if (reuse) {
                current_segments_[key] = previous_generation;
                ++stats_.segments_reused;
            } else {
                const std::uint32_t generation = acquire_segment();
                if (staged_segment_count_ >= staged_segments_.size()) {
                    ++stats_.capacity_failures;
                    throw std::length_error("Paged segmented staging segment capacity exceeded");
                }
                staged_segments_[staged_segment_count_++] = generation;
                SegmentGeneration& segment = segment_generations_[generation];
                segment.key = static_cast<std::uint32_t>(key);
                segment.count = static_cast<std::uint32_t>(run.size());
                BroadphasePair* payload = segment_storage(generation);
                for (std::size_t index = 0U; index < run.size(); ++index) payload[index] = run[index].pair;
                current_segments_[key] = generation;
                ++stats_.segments_written;
            }
            run_begin = run_end;
        }

        staging_table_ = acquire_table();
        TableGeneration& table = tables_[staging_table_];
        table.island_count = static_cast<std::uint32_t>(current_island_count_);
        table.topology_signature = current_topology_signature_;
        table.pair_count = pairs.size();
        std::uint32_t* output_body_pages = table_body_pages(staging_table_);
        std::uint32_t* output_segment_pages = table_segment_pages(staging_table_);

        for (std::size_t page = 0U; page < page_count_; ++page) {
            const std::size_t begin = page * config_.table_page_elements;
            const std::size_t count = std::min(config_.table_page_elements, config_.bodies - begin);
            const std::uint32_t previous_body = previous_table != nullptr
                ? table_body_pages(previous_snapshot->table)[page] : no_generation;
            if (previous_body != no_generation && body_page_equals(previous_body, page)) {
                output_body_pages[page] = previous_body;
                ++stats_.body_pages_shared;
            } else {
                const std::uint32_t generation = acquire_body_page();
                staged_body_pages_[staged_body_page_count_++] = generation;
                std::copy(current_body_keys_.begin() + static_cast<std::ptrdiff_t>(begin),
                    current_body_keys_.begin() + static_cast<std::ptrdiff_t>(begin + count),
                    body_page_storage(generation));
                output_body_pages[page] = generation;
                ++stats_.body_pages_written;
            }

            const std::uint32_t previous_segment = previous_table != nullptr
                ? table_segment_pages(previous_snapshot->table)[page] : no_generation;
            if (previous_segment != no_generation && segment_page_equals(previous_segment, page)) {
                output_segment_pages[page] = previous_segment;
                ++stats_.segment_pages_shared;
            } else {
                const std::uint32_t generation = acquire_segment_page();
                staged_segment_pages_[staged_segment_page_count_++] = generation;
                std::copy(current_segments_.begin() + static_cast<std::ptrdiff_t>(begin),
                    current_segments_.begin() + static_cast<std::ptrdiff_t>(begin + count),
                    segment_page_storage(generation));
                output_segment_pages[page] = generation;
                ++stats_.segment_pages_written;
            }
        }

        const std::span<const BroadphasePair> spill(spill_pairs_.data(), spill_pair_count_);
        std::uint32_t previous_spill = no_generation;
        if (previous_table != nullptr) previous_spill = previous_table->spill_generation;
        if (same_spill(previous_spill, spill)) {
            table.spill_generation = previous_spill;
            ++stats_.spill_reused;
        } else if (!spill.empty()) {
            staging_spill_ = acquire_spill();
            spill_generations_[staging_spill_].count = static_cast<std::uint32_t>(spill.size());
            std::copy(spill.begin(), spill.end(), spill_storage(staging_spill_));
            table.spill_generation = staging_spill_;
            ++stats_.spill_written;
        }

        // Commit page references. Segment payload references are owned by segment-map pages.
        for (std::size_t page = 0U; page < page_count_; ++page) {
            BodyPageGeneration& body_item = body_page_generations_[output_body_pages[page]];
            if (body_item.refs == staging_refcount) body_item.refs = 1U; else ++body_item.refs;

            SegmentPageGeneration& segment_page = segment_page_generations_[output_segment_pages[page]];
            if (segment_page.refs == staging_refcount) {
                segment_page.refs = 1U;
                const std::uint32_t* payload = segment_page_storage(output_segment_pages[page]);
                for (std::size_t offset = 0U; offset < config_.table_page_elements; ++offset) {
                    const std::uint32_t generation = payload[offset];
                    if (generation == no_generation) continue;
                    SegmentGeneration& segment = segment_generations_[generation];
                    if (segment.refs == staging_refcount) segment.refs = 1U; else ++segment.refs;
                }
            } else {
                ++segment_page.refs;
            }
        }
        if (table.spill_generation != no_generation) {
            SpillGeneration& spill_item = spill_generations_[table.spill_generation];
            if (spill_item.refs == staging_refcount) spill_item.refs = 1U; else ++spill_item.refs;
        }
        table.refs = 1U;

        Snapshot& destination = slot(frame);
        if (destination.frame != empty_frame) release_snapshot(destination);
        destination = {.frame = frame, .table = staging_table_};
        staging_table_ = no_generation; staging_spill_ = no_generation;
        std::fill(staged_segments_.begin(), staged_segments_.begin() + static_cast<std::ptrdiff_t>(staged_segment_count_), no_generation);
        std::fill(staged_body_pages_.begin(), staged_body_pages_.begin() + static_cast<std::ptrdiff_t>(staged_body_page_count_), no_generation);
        std::fill(staged_segment_pages_.begin(), staged_segment_pages_.begin() + static_cast<std::ptrdiff_t>(staged_segment_page_count_), no_generation);
        staged_segment_count_ = staged_body_page_count_ = staged_segment_page_count_ = 0U;
        ++stats_.snapshots_captured; ++stats_.tables_written;
        stats_.live_pairs = pairs.size(); stats_.live_islands = current_island_count_; stats_.live_spill_pairs = spill_pair_count_;
        initialized_ = true;
    } catch (...) {
        release_staging();
        throw;
    }
}

bool PagedSegmentedPairHistory::contains(std::uint64_t frame) const noexcept {
    return !snapshots_.empty() && slot(frame).frame == frame;
}
std::size_t PagedSegmentedPairHistory::pair_count(std::uint64_t frame) const {
    if (!contains(frame)) throw std::out_of_range("Paged segmented pair frame is unavailable");
    return tables_[slot(frame).table].pair_count;
}
std::size_t PagedSegmentedPairHistory::island_count(std::uint64_t frame) const {
    if (!contains(frame)) throw std::out_of_range("Paged segmented pair frame is unavailable");
    return tables_[slot(frame).table].island_count;
}
std::size_t PagedSegmentedPairHistory::spill_pair_count(std::uint64_t frame) const {
    if (!contains(frame)) throw std::out_of_range("Paged segmented pair frame is unavailable");
    const TableGeneration& table = tables_[slot(frame).table];
    return table.spill_generation == no_generation ? 0U : spill_generations_[table.spill_generation].count;
}

std::size_t PagedSegmentedPairHistory::restore_pairs(
    std::uint64_t frame, std::span<BroadphasePair> output) const {
    if (!contains(frame)) throw std::out_of_range("Paged segmented pair frame is unavailable");
    const Snapshot& snapshot = slot(frame);
    const TableGeneration& table = tables_[snapshot.table];
    if (output.size() < table.pair_count) throw std::length_error("Paged segmented restore buffer is too small");

    const std::uint32_t* map_pages = table_segment_pages(snapshot.table);
    bool globally_ordered = true;
    bool have_previous = false;
    BroadphasePair previous_last{};
    std::size_t run_count = 0U;
    for (std::size_t page = 0U; page < page_count_; ++page) {
        const std::uint32_t* map = segment_page_storage(map_pages[page]);
        const std::size_t begin = page * config_.table_page_elements;
        const std::size_t entries = std::min(config_.table_page_elements, config_.bodies - begin);
        for (std::size_t offset = 0U; offset < entries; ++offset) {
            const std::uint32_t generation = map[offset];
            if (generation == no_generation) continue;
            const SegmentGeneration& segment = segment_generations_[generation];
            if (segment.count == 0U) continue;
            const BroadphasePair* payload = segment_storage(generation);
            if (have_previous && payload[0] < previous_last) globally_ordered = false;
            previous_last = payload[segment.count - 1U];
            have_previous = true;
            ++run_count;
        }
    }
    if (table.spill_generation != no_generation) {
        const SpillGeneration& spill = spill_generations_[table.spill_generation];
        if (spill.count != 0U) {
            const BroadphasePair* payload = spill_storage(table.spill_generation);
            if (have_previous && payload[0] < previous_last) globally_ordered = false;
            ++run_count;
        }
    }

    if (globally_ordered) {
        std::size_t count = 0U;
        for (std::size_t page = 0U; page < page_count_; ++page) {
            const std::uint32_t* map = segment_page_storage(map_pages[page]);
            const std::size_t begin = page * config_.table_page_elements;
            const std::size_t entries = std::min(config_.table_page_elements, config_.bodies - begin);
            for (std::size_t offset = 0U; offset < entries; ++offset) {
                const std::uint32_t generation = map[offset];
                if (generation == no_generation) continue;
                const SegmentGeneration& segment = segment_generations_[generation];
                std::copy(segment_storage(generation), segment_storage(generation) + segment.count,
                    output.begin() + static_cast<std::ptrdiff_t>(count));
                count += segment.count;
            }
        }
        if (table.spill_generation != no_generation) {
            const SpillGeneration& spill = spill_generations_[table.spill_generation];
            std::copy(spill_storage(table.spill_generation), spill_storage(table.spill_generation) + spill.count,
                output.begin() + static_cast<std::ptrdiff_t>(count));
            count += spill.count;
        }
        ++stats_.direct_ordered_restores;
        return count;
    }

    if (run_count > restore_heap_.size()) throw std::length_error("Paged segmented merge heap is too small");
    std::size_t heap_size = 0U;
    const auto greater_cursor = [](const MergeCursor& first, const MergeCursor& second) noexcept {
        return second.value() < first.value();
    };
    const auto push_run = [&](const BroadphasePair* data, std::uint32_t count) {
        if (count == 0U) return;
        restore_heap_[heap_size++] = {.data = data, .count = count, .index = 0U};
        std::push_heap(restore_heap_.begin(), restore_heap_.begin() + static_cast<std::ptrdiff_t>(heap_size), greater_cursor);
        ++stats_.merge_heap_pushes;
    };
    for (std::size_t page = 0U; page < page_count_; ++page) {
        const std::uint32_t* map = segment_page_storage(map_pages[page]);
        const std::size_t begin = page * config_.table_page_elements;
        const std::size_t entries = std::min(config_.table_page_elements, config_.bodies - begin);
        for (std::size_t offset = 0U; offset < entries; ++offset) {
            const std::uint32_t generation = map[offset];
            if (generation == no_generation) continue;
            const SegmentGeneration& segment = segment_generations_[generation];
            push_run(segment_storage(generation), segment.count);
        }
    }
    if (table.spill_generation != no_generation) {
        const SpillGeneration& spill = spill_generations_[table.spill_generation];
        push_run(spill_storage(table.spill_generation), spill.count);
    }

    std::size_t count = 0U;
    while (heap_size != 0U) {
        std::pop_heap(restore_heap_.begin(), restore_heap_.begin() + static_cast<std::ptrdiff_t>(heap_size), greater_cursor);
        MergeCursor cursor = restore_heap_[--heap_size];
        output[count++] = cursor.value();
        ++cursor.index;
        if (cursor.index < cursor.count) {
            restore_heap_[heap_size++] = cursor;
            std::push_heap(restore_heap_.begin(), restore_heap_.begin() + static_cast<std::ptrdiff_t>(heap_size), greater_cursor);
        }
    }
    ++stats_.merged_restores;
    return count;
}
std::size_t PagedSegmentedPairHistory::restore_spill_pairs(
    std::uint64_t frame, std::span<BroadphasePair> output) const {
    if (!contains(frame)) throw std::out_of_range("Paged segmented spill frame is unavailable");
    const TableGeneration& table = tables_[slot(frame).table];
    if (table.spill_generation == no_generation) return 0U;
    const SpillGeneration& spill = spill_generations_[table.spill_generation];
    if (output.size() < spill.count) throw std::length_error("Paged segmented spill output is too small");
    std::copy(spill_storage(table.spill_generation), spill_storage(table.spill_generation) + spill.count, output.begin());
    return spill.count;
}
std::size_t PagedSegmentedPairHistory::restore_body_island_keys(
    std::uint64_t frame, std::span<std::size_t> output) const {
    if (!contains(frame)) throw std::out_of_range("Paged segmented topology frame is unavailable");
    if (output.size() < config_.bodies) throw std::length_error("Paged segmented topology output is too small");
    const Snapshot& snapshot = slot(frame);
    const TableGeneration& table = tables_[snapshot.table];
    const std::uint32_t* pages = table_body_pages(snapshot.table);
    for (std::size_t page = 0U; page < page_count_; ++page) {
        const std::size_t begin = page * config_.table_page_elements;
        const std::size_t count = std::min(config_.table_page_elements, config_.bodies - begin);
        std::copy(body_page_storage(pages[page]), body_page_storage(pages[page]) + count,
            output.begin() + static_cast<std::ptrdiff_t>(begin));
    }
    return table.island_count;
}

void PagedSegmentedPairHistory::truncate_after(std::uint64_t frame) {
    for (Snapshot& snapshot : snapshots_) if (snapshot.frame != empty_frame && snapshot.frame > frame) release_snapshot(snapshot);
}
void PagedSegmentedPairHistory::clear() noexcept {
    for (Snapshot& snapshot : snapshots_) release_snapshot(snapshot);
    release_staging();
    std::fill(segment_generations_.begin(), segment_generations_.end(), SegmentGeneration{});
    std::fill(spill_generations_.begin(), spill_generations_.end(), SpillGeneration{});
    std::fill(body_page_generations_.begin(), body_page_generations_.end(), BodyPageGeneration{});
    std::fill(segment_page_generations_.begin(), segment_page_generations_.end(), SegmentPageGeneration{});
    std::fill(tables_.begin(), tables_.end(), TableGeneration{});
    for (TableGeneration& table : tables_) table.spill_generation = no_generation;
    for (Snapshot& snapshot : snapshots_) snapshot = {};
    stats_ = {};
    table_allocator_.reset(); segment_allocator_.reset(); spill_allocator_.reset();
    body_page_allocator_.reset(); segment_page_allocator_.reset();
    initialized_ = false;
}

std::uint64_t PagedSegmentedPairHistory::hash(std::uint64_t frame) const {
    if (!contains(frame)) throw std::out_of_range("Paged segmented pair frame is unavailable");
    const Snapshot& snapshot = slot(frame);
    const TableGeneration& table = tables_[snapshot.table];
    std::uint64_t value = 0xCBF29CE484222325ULL;
    mix_u64(value, frame); mix_u64(value, table.topology_signature); mix_u64(value, table.island_count);
    const std::uint32_t* pages = table_segment_pages(snapshot.table);
    for (std::size_t page = 0U; page < page_count_; ++page) {
        const std::uint32_t* map = segment_page_storage(pages[page]);
        const std::size_t begin = page * config_.table_page_elements;
        const std::size_t count = std::min(config_.table_page_elements, config_.bodies - begin);
        for (std::size_t offset = 0U; offset < count; ++offset) {
            const std::uint32_t generation = map[offset];
            if (generation == no_generation) continue;
            const std::size_t key = begin + offset;
            const SegmentGeneration& segment = segment_generations_[generation];
            mix_u64(value, key); mix_u64(value, segment.count);
            const BroadphasePair* payload = segment_storage(generation);
            for (std::size_t index = 0U; index < segment.count; ++index) {
                mix_u64(value, payload[index].first); mix_u64(value, payload[index].second);
            }
        }
    }
    if (table.spill_generation != no_generation) {
        const SpillGeneration& spill = spill_generations_[table.spill_generation];
        mix_u64(value, std::numeric_limits<std::uint64_t>::max()); mix_u64(value, spill.count);
        const BroadphasePair* payload = spill_storage(table.spill_generation);
        for (std::size_t index = 0U; index < spill.count; ++index) {
            mix_u64(value, payload[index].first); mix_u64(value, payload[index].second);
        }
    }
    return value;
}

std::size_t PagedSegmentedPairHistory::reserved_bytes() const noexcept {
    return segment_generations_.capacity() * sizeof(SegmentGeneration)
        + spill_generations_.capacity() * sizeof(SpillGeneration)
        + body_page_generations_.capacity() * sizeof(BodyPageGeneration)
        + segment_page_generations_.capacity() * sizeof(SegmentPageGeneration)
        + tables_.capacity() * sizeof(TableGeneration)
        + segment_payload_.capacity() * sizeof(BroadphasePair)
        + spill_payload_.capacity() * sizeof(BroadphasePair)
        + body_page_payload_.capacity() * sizeof(std::size_t)
        + segment_page_payload_.capacity() * sizeof(std::uint32_t)
        + table_body_page_ids_.capacity() * sizeof(std::uint32_t)
        + table_segment_page_ids_.capacity() * sizeof(std::uint32_t)
        + snapshots_.capacity() * sizeof(Snapshot)
        + (parent_.capacity() + current_body_keys_.capacity()) * sizeof(std::size_t)
        + current_segments_.capacity() * sizeof(std::uint32_t)
        + dirty_keys_.capacity() * sizeof(std::uint8_t)
        + keyed_pairs_.capacity() * sizeof(KeyedPair)
        + spill_pairs_.capacity() * sizeof(BroadphasePair)
        + staged_segments_.capacity() * sizeof(std::uint32_t)
        + staged_body_pages_.capacity() * sizeof(std::uint32_t)
        + staged_segment_pages_.capacity() * sizeof(std::uint32_t)
        + restore_heap_.capacity() * sizeof(MergeCursor)
        + table_allocator_.reserved_bytes() + segment_allocator_.reserved_bytes()
        + spill_allocator_.reserved_bytes() + body_page_allocator_.reserved_bytes()
        + segment_page_allocator_.reserved_bytes();
}

} // namespace neoeng::core
