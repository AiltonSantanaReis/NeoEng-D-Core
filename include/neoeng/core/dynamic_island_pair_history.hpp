#pragma once

#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/broadphase.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct DynamicIslandPairHistoryConfig final {
    std::size_t bodies{};
    std::size_t maximum_contacts{};
    std::size_t maximum_pairs{};
    std::size_t history_capacity{32U};
    std::size_t pair_generations{};
    std::size_t topology_generations{};
};

struct DynamicIslandPairHistoryStats final {
    std::uint64_t snapshots_captured{};
    std::uint64_t pair_generations_shared{};
    std::uint64_t pair_generations_written{};
    std::uint64_t topology_generations_shared{};
    std::uint64_t topology_generations_written{};
    std::uint64_t topology_changes{};
    std::uint64_t spill_pairs_written{};
    std::uint64_t capacity_failures{};
    std::size_t live_pairs{};
    std::size_t live_islands{};
    std::size_t live_spill_pairs{};
};

// v0.21 authoritative retained pair history.
//
// Unlike VersionedIslandPairHistory, topology is versioned and may change at any frame.
// Pairs crossing the current contact islands are retained in an explicit spill segment
// instead of being rejected. Pair and topology generations are fixed-capacity and are
// committed transactionally: a failed capture leaves all retained snapshots untouched.
class DynamicIslandPairHistory final {
public:
    explicit DynamicIslandPairHistory(DynamicIslandPairHistoryConfig config);

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
        bool topology_hint_complete = false);

    [[nodiscard]] std::size_t restore_pairs(
        std::uint64_t frame,
        std::span<BroadphasePair> output) const;
    [[nodiscard]] std::size_t restore_spill_pairs(
        std::uint64_t frame,
        std::span<BroadphasePair> output) const;
    [[nodiscard]] std::size_t restore_body_islands(
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
    [[nodiscard]] const DynamicIslandPairHistoryStats& stats() const noexcept { return stats_; }

private:
    static constexpr std::uint64_t empty_frame = ~std::uint64_t{0};
    static constexpr std::uint32_t no_generation = ~std::uint32_t{0};
    static constexpr std::uint32_t staging_refcount = ~std::uint32_t{0};

    struct PairGeneration final {
        std::uint32_t refs{};
        std::uint32_t count{};
        std::uint32_t spill_count{};
    };
    struct TopologyGeneration final {
        std::uint32_t refs{};
        std::uint32_t island_count{};
        std::uint64_t signature{};
    };
    struct Snapshot final {
        std::uint64_t frame{empty_frame};
        std::uint32_t pair_generation{no_generation};
        std::uint32_t topology_generation{no_generation};
    };

    [[nodiscard]] Snapshot& slot(std::uint64_t frame) noexcept;
    [[nodiscard]] const Snapshot& slot(std::uint64_t frame) const noexcept;
    [[nodiscard]] BroadphasePair* pair_storage(std::uint32_t generation) noexcept;
    [[nodiscard]] const BroadphasePair* pair_storage(std::uint32_t generation) const noexcept;
    [[nodiscard]] std::size_t* topology_storage(std::uint32_t generation) noexcept;
    [[nodiscard]] const std::size_t* topology_storage(std::uint32_t generation) const noexcept;
    [[nodiscard]] std::uint32_t acquire_pair_generation();
    [[nodiscard]] std::uint32_t acquire_topology_generation();
    void release_pair_generation(std::uint32_t generation) noexcept;
    void release_topology_generation(std::uint32_t generation) noexcept;
    void release_snapshot(Snapshot& snapshot) noexcept;
    void release_staging() noexcept;
    void build_topology(
        std::span<const NormalContact> contacts,
        std::uint32_t topology_generation);
    void write_pairs(
        std::span<const BroadphasePair> pairs,
        std::uint32_t topology_generation,
        std::uint32_t pair_generation);
    [[nodiscard]] std::uint64_t topology_signature(
        std::span<const NormalContact> contacts) const noexcept;
    [[nodiscard]] std::size_t find_root(std::size_t body) noexcept;
    void unite(std::size_t first, std::size_t second) noexcept;

    DynamicIslandPairHistoryConfig config_{};
    std::size_t pair_generation_count_{};
    std::size_t topology_generation_count_{};
    std::vector<PairGeneration> pair_generations_{};
    std::vector<TopologyGeneration> topology_generations_{};
    std::vector<BroadphasePair> pair_storage_{};
    std::vector<BroadphasePair> spill_storage_{};
    std::vector<std::size_t> topology_storage_{};
    std::vector<Snapshot> snapshots_{};
    std::vector<std::size_t> parent_{};
    std::vector<std::size_t> root_to_island_{};
    std::vector<BroadphasePair> staging_pairs_{};
    std::uint32_t staging_pair_{no_generation};
    std::uint32_t staging_topology_{no_generation};
    DynamicIslandPairHistoryStats stats_{};
    bool initialized_{};
};

} // namespace neoeng::core
