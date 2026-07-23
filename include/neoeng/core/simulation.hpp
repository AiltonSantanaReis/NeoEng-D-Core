#pragma once

#include "neoeng/core/dirty.hpp"
#include "neoeng/core/types.hpp"

#include <span>

namespace neoeng::core {

struct StepResult final {
    WorldState state{};
    DirtySet dirty{};
};

void validate_world(const WorldState& state);
[[nodiscard]] StepResult step_with_dirty(
    const WorldState& current, std::span<const InputCommand> inputs);
[[nodiscard]] WorldState step(const WorldState& current, std::span<const InputCommand> inputs);
[[nodiscard]] bool dirty_set_describes_transition(
    const WorldState& previous, const WorldState& next, const DirtySet& dirty) noexcept;

} // namespace neoeng::core
