#include "neoeng/core/temporal_contact.hpp"
#include "neoeng/core/chain_solver.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace neoeng::core {
namespace {

[[nodiscard]] Fixed fixed_from_wide(WideInteger value) {
    constexpr auto minimum = static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::min());
    constexpr auto maximum = static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::max());
    if (value < minimum || value > maximum) {
        throw std::overflow_error("Temporal broadphase fixed-point range overflow");
    }
    return Fixed::from_raw(static_cast<Fixed::rep>(value));
}

[[nodiscard]] WideInteger scaled_displacement_raw(Fixed velocity, std::size_t frames) {
    const WideInteger one_frame = static_cast<WideInteger>(velocity.raw())
        * static_cast<WideInteger>(kSimulationDelta.raw())
        / static_cast<WideInteger>(Fixed::scale);
    return one_frame * static_cast<WideInteger>(frames);
}

[[nodiscard]] FatAabb make_fat_bound_frames(
    const ComponentWorldState& state,
    std::size_t index,
    ContactSolverConfig contacts,
    std::size_t horizon_frames) {
    const WideInteger x = state.position_x_at(index).raw();
    const WideInteger y = state.position_y_at(index).raw();
    const WideInteger future_x = x
        + scaled_displacement_raw(state.velocity_x_at(index), horizon_frames);
    const WideInteger future_y = y
        + scaled_displacement_raw(state.velocity_y_at(index), horizon_frames);
    const WideInteger half = contacts.half_extent.raw();
    return FatAabb{
        .minimum_x = fixed_from_wide(std::min(x, future_x) - half),
        .maximum_x = fixed_from_wide(std::max(x, future_x) + half),
        .minimum_y = fixed_from_wide(std::min(y, future_y) - half),
        .maximum_y = fixed_from_wide(std::max(y, future_y) + half),
    };
}

[[nodiscard]] FatAabb make_fat_bound(
    const ComponentWorldState& state,
    std::size_t index,
    ContactSolverConfig contacts,
    TemporalBroadphaseConfig temporal) {
    return make_fat_bound_frames(state, index, contacts, temporal.horizon_frames);
}

[[nodiscard]] FatAabb make_fat_bound_transition(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    std::size_t index,
    ContactSolverConfig contacts,
    std::size_t future_frames) {
    const WideInteger current_x = current.position_x_at(index).raw();
    const WideInteger current_y = current.position_y_at(index).raw();
    const WideInteger predicted_x = predicted.position_x_at(index).raw();
    const WideInteger predicted_y = predicted.position_y_at(index).raw();
    const WideInteger future_x = predicted_x
        + scaled_displacement_raw(predicted.velocity_x_at(index), future_frames);
    const WideInteger future_y = predicted_y
        + scaled_displacement_raw(predicted.velocity_y_at(index), future_frames);
    const WideInteger half = contacts.half_extent.raw();
    return FatAabb{
        .minimum_x = fixed_from_wide(std::min({current_x, predicted_x, future_x}) - half),
        .maximum_x = fixed_from_wide(std::max({current_x, predicted_x, future_x}) + half),
        .minimum_y = fixed_from_wide(std::min({current_y, predicted_y, future_y}) - half),
        .maximum_y = fixed_from_wide(std::max({current_y, predicted_y, future_y}) + half),
    };
}

[[nodiscard]] bool contains_bound(const FatAabb& outer, const FatAabb& inner) noexcept {
    return inner.minimum_x >= outer.minimum_x
        && inner.maximum_x <= outer.maximum_x
        && inner.minimum_y >= outer.minimum_y
        && inner.maximum_y <= outer.maximum_y;
}

[[nodiscard]] bool overlaps(const FatAabb& first, const FatAabb& second) noexcept {
    return first.minimum_x <= second.maximum_x
        && second.minimum_x <= first.maximum_x
        && first.minimum_y <= second.maximum_y
        && second.minimum_y <= first.maximum_y;
}

[[nodiscard]] bool contains_sweep(
    const FatAabb& bound,
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    std::size_t index,
    Fixed half_extent) {
    const WideInteger minimum_x = std::min<WideInteger>(
        current.position_x_at(index).raw(), predicted.position_x_at(index).raw())
        - half_extent.raw();
    const WideInteger maximum_x = std::max<WideInteger>(
        current.position_x_at(index).raw(), predicted.position_x_at(index).raw())
        + half_extent.raw();
    const WideInteger minimum_y = std::min<WideInteger>(
        current.position_y_at(index).raw(), predicted.position_y_at(index).raw())
        - half_extent.raw();
    const WideInteger maximum_y = std::max<WideInteger>(
        current.position_y_at(index).raw(), predicted.position_y_at(index).raw())
        + half_extent.raw();
    return minimum_x >= static_cast<WideInteger>(bound.minimum_x.raw())
        && maximum_x <= static_cast<WideInteger>(bound.maximum_x.raw())
        && minimum_y >= static_cast<WideInteger>(bound.minimum_y.raw())
        && maximum_y <= static_cast<WideInteger>(bound.maximum_y.raw());
}

[[nodiscard]] std::vector<BroadphasePair> build_pairs(
    std::span<const FatAabb> bounds,
    TemporalBroadphaseStats& stats) {
    std::vector<std::size_t> order(bounds.size());
    std::iota(order.begin(), order.end(), 0U);
    std::sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) {
        if (bounds[lhs].minimum_x != bounds[rhs].minimum_x) {
            return bounds[lhs].minimum_x < bounds[rhs].minimum_x;
        }
        return lhs < rhs;
    });

    std::vector<BroadphasePair> pairs;
    for (std::size_t left = 0U; left < order.size(); ++left) {
        const std::size_t first = order[left];
        for (std::size_t right = left + 1U; right < order.size(); ++right) {
            const std::size_t second = order[right];
            if (bounds[second].minimum_x > bounds[first].maximum_x) break;
            ++stats.fat_pair_tests;
            if (!overlaps(bounds[first], bounds[second])) continue;
            pairs.push_back(BroadphasePair{
                .first = std::min(first, second),
                .second = std::max(first, second),
            });
        }
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    stats.cached_pairs = pairs.size();
    return pairs;
}

[[nodiscard]] TemporalBroadphaseState rebuild_from_state(
    const ComponentWorldState& state,
    ContactSolverConfig contacts,
    TemporalBroadphaseConfig temporal,
    TemporalBroadphaseStats& stats) {
    auto bounds = std::make_shared<std::vector<FatAabb>>();
    bounds->reserve(state.body_count());
    for (std::size_t index = 0U; index < state.body_count(); ++index) {
        bounds->push_back(make_fat_bound(state, index, contacts, temporal));
    }
    std::vector<BroadphasePair> pairs = build_pairs(*bounds, stats);
    ++stats.pair_cache_builds;
    if (state.frame() > std::numeric_limits<std::uint64_t>::max() - temporal.horizon_frames) {
        throw std::overflow_error("Temporal broadphase horizon would wrap the frame counter");
    }
    return TemporalBroadphaseState(
        state.frame(), state.body_count(), state.frame() + temporal.horizon_frames,
        std::move(bounds),
        std::make_shared<const std::vector<BroadphasePair>>(std::move(pairs)));
}

[[nodiscard]] TemporalBroadphaseState update_escaped_bounds(
    const ComponentWorldState& state,
    const TemporalBroadphaseState& previous,
    std::span<const std::size_t> escaped,
    ContactSolverConfig contacts,
    TemporalBroadphaseConfig temporal,
    TemporalBroadphaseStats& stats,
    const ComponentWorldState* sweep_start = nullptr) {
    if (escaped.empty()) {
        ++stats.pair_cache_reuses;
        stats.cached_pairs = previous.pairs().size();
        return TemporalBroadphaseState(
            state.frame(), state.body_count(), previous.valid_until_frame(),
            previous.shared_bounds(), previous.shared_pairs());
    }
    auto bounds = std::make_shared<std::vector<FatAabb>>(
        previous.bounds().begin(), previous.bounds().end());
    for (const std::size_t index : escaped) {
        (*bounds)[index] = sweep_start == nullptr
            ? make_fat_bound(state, index, contacts, temporal)
            : make_fat_bound_transition(
                *sweep_start, state, index, contacts, temporal.horizon_frames);
    }

    std::vector<BroadphasePair> pairs;
    if (escaped.size() <= temporal.incremental_body_limit) {
        std::vector<std::uint8_t> escaped_mask(state.body_count(), 0U);
        for (const std::size_t index : escaped) escaped_mask[index] = 1U;
        pairs.reserve(previous.pairs().size() + escaped.size() * 8U);
        for (const BroadphasePair& pair : previous.pairs()) {
            if (escaped_mask[pair.first] == 0U && escaped_mask[pair.second] == 0U) {
                pairs.push_back(pair);
            }
        }
        for (const std::size_t first : escaped) {
            for (std::size_t second = 0U; second < state.body_count(); ++second) {
                if (first == second) continue;
                if (escaped_mask[second] != 0U && second < first) continue;
                ++stats.fat_pair_tests;
                if (!overlaps((*bounds)[first], (*bounds)[second])) continue;
                pairs.push_back(BroadphasePair{
                    .first = std::min(first, second),
                    .second = std::max(first, second),
                });
            }
        }
        std::sort(pairs.begin(), pairs.end());
        pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
        ++stats.pair_cache_incremental_updates;
        stats.cached_pairs = pairs.size();
    } else {
        pairs = build_pairs(*bounds, stats);
        ++stats.pair_cache_builds;
    }
    return TemporalBroadphaseState(
        state.frame(), state.body_count(), previous.valid_until_frame(),
        std::move(bounds),
        std::make_shared<const std::vector<BroadphasePair>>(std::move(pairs)));
}

[[nodiscard]] TemporalBroadphaseState update_for_sweep(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    const DirtySet& dirty,
    const TemporalBroadphaseState& previous,
    ContactSolverConfig contacts,
    TemporalBroadphaseConfig temporal,
    TemporalBroadphaseStats& stats) {
    if (previous.frame() != current.frame()
        || previous.body_count() != current.body_count()) {
        throw std::invalid_argument("Temporal broadphase does not match the current state");
    }
    if (predicted.frame() > previous.valid_until_frame()) {
        TemporalBroadphaseState rebuilt = rebuild_from_state(
            current, contacts, temporal, stats);
        return TemporalBroadphaseState(
            predicted.frame(), predicted.body_count(), rebuilt.valid_until_frame(),
            rebuilt.shared_bounds(), rebuilt.shared_pairs());
    }

    std::vector<std::size_t> escaped;
    dirty.for_each_dirty([&](std::size_t index, std::uint8_t mask) {
        const std::uint8_t velocity_mask = component_mask(DirtyComponent::VelocityX)
            | component_mask(DirtyComponent::VelocityY);
        if ((mask & velocity_mask) == 0U) return;
        ++stats.fat_bounds_tested;
        if (!contains_sweep(previous.bounds()[index], current, predicted,
                index, contacts.half_extent)) {
            escaped.push_back(index);
        }
    });
    std::sort(escaped.begin(), escaped.end());
    escaped.erase(std::unique(escaped.begin(), escaped.end()), escaped.end());
    stats.escaped_bodies += escaped.size();
    return update_escaped_bounds(
        predicted, previous, escaped, contacts, temporal, stats, &current);
}

[[nodiscard]] TemporalBroadphaseState refresh_after_solver(
    const ComponentWorldState& solved,
    const TemporalBroadphaseState& previous,
    std::span<const SweptContact> contacts_found,
    const DirtySet& dirty,
    ContactSolverConfig contacts,
    TemporalBroadphaseConfig temporal,
    TemporalBroadphaseStats& stats) {
    if (solved.frame() > previous.valid_until_frame()) {
        return rebuild_from_state(solved, contacts, temporal, stats);
    }
    std::vector<std::uint8_t> suspect_mask(solved.body_count(), 0U);
    for (const SweptContact& contact : contacts_found) {
        suspect_mask[contact.first] = 1U;
        suspect_mask[contact.second] = 1U;
    }
    dirty.for_each_dirty([&](std::size_t index, std::uint8_t mask) {
        const std::uint8_t velocity_mask = component_mask(DirtyComponent::VelocityX)
            | component_mask(DirtyComponent::VelocityY);
        if ((mask & velocity_mask) != 0U) suspect_mask[index] = 1U;
    });

    const std::size_t remaining = static_cast<std::size_t>(
        previous.valid_until_frame() - solved.frame());
    std::vector<std::size_t> escaped;
    escaped.reserve(dirty.changed_count());
    for (std::size_t index = 0U; index < suspect_mask.size(); ++index) {
        if (suspect_mask[index] == 0U) continue;
        ++stats.fat_bounds_tested;
        const FatAabb required = make_fat_bound_frames(
            solved, index, contacts, remaining);
        if (!contains_bound(previous.bounds()[index], required)) {
            escaped.push_back(index);
        }
    }
    stats.escaped_bodies += escaped.size();
    return update_escaped_bounds(
        solved, previous, escaped, contacts, temporal, stats);
}

[[nodiscard]] std::uint64_t count_manifold_reuse(
    std::span<const SweptContact> previous,
    std::span<const SweptContact> current) {
    std::uint64_t reused = 0U;
    std::size_t lhs = 0U;
    std::size_t rhs = 0U;
    while (lhs < previous.size() && rhs < current.size()) {
        const BroadphasePair first{
            .first = previous[lhs].first,
            .second = previous[lhs].second,
        };
        const BroadphasePair second{
            .first = current[rhs].first,
            .second = current[rhs].second,
        };
        if (first < second) ++lhs;
        else if (second < first) ++rhs;
        else {
            ++reused;
            ++lhs;
            ++rhs;
        }
    }
    return reused;
}

} // namespace

TemporalBroadphaseState make_temporal_broadphase(
    const ComponentWorldState& state,
    ContactSolverConfig contacts,
    TemporalBroadphaseConfig temporal,
    TemporalBroadphaseStats* stats_output) {
    if (temporal.horizon_frames == 0U) {
        throw std::invalid_argument("Temporal broadphase horizon must be positive");
    }
    TemporalBroadphaseStats local;
    TemporalBroadphaseState result = rebuild_from_state(
        state, contacts, temporal, local);
    if (stats_output != nullptr) *stats_output = local;
    return result;
}

TemporalContactStepResult step_component_contacts_temporal(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    const TemporalBroadphaseState& broadphase,
    const PersistentManifoldState& manifold,
    std::span<const InputCommand> inputs,
    ContactSolverConfig contacts,
    TemporalBroadphaseConfig temporal,
    ComponentStepOptions options) {
    TemporalBroadphaseStats temporal_stats;
    if (inputs.empty() && !active.empty()
        && current.frame() < broadphase.valid_until_frame()) {
        if (contacts.connected_solver_mode != ConnectedContactSolverMode::GeneralColored) {
            PersistentManifoldState chain_manifold;
            const auto chain = step_component_contacts_chain_fused(
                current, active, broadphase.pairs(), contacts,
                &manifold, &chain_manifold);
            if (chain.has_value()) {
                ContactStepResult contact = *chain;
                TemporalBroadphaseState next_broadphase(
                    contact.state.frame(), contact.state.body_count(),
                    broadphase.valid_until_frame(),
                    broadphase.shared_bounds(), broadphase.shared_pairs());
                ++temporal_stats.pair_cache_reuses;
                temporal_stats.cached_pairs = broadphase.pairs().size();
                next_broadphase = refresh_after_solver(
                    contact.state, next_broadphase, contact.contacts, contact.dirty,
                    contacts, temporal, temporal_stats);
                temporal_stats.manifold_pairs_reused = count_manifold_reuse(
                    manifold.contacts, contact.contacts);
                return TemporalContactStepResult{
                    .contact = std::move(contact),
                    .broadphase = std::move(next_broadphase),
                    .manifold = std::move(chain_manifold),
                    .temporal_stats = temporal_stats,
                };
            }
        }
        const auto fused = step_component_contacts_matching_fused(
            current, active, broadphase.pairs(), contacts);
        if (fused.has_value()) {
            ContactStepResult contact = *fused;
            TemporalBroadphaseState next_broadphase(
                contact.state.frame(), contact.state.body_count(),
                broadphase.valid_until_frame(),
                broadphase.shared_bounds(), broadphase.shared_pairs());
            ++temporal_stats.pair_cache_reuses;
            temporal_stats.cached_pairs = broadphase.pairs().size();
            next_broadphase = refresh_after_solver(
                contact.state, next_broadphase, contact.contacts, contact.dirty,
                contacts, temporal, temporal_stats);
            temporal_stats.manifold_pairs_reused = count_manifold_reuse(
                manifold.contacts, contact.contacts);
            PersistentManifoldState next_manifold{
                .frame = contact.state.frame(),
                .contacts = contact.contacts,
            };
            return TemporalContactStepResult{
                .contact = std::move(contact),
                .broadphase = std::move(next_broadphase),
                .manifold = std::move(next_manifold),
                .temporal_stats = temporal_stats,
            };
        }
    }

    ComponentStepResult integrated = step_component_active(current, active, inputs, options);
    TemporalBroadphaseState next_broadphase = update_for_sweep(
        current, integrated.state, integrated.dirty, broadphase,
        contacts, temporal, temporal_stats);

    ContactSolverStats narrow_stats;
    std::vector<SweptContact> contacts_found;
    if (!active.empty() || !inputs.empty()) {
        contacts_found = swept_aabb_contacts_for_pairs(
            current, integrated.state, next_broadphase.pairs(), contacts.half_extent,
            &narrow_stats);
    }
    temporal_stats.manifold_pairs_reused = count_manifold_reuse(
        manifold.contacts, contacts_found);
    PersistentManifoldState next_manifold;
    ContactStepResult contact = solve_component_contact_constraints(
        current, std::move(integrated), std::move(contacts_found), contacts,
        narrow_stats, &manifold, &next_manifold);
    next_broadphase = refresh_after_solver(
        contact.state, next_broadphase, contact.contacts, contact.dirty,
        contacts, temporal, temporal_stats);
    return TemporalContactStepResult{
        .contact = std::move(contact),
        .broadphase = std::move(next_broadphase),
        .manifold = std::move(next_manifold),
        .temporal_stats = temporal_stats,
    };
}

bool temporal_cache_is_conservative(
    const ComponentWorldState& state,
    const TemporalBroadphaseState& broadphase,
    ContactSolverConfig contacts) {
    if (state.frame() != broadphase.frame() || state.body_count() != broadphase.body_count()) {
        return false;
    }
    const Fixed diameter = fixed_from_wide(
        static_cast<WideInteger>(contacts.half_extent.raw()) * 2);
    for (std::size_t first = 0U; first < state.body_count(); ++first) {
        for (std::size_t second = first + 1U; second < state.body_count(); ++second) {
            const WideInteger dx = static_cast<WideInteger>(state.position_x_at(second).raw())
                - state.position_x_at(first).raw();
            const WideInteger dy = static_cast<WideInteger>(state.position_y_at(second).raw())
                - state.position_y_at(first).raw();
            const bool overlap = (dx < 0 ? -dx : dx) <= diameter.raw()
                && (dy < 0 ? -dy : dy) <= diameter.raw();
            if (!overlap) continue;
            const BroadphasePair pair{.first = first, .second = second};
            if (!std::binary_search(broadphase.pairs().begin(), broadphase.pairs().end(), pair)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace neoeng::core
