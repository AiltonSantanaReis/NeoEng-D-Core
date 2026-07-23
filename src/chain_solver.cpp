#include "neoeng/core/chain_solver.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace neoeng::core {
namespace {

struct IsotonicBlock final {
    std::size_t begin{};
    std::size_t end{}; // exclusive
    WideInteger sum{};
};

struct IsotonicResult final {
    std::vector<Fixed::rep> projected{};
    std::vector<std::size_t> block_ends{};
    bool warm_start_attempted{};
    bool warm_start_accepted{};
};

[[nodiscard]] Fixed::rep checked_rep(WideInteger value) {
    constexpr WideInteger minimum = static_cast<WideInteger>(
        std::numeric_limits<Fixed::rep>::min());
    constexpr WideInteger maximum = static_cast<WideInteger>(
        std::numeric_limits<Fixed::rep>::max());
    if (value < minimum || value > maximum) {
        throw std::overflow_error("Chain solver fixed-point range overflow");
    }
    return static_cast<Fixed::rep>(value);
}

[[nodiscard]] WideInteger floor_div(WideInteger numerator, std::size_t denominator) {
    if (denominator == 0U) throw std::domain_error("Isotonic block has zero weight");
    const WideInteger divisor = static_cast<WideInteger>(denominator);
    WideInteger quotient = numerator / divisor;
    const WideInteger remainder = numerator % divisor;
    if (remainder != 0 && numerator < 0) --quotient;
    return quotient;
}

[[nodiscard]] bool mean_greater(const IsotonicBlock& lhs, const IsotonicBlock& rhs) {
    const WideInteger lhs_count = static_cast<WideInteger>(lhs.end - lhs.begin);
    const WideInteger rhs_count = static_cast<WideInteger>(rhs.end - rhs.begin);
    return lhs.sum * rhs_count > rhs.sum * lhs_count;
}

[[nodiscard]] bool block_is_irreducible(
    std::span<const Fixed::rep> values,
    std::size_t begin,
    std::size_t end,
    WideInteger total) {
    if (begin >= end || end > values.size()) return false;
    const WideInteger count = static_cast<WideInteger>(end - begin);
    WideInteger prefix = 0;
    for (std::size_t index = begin; index + 1U < end; ++index) {
        prefix += static_cast<WideInteger>(values[index]);
        const WideInteger prefix_count = static_cast<WideInteger>(index - begin + 1U);
        // KKT condition for a constant block in nondecreasing L2 isotonic regression.
        if (prefix * count < total * prefix_count) return false;
    }
    return true;
}

[[nodiscard]] bool warm_partition(
    std::span<const Fixed::rep> values,
    std::span<const std::size_t> ends,
    std::vector<IsotonicBlock>& blocks) {
    if (values.empty() || ends.empty() || ends.back() != values.size()) return false;
    std::size_t begin = 0U;
    blocks.clear();
    blocks.reserve(ends.size());
    for (const std::size_t end : ends) {
        if (end <= begin || end > values.size()) return false;
        WideInteger sum = 0;
        for (std::size_t index = begin; index < end; ++index) {
            sum += static_cast<WideInteger>(values[index]);
        }
        if (!block_is_irreducible(values, begin, end, sum)) return false;
        blocks.push_back(IsotonicBlock{.begin = begin, .end = end, .sum = sum});
        begin = end;
    }
    return true;
}

[[nodiscard]] IsotonicResult isotonic_floor_projection(
    std::span<const Fixed::rep> values,
    const std::vector<std::size_t>* warm_block_ends) {
    IsotonicResult result;
    if (values.empty()) return result;

    std::vector<IsotonicBlock> blocks;
    if (warm_block_ends != nullptr) {
        result.warm_start_attempted = true;
        result.warm_start_accepted = warm_partition(values, *warm_block_ends, blocks);
    }
    if (!result.warm_start_accepted) {
        blocks.reserve(values.size());
        for (std::size_t index = 0U; index < values.size(); ++index) {
            blocks.push_back(IsotonicBlock{
                .begin = index,
                .end = index + 1U,
                .sum = static_cast<WideInteger>(values[index]),
            });
        }
    }

    std::vector<IsotonicBlock> pooled;
    pooled.reserve(blocks.size());
    for (const IsotonicBlock& block : blocks) {
        pooled.push_back(block);
        while (pooled.size() >= 2U
            && mean_greater(pooled[pooled.size() - 2U], pooled.back())) {
            IsotonicBlock right = pooled.back();
            pooled.pop_back();
            IsotonicBlock& left = pooled.back();
            left.end = right.end;
            left.sum += right.sum;
        }
    }

    result.projected.resize(values.size());
    result.block_ends.reserve(pooled.size());
    for (const IsotonicBlock& block : pooled) {
        const Fixed::rep value = checked_rep(floor_div(block.sum, block.end - block.begin));
        std::fill(result.projected.begin() + static_cast<std::ptrdiff_t>(block.begin),
            result.projected.begin() + static_cast<std::ptrdiff_t>(block.end), value);
        result.block_ends.push_back(block.end);
    }
    return result;
}

void merge_dirty(DirtySet& destination, const DirtySet& source) {
    source.for_each_dirty([&](std::size_t index, std::uint8_t mask) {
        destination.mark(index, static_cast<DirtyComponent>(mask));
    });
}

[[nodiscard]] Fixed axis_position(
    const ComponentWorldState& state, std::size_t index, ContactAxis axis) {
    return axis == ContactAxis::X
        ? state.position_x_at(index) : state.position_y_at(index);
}

[[nodiscard]] Fixed axis_velocity(
    const ComponentWorldState& state, std::size_t index, ContactAxis axis) {
    return axis == ContactAxis::X
        ? state.velocity_x_at(index) : state.velocity_y_at(index);
}

struct ChainTopology final {
    ContactAxis axis{ContactAxis::X};
    int direction{1};
    std::vector<std::size_t> bodies{};
};

[[nodiscard]] std::optional<ChainTopology> recognize_chain(
    const ComponentWorldState& current,
    std::span<const SweptContact> contacts) {
    if (contacts.size() < 2U) return std::nullopt;
    const ContactAxis axis = contacts.front().axis;
    std::vector<std::size_t> involved;
    involved.reserve(contacts.size() * 2U);
    for (const SweptContact& contact : contacts) {
        if (contact.axis != axis || (!contact.initial_overlap && !contact.final_overlap)) {
            return std::nullopt;
        }
        involved.push_back(contact.first);
        involved.push_back(contact.second);
    }
    std::sort(involved.begin(), involved.end());
    involved.erase(std::unique(involved.begin(), involved.end()), involved.end());
    if (contacts.size() + 1U != involved.size()) return std::nullopt;

    constexpr std::size_t missing = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> local(current.body_count(), missing);
    for (std::size_t i = 0U; i < involved.size(); ++i) local[involved[i]] = i;
    std::vector<std::vector<std::size_t>> adjacent(involved.size());
    for (const SweptContact& contact : contacts) {
        const std::size_t first = local[contact.first];
        const std::size_t second = local[contact.second];
        if (first == missing || second == missing) return std::nullopt;
        adjacent[first].push_back(second);
        adjacent[second].push_back(first);
    }
    std::vector<std::size_t> endpoints;
    for (std::size_t i = 0U; i < adjacent.size(); ++i) {
        std::sort(adjacent[i].begin(), adjacent[i].end());
        if (adjacent[i].size() == 1U) endpoints.push_back(i);
        else if (adjacent[i].size() != 2U) return std::nullopt;
    }
    if (endpoints.size() != 2U) return std::nullopt;

    auto endpoint_less = [&](std::size_t lhs, std::size_t rhs) {
        const Fixed lhs_position = axis_position(current, involved[lhs], axis);
        const Fixed rhs_position = axis_position(current, involved[rhs], axis);
        if (lhs_position != rhs_position) return lhs_position < rhs_position;
        return current.entity_at(involved[lhs]) < current.entity_at(involved[rhs]);
    };
    std::size_t cursor = endpoint_less(endpoints[0], endpoints[1])
        ? endpoints[0] : endpoints[1];
    const std::size_t other_endpoint = cursor == endpoints[0] ? endpoints[1] : endpoints[0];
    const int direction = axis_position(current, involved[cursor], axis)
            <= axis_position(current, involved[other_endpoint], axis)
        ? 1 : -1;

    std::vector<std::size_t> ordered;
    ordered.reserve(involved.size());
    std::size_t previous = missing;
    while (true) {
        ordered.push_back(involved[cursor]);
        std::size_t next = missing;
        for (const std::size_t candidate : adjacent[cursor]) {
            if (candidate != previous) {
                next = candidate;
                break;
            }
        }
        if (next == missing) break;
        previous = cursor;
        cursor = next;
        if (ordered.size() > involved.size()) return std::nullopt;
    }
    if (ordered.size() != involved.size()) return std::nullopt;

    for (std::size_t i = 1U; i < ordered.size(); ++i) {
        const WideInteger before = static_cast<WideInteger>(
            axis_position(current, ordered[i - 1U], axis).raw()) * direction;
        const WideInteger after = static_cast<WideInteger>(
            axis_position(current, ordered[i], axis).raw()) * direction;
        if (after < before) return std::nullopt;
    }
    return ChainTopology{.axis = axis, .direction = direction, .bodies = std::move(ordered)};
}

[[nodiscard]] bool same_warm_topology(
    const ChainWarmStartState& warm, const ChainTopology& topology) {
    return warm.axis == topology.axis
        && warm.direction == topology.direction
        && warm.bodies == topology.bodies;
}

} // namespace

std::optional<ContactStepResult> solve_chain_contacts_isotonic(
    const ComponentWorldState& current,
    ComponentStepResult integrated,
    std::vector<SweptContact> contacts,
    ContactSolverConfig config,
    ContactSolverStats stats,
    const PersistentManifoldState* previous_manifold,
    PersistentManifoldState* next_manifold) {
    ++stats.chain_solver_attempts;
    const auto topology = recognize_chain(current, contacts);
    if (!topology.has_value()) {
        ++stats.chain_solver_fallbacks;
        return std::nullopt;
    }

    const std::size_t count = topology->bodies.size();
    if (count > config.max_chain_bodies) return std::nullopt;
    const WideInteger diameter = static_cast<WideInteger>(config.half_extent.raw()) * 2;
    std::vector<Fixed::rep> velocity_values(count);
    std::vector<Fixed::rep> position_values(count);
    WideInteger input_momentum = 0;
    for (std::size_t i = 0U; i < count; ++i) {
        const std::size_t body = topology->bodies[i];
        const WideInteger velocity = static_cast<WideInteger>(
            axis_velocity(integrated.state, body, topology->axis).raw())
            * topology->direction;
        const WideInteger position = static_cast<WideInteger>(
            axis_position(integrated.state, body, topology->axis).raw())
            * topology->direction - static_cast<WideInteger>(i) * diameter;
        velocity_values[i] = checked_rep(velocity);
        position_values[i] = checked_rep(position);
        input_momentum += velocity;
    }

    const ChainWarmStartState* warm = nullptr;
    if (config.enable_chain_warm_start && previous_manifold != nullptr
        && previous_manifold->chain_warm_start.has_value()
        && same_warm_topology(*previous_manifold->chain_warm_start, *topology)) {
        warm = &*previous_manifold->chain_warm_start;
    }
    const IsotonicResult velocities = isotonic_floor_projection(
        velocity_values, warm == nullptr ? nullptr : &warm->velocity_block_ends);
    const IsotonicResult positions = isotonic_floor_projection(
        position_values, warm == nullptr ? nullptr : &warm->position_block_ends);
    stats.warm_start_attempts += velocities.warm_start_attempted + positions.warm_start_attempted;
    stats.warm_start_accepts += velocities.warm_start_accepted + positions.warm_start_accepted;
    stats.warm_start_rejects += (velocities.warm_start_attempted && !velocities.warm_start_accepted)
        + (positions.warm_start_attempted && !positions.warm_start_accepted);
    stats.isotonic_blocks += velocities.block_ends.size() + positions.block_ends.size();

    std::vector<ComponentPatch> patches;
    patches.reserve(count);
    DirtySet dirty(current.body_count());
    merge_dirty(dirty, integrated.dirty);
    std::vector<std::uint8_t> involved_mask(current.body_count(), 0U);
    std::vector<std::size_t> next_active;
    next_active.reserve(integrated.active.size() + count);
    WideInteger output_momentum = 0;

    for (std::size_t i = 0U; i < count; ++i) {
        const std::size_t index = topology->bodies[i];
        involved_mask[index] = 1U;
        Fixed position_x = integrated.state.position_x_at(index);
        Fixed position_y = integrated.state.position_y_at(index);
        Fixed velocity_x = integrated.state.velocity_x_at(index);
        Fixed velocity_y = integrated.state.velocity_y_at(index);
        const Fixed::rep oriented_position = checked_rep(
            static_cast<WideInteger>(positions.projected[i])
            + static_cast<WideInteger>(i) * diameter);
        const Fixed::rep axis_position_raw = checked_rep(
            static_cast<WideInteger>(oriented_position) * topology->direction);
        const Fixed::rep axis_velocity_raw = checked_rep(
            static_cast<WideInteger>(velocities.projected[i]) * topology->direction);
        output_momentum += velocities.projected[i];

        std::uint8_t mask = 0U;
        if (topology->axis == ContactAxis::X) {
            const Fixed next_position = Fixed::from_raw(axis_position_raw);
            const Fixed next_velocity = Fixed::from_raw(axis_velocity_raw);
            if (next_position != position_x) {
                position_x = next_position;
                mask |= component_mask(DirtyComponent::PositionX);
                ++stats.position_projections;
            }
            if (next_velocity != velocity_x) {
                velocity_x = next_velocity;
                mask |= component_mask(DirtyComponent::VelocityX);
                ++stats.velocity_resolutions;
            }
        } else {
            const Fixed next_position = Fixed::from_raw(axis_position_raw);
            const Fixed next_velocity = Fixed::from_raw(axis_velocity_raw);
            if (next_position != position_y) {
                position_y = next_position;
                mask |= component_mask(DirtyComponent::PositionY);
                ++stats.position_projections;
            }
            if (next_velocity != velocity_y) {
                velocity_y = next_velocity;
                mask |= component_mask(DirtyComponent::VelocityY);
                ++stats.velocity_resolutions;
            }
        }
        if (mask != 0U) {
            patches.push_back(ComponentPatch{
                .index = index,
                .position_x = position_x,
                .position_y = position_y,
                .velocity_x = velocity_x,
                .velocity_y = velocity_y,
                .mask = mask,
            });
            dirty.mark(index, static_cast<DirtyComponent>(mask));
        }
        if (velocity_x.raw() != 0 || velocity_y.raw() != 0) next_active.push_back(index);
    }
    for (const std::size_t index : integrated.active.indices()) {
        if (involved_mask[index] == 0U) next_active.push_back(index);
    }
    std::sort(next_active.begin(), next_active.end());
    next_active.erase(std::unique(next_active.begin(), next_active.end()), next_active.end());
    std::sort(patches.begin(), patches.end(), [](const ComponentPatch& lhs, const ComponentPatch& rhs) {
        return lhs.index < rhs.index;
    });

    ComponentWorldState solved = apply_component_patches(
        integrated.state, patches, &stats.solver_allocation);
    ++stats.chain_solver_accepts;
    stats.chain_bodies_solved += count;
    stats.constraint_islands = 1U;
    stats.graph_colors = contacts.empty() ? 0U : 2U;
    const WideInteger momentum_error = output_momentum - input_momentum;
    stats.momentum_rounding_error_raw = checked_rep(momentum_error);

    if (next_manifold != nullptr) {
        next_manifold->frame = solved.frame();
        next_manifold->contacts = contacts;
        next_manifold->points.clear();
        next_manifold->points.reserve(count - 1U);
        WideInteger prefix_impulse = 0;
        for (std::size_t i = 0U; i + 1U < count; ++i) {
            prefix_impulse += static_cast<WideInteger>(velocity_values[i])
                - velocities.projected[i];
            const std::size_t first = std::min(topology->bodies[i], topology->bodies[i + 1U]);
            const std::size_t second = std::max(topology->bodies[i], topology->bodies[i + 1U]);
            next_manifold->points.push_back(ContactManifoldPoint{
                .first = first,
                .second = second,
                .axis = topology->axis,
                .accumulated_normal_impulse = Fixed::from_raw(
                    checked_rep(std::max<WideInteger>(0, prefix_impulse))),
            });
        }
        next_manifold->chain_warm_start = ChainWarmStartState{
            .axis = topology->axis,
            .direction = topology->direction,
            .bodies = topology->bodies,
            .velocity_block_ends = velocities.block_ends,
            .position_block_ends = positions.block_ends,
        };
    }

    return ContactStepResult{
        .state = std::move(solved),
        .active = DeterministicActiveSet(std::move(next_active)),
        .dirty = std::move(dirty),
        .contacts = std::move(contacts),
        .stats = stats,
    };
}


std::optional<ContactStepResult> step_component_contacts_chain_fused(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const BroadphasePair> pairs,
    ContactSolverConfig config,
    const PersistentManifoldState* previous_manifold,
    PersistentManifoldState* next_manifold) {
    if (pairs.size() < 2U || active.empty()) return std::nullopt;
    const std::size_t first_body = pairs.front().first;
    if (pairs.front().second != first_body + 1U
        || first_body + pairs.size() >= current.body_count()) return std::nullopt;
    for (std::size_t edge = 0U; edge < pairs.size(); ++edge) {
        if (pairs[edge].first != first_body + edge
            || pairs[edge].second != first_body + edge + 1U) return std::nullopt;
    }

    const std::size_t count = pairs.size() + 1U;
    if (count > config.max_chain_bodies) return std::nullopt;
    const WideInteger diameter = static_cast<WideInteger>(config.half_extent.raw()) * 2;
    const WideInteger dx = static_cast<WideInteger>(current.position_x_at(first_body + 1U).raw())
        - current.position_x_at(first_body).raw();
    const WideInteger dy = static_cast<WideInteger>(current.position_y_at(first_body + 1U).raw())
        - current.position_y_at(first_body).raw();
    const ContactAxis axis = (dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)
        ? ContactAxis::X : ContactAxis::Y;
    const WideInteger endpoint_delta = static_cast<WideInteger>(
        axis_position(current, first_body + count - 1U, axis).raw())
        - axis_position(current, first_body, axis).raw();
    const int direction = endpoint_delta >= 0 ? 1 : -1;
    ChainTopology topology{.axis = axis, .direction = direction};
    topology.bodies.resize(count);
    std::iota(topology.bodies.begin(), topology.bodies.end(), first_body);

    std::vector<std::uint8_t> active_mask(current.body_count(), 0U);
    for (const std::size_t index : active.indices()) {
        if (index >= current.body_count()) return std::nullopt;
        active_mask[index] = 1U;
    }
    std::vector<Fixed> predicted_x(count);
    std::vector<Fixed> predicted_y(count);
    std::vector<Fixed::rep> velocity_values(count);
    std::vector<Fixed::rep> position_values(count);
    WideInteger input_momentum = 0;
    ContactSolverStats stats;
    stats.candidate_pairs = pairs.size();
    stats.narrowphase_tests = pairs.size();
    stats.integration_allocation.candidate_bodies_scanned = active.size();
    stats.integration_allocation.fixed_kernel.lanes = active.size() * 2U;
    stats.integration_allocation.fixed_kernel.scalar_lanes = active.size() * 2U;

    for (std::size_t i = 0U; i < count; ++i) {
        const std::size_t index = first_body + i;
        predicted_x[i] = current.position_x_at(index);
        predicted_y[i] = current.position_y_at(index);
        if (active_mask[index] != 0U) {
            predicted_x[i] += current.velocity_x_at(index) * kSimulationDelta;
            predicted_y[i] += current.velocity_y_at(index) * kSimulationDelta;
        }
        const WideInteger velocity = static_cast<WideInteger>(
            axis_velocity(current, index, axis).raw()) * direction;
        const Fixed predicted_axis = axis == ContactAxis::X ? predicted_x[i] : predicted_y[i];
        const WideInteger position = static_cast<WideInteger>(predicted_axis.raw()) * direction
            - static_cast<WideInteger>(i) * diameter;
        velocity_values[i] = checked_rep(velocity);
        position_values[i] = checked_rep(position);
        input_momentum += velocity;
    }

    std::vector<SweptContact> contacts;
    contacts.reserve(pairs.size());
    for (std::size_t edge = 0U; edge < pairs.size(); ++edge) {
        const std::size_t first = edge;
        const std::size_t second = edge + 1U;
        const WideInteger current_dx = static_cast<WideInteger>(
            current.position_x_at(first_body + second).raw())
            - current.position_x_at(first_body + first).raw();
        const WideInteger current_dy = static_cast<WideInteger>(
            current.position_y_at(first_body + second).raw())
            - current.position_y_at(first_body + first).raw();
        if ((current_dx < 0 ? -current_dx : current_dx) > diameter
            || (current_dy < 0 ? -current_dy : current_dy) > diameter) return std::nullopt;
        const WideInteger final_dx = static_cast<WideInteger>(predicted_x[second].raw())
            - predicted_x[first].raw();
        const WideInteger final_dy = static_cast<WideInteger>(predicted_y[second].raw())
            - predicted_y[first].raw();
        const bool final_overlap = (final_dx < 0 ? -final_dx : final_dx) <= diameter
            && (final_dy < 0 ? -final_dy : final_dy) <= diameter;
        contacts.push_back(SweptContact{
            .first = first_body + first,
            .second = first_body + second,
            .axis = axis,
            .toi = {},
            .initial_overlap = true,
            .final_overlap = final_overlap,
        });
    }
    stats.swept_hits = contacts.size();
    stats.initial_overlaps = contacts.size();
    stats.final_overlaps = static_cast<std::uint64_t>(std::count_if(
        contacts.begin(), contacts.end(), [](const SweptContact& c) { return c.final_overlap; }));

    const ChainWarmStartState* warm = nullptr;
    if (config.enable_chain_warm_start && previous_manifold != nullptr
        && previous_manifold->chain_warm_start.has_value()
        && same_warm_topology(*previous_manifold->chain_warm_start, topology)) {
        warm = &*previous_manifold->chain_warm_start;
    }
    const IsotonicResult velocities = isotonic_floor_projection(
        velocity_values, warm == nullptr ? nullptr : &warm->velocity_block_ends);
    const IsotonicResult positions = isotonic_floor_projection(
        position_values, warm == nullptr ? nullptr : &warm->position_block_ends);
    stats.warm_start_attempts = velocities.warm_start_attempted + positions.warm_start_attempted;
    stats.warm_start_accepts = velocities.warm_start_accepted + positions.warm_start_accepted;
    stats.warm_start_rejects = (velocities.warm_start_attempted && !velocities.warm_start_accepted)
        + (positions.warm_start_attempted && !positions.warm_start_accepted);
    stats.isotonic_blocks = velocities.block_ends.size() + positions.block_ends.size();

    std::vector<ComponentPatch> patches;
    patches.reserve(count);
    DirtySet dirty(current.body_count());
    std::vector<std::size_t> next_active;
    next_active.reserve(active.size() + count);
    WideInteger output_momentum = 0;
    for (std::size_t i = 0U; i < count; ++i) {
        const std::size_t index = first_body + i;
        Fixed px = predicted_x[i];
        Fixed py = predicted_y[i];
        Fixed vx = current.velocity_x_at(index);
        Fixed vy = current.velocity_y_at(index);
        const Fixed::rep oriented_position = checked_rep(
            static_cast<WideInteger>(positions.projected[i])
            + static_cast<WideInteger>(i) * diameter);
        const Fixed next_position = Fixed::from_raw(checked_rep(
            static_cast<WideInteger>(oriented_position) * direction));
        const Fixed next_velocity = Fixed::from_raw(checked_rep(
            static_cast<WideInteger>(velocities.projected[i]) * direction));
        output_momentum += velocities.projected[i];
        if (axis == ContactAxis::X) { px = next_position; vx = next_velocity; }
        else { py = next_position; vy = next_velocity; }
        std::uint8_t mask = 0U;
        if (px != current.position_x_at(index)) mask |= component_mask(DirtyComponent::PositionX);
        if (py != current.position_y_at(index)) mask |= component_mask(DirtyComponent::PositionY);
        if (vx != current.velocity_x_at(index)) mask |= component_mask(DirtyComponent::VelocityX);
        if (vy != current.velocity_y_at(index)) mask |= component_mask(DirtyComponent::VelocityY);
        if (mask != 0U) {
            patches.push_back(ComponentPatch{
                .index = index, .position_x = px, .position_y = py,
                .velocity_x = vx, .velocity_y = vy, .mask = mask,
            });
            dirty.mark(index, static_cast<DirtyComponent>(mask));
        }
        if (vx.raw() != 0 || vy.raw() != 0) next_active.push_back(index);
    }
    for (const std::size_t index : active.indices()) {
        if (index < first_body || index >= first_body + count) next_active.push_back(index);
    }
    std::sort(next_active.begin(), next_active.end());
    next_active.erase(std::unique(next_active.begin(), next_active.end()), next_active.end());
    ComponentWorldState solved = apply_component_patches_next_frame(
        current, patches, &stats.solver_allocation);
    stats.chain_solver_attempts = 1U;
    stats.chain_solver_accepts = 1U;
    stats.chain_bodies_solved = count;
    stats.constraint_islands = 1U;
    stats.graph_colors = 2U;
    stats.velocity_resolutions = count;
    stats.position_projections = count;
    stats.momentum_rounding_error_raw = checked_rep(output_momentum - input_momentum);

    if (next_manifold != nullptr) {
        next_manifold->frame = solved.frame();
        next_manifold->contacts = contacts;
        next_manifold->points.clear();
        next_manifold->points.reserve(count - 1U);
        WideInteger prefix_impulse = 0;
        for (std::size_t i = 0U; i + 1U < count; ++i) {
            prefix_impulse += static_cast<WideInteger>(velocity_values[i])
                - velocities.projected[i];
            next_manifold->points.push_back(ContactManifoldPoint{
                .first = first_body + i,
                .second = first_body + i + 1U,
                .axis = axis,
                .accumulated_normal_impulse = Fixed::from_raw(
                    checked_rep(std::max<WideInteger>(0, prefix_impulse))),
            });
        }
        next_manifold->chain_warm_start = ChainWarmStartState{
            .axis = axis, .direction = direction, .bodies = topology.bodies,
            .velocity_block_ends = velocities.block_ends,
            .position_block_ends = positions.block_ends,
        };
    }
    return ContactStepResult{
        .state = std::move(solved),
        .active = DeterministicActiveSet(std::move(next_active)),
        .dirty = std::move(dirty),
        .contacts = std::move(contacts),
        .stats = stats,
    };
}

} // namespace neoeng::core
