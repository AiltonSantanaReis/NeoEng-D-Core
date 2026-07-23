#pragma once

#include "neoeng/core/temporal_contact.hpp"
#include "neoeng/core/epoch_arena.hpp"
#include "neoeng/core/indexed_ring.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct InteractiveRollbackConfig final {
    std::size_t capacity{300U};
    std::size_t page_size{256U};
    std::size_t arena_bytes_per_epoch{64U * 1024U};
    ContactSolverConfig contacts{};
    TemporalBroadphaseConfig temporal{};
    bool use_temporal_cache{true};
    ComponentStepOptions step_options{.kernel_mode = FixedKernelMode::Scalar};
};

struct InteractiveRollbackStats final {
    std::size_t retained_frames{};
    std::size_t active_bodies{};
    std::uint64_t frames_simulated{};
    std::uint64_t contacts_solved{};
    std::uint64_t rollback_frames_resimulated{};
    ContactSolverStats cumulative_solver{};
    TemporalBroadphaseStats cumulative_temporal{};
    EpochArenaStats arena{};
};

class InteractiveRollbackEngine final {
public:
    explicit InteractiveRollbackEngine(
        WorldState initial,
        InteractiveRollbackConfig config = {});

    void advance(std::span<const InputCommand> inputs);
    [[nodiscard]] std::size_t correct_input_and_resimulate(
        std::uint64_t input_frame,
        std::span<const InputCommand> corrected_inputs);

    [[nodiscard]] const ComponentWorldState& state() const noexcept;
    [[nodiscard]] const DeterministicActiveSet& active_set() const noexcept;
    [[nodiscard]] WorldState materialized_state() const;
    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept { return versions_.contains(frame); }
    [[nodiscard]] InteractiveRollbackStats stats() const noexcept;

private:
    struct StoredInputs final {
        const InputCommand* data{};
        std::size_t size{};
    };

    struct Version final {
        ComponentWorldState state{};
        DeterministicActiveSet active{};
        TemporalBroadphaseState broadphase{};
        PersistentManifoldState manifold{};
        StoredInputs inputs_from_previous{};
        std::size_t contact_count{};
    };

    [[nodiscard]] StoredInputs store_inputs(
        std::uint64_t destination_frame,
        std::span<const InputCommand> inputs);
    [[nodiscard]] const Version& current_version() const noexcept;
    void advance_internal(std::span<const InputCommand> inputs);
    static void accumulate(ContactSolverStats& target, const ContactSolverStats& source) noexcept;
    static void accumulate(TemporalBroadphaseStats& target, const TemporalBroadphaseStats& source) noexcept;

    InteractiveRollbackConfig config_{};
    IndexedFrameRing<Version> versions_;
    PersistentEpochArena arena_;
    std::uint64_t frames_simulated_{};
    std::uint64_t contacts_solved_{};
    std::uint64_t rollback_frames_resimulated_{};
    ContactSolverStats cumulative_solver_{};
    TemporalBroadphaseStats cumulative_temporal_{};
};

} // namespace neoeng::core
