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

// Fundamental transition rejection contract:
// - current.frame == UINT64_MAX throws std::overflow_error with
//   "World frame maximum reached";
// - any InputCommand whose EntityId is absent from current.bodies throws
//   std::out_of_range with "Input references unknown EntityId";
// - rejected transitions do not modify current and produce no partial result.
// Host SDK translation of these C++ classes is specified separately.
[[nodiscard]] StepResult step_with_dirty(
    const WorldState& current, std::span<const InputCommand> inputs);
[[nodiscard]] WorldState step(const WorldState& current, std::span<const InputCommand> inputs);
[[nodiscard]] bool dirty_set_describes_transition(
    const WorldState& previous, const WorldState& next, const DirtySet& dirty) noexcept;

} // namespace neoeng::core
