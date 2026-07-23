#include "neoeng/core/immutable_rollback.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace neoeng::core {

ImmutableRollbackEngine::ImmutableRollbackEngine(
    WorldState initial,
    std::size_t snapshot_capacity,
    std::size_t chunk_size)
    : capacity_(snapshot_capacity) {
    if (capacity_ == 0U) {
        throw std::invalid_argument("Immutable rollback capacity must be greater than zero");
    }
    current_ = make_immutable_world(initial, chunk_size, &cumulative_allocation_);
    retain(current_);
}

void ImmutableRollbackEngine::retain(ImmutableWorldState state) {
    if (!snapshots_.empty() && state.frame() != snapshots_.back().frame() + 1U) {
        throw std::invalid_argument("Immutable snapshots must be retained consecutively");
    }
    snapshots_.push_back(std::move(state));
    while (snapshots_.size() > capacity_) snapshots_.pop_front();
}

void ImmutableRollbackEngine::advance(std::span<const InputCommand> inputs) {
    input_history_[current_.frame()] = std::vector<InputCommand>(inputs.begin(), inputs.end());
    ImmutableStepResult result = step_immutable(current_, inputs);
    cumulative_allocation_ += result.allocation;
    current_ = std::move(result.state);
    retain(current_);
}

bool ImmutableRollbackEngine::contains(std::uint64_t frame) const noexcept {
    return std::any_of(snapshots_.begin(), snapshots_.end(), [frame](const ImmutableWorldState& state) {
        return state.frame() == frame;
    });
}

ImmutableWorldState ImmutableRollbackEngine::restore(std::uint64_t frame) const {
    const auto iterator = std::find_if(snapshots_.rbegin(), snapshots_.rend(),
        [frame](const ImmutableWorldState& state) { return state.frame() == frame; });
    if (iterator == snapshots_.rend()) {
        throw std::out_of_range("Immutable rollback frame is not retained");
    }
    return *iterator;
}

void ImmutableRollbackEngine::truncate_after(std::uint64_t frame) {
    while (!snapshots_.empty() && snapshots_.back().frame() > frame) snapshots_.pop_back();
}

std::size_t ImmutableRollbackEngine::correct_input_and_resimulate(
    std::uint64_t input_frame,
    std::span<const InputCommand> corrected_inputs) {
    const std::uint64_t target_frame = current_.frame();
    if (input_frame >= target_frame) {
        throw std::out_of_range("Correction frame must precede the immutable current frame");
    }
    ImmutableWorldState restored = restore(input_frame);
    input_history_[input_frame] = std::vector<InputCommand>(
        corrected_inputs.begin(), corrected_inputs.end());
    truncate_after(input_frame);

    std::size_t resimulated = 0U;
    while (restored.frame() < target_frame) {
        const auto history = input_history_.find(restored.frame());
        const std::span<const InputCommand> frame_inputs =
            history == input_history_.end()
                ? std::span<const InputCommand>{}
                : std::span<const InputCommand>{history->second};
        ImmutableStepResult result = step_immutable(restored, frame_inputs);
        cumulative_allocation_ += result.allocation;
        restored = std::move(result.state);
        retain(restored);
        ++resimulated;
    }
    current_ = std::move(restored);
    return resimulated;
}

ImmutableRollbackStats ImmutableRollbackEngine::stats() const {
    std::vector<ImmutableWorldState> retained(snapshots_.begin(), snapshots_.end());
    return ImmutableRollbackStats{
        .cumulative_allocation = cumulative_allocation_,
        .retained_memory = estimate_retained_immutable_memory(retained),
        .retained_frames = snapshots_.size(),
        .capacity = capacity_,
    };
}

} // namespace neoeng::core
