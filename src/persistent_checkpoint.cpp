#include "neoeng/core/persistent_checkpoint.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

namespace neoeng::core {

PersistentCheckpointHistory::PersistentCheckpointHistory(PersistentCheckpointConfig config)
    : config_(config) {
    if (config_.capacity == 0U || config_.max_delta_depth == 0U) {
        throw std::invalid_argument("Persistent checkpoint capacity and depth must be positive");
    }
    if (config_.adaptive_density_ppm > 1'000'000U) {
        throw std::invalid_argument("Persistent checkpoint density must be at most one million ppm");
    }
}

bool PersistentCheckpointHistory::should_checkpoint(
    const ImmutableWorldState& state,
    const DirtySet& dirty) const noexcept {
    if (entries_.empty() || delta_depth_ >= config_.max_delta_depth) return true;
    switch (config_.policy) {
    case PersistentCheckpointPolicy::Fixed:
        return state.frame() % config_.max_delta_depth == 0U;
    case PersistentCheckpointPolicy::Geometric:
        return std::has_single_bit(state.frame())
            || state.frame() % config_.max_delta_depth == 0U;
    case PersistentCheckpointPolicy::Adaptive: {
        const std::uint64_t density = state.body_count() == 0U
            ? 0U
            : dirty.changed_count() * 1'000'000ULL / state.body_count();
        return density >= config_.adaptive_density_ppm;
    }
    }
    return true;
}

void PersistentCheckpointHistory::capture(
    const ImmutableWorldState& state,
    const DirtySet& dirty) {
    if (dirty.entity_count() != state.body_count()) {
        throw std::invalid_argument("Persistent checkpoint dirty set does not match world");
    }
    if (!entries_.empty() && state.frame() != entries_.back().frame + 1U) {
        throw std::invalid_argument("Persistent checkpoint frames must be consecutive");
    }

    Entry entry;
    entry.frame = state.frame();
    if (should_checkpoint(state, dirty)) {
        entry.checkpoint = state;
        delta_depth_ = 0U;
    } else {
        entry.indices.reserve(dirty.changed_count());
        entry.bodies.reserve(dirty.changed_count());
        dirty.for_each_dirty([&](std::size_t index, std::uint8_t) {
            entry.indices.push_back(index);
            entry.bodies.push_back(state.body_at(index));
        });
        ++delta_depth_;
    }
    entries_.push_back(std::move(entry));
    enforce_capacity();
}

std::size_t PersistentCheckpointHistory::entry_index(std::uint64_t frame) const {
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (entries_[index].frame == frame) return index;
    }
    throw std::out_of_range("Persistent checkpoint frame is not retained");
}

ImmutableWorldState PersistentCheckpointHistory::restore(std::uint64_t frame) const {
    const std::size_t target = entry_index(frame);
    std::size_t checkpoint = target;
    while (checkpoint > 0U && !entries_[checkpoint].checkpoint.has_value()) --checkpoint;
    if (!entries_[checkpoint].checkpoint.has_value()) {
        throw std::logic_error("Persistent history has no checkpoint base");
    }

    ImmutableWorldState state = *entries_[checkpoint].checkpoint;
    for (std::size_t index = checkpoint + 1U; index <= target; ++index) {
        const Entry& entry = entries_[index];
        if (entry.checkpoint.has_value()) {
            state = *entry.checkpoint;
        } else {
            state = apply_immutable_updates(
                state, entry.frame, entry.indices, entry.bodies, nullptr);
            ++restore_deltas_applied_;
        }
    }
    return state;
}

bool PersistentCheckpointHistory::contains(std::uint64_t frame) const noexcept {
    return std::any_of(entries_.begin(), entries_.end(), [frame](const Entry& entry) {
        return entry.frame == frame;
    });
}

void PersistentCheckpointHistory::truncate_after(std::uint64_t frame) {
    while (!entries_.empty() && entries_.back().frame > frame) entries_.pop_back();
    delta_depth_ = 0U;
    for (auto iterator = entries_.rbegin(); iterator != entries_.rend(); ++iterator) {
        if (iterator->checkpoint.has_value()) break;
        ++delta_depth_;
    }
}

void PersistentCheckpointHistory::enforce_capacity() {
    while (entries_.size() > config_.capacity) {
        if (entries_.size() >= 2U && !entries_[1].checkpoint.has_value()) {
            const std::uint64_t promoted_frame = entries_[1].frame;
            ImmutableWorldState promoted = restore(promoted_frame);
            entries_[1].checkpoint = std::move(promoted);
            entries_[1].indices.clear();
            entries_[1].bodies.clear();
        }
        entries_.pop_front();
    }
}

PersistentCheckpointStats PersistentCheckpointHistory::stats() const {
    PersistentCheckpointStats result;
    std::vector<ImmutableWorldState> checkpoints;
    for (const Entry& entry : entries_) {
        if (entry.checkpoint.has_value()) {
            ++result.checkpoint_frames;
            checkpoints.push_back(*entry.checkpoint);
        } else {
            ++result.delta_frames;
            result.delta_bodies_stored += entry.bodies.size();
            result.live_delta_payload_bytes += entry.indices.capacity() * sizeof(std::size_t)
                + entry.bodies.capacity() * sizeof(Body);
        }
    }
    result.restore_deltas_applied = restore_deltas_applied_;
    result.retained_frames = entries_.size();
    result.checkpoint_memory = estimate_retained_immutable_memory(checkpoints);
    return result;
}

} // namespace neoeng::core
