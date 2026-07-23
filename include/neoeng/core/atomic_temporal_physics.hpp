#pragma once

#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/axis_forest_projection.hpp"
#include "neoeng/core/broadphase.hpp"
#include "neoeng/core/atomic_physics.hpp"
#include "neoeng/core/temporal_contact.hpp"
#include "neoeng/core/weighted_tree_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct AtomicTemporalPhysicsConfig final {
    std::size_t bodies{};
    std::size_t contacts{};
    std::size_t maximum_candidate_pairs{};
    std::size_t history_capacity{32U};
    std::size_t horizon_frames{8U};
    std::size_t maximum_velocity_mutations{16U};
    std::size_t maximum_mass_mutations{4U};
    std::size_t maximum_contact_mutations{4U};
    Fixed half_extent{Fixed::from_ratio(1, 2)};
    Fixed::rep velocity_mutation_guard_raw{4'096};
    ArbitraryNormalConfig projection{};
    bool force_rebuild_each_frame{};
    bool enable_single_tree_solver{};
    bool enable_axis_forest_solver{};
    bool disable_internal_history{};
};

struct AtomicTemporalPhysicsStats final {
    std::uint64_t frames_simulated{};
    std::uint64_t snapshots_captured{};
    std::uint64_t broadphase_builds{};
    std::uint64_t broadphase_reuses{};
    std::uint64_t broadphase_pair_tests{};
    std::uint64_t narrowphase_pair_tests{};
    std::uint64_t narrowphase_contacts{};
    std::uint64_t contacts_projected{};
    std::uint64_t escaped_bodies{};
    std::uint64_t payload_values_copied{};
    std::uint64_t candidate_pairs_peak{};
    bool matching_fast_path{};
    std::uint64_t tree_solver_calls{};
    std::uint64_t tree_solver_certified{};
    std::uint64_t tree_chain_certified{};
    std::uint64_t tree_general_certified{};
    std::uint64_t tree_solver_fallbacks{};
    std::uint64_t axis_forest_calls{};
    std::uint64_t axis_forest_certified{};
    std::uint64_t axis_forest_fallbacks{};
    std::uint64_t island_index_rebuilds{};
    std::uint64_t islands_expanded{};
    std::uint64_t island_bodies_added{};
    std::uint64_t island_count_peak{};
    std::uint64_t topology_restore_reuses{};
};



struct AtomicTemporalStateView final {
    std::uint64_t frame{};
    std::uint64_t valid_until_frame{};
    std::uint64_t topology_signature{};
    std::span<const Fixed::rep> position_x{};
    std::span<const Fixed::rep> position_y{};
    std::span<const Fixed::rep> velocity_x{};
    std::span<const Fixed::rep> velocity_y{};
    std::span<const std::uint32_t> masses{};
    std::span<const Fixed::rep> dual{};
    std::span<const NormalContact> manifold{};
    std::span<const std::uint8_t> contact_stable{};
    std::span<const std::uint8_t> contact_candidate{};
    std::span<const FatAabb> fat_bounds{};
    std::span<const BroadphasePair> pairs{};
};

struct AtomicTemporalMutableStateView final {
    std::span<Fixed::rep> position_x{};
    std::span<Fixed::rep> position_y{};
    std::span<Fixed::rep> velocity_x{};
    std::span<Fixed::rep> velocity_y{};
    std::span<std::uint32_t> masses{};
    std::span<Fixed::rep> dual{};
    std::span<NormalContact> manifold{};
    std::span<std::uint8_t> contact_stable{};
    std::span<std::uint8_t> contact_candidate{};
    std::span<FatAabb> fat_bounds{};
    std::span<BroadphasePair> pairs{};
};

struct AtomicTemporalRestoreMetadata final {
    std::uint64_t frame{};
    std::uint64_t valid_until_frame{};
    std::uint64_t topology_signature{};
    std::size_t pair_count{};
};

struct AtomicTemporalCaptureHints final {
    // Bodies whose positions changed during the completed transition. This is distinct
    // from changed_bodies, which tracks velocity/mass/cache dependencies.
    std::span<const std::size_t> changed_position_bodies{};
    // False means the producer cannot prove the position hint set is complete;
    // history must conservatively compare every position page.
    bool position_hints_complete{};
    std::span<const std::size_t> changed_bodies{};
    std::span<const std::uint8_t> changed_contacts{};
    bool cache_rebuilt{};
    bool topology_changed{};
};

struct AtomicTemporalExternalState final {
    std::uint64_t frame{};
    std::uint64_t valid_until_frame{};
    std::uint64_t topology_signature{};
    std::size_t pair_count{};
    std::vector<Fixed::rep> position_x{};
    std::vector<Fixed::rep> position_y{};
    std::vector<Fixed::rep> velocity_x{};
    std::vector<Fixed::rep> velocity_y{};
    std::vector<std::uint32_t> masses{};
    std::vector<Fixed::rep> dual{};
    std::vector<NormalContact> manifold{};
    std::vector<std::uint8_t> contact_stable{};
    std::vector<std::uint8_t> contact_candidate{};
    std::vector<FatAabb> fat_bounds{};
    std::vector<BroadphasePair> pairs{};

    AtomicTemporalExternalState() = default;
    AtomicTemporalExternalState(std::size_t bodies, std::size_t contacts, std::size_t maximum_pairs);
};

// Atomic temporal kernel inherited from v0.16. The complete physical version is captured atomically:
// body state, masses, dual, manifold, temporal fat bounds and pair cache.
// Storage is fixed-capacity and performs no C++ allocation after construction.
class AtomicTemporalPhysicsEngine final {
public:
    explicit AtomicTemporalPhysicsEngine(AtomicTemporalPhysicsConfig config);

    void initialize(
        std::span<const Fixed::rep> position_x,
        std::span<const Fixed::rep> position_y,
        std::span<const Fixed::rep> velocity_x,
        std::span<const Fixed::rep> velocity_y,
        std::span<const std::uint32_t> masses,
        std::span<const NormalContact> contacts);

    void set_input(std::uint64_t frame, AtomicPhysicsFrameInput input);
    void simulate_to(std::uint64_t target_frame);
    void correct_and_resimulate(
        std::uint64_t frame,
        AtomicPhysicsFrameInput corrected,
        std::uint64_t target_frame);
    void restore(std::uint64_t frame);
    void truncate_after(std::uint64_t frame);

    [[nodiscard]] std::uint64_t frame() const noexcept { return frame_; }
    [[nodiscard]] std::uint64_t hash() const noexcept;
    [[nodiscard]] std::uint64_t physical_hash() const noexcept;
    [[nodiscard]] bool physically_equivalent_to(const AtomicTemporalPhysicsEngine& other) const noexcept;
    [[nodiscard]] bool equivalent_to(const AtomicTemporalPhysicsEngine& other) const noexcept;
    [[nodiscard]] const AtomicTemporalPhysicsStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;
    [[nodiscard]] std::size_t candidate_pair_count() const noexcept { return pair_count_; }
    [[nodiscard]] std::uint64_t cache_valid_until_frame() const noexcept { return valid_until_frame_; }
    [[nodiscard]] AtomicTemporalStateView state_view() const noexcept;
    [[nodiscard]] AtomicTemporalMutableStateView mutable_state_view() noexcept;
    [[nodiscard]] AtomicTemporalCaptureHints capture_hints() const noexcept;
    void export_state(AtomicTemporalExternalState& output) const;
    void import_state(const AtomicTemporalExternalState& input);
    // Completes a direct page restore after PagedAtomicHistory has copied data into
    // mutable_state_view(). This removes the former external-state double copy.
    void finalize_direct_restore(AtomicTemporalRestoreMetadata metadata);

private:
    struct SnapshotSlot final {
        std::uint64_t frame{~std::uint64_t{0}};
        std::size_t cache_slot{};
        std::uint64_t cache_generation{};
        std::vector<Fixed::rep> position_x{};
        std::vector<Fixed::rep> position_y{};
        std::vector<Fixed::rep> velocity_x{};
        std::vector<Fixed::rep> velocity_y{};
        std::vector<std::uint32_t> masses{};
        std::vector<Fixed::rep> dual{};
        std::vector<NormalContact> manifold{};
        std::vector<std::uint8_t> contact_stable{};
    };

    struct CacheSlot final {
        std::uint64_t generation{};
        std::uint64_t valid_until_frame{};
        std::size_t pair_count{};
        std::vector<FatAabb> fat_bounds{};
        std::vector<BroadphasePair> pairs{};
    };

    struct InputSlot final {
        std::uint64_t frame{~std::uint64_t{0}};
        std::vector<VelocityMutation> velocity{};
        std::vector<MassMutation> mass{};
        std::vector<ContactMutation> contact{};
        std::size_t velocity_count{};
        std::size_t mass_count{};
        std::size_t contact_count{};
    };

    struct ContactLookup final {
        BroadphasePair pair{};
        std::size_t contact{};
        auto operator<=>(const ContactLookup&) const = default;
    };

    void capture();
    void step_one();
    void rebuild_contact_lookup();
    void rebuild_island_index();
    void expand_cache_checks_to_islands();
    void rebuild_temporal_cache();
    void rebuild_candidate_map();
    void persist_current_cache();
    [[nodiscard]] bool try_project_single_tree(std::size_t active_count);
    [[nodiscard]] bool project_tree_guard_velocities();
    [[nodiscard]] std::size_t acquire_cache_slot() const;
    [[nodiscard]] bool cache_contains_transition_and_horizon(
        std::span<const std::size_t> bodies_to_check);
    [[nodiscard]] bool narrowphase_pair(const BroadphasePair& pair) const;
    [[nodiscard]] std::size_t contact_for_pair(const BroadphasePair& pair) const noexcept;
    [[nodiscard]] SnapshotSlot& snapshot_slot(std::uint64_t frame) noexcept;
    [[nodiscard]] const SnapshotSlot& snapshot_slot(std::uint64_t frame) const noexcept;
    [[nodiscard]] InputSlot& input_slot(std::uint64_t frame) noexcept;
    [[nodiscard]] const InputSlot* find_input(std::uint64_t frame) const noexcept;

    AtomicTemporalPhysicsConfig config_{};
    std::uint64_t frame_{};
    std::uint64_t valid_until_frame_{};
    std::uint64_t topology_signature_{};
    std::size_t pair_count_{};
    std::vector<Fixed::rep> position_x_{};
    std::vector<Fixed::rep> position_y_{};
    std::vector<Fixed::rep> velocity_x_{};
    std::vector<Fixed::rep> velocity_y_{};
    std::vector<Fixed::rep> predicted_x_{};
    std::vector<Fixed::rep> predicted_y_{};
    std::vector<Fixed::rep> guard_velocity_x_{};
    std::vector<Fixed::rep> guard_velocity_y_{};
    std::vector<std::uint32_t> masses_{};
    std::vector<Fixed::rep> dual_{};
    std::vector<NormalContact> manifold_{};
    std::vector<FatAabb> fat_bounds_{};
    std::vector<BroadphasePair> pair_cache_{};
    std::vector<std::size_t> broadphase_order_{};
    std::vector<ContactLookup> contact_lookup_{};
    std::vector<std::int64_t> body_to_contact_{};
    std::vector<std::uint8_t> contact_stable_{};
    std::vector<std::uint8_t> contact_candidate_{};
    std::vector<std::uint8_t> contact_dirty_{};
    std::vector<std::uint8_t> contact_active_{};
    std::vector<std::uint8_t> body_check_mask_{};
    std::vector<std::size_t> body_check_indices_{};
    std::size_t body_check_count_{};
    std::vector<std::uint8_t> position_changed_mask_{};
    std::vector<std::size_t> position_changed_indices_{};
    std::size_t position_changed_count_{};
    std::vector<std::size_t> active_contact_indices_{};
    std::vector<std::size_t> island_uf_{}, body_island_{}, island_offsets_{},
        island_bodies_{}, island_counts_{}, island_cursors_{};
    std::vector<std::uint8_t> island_mark_{};
    std::size_t island_count_{};
    std::vector<NormalContact> active_contacts_{};
    std::vector<SnapshotSlot> snapshots_{};
    std::vector<CacheSlot> cache_slots_{};
    std::vector<InputSlot> inputs_{};
    std::size_t current_cache_slot_{};
    std::uint64_t current_cache_generation_{};
    std::uint64_t next_cache_generation_{1U};
    ArbitraryNormalScratch scratch_;
    WeightedTreeScratch tree_scratch_;
    AxisForestScratch axis_forest_scratch_;
    std::vector<std::size_t> tree_global_bodies_{};
    std::vector<std::size_t> tree_local_index_{};
    std::vector<Fixed::rep> tree_velocity_x_{};
    std::vector<Fixed::rep> tree_velocity_y_{};
    std::vector<std::uint32_t> tree_masses_{};
    std::vector<DirectedTreeEdge> tree_edges_{};
    std::vector<std::size_t> tree_contact_indices_{};
    AtomicTemporalPhysicsStats stats_{};
    bool initialized_{};
    bool matching_{};
    bool last_cache_rebuilt_{};
    bool last_topology_changed_{};
};

} // namespace neoeng::core
