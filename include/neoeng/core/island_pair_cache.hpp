#pragma once

#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/broadphase.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct IslandPairCacheConfig final {
    std::size_t bodies{};
    std::size_t maximum_contacts{};
    std::size_t extra_pairs_per_island{1U};
};

struct IslandPairCacheStats final {
    std::uint64_t full_rebuilds{};
    std::uint64_t island_rebuilds{};
    std::uint64_t pairs_written{};
    std::uint64_t capacity_failures{};
    std::size_t island_count{};
    std::size_t live_pairs{};
};

// Fixed-capacity pair storage physically segmented by canonical contact island.
// Island ids are assigned by ascending minimum body index. Replacing one island
// never moves or rewrites the pair payload of another island.
class IslandPairCache final {
public:
    explicit IslandPairCache(IslandPairCacheConfig config);

    void initialize(std::span<const NormalContact> contacts);
    void replace_island_pairs(std::size_t island, std::span<const BroadphasePair> pairs);
    void replace_pairs_for_body(std::size_t body, std::span<const BroadphasePair> pairs);

    [[nodiscard]] std::size_t island_count() const noexcept { return island_count_; }
    [[nodiscard]] std::size_t island_of_body(std::size_t body) const;
    [[nodiscard]] std::span<const BroadphasePair> pairs_for_island(std::size_t island) const;
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;
    [[nodiscard]] std::uint64_t hash() const noexcept;
    [[nodiscard]] const IslandPairCacheStats& stats() const noexcept { return stats_; }

private:
    [[nodiscard]] std::size_t find_root(std::size_t body) noexcept;
    void unite(std::size_t first, std::size_t second) noexcept;

    IslandPairCacheConfig config_{};
    std::size_t island_count_{};
    std::vector<std::size_t> parent_{};
    std::vector<std::size_t> root_to_island_{};
    std::vector<std::size_t> body_island_{};
    std::vector<std::size_t> contact_counts_{};
    std::vector<std::size_t> offsets_{};
    std::vector<std::size_t> capacities_{};
    std::vector<std::size_t> counts_{};
    std::vector<BroadphasePair> storage_{};
    IslandPairCacheStats stats_{};
};

} // namespace neoeng::core
