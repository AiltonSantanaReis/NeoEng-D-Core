#include "neoeng/core/rollback.hpp"

#include <stdexcept>
#include <utility>

namespace neoeng::core {

RollbackEngine::RollbackEngine(
    WorldState initial,
    std::size_t snapshot_capacity,
    SnapshotStrategy strategy)
    : RollbackEngine(std::move(initial), SnapshotStoreConfig{
          .strategy = strategy, .capacity = snapshot_capacity}) {}

RollbackEngine::RollbackEngine(WorldState initial, SnapshotStoreConfig config)
    : current_(std::move(initial)), snapshots_(make_snapshot_store(config)) {
    validate_world(current_);
    const DirtySet initial_dirty = DirtySet::full(current_.bodies.size());
    snapshots_->capture(current_, &initial_dirty);
}

void RollbackEngine::advance(std::span<const InputCommand> inputs) {
    input_history_[current_.frame] = std::vector<InputCommand>(inputs.begin(), inputs.end());
    StepResult result = step_with_dirty(current_, inputs);
    snapshots_->capture(result.state, &result.dirty);
    current_ = std::move(result.state);

    // Input frames older than the oldest retained snapshot can no longer be
    // corrected. Keeping them would make long-running rollback sessions grow
    // without bound even though the snapshot window itself is bounded.
    while (!input_history_.empty() && !snapshots_->contains(input_history_.begin()->first)) {
        input_history_.erase(input_history_.begin());
    }
}

void RollbackEngine::restore_checkpoint(std::uint64_t frame) {
    if (!snapshots_->contains(frame)) {
        throw std::out_of_range("Required recovery checkpoint is no longer retained");
    }
    current_ = snapshots_->restore(frame);
    snapshots_->truncate_after(frame);
    input_history_.erase(input_history_.lower_bound(frame), input_history_.end());
}

std::size_t RollbackEngine::correct_input_and_resimulate(
    std::uint64_t input_frame,
    std::span<const InputCommand> corrected_inputs) {
    const std::uint64_t target_frame = current_.frame;
    if (input_frame >= target_frame) {
        throw std::out_of_range("Correction frame must precede the current frame");
    }
    if (!snapshots_->contains(input_frame)) {
        throw std::out_of_range("Required rollback snapshot is no longer retained");
    }

    WorldState restored = snapshots_->restore(input_frame);
    input_history_[input_frame] = std::vector<InputCommand>(
        corrected_inputs.begin(), corrected_inputs.end());
    snapshots_->truncate_after(input_frame);

    std::size_t resimulated = 0;
    while (restored.frame < target_frame) {
        const auto history = input_history_.find(restored.frame);
        const std::span<const InputCommand> frame_inputs =
            history == input_history_.end()
                ? std::span<const InputCommand>{}
                : std::span<const InputCommand>{history->second};
        StepResult result = step_with_dirty(restored, frame_inputs);
        snapshots_->capture(result.state, &result.dirty);
        restored = std::move(result.state);
        ++resimulated;
    }

    current_ = std::move(restored);
    return resimulated;
}

} // namespace neoeng::core
