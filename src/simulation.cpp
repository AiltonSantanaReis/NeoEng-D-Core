#include "neoeng/core/simulation.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace neoeng::core {

void validate_world(const WorldState& state) {
    if (!std::is_sorted(state.bodies.begin(), state.bodies.end(),
            [](const Body& lhs, const Body& rhs) { return lhs.id < rhs.id; })) {
        throw std::invalid_argument("World bodies must be sorted by EntityId");
    }
    const auto duplicate = std::adjacent_find(state.bodies.begin(), state.bodies.end(),
        [](const Body& lhs, const Body& rhs) { return lhs.id == rhs.id; });
    if (duplicate != state.bodies.end()) {
        throw std::invalid_argument("World contains duplicate EntityId");
    }
}

StepResult step_with_dirty(const WorldState& current, std::span<const InputCommand> inputs) {
    validate_world(current);

    std::vector<InputCommand> canonical_inputs(inputs.begin(), inputs.end());
    std::sort(canonical_inputs.begin(), canonical_inputs.end(),
        [](const InputCommand& lhs, const InputCommand& rhs) {
            if (lhs.entity != rhs.entity) {
                return lhs.entity < rhs.entity;
            }
            if (lhs.acceleration.x != rhs.acceleration.x) {
                return lhs.acceleration.x < rhs.acceleration.x;
            }
            return lhs.acceleration.y < rhs.acceleration.y;
        });

    StepResult result{.state = current, .dirty = DirtySet(current.bodies.size())};
    result.state.frame = current.frame + 1U;

    std::size_t input_index = 0;
    for (std::size_t body_index = 0; body_index < result.state.bodies.size(); ++body_index) {
        Body& body = result.state.bodies[body_index];
        const Body before = body;
        Vec2 total_acceleration{};
        while (input_index < canonical_inputs.size()
               && canonical_inputs[input_index].entity < body.id) {
            ++input_index;
        }
        std::size_t cursor = input_index;
        while (cursor < canonical_inputs.size()
               && canonical_inputs[cursor].entity == body.id) {
            total_acceleration.x += canonical_inputs[cursor].acceleration.x;
            total_acceleration.y += canonical_inputs[cursor].acceleration.y;
            ++cursor;
        }
        input_index = cursor;

        body.velocity.x += total_acceleration.x * kSimulationDelta;
        body.velocity.y += total_acceleration.y * kSimulationDelta;
        body.position.x += body.velocity.x * kSimulationDelta;
        body.position.y += body.velocity.y * kSimulationDelta;

        if (body.position.x != before.position.x) {
            result.dirty.mark(body_index, DirtyComponent::PositionX);
        }
        if (body.position.y != before.position.y) {
            result.dirty.mark(body_index, DirtyComponent::PositionY);
        }
        if (body.velocity.x != before.velocity.x) {
            result.dirty.mark(body_index, DirtyComponent::VelocityX);
        }
        if (body.velocity.y != before.velocity.y) {
            result.dirty.mark(body_index, DirtyComponent::VelocityY);
        }
    }

    return result;
}

WorldState step(const WorldState& current, std::span<const InputCommand> inputs) {
    return step_with_dirty(current, inputs).state;
}

bool dirty_set_describes_transition(
    const WorldState& previous, const WorldState& next, const DirtySet& dirty) noexcept {
    if (previous.bodies.size() != next.bodies.size()
        || dirty.entity_count() != next.bodies.size()) {
        return false;
    }
    for (std::size_t index = 0; index < next.bodies.size(); ++index) {
        const Body& lhs = previous.bodies[index];
        const Body& rhs = next.bodies[index];
        const std::uint8_t expected =
            (lhs.id != rhs.id ? component_mask(DirtyComponent::Identity) : 0U)
            | (lhs.position.x != rhs.position.x ? component_mask(DirtyComponent::PositionX) : 0U)
            | (lhs.position.y != rhs.position.y ? component_mask(DirtyComponent::PositionY) : 0U)
            | (lhs.velocity.x != rhs.velocity.x ? component_mask(DirtyComponent::VelocityX) : 0U)
            | (lhs.velocity.y != rhs.velocity.y ? component_mask(DirtyComponent::VelocityY) : 0U);
        if (dirty.mask(index) != expected) {
            return false;
        }
    }
    return true;
}

} // namespace neoeng::core
