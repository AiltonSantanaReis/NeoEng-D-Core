#pragma once

#include "neoeng/core/broadphase.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace neoeng::core {

enum class ContactAxis : std::uint8_t {
    X,
    Y,
};

struct SweptContact final {
    std::size_t first{};
    std::size_t second{};
    ContactAxis axis{ContactAxis::X};
    Fixed toi{}; // normalized [0, 1]
    bool initial_overlap{};
    bool final_overlap{};

    auto operator<=>(const SweptContact&) const = default;
};

enum class ContactBroadphaseMode : std::uint8_t {
    SweptCellRaster,
    CenterGridExpansion,
};

enum class ConnectedContactSolverMode : std::uint8_t {
    GeneralColored,
    ChainIsotonic,
    Auto,
};

struct ContactSolverConfig final {
    Fixed half_extent{Fixed::from_ratio(1, 2)};
    Fixed cell_size{Fixed::from_integer(2)};
    std::size_t position_iterations{4U};
    std::size_t max_cells_per_body{256U};
    ContactBroadphaseMode broadphase_mode{ContactBroadphaseMode::SweptCellRaster};
    ConnectedContactSolverMode connected_solver_mode{ConnectedContactSolverMode::GeneralColored};
    bool enable_chain_warm_start{true};
    std::size_t max_chain_bodies{1'000'000U};
};

struct ContactSolverStats final {
    std::uint64_t bodies_scanned{};
    std::uint64_t cell_entries{};
    std::uint64_t candidate_pairs{};
    std::uint64_t narrowphase_tests{};
    std::uint64_t swept_hits{};
    std::uint64_t initial_overlaps{};
    std::uint64_t final_overlaps{};
    std::uint64_t constraint_islands{};
    std::uint64_t resting_islands_skipped{};
    std::uint64_t graph_colors{};
    std::uint64_t velocity_resolutions{};
    std::uint64_t position_projections{};
    std::uint64_t fallback_all_pairs{};
    std::uint64_t chain_solver_attempts{};
    std::uint64_t chain_solver_accepts{};
    std::uint64_t chain_solver_fallbacks{};
    std::uint64_t chain_bodies_solved{};
    std::uint64_t isotonic_blocks{};
    std::uint64_t warm_start_attempts{};
    std::uint64_t warm_start_accepts{};
    std::uint64_t warm_start_rejects{};
    std::uint64_t manifold_points_reused{};
    std::uint64_t manifold_points_created{};
    std::int64_t momentum_rounding_error_raw{};
    ComponentAllocationStats integration_allocation{};
    ComponentAllocationStats solver_allocation{};
};


struct ChainWarmStartState final {
    ContactAxis axis{ContactAxis::X};
    int direction{1};
    std::vector<std::size_t> bodies{};
    std::vector<std::size_t> velocity_block_ends{};
    std::vector<std::size_t> position_block_ends{};
};

struct ContactManifoldPoint final {
    std::size_t first{};
    std::size_t second{};
    ContactAxis axis{ContactAxis::X};
    Fixed accumulated_normal_impulse{};

    auto operator<=>(const ContactManifoldPoint&) const = default;
};

struct PersistentManifoldState final {
    std::uint64_t frame{};
    std::vector<SweptContact> contacts{};
    std::vector<ContactManifoldPoint> points{};
    std::optional<ChainWarmStartState> chain_warm_start{};
};

struct ContactStepResult final {
    ComponentWorldState state{};
    DeterministicActiveSet active{};
    DirtySet dirty{};
    std::vector<SweptContact> contacts{};
    ContactSolverStats stats{};
};

[[nodiscard]] std::vector<SweptContact> swept_aabb_contacts(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    ContactSolverConfig config,
    ContactSolverStats* stats = nullptr);

[[nodiscard]] std::vector<SweptContact> brute_force_swept_aabb_contacts(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    Fixed half_extent);


[[nodiscard]] std::vector<SweptContact> swept_aabb_contacts_for_pairs(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    std::span<const BroadphasePair> pairs,
    Fixed half_extent,
    ContactSolverStats* stats = nullptr);

[[nodiscard]] ContactStepResult solve_component_contact_constraints(
    const ComponentWorldState& current,
    ComponentStepResult integrated,
    std::vector<SweptContact> contacts,
    ContactSolverConfig config,
    ContactSolverStats broadphase_stats = {},
    const PersistentManifoldState* previous_manifold = nullptr,
    PersistentManifoldState* next_manifold = nullptr);


[[nodiscard]] std::optional<ContactStepResult> step_component_contacts_matching_fused(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const BroadphasePair> pairs,
    ContactSolverConfig config = {});

[[nodiscard]] ContactStepResult step_component_contacts(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs,
    ContactSolverConfig config = {},
    ComponentStepOptions options = {},
    const PersistentManifoldState* previous_manifold = nullptr,
    PersistentManifoldState* next_manifold = nullptr);

} // namespace neoeng::core
