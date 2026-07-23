#pragma once

#include "neoeng/core/contact_solver.hpp"

#include <optional>

namespace neoeng::core {

[[nodiscard]] std::optional<ContactStepResult> solve_chain_contacts_isotonic(
    const ComponentWorldState& current,
    ComponentStepResult integrated,
    std::vector<SweptContact> contacts,
    ContactSolverConfig config,
    ContactSolverStats stats,
    const PersistentManifoldState* previous_manifold,
    PersistentManifoldState* next_manifold);

[[nodiscard]] std::optional<ContactStepResult> step_component_contacts_chain_fused(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const BroadphasePair> pairs,
    ContactSolverConfig config,
    const PersistentManifoldState* previous_manifold,
    PersistentManifoldState* next_manifold);

} // namespace neoeng::core
