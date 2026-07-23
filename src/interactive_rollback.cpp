#include "neoeng/core/interactive_rollback.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace neoeng::core {
namespace {

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

} // namespace

InteractiveRollbackEngine::InteractiveRollbackEngine(
    WorldState initial,
    InteractiveRollbackConfig config)
    : config_(config),
      versions_(config.capacity),
      arena_(config.capacity, config.arena_bytes_per_epoch) {
    if (config.capacity == 0U || config.page_size == 0U) {
        throw std::invalid_argument("Interactive rollback dimensions must be positive");
    }
    ComponentWorldState component = make_component_world(initial, config.page_size);
    TemporalBroadphaseState broadphase = make_temporal_broadphase(
        component, config.contacts, config.temporal);
    Version initial_version{
        .state = std::move(component),
        .active = DeterministicActiveSet::from_world(initial),
        .broadphase = std::move(broadphase),
        .manifold = PersistentManifoldState{.frame = initial.frame, .contacts = {}},
        .inputs_from_previous = {},
        .contact_count = 0U,
    };
    versions_.capture(initial.frame, std::move(initial_version));
}

const InteractiveRollbackEngine::Version&
InteractiveRollbackEngine::current_version() const noexcept {
    return versions_.at(*versions_.latest_frame());
}

const ComponentWorldState& InteractiveRollbackEngine::state() const noexcept {
    return current_version().state;
}

const DeterministicActiveSet& InteractiveRollbackEngine::active_set() const noexcept {
    return current_version().active;
}

WorldState InteractiveRollbackEngine::materialized_state() const {
    return current_version().state.materialize();
}

InteractiveRollbackEngine::StoredInputs InteractiveRollbackEngine::store_inputs(
    std::uint64_t destination_frame,
    std::span<const InputCommand> inputs) {
    const std::vector<InputCommand> canonical = canonicalize_inputs(inputs);
    arena_.begin_epoch(destination_frame);
    if (canonical.empty()) return {};
    auto storage = arena_.allocate_array<InputCommand>(canonical.size());
    std::copy(canonical.begin(), canonical.end(), storage.begin());
    return StoredInputs{.data = storage.data(), .size = storage.size()};
}

void InteractiveRollbackEngine::accumulate(
    ContactSolverStats& target,
    const ContactSolverStats& source) noexcept {
    target.bodies_scanned += source.bodies_scanned;
    target.cell_entries += source.cell_entries;
    target.candidate_pairs += source.candidate_pairs;
    target.narrowphase_tests += source.narrowphase_tests;
    target.swept_hits += source.swept_hits;
    target.initial_overlaps += source.initial_overlaps;
    target.final_overlaps += source.final_overlaps;
    target.constraint_islands += source.constraint_islands;
    target.resting_islands_skipped += source.resting_islands_skipped;
    target.graph_colors += source.graph_colors;
    target.velocity_resolutions += source.velocity_resolutions;
    target.position_projections += source.position_projections;
    target.fallback_all_pairs += source.fallback_all_pairs;
    target.chain_solver_attempts += source.chain_solver_attempts;
    target.chain_solver_accepts += source.chain_solver_accepts;
    target.chain_solver_fallbacks += source.chain_solver_fallbacks;
    target.chain_bodies_solved += source.chain_bodies_solved;
    target.isotonic_blocks += source.isotonic_blocks;
    target.warm_start_attempts += source.warm_start_attempts;
    target.warm_start_accepts += source.warm_start_accepts;
    target.warm_start_rejects += source.warm_start_rejects;
    {
        const WideInteger sum = static_cast<WideInteger>(target.momentum_rounding_error_raw)
            + static_cast<WideInteger>(source.momentum_rounding_error_raw);
        constexpr WideInteger minimum = static_cast<WideInteger>(
            std::numeric_limits<std::int64_t>::min());
        constexpr WideInteger maximum = static_cast<WideInteger>(
            std::numeric_limits<std::int64_t>::max());
        target.momentum_rounding_error_raw = static_cast<std::int64_t>(
            sum < minimum ? minimum : sum > maximum ? maximum : sum);
    }
    target.integration_allocation += source.integration_allocation;
    target.solver_allocation += source.solver_allocation;
}

void InteractiveRollbackEngine::accumulate(
    TemporalBroadphaseStats& target,
    const TemporalBroadphaseStats& source) noexcept {
    target.pair_cache_builds += source.pair_cache_builds;
    target.pair_cache_incremental_updates += source.pair_cache_incremental_updates;
    target.pair_cache_reuses += source.pair_cache_reuses;
    target.escaped_bodies += source.escaped_bodies;
    target.fat_bounds_tested += source.fat_bounds_tested;
    target.fat_pair_tests += source.fat_pair_tests;
    target.cached_pairs = source.cached_pairs;
    target.manifold_pairs_reused += source.manifold_pairs_reused;
}

void InteractiveRollbackEngine::advance_internal(std::span<const InputCommand> inputs) {
    ContactStepResult result;
    TemporalBroadphaseState next_broadphase;
    PersistentManifoldState next_manifold;
    TemporalBroadphaseStats temporal_stats;
    const Version& current = current_version();
    if (config_.use_temporal_cache) {
        TemporalContactStepResult temporal_result = step_component_contacts_temporal(
            current.state, current.active, current.broadphase, current.manifold,
            inputs, config_.contacts, config_.temporal, config_.step_options);
        result = std::move(temporal_result.contact);
        next_broadphase = std::move(temporal_result.broadphase);
        next_manifold = std::move(temporal_result.manifold);
        temporal_stats = temporal_result.temporal_stats;
    } else {
        result = step_component_contacts(
            current.state, current.active, inputs,
            config_.contacts, config_.step_options,
            &current.manifold, &next_manifold);
        next_broadphase = make_temporal_broadphase(
            result.state, config_.contacts, config_.temporal, &temporal_stats);
    }
    const StoredInputs stored = store_inputs(result.state.frame(), inputs);
    Version next{
        .state = std::move(result.state),
        .active = std::move(result.active),
        .broadphase = std::move(next_broadphase),
        .manifold = std::move(next_manifold),
        .inputs_from_previous = stored,
        .contact_count = result.contacts.size(),
    };
    ++frames_simulated_;
    contacts_solved_ += next.contact_count;
    accumulate(cumulative_solver_, result.stats);
    accumulate(cumulative_temporal_, temporal_stats);
    const std::uint64_t destination_frame = next.state.frame();
    versions_.capture(destination_frame, std::move(next));
}

void InteractiveRollbackEngine::advance(std::span<const InputCommand> inputs) {
    advance_internal(inputs);
}

std::size_t InteractiveRollbackEngine::correct_input_and_resimulate(
    std::uint64_t input_frame,
    std::span<const InputCommand> corrected_inputs) {
    const auto latest = versions_.latest_frame();
    if (!latest.has_value() || input_frame >= *latest || !versions_.contains(input_frame)) {
        throw std::out_of_range("Interactive rollback input frame is not correctable");
    }

    std::vector<std::vector<InputCommand>> history;
    history.reserve(static_cast<std::size_t>(*latest - input_frame));
    for (std::uint64_t destination = input_frame + 1U; destination <= *latest; ++destination) {
        const StoredInputs stored = versions_.at(destination).inputs_from_previous;
        if (stored.size == 0U) history.emplace_back();
        else history.emplace_back(stored.data, stored.data + stored.size);
    }
    history.front() = canonicalize_inputs(corrected_inputs);

    versions_.truncate_after(input_frame);
    for (const std::vector<InputCommand>& inputs : history) {
        advance_internal(inputs);
    }
    rollback_frames_resimulated_ += history.size();
    return history.size();
}

InteractiveRollbackStats InteractiveRollbackEngine::stats() const noexcept {
    return InteractiveRollbackStats{
        .retained_frames = versions_.size(),
        .active_bodies = current_version().active.size(),
        .frames_simulated = frames_simulated_,
        .contacts_solved = contacts_solved_,
        .rollback_frames_resimulated = rollback_frames_resimulated_,
        .cumulative_solver = cumulative_solver_,
        .cumulative_temporal = cumulative_temporal_,
        .arena = arena_.stats(),
    };
}

} // namespace neoeng::core
