#include "neoeng/core/active_world.hpp"

#include "neoeng/core/simulation.hpp"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace neoeng::core {
namespace {

[[nodiscard]] bool is_nonzero(Fixed value) noexcept {
    return value.raw() != 0;
}

[[nodiscard]] bool body_is_active(const Body& body) noexcept {
    return is_nonzero(body.velocity.x) || is_nonzero(body.velocity.y);
}

[[nodiscard]] std::vector<InputCommand> canonicalize_inputs(
    std::span<const InputCommand> inputs) {
    std::vector<InputCommand> canonical(inputs.begin(), inputs.end());
    std::sort(canonical.begin(), canonical.end(), [](const InputCommand& lhs, const InputCommand& rhs) {
        if (lhs.entity != rhs.entity) return lhs.entity < rhs.entity;
        if (lhs.acceleration.x != rhs.acceleration.x) return lhs.acceleration.x < rhs.acceleration.x;
        return lhs.acceleration.y < rhs.acceleration.y;
    });
    return canonical;
}

[[nodiscard]] std::size_t find_entity_index(
    const ImmutableWorldState& state,
    EntityId entity,
    std::uint64_t& probes) {
    std::size_t first = 0U;
    std::size_t count = state.body_count();
    while (count != 0U) {
        const std::size_t step = count / 2U;
        const std::size_t middle = first + step;
        const Body body = state.body_at(middle);
        ++probes;
        if (body.id < entity) {
            first = middle + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    if (first < state.body_count()) {
        const Body body = state.body_at(first);
        ++probes;
        if (body.id == entity) return first;
    }
    return state.body_count();
}

void accumulate(ActiveStepStats& destination, const ActiveStepStats& source) noexcept {
    destination.candidate_bodies_scanned += source.candidate_bodies_scanned;
    destination.inactive_bodies_skipped += source.inactive_bodies_skipped;
    destination.entity_binary_search_probes += source.entity_binary_search_probes;
    destination.input_commands_consumed += source.input_commands_consumed;
    destination.immutable_allocation += source.immutable_allocation;
}

} // namespace

DeterministicActiveSet::DeterministicActiveSet(std::vector<std::size_t> sorted_indices)
    : indices_(std::move(sorted_indices)) {
    if (!std::is_sorted(indices_.begin(), indices_.end())) {
        throw std::invalid_argument("Active-set indices must be sorted");
    }
    if (std::adjacent_find(indices_.begin(), indices_.end()) != indices_.end()) {
        throw std::invalid_argument("Active-set indices must be unique");
    }
}

DeterministicActiveSet DeterministicActiveSet::from_world(const WorldState& state) {
    validate_world(state);
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < state.bodies.size(); ++index) {
        if (body_is_active(state.bodies[index])) indices.push_back(index);
    }
    return DeterministicActiveSet(std::move(indices));
}

DeterministicActiveSet DeterministicActiveSet::from_immutable(const ImmutableWorldState& state) {
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < state.body_count(); ++index) {
        if (body_is_active(state.body_at(index))) indices.push_back(index);
    }
    return DeterministicActiveSet(std::move(indices));
}

bool DeterministicActiveSet::contains(std::size_t index) const noexcept {
    return std::binary_search(indices_.begin(), indices_.end(), index);
}

ActiveImmutableStepResult step_immutable_active(
    const ImmutableWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs) {
    if (current.empty()) {
        throw std::invalid_argument("Active immutable step requires an initialized world");
    }
    if (!active.indices().empty() && active.indices().back() >= current.body_count()) {
        throw std::invalid_argument("Active set references a body outside the world");
    }

    const std::vector<InputCommand> canonical = canonicalize_inputs(inputs);
    std::vector<std::size_t> input_indices;
    input_indices.reserve(canonical.size());
    std::size_t cursor = 0U;
    ActiveStepStats stats;
    while (cursor < canonical.size()) {
        const EntityId entity = canonical[cursor].entity;
        const std::size_t index = find_entity_index(
            current, entity, stats.entity_binary_search_probes);
        if (index != current.body_count()) input_indices.push_back(index);
        while (cursor < canonical.size() && canonical[cursor].entity == entity) {
            ++stats.input_commands_consumed;
            ++cursor;
        }
    }
    std::sort(input_indices.begin(), input_indices.end());
    input_indices.erase(std::unique(input_indices.begin(), input_indices.end()), input_indices.end());

    std::vector<std::size_t> candidates;
    candidates.reserve(active.size() + input_indices.size());
    std::set_union(
        active.indices().begin(), active.indices().end(),
        input_indices.begin(), input_indices.end(),
        std::back_inserter(candidates));

    std::vector<std::size_t> changed_indices;
    std::vector<Body> changed_bodies;
    std::vector<std::size_t> next_active;
    changed_indices.reserve(candidates.size());
    changed_bodies.reserve(candidates.size());
    next_active.reserve(candidates.size());
    DirtySet dirty(current.body_count());

    std::size_t input_cursor = 0U;
    for (const std::size_t index : candidates) {
        const Body before = current.body_at(index);
        ++stats.candidate_bodies_scanned;
        while (input_cursor < canonical.size() && canonical[input_cursor].entity < before.id) {
            ++input_cursor;
        }
        std::size_t command_cursor = input_cursor;
        Vec2 total_acceleration{};
        while (command_cursor < canonical.size() && canonical[command_cursor].entity == before.id) {
            total_acceleration.x += canonical[command_cursor].acceleration.x;
            total_acceleration.y += canonical[command_cursor].acceleration.y;
            ++command_cursor;
        }
        input_cursor = command_cursor;

        Body after = before;
        after.velocity.x += total_acceleration.x * kSimulationDelta;
        after.velocity.y += total_acceleration.y * kSimulationDelta;
        after.position.x += after.velocity.x * kSimulationDelta;
        after.position.y += after.velocity.y * kSimulationDelta;

        std::uint8_t mask = 0U;
        if (after.position.x != before.position.x) mask |= component_mask(DirtyComponent::PositionX);
        if (after.position.y != before.position.y) mask |= component_mask(DirtyComponent::PositionY);
        if (after.velocity.x != before.velocity.x) mask |= component_mask(DirtyComponent::VelocityX);
        if (after.velocity.y != before.velocity.y) mask |= component_mask(DirtyComponent::VelocityY);
        if (mask != 0U) {
            changed_indices.push_back(index);
            changed_bodies.push_back(after);
            dirty.mark(index, static_cast<DirtyComponent>(mask));
        }
        if (body_is_active(after)) next_active.push_back(index);
    }

    stats.inactive_bodies_skipped = current.body_count() - candidates.size();
    ImmutableWorldState next = apply_immutable_updates(
        current, current.frame() + 1U, changed_indices, changed_bodies,
        &stats.immutable_allocation);
    return ActiveImmutableStepResult{
        .state = std::move(next),
        .active = DeterministicActiveSet(std::move(next_active)),
        .dirty = std::move(dirty),
        .stats = stats,
    };
}

ActiveRollbackEngine::ActiveRollbackEngine(
    WorldState initial,
    std::size_t snapshot_capacity,
    std::size_t chunk_size)
    : capacity_(snapshot_capacity) {
    if (capacity_ == 0U) {
        throw std::invalid_argument("Active rollback capacity must be greater than zero");
    }
    current_.active = DeterministicActiveSet::from_world(initial);
    current_.state = make_immutable_world(initial, chunk_size,
        &cumulative_step_.immutable_allocation);
    retain(current_);
}

void ActiveRollbackEngine::retain(Version version) {
    if (!snapshots_.empty() && version.state.frame() != snapshots_.back().state.frame() + 1U) {
        throw std::invalid_argument("Active rollback versions must be consecutive");
    }
    snapshots_.push_back(std::move(version));
    while (snapshots_.size() > capacity_) snapshots_.pop_front();
}

void ActiveRollbackEngine::advance(std::span<const InputCommand> inputs) {
    input_history_[current_.state.frame()] = std::vector<InputCommand>(inputs.begin(), inputs.end());
    ActiveImmutableStepResult result = step_immutable_active(current_.state, current_.active, inputs);
    accumulate(cumulative_step_, result.stats);
    current_ = Version{.state = std::move(result.state), .active = std::move(result.active)};
    retain(current_);
}

bool ActiveRollbackEngine::contains(std::uint64_t frame) const noexcept {
    return std::any_of(snapshots_.begin(), snapshots_.end(), [frame](const Version& version) {
        return version.state.frame() == frame;
    });
}

ActiveRollbackEngine::Version ActiveRollbackEngine::restore(std::uint64_t frame) const {
    const auto iterator = std::find_if(snapshots_.rbegin(), snapshots_.rend(),
        [frame](const Version& version) { return version.state.frame() == frame; });
    if (iterator == snapshots_.rend()) {
        throw std::out_of_range("Active rollback frame is not retained");
    }
    return *iterator;
}

void ActiveRollbackEngine::truncate_after(std::uint64_t frame) {
    while (!snapshots_.empty() && snapshots_.back().state.frame() > frame) snapshots_.pop_back();
}

std::size_t ActiveRollbackEngine::correct_input_and_resimulate(
    std::uint64_t input_frame,
    std::span<const InputCommand> corrected_inputs) {
    const std::uint64_t target_frame = current_.state.frame();
    if (input_frame >= target_frame) {
        throw std::out_of_range("Correction frame must precede the active current frame");
    }
    Version restored = restore(input_frame);
    input_history_[input_frame] = std::vector<InputCommand>(
        corrected_inputs.begin(), corrected_inputs.end());
    truncate_after(input_frame);

    std::size_t resimulated = 0U;
    while (restored.state.frame() < target_frame) {
        const auto history = input_history_.find(restored.state.frame());
        const std::span<const InputCommand> frame_inputs = history == input_history_.end()
            ? std::span<const InputCommand>{}
            : std::span<const InputCommand>{history->second};
        ActiveImmutableStepResult result = step_immutable_active(
            restored.state, restored.active, frame_inputs);
        accumulate(cumulative_step_, result.stats);
        restored = Version{.state = std::move(result.state), .active = std::move(result.active)};
        retain(restored);
        ++resimulated;
    }
    current_ = std::move(restored);
    return resimulated;
}

ActiveRollbackStats ActiveRollbackEngine::stats() const {
    std::vector<ImmutableWorldState> retained;
    retained.reserve(snapshots_.size());
    for (const Version& version : snapshots_) retained.push_back(version.state);
    return ActiveRollbackStats{
        .cumulative_step = cumulative_step_,
        .retained_memory = estimate_retained_immutable_memory(retained),
        .retained_frames = snapshots_.size(),
        .current_active_bodies = current_.active.size(),
        .capacity = capacity_,
    };
}

} // namespace neoeng::core
