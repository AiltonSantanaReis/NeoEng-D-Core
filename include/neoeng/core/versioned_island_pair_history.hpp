#pragma once

#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/broadphase.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct VersionedIslandPairHistoryConfig final {
    std::size_t bodies{};
    std::size_t maximum_contacts{};
    std::size_t history_capacity{32U};
    std::size_t extra_pairs_per_island{1U};
    // Zero selects history_capacity + 2. Smaller explicit values trade retained
    // dirty generations for lower reserve and fail explicitly on exhaustion.
    std::size_t segment_generations_per_island{};
};

struct VersionedIslandPairHistoryStats final {
    std::uint64_t snapshots_captured{};
    std::uint64_t tables_shared{};
    std::uint64_t tables_copied{};
    std::uint64_t segments_written{};
    std::uint64_t segments_reused{};
    std::uint64_t pairs_written{};
    std::uint64_t full_captures{};
    std::uint64_t incremental_captures{};
    std::uint64_t capacity_failures{};
    std::uint64_t cross_island_pairs{};
    std::size_t island_count{};
    std::size_t live_pairs{};
};

// Fixed-capacity COW history for a pair cache partitioned by canonical contact islands.
// Snapshots share an immutable generation table in O(1) when the broadphase cache did
// not change. A dirty capture copies the table once and replaces only dirty segments.
// Every island and table pool has one transactional staging slot beyond the retained ring.
class VersionedIslandPairHistory final {
public:
    explicit VersionedIslandPairHistory(VersionedIslandPairHistoryConfig config);

    void initialize(
        std::uint64_t frame,
        std::span<const NormalContact> contacts,
        std::span<const BroadphasePair> pairs);

    void capture(
        std::uint64_t frame,
        std::span<const BroadphasePair> pairs,
        std::span<const std::size_t> dirty_bodies,
        bool force_full = false);

    [[nodiscard]] std::size_t restore(
        std::uint64_t frame,
        std::span<BroadphasePair> output) const;

    void truncate_after(std::uint64_t frame);
    void clear() noexcept;

    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept;
    [[nodiscard]] std::size_t island_count() const noexcept { return island_count_; }
    [[nodiscard]] std::size_t island_of_body(std::size_t body) const;
    [[nodiscard]] std::size_t pair_count(std::uint64_t frame) const;
    [[nodiscard]] std::uint64_t hash(std::uint64_t frame) const;
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;
    [[nodiscard]] const VersionedIslandPairHistoryStats& stats() const noexcept { return stats_; }

private:
    static constexpr std::uint64_t empty_frame = ~std::uint64_t{0};
    static constexpr std::uint32_t no_generation = ~std::uint32_t{0};
    static constexpr std::uint32_t staging_refcount = ~std::uint32_t{0};

    struct Generation final {
        std::uint32_t refs{};
        std::uint32_t count{};
    };

    struct GenerationTable final {
        std::uint32_t refs{};
        std::vector<std::uint32_t> generation{};
    };

    struct Snapshot final {
        std::uint64_t frame{empty_frame};
        std::size_t pair_count{};
        std::uint32_t table{no_generation};
    };

    [[nodiscard]] std::size_t find_root(std::size_t body) noexcept;
    void unite(std::size_t first, std::size_t second) noexcept;
    [[nodiscard]] Snapshot& slot(std::uint64_t frame) noexcept;
    [[nodiscard]] const Snapshot& slot(std::uint64_t frame) const noexcept;
    [[nodiscard]] std::size_t generation_index(std::size_t island, std::uint32_t local) const noexcept;
    [[nodiscard]] BroadphasePair* generation_storage(std::size_t island, std::uint32_t local) noexcept;
    [[nodiscard]] const BroadphasePair* generation_storage(std::size_t island, std::uint32_t local) const noexcept;
    [[nodiscard]] std::uint32_t acquire_generation(std::size_t island);
    [[nodiscard]] std::uint32_t acquire_table();
    void release_table(std::uint32_t table) noexcept;
    void release_snapshot(Snapshot& snapshot) noexcept;
    void release_staging() noexcept;
    void build_topology(std::span<const NormalContact> contacts);
    void partition_pairs(
        std::span<const BroadphasePair> pairs,
        std::span<const std::uint8_t> dirty_islands);

    VersionedIslandPairHistoryConfig config_{};
    std::size_t island_count_{};
    std::size_t generations_per_island_{};
    std::vector<std::size_t> parent_{};
    std::vector<std::size_t> root_to_island_{};
    std::vector<std::size_t> body_island_{};
    std::vector<std::size_t> contact_counts_{};
    std::vector<std::size_t> capacities_{};
    std::vector<std::size_t> storage_base_{};
    std::vector<Generation> generations_{};
    std::vector<BroadphasePair> storage_{};
    std::vector<GenerationTable> tables_{};
    std::vector<Snapshot> snapshots_{};
    std::uint32_t staging_table_{no_generation};
    std::vector<std::uint8_t> dirty_islands_{};
    std::vector<std::size_t> staging_counts_{};
    std::vector<std::size_t> staging_base_{};
    std::vector<std::size_t> staging_write_{};
    std::vector<BroadphasePair> staging_pairs_{};
    std::vector<std::uint32_t> staged_local_generation_{};
    VersionedIslandPairHistoryStats stats_{};
    bool initialized_{};
};

} // namespace neoeng::core
