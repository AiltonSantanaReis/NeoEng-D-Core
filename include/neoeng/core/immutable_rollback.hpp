#pragma once

#include "neoeng/core/immutable_world.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <vector>

namespace neoeng::core {

struct ImmutableRollbackStats final {
    ImmutableAllocationStats cumulative_allocation{};
    ImmutableMemoryFootprint retained_memory{};
    std::size_t retained_frames{};
    std::size_t capacity{};
};

class ImmutableRollbackEngine final {
public:
    explicit ImmutableRollbackEngine(
        WorldState initial,
        std::size_t snapshot_capacity = 300U,
        std::size_t chunk_size = 64U);

    void advance(std::span<const InputCommand> inputs);
    [[nodiscard]] std::size_t correct_input_and_resimulate(
        std::uint64_t input_frame,
        std::span<const InputCommand> corrected_inputs);

    [[nodiscard]] const ImmutableWorldState& state() const noexcept { return current_; }
    [[nodiscard]] WorldState materialized_state() const { return current_.materialize(); }
    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept;
    [[nodiscard]] ImmutableWorldState restore(std::uint64_t frame) const;
    [[nodiscard]] std::size_t snapshot_count() const noexcept { return snapshots_.size(); }
    [[nodiscard]] ImmutableRollbackStats stats() const;

private:
    void retain(ImmutableWorldState state);
    void truncate_after(std::uint64_t frame);

    ImmutableWorldState current_{};
    std::size_t capacity_{};
    std::deque<ImmutableWorldState> snapshots_{};
    std::map<std::uint64_t, std::vector<InputCommand>> input_history_{};
    ImmutableAllocationStats cumulative_allocation_{};
};

} // namespace neoeng::core
