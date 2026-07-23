#pragma once

#include "neoeng/core/simulation.hpp"
#include "neoeng/core/snapshot_store.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace neoeng::core {

class RollbackEngine final {
public:
    explicit RollbackEngine(
        WorldState initial,
        std::size_t snapshot_capacity = 300,
        SnapshotStrategy strategy = SnapshotStrategy::FullCopy);
    explicit RollbackEngine(WorldState initial, SnapshotStoreConfig config);

    void advance(std::span<const InputCommand> inputs);
    [[nodiscard]] std::size_t correct_input_and_resimulate(
        std::uint64_t input_frame,
        std::span<const InputCommand> corrected_inputs);

    [[nodiscard]] const WorldState& state() const noexcept { return current_; }
    [[nodiscard]] const ISnapshotStore& snapshots() const noexcept { return *snapshots_; }
    [[nodiscard]] SnapshotStrategy strategy() const noexcept { return snapshots_->strategy(); }
    [[nodiscard]] std::size_t retained_input_frames() const noexcept { return input_history_.size(); }

private:
    WorldState current_;
    std::unique_ptr<ISnapshotStore> snapshots_;
    std::map<std::uint64_t, std::vector<InputCommand>> input_history_;
};

} // namespace neoeng::core
