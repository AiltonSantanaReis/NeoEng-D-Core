#pragma once

#include "neoeng/core/component_world.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace neoeng::core {

struct GridCell final {
    std::int64_t x{};
    std::int64_t y{};

    auto operator<=>(const GridCell&) const = default;
};

struct BroadphasePair final {
    std::size_t first{};
    std::size_t second{};

    auto operator<=>(const BroadphasePair&) const = default;
};

struct BroadphaseStats final {
    std::uint64_t seed_bodies{};
    std::uint64_t cells_queried{};
    std::uint64_t candidate_pairs_tested{};
    std::uint64_t exact_overlaps{};
    std::uint64_t bodies_woken{};
    std::uint64_t cell_memberships_moved{};
    std::uint64_t cell_maps_cloned{};
};

class GridBroadphaseState final {
public:
    using CellMap = std::map<GridCell, std::vector<std::size_t>>;

    GridBroadphaseState() = default;

    [[nodiscard]] std::uint64_t frame() const noexcept { return frame_; }
    [[nodiscard]] std::size_t body_count() const noexcept { return body_count_; }
    [[nodiscard]] Fixed cell_size() const noexcept { return cell_size_; }
    [[nodiscard]] Fixed half_extent() const noexcept { return half_extent_; }
    [[nodiscard]] std::size_t occupied_cells() const noexcept {
        return cells_ ? cells_->size() : 0U;
    }
    [[nodiscard]] const GridCell& cell_of(std::size_t index) const;
    [[nodiscard]] std::span<const std::size_t> occupants(const GridCell& cell) const noexcept;

private:
    friend GridBroadphaseState make_grid_broadphase(
        const ComponentWorldState&, Fixed, Fixed);
    friend GridBroadphaseState update_grid_broadphase(
        const GridBroadphaseState&, const ComponentWorldState&, const DirtySet&,
        BroadphaseStats*);

    std::uint64_t frame_{};
    std::size_t body_count_{};
    Fixed cell_size_{};
    Fixed half_extent_{};
    std::shared_ptr<const CellMap> cells_{};
    std::shared_ptr<const std::vector<GridCell>> membership_{};
};

struct IslandClosure final {
    DeterministicActiveSet bodies{};
    std::vector<BroadphasePair> overlaps{};
    BroadphaseStats stats{};
};

[[nodiscard]] GridBroadphaseState make_grid_broadphase(
    const ComponentWorldState& state,
    Fixed cell_size,
    Fixed half_extent);

[[nodiscard]] GridBroadphaseState update_grid_broadphase(
    const GridBroadphaseState& previous,
    const ComponentWorldState& next,
    const DirtySet& dirty,
    BroadphaseStats* stats = nullptr);

[[nodiscard]] IslandClosure conservative_island_closure(
    const ComponentWorldState& state,
    const GridBroadphaseState& broadphase,
    const DeterministicActiveSet& seeds);

[[nodiscard]] std::vector<BroadphasePair> brute_force_overlap_pairs(
    const ComponentWorldState& state,
    Fixed half_extent);

struct IslandStepResult final {
    ComponentWorldState state{};
    GridBroadphaseState broadphase{};
    DeterministicActiveSet active{};
    DeterministicActiveSet island_candidates{};
    DirtySet dirty{};
    std::vector<BroadphasePair> overlaps{};
    ComponentAllocationStats component_stats{};
    BroadphaseStats broadphase_stats{};
};

[[nodiscard]] IslandStepResult step_component_islands(
    const ComponentWorldState& current,
    const GridBroadphaseState& broadphase,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs,
    ComponentStepOptions options = {});

} // namespace neoeng::core
