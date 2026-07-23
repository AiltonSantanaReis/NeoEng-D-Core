#pragma once

#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/broadphase.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct SegmentedDynamicPairHistoryConfig final {
    std::size_t bodies{};
    std::size_t maximum_contacts{};
    std::size_t maximum_pairs{};
    std::size_t maximum_pairs_per_segment{};
    std::size_t history_capacity{32U};
    std::size_t segment_generations{};
    std::size_t spill_generations{};
    std::size_t table_generations{};
};

struct SegmentedDynamicPairHistoryStats final {
    std::uint64_t snapshots_captured{};
    std::uint64_t tables_shared{};
    std::uint64_t tables_written{};
    std::uint64_t segments_reused{};
    std::uint64_t segments_written{};
    std::uint64_t spill_reused{};
    std::uint64_t spill_written{};
    std::uint64_t topology_changes{};
    std::uint64_t capacity_failures{};
    std::size_t live_pairs{};
    std::size_t live_islands{};
    std::size_t live_spill_pairs{};
};

// Fixed-capacity dynamic topology history with copy-on-write pair segments.
//
// Each canonical contact island is keyed by its minimum body index. A snapshot owns
// one immutable table mapping island keys to immutable pair-segment generations.
// Unchanged segments are shared across tables; only changed islands acquire payload.
// Cross-island pairs are kept in a separately versioned spill generation.
class SegmentedDynamicPairHistory final {
public:
    explicit SegmentedDynamicPairHistory(SegmentedDynamicPairHistoryConfig config);

    void initialize(
        std::uint64_t frame,
        std::span<const NormalContact> contacts,
        std::span<const BroadphasePair> pairs);

    void capture(
        std::uint64_t frame,
        std::span<const NormalContact> contacts,
        std::span<const BroadphasePair> pairs,
        std::span<const std::size_t> dirty_bodies,
        bool topology_changed,
        bool topology_hint_complete = false,
        bool pair_hint_complete = false);

    [[nodiscard]] std::size_t restore_pairs(
        std::uint64_t frame,
        std::span<BroadphasePair> output) const;
    [[nodiscard]] std::size_t restore_spill_pairs(
        std::uint64_t frame,
        std::span<BroadphasePair> output) const;
    [[nodiscard]] std::size_t restore_body_island_keys(
        std::uint64_t frame,
        std::span<std::size_t> output) const;

    void truncate_after(std::uint64_t frame);
    void clear() noexcept;

    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept;
    [[nodiscard]] std::size_t pair_count(std::uint64_t frame) const;
    [[nodiscard]] std::size_t island_count(std::uint64_t frame) const;
    [[nodiscard]] std::size_t spill_pair_count(std::uint64_t frame) const;
    [[nodiscard]] std::uint64_t hash(std::uint64_t frame) const;
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;
    [[nodiscard]] const SegmentedDynamicPairHistoryStats& stats() const noexcept { return stats_; }

private:
    static constexpr std::uint64_t empty_frame = ~std::uint64_t{0};
    static constexpr std::uint32_t no_generation = ~std::uint32_t{0};
    static constexpr std::uint32_t staging_refcount = ~std::uint32_t{0};

    struct SegmentGeneration final {
        std::uint32_t refs{};
        std::uint32_t count{};
        std::uint32_t key{};
    };
    struct SpillGeneration final {
        std::uint32_t refs{};
        std::uint32_t count{};
    };
    struct TableGeneration final {
        std::uint32_t refs{};
        std::uint32_t island_count{};
        std::uint32_t spill_generation{no_generation};
        std::uint64_t topology_signature{};
        std::size_t pair_count{};
    };
    struct Snapshot final {
        std::uint64_t frame{empty_frame};
        std::uint32_t table{no_generation};
    };
    struct KeyedPair final {
        std::size_t key{};
        BroadphasePair pair{};
        friend bool operator<(const KeyedPair& first, const KeyedPair& second) noexcept {
            if (first.key != second.key) return first.key < second.key;
            return first.pair < second.pair;
        }
    };

    [[nodiscard]] Snapshot& slot(std::uint64_t frame) noexcept;
    [[nodiscard]] const Snapshot& slot(std::uint64_t frame) const noexcept;
    [[nodiscard]] std::size_t find_root(std::size_t body) noexcept;
    void unite(std::size_t first, std::size_t second) noexcept;
    [[nodiscard]] std::uint32_t acquire_table();
    [[nodiscard]] std::uint32_t acquire_segment();
    [[nodiscard]] std::uint32_t acquire_spill();
    void release_table(std::uint32_t table) noexcept;
    void release_snapshot(Snapshot& snapshot) noexcept;
    void release_staging() noexcept;
    [[nodiscard]] std::size_t* table_body_keys(std::uint32_t table) noexcept;
    [[nodiscard]] const std::size_t* table_body_keys(std::uint32_t table) const noexcept;
    [[nodiscard]] std::uint32_t* table_segments(std::uint32_t table) noexcept;
    [[nodiscard]] const std::uint32_t* table_segments(std::uint32_t table) const noexcept;
    [[nodiscard]] BroadphasePair* segment_storage(std::uint32_t generation) noexcept;
    [[nodiscard]] const BroadphasePair* segment_storage(std::uint32_t generation) const noexcept;
    [[nodiscard]] BroadphasePair* spill_storage(std::uint32_t generation) noexcept;
    [[nodiscard]] const BroadphasePair* spill_storage(std::uint32_t generation) const noexcept;
    [[nodiscard]] std::uint64_t topology_signature(std::span<const NormalContact> contacts) const noexcept;
    void build_topology(std::span<const NormalContact> contacts);
    void partition_pairs(std::span<const BroadphasePair> pairs);
    [[nodiscard]] bool same_segment(
        std::uint32_t generation,
        std::span<const KeyedPair> run) const noexcept;
    [[nodiscard]] bool same_spill(
        std::uint32_t generation,
        std::span<const BroadphasePair> pairs) const noexcept;

    SegmentedDynamicPairHistoryConfig config_{};
    std::size_t segment_generation_count_{};
    std::size_t spill_generation_count_{};
    std::size_t table_generation_count_{};
    std::vector<SegmentGeneration> segment_generations_{};
    std::vector<SpillGeneration> spill_generations_{};
    std::vector<TableGeneration> tables_{};
    std::vector<BroadphasePair> segment_payload_{};
    std::vector<BroadphasePair> spill_payload_{};
    std::vector<std::size_t> table_body_keys_{};
    std::vector<std::uint32_t> table_segments_{};
    std::vector<Snapshot> snapshots_{};
    std::vector<std::size_t> parent_{};
    std::vector<std::size_t> current_body_keys_{};
    std::vector<std::uint8_t> dirty_keys_{};
    std::vector<KeyedPair> keyed_pairs_{};
    std::vector<BroadphasePair> spill_pairs_{};
    std::vector<std::uint32_t> staged_segments_{};
    std::size_t staged_segment_count_{};
    std::uint32_t staging_table_{no_generation};
    std::uint32_t staging_spill_{no_generation};
    std::size_t keyed_pair_count_{};
    std::size_t spill_pair_count_{};
    std::size_t current_island_count_{};
    std::uint64_t current_topology_signature_{};
    SegmentedDynamicPairHistoryStats stats_{};
    bool initialized_{};
};

} // namespace neoeng::core
