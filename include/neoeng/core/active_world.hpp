#pragma once

#include "neoeng/core/immutable_world.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <vector>

namespace neoeng::core {

class DeterministicActiveSet final {
public:
    DeterministicActiveSet() = default;
    explicit DeterministicActiveSet(std::vector<std::size_t> sorted_indices);

    [[nodiscard]] static DeterministicActiveSet from_world(const WorldState& state);
    [[nodiscard]] static DeterministicActiveSet from_immutable(const ImmutableWorldState& state);

    [[nodiscard]] std::span<const std::size_t> indices() const noexcept { return indices_; }
    [[nodiscard]] std::size_t size() const noexcept { return indices_.size(); }
    [[nodiscard]] bool empty() const noexcept { return indices_.empty(); }
    [[nodiscard]] bool contains(std::size_t index) const noexcept;

    auto operator<=>(const DeterministicActiveSet&) const = default;

private:
    std::vector<std::size_t> indices_{};
};

struct ActiveStepStats final {
    std::uint64_t candidate_bodies_scanned{};
    std::uint64_t inactive_bodies_skipped{};
    std::uint64_t entity_binary_search_probes{};
    std::uint64_t input_commands_consumed{};
    ImmutableAllocationStats immutable_allocation{};
};

struct ActiveImmutableStepResult final {
    ImmutableWorldState state{};
    DeterministicActiveSet active{};
    DirtySet dirty{};
    ActiveStepStats stats{};
};

[[nodiscard]] ActiveImmutableStepResult step_immutable_active(
    const ImmutableWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs);

struct ActiveRollbackStats final {
    ActiveStepStats cumulative_step{};
    ImmutableMemoryFootprint retained_memory{};
    std::size_t retained_frames{};
    std::size_t current_active_bodies{};
    std::size_t capacity{};
};

class ActiveRollbackEngine final {
public:
    explicit ActiveRollbackEngine(
        WorldState initial,
        std::size_t snapshot_capacity = 300U,
        std::size_t chunk_size = 64U);

    void advance(std::span<const InputCommand> inputs);
    [[nodiscard]] std::size_t correct_input_and_resimulate(
        std::uint64_t input_frame,
        std::span<const InputCommand> corrected_inputs);

    [[nodiscard]] const ImmutableWorldState& state() const noexcept { return current_.state; }
    [[nodiscard]] const DeterministicActiveSet& active_set() const noexcept { return current_.active; }
    [[nodiscard]] WorldState materialized_state() const { return current_.state.materialize(); }
    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept;
    [[nodiscard]] ActiveRollbackStats stats() const;

private:
    struct Version final {
        ImmutableWorldState state{};
        DeterministicActiveSet active{};
    };

    [[nodiscard]] Version restore(std::uint64_t frame) const;
    void retain(Version version);
    void truncate_after(std::uint64_t frame);

    Version current_{};
    std::size_t capacity_{};
    std::deque<Version> snapshots_{};
    std::map<std::uint64_t, std::vector<InputCommand>> input_history_{};
    ActiveStepStats cumulative_step_{};
};

} // namespace neoeng::core
