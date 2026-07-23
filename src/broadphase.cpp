#include "neoeng/core/broadphase.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace neoeng::core {
namespace {

[[nodiscard]] std::int64_t floor_div_raw(std::int64_t value, std::int64_t divisor) {
    if (divisor <= 0) throw std::invalid_argument("Broadphase cell size must be positive");
    std::int64_t quotient = value / divisor;
    const std::int64_t remainder = value % divisor;
    if (remainder != 0 && value < 0) --quotient;
    return quotient;
}

[[nodiscard]] GridCell cell_for(Fixed x, Fixed y, Fixed cell_size) {
    return GridCell{
        .x = floor_div_raw(x.raw(), cell_size.raw()),
        .y = floor_div_raw(y.raw(), cell_size.raw()),
    };
}

[[nodiscard]] bool overlaps(
    const ComponentWorldState& state,
    std::size_t lhs,
    std::size_t rhs,
    Fixed half_extent) noexcept {
    const WideInteger diameter = static_cast<WideInteger>(half_extent.raw()) * 2;
    const WideInteger dx = static_cast<WideInteger>(state.position_x_at(lhs).raw())
        - static_cast<WideInteger>(state.position_x_at(rhs).raw());
    const WideInteger dy = static_cast<WideInteger>(state.position_y_at(lhs).raw())
        - static_cast<WideInteger>(state.position_y_at(rhs).raw());
    const WideInteger abs_x = dx < 0 ? -dx : dx;
    const WideInteger abs_y = dy < 0 ? -dy : dy;
    return abs_x <= diameter && abs_y <= diameter;
}

[[nodiscard]] std::size_t find_entity_index(
    const ComponentWorldState& state, EntityId entity) {
    std::size_t first = 0U;
    std::size_t count = state.body_count();
    while (count != 0U) {
        const std::size_t step = count / 2U;
        const std::size_t middle = first + step;
        if (state.entity_at(middle) < entity) {
            first = middle + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    if (first < state.body_count() && state.entity_at(first) == entity) return first;
    return state.body_count();
}

[[nodiscard]] DeterministicActiveSet seeds_with_inputs(
    const ComponentWorldState& state,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs) {
    std::vector<std::size_t> input_indices;
    input_indices.reserve(inputs.size());
    for (const InputCommand& input : inputs) {
        const std::size_t index = find_entity_index(state, input.entity);
        if (index != state.body_count()) input_indices.push_back(index);
    }
    std::sort(input_indices.begin(), input_indices.end());
    input_indices.erase(std::unique(input_indices.begin(), input_indices.end()), input_indices.end());
    std::vector<std::size_t> seeds;
    seeds.reserve(active.size() + input_indices.size());
    std::set_union(active.indices().begin(), active.indices().end(),
        input_indices.begin(), input_indices.end(), std::back_inserter(seeds));
    return DeterministicActiveSet(std::move(seeds));
}

} // namespace

const GridCell& GridBroadphaseState::cell_of(std::size_t index) const {
    if (!membership_ || index >= membership_->size()) {
        throw std::out_of_range("Broadphase body index is outside world");
    }
    return (*membership_)[index];
}

std::span<const std::size_t> GridBroadphaseState::occupants(const GridCell& cell) const noexcept {
    if (!cells_) return {};
    const auto iterator = cells_->find(cell);
    if (iterator == cells_->end()) return {};
    return iterator->second;
}

GridBroadphaseState make_grid_broadphase(
    const ComponentWorldState& state,
    Fixed cell_size,
    Fixed half_extent) {
    if (state.empty()) throw std::invalid_argument("Broadphase requires an initialized world");
    if (cell_size.raw() <= 0 || half_extent.raw() < 0) {
        throw std::invalid_argument("Broadphase dimensions must be non-negative and cell size positive");
    }
    if (static_cast<WideInteger>(half_extent.raw()) * 2
        > static_cast<WideInteger>(cell_size.raw())) {
        throw std::invalid_argument("Cell size must be at least the body diameter");
    }
    auto cells = std::make_shared<GridBroadphaseState::CellMap>();
    auto membership = std::make_shared<std::vector<GridCell>>();
    membership->reserve(state.body_count());
    for (std::size_t index = 0U; index < state.body_count(); ++index) {
        const GridCell cell = cell_for(
            state.position_x_at(index), state.position_y_at(index), cell_size);
        membership->push_back(cell);
        (*cells)[cell].push_back(index);
    }
    GridBroadphaseState result;
    result.frame_ = state.frame();
    result.body_count_ = state.body_count();
    result.cell_size_ = cell_size;
    result.half_extent_ = half_extent;
    result.cells_ = std::move(cells);
    result.membership_ = std::move(membership);
    return result;
}

GridBroadphaseState update_grid_broadphase(
    const GridBroadphaseState& previous,
    const ComponentWorldState& next,
    const DirtySet& dirty,
    BroadphaseStats* stats_output) {
    if (next.body_count() != previous.body_count_
        || dirty.entity_count() != previous.body_count_) {
        throw std::invalid_argument("Broadphase update shape mismatch");
    }
    GridBroadphaseState result = previous;
    result.frame_ = next.frame();
    std::vector<std::pair<std::size_t, GridCell>> moves;
    dirty.for_each_dirty([&](std::size_t index, std::uint8_t mask) {
        const std::uint8_t position_mask = component_mask(DirtyComponent::PositionX)
            | component_mask(DirtyComponent::PositionY);
        if ((mask & position_mask) == 0U) return;
        const GridCell new_cell = cell_for(
            next.position_x_at(index), next.position_y_at(index), previous.cell_size_);
        if (new_cell != previous.cell_of(index)) moves.emplace_back(index, new_cell);
    });
    if (moves.empty()) return result;

    auto cells = std::make_shared<GridBroadphaseState::CellMap>(*previous.cells_);
    auto membership = std::make_shared<std::vector<GridCell>>(*previous.membership_);
    BroadphaseStats local;
    local.cell_maps_cloned = 1U;
    for (const auto& [index, new_cell] : moves) {
        const GridCell old_cell = (*membership)[index];
        auto old_iterator = cells->find(old_cell);
        if (old_iterator == cells->end()) throw std::logic_error("Broadphase membership is inconsistent");
        auto& old_members = old_iterator->second;
        const auto position = std::lower_bound(old_members.begin(), old_members.end(), index);
        if (position == old_members.end() || *position != index) {
            throw std::logic_error("Broadphase cell is missing body membership");
        }
        old_members.erase(position);
        if (old_members.empty()) cells->erase(old_iterator);

        auto& new_members = (*cells)[new_cell];
        new_members.insert(std::lower_bound(new_members.begin(), new_members.end(), index), index);
        (*membership)[index] = new_cell;
        ++local.cell_memberships_moved;
    }
    result.cells_ = std::move(cells);
    result.membership_ = std::move(membership);
    if (stats_output != nullptr) {
        stats_output->cell_maps_cloned += local.cell_maps_cloned;
        stats_output->cell_memberships_moved += local.cell_memberships_moved;
    }
    return result;
}

IslandClosure conservative_island_closure(
    const ComponentWorldState& state,
    const GridBroadphaseState& broadphase,
    const DeterministicActiveSet& seeds) {
    if (state.frame() != broadphase.frame() || state.body_count() != broadphase.body_count()) {
        throw std::invalid_argument("Broadphase and component state must describe the same frame");
    }
    if (!seeds.indices().empty() && seeds.indices().back() >= state.body_count()) {
        throw std::invalid_argument("Island seed references body outside world");
    }
    std::vector<bool> visited(state.body_count(), false);
    std::deque<std::size_t> queue;
    for (const std::size_t seed : seeds.indices()) {
        visited[seed] = true;
        queue.push_back(seed);
    }

    IslandClosure result;
    result.stats.seed_bodies = seeds.size();
    std::vector<std::size_t> closure(seeds.indices().begin(), seeds.indices().end());
    while (!queue.empty()) {
        const std::size_t current = queue.front();
        queue.pop_front();
        const GridCell center = broadphase.cell_of(current);
        for (std::int64_t dy = -1; dy <= 1; ++dy) {
            for (std::int64_t dx = -1; dx <= 1; ++dx) {
                ++result.stats.cells_queried;
                const GridCell cell{.x = center.x + dx, .y = center.y + dy};
                for (const std::size_t candidate : broadphase.occupants(cell)) {
                    if (candidate == current) continue;
                    ++result.stats.candidate_pairs_tested;
                    if (!overlaps(state, current, candidate, broadphase.half_extent())) continue;
                    const BroadphasePair pair{
                        .first = std::min(current, candidate),
                        .second = std::max(current, candidate),
                    };
                    result.overlaps.push_back(pair);
                    ++result.stats.exact_overlaps;
                    if (!visited[candidate]) {
                        visited[candidate] = true;
                        queue.push_back(candidate);
                        closure.push_back(candidate);
                        ++result.stats.bodies_woken;
                    }
                }
            }
        }
    }
    std::sort(closure.begin(), closure.end());
    closure.erase(std::unique(closure.begin(), closure.end()), closure.end());
    std::sort(result.overlaps.begin(), result.overlaps.end());
    result.overlaps.erase(std::unique(result.overlaps.begin(), result.overlaps.end()),
        result.overlaps.end());
    result.bodies = DeterministicActiveSet(std::move(closure));
    return result;
}

std::vector<BroadphasePair> brute_force_overlap_pairs(
    const ComponentWorldState& state,
    Fixed half_extent) {
    std::vector<BroadphasePair> pairs;
    for (std::size_t lhs = 0U; lhs < state.body_count(); ++lhs) {
        for (std::size_t rhs = lhs + 1U; rhs < state.body_count(); ++rhs) {
            if (overlaps(state, lhs, rhs, half_extent)) {
                pairs.push_back(BroadphasePair{.first = lhs, .second = rhs});
            }
        }
    }
    return pairs;
}

IslandStepResult step_component_islands(
    const ComponentWorldState& current,
    const GridBroadphaseState& broadphase,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs,
    ComponentStepOptions options) {
    const DeterministicActiveSet seeds = seeds_with_inputs(current, active, inputs);
    IslandClosure closure = conservative_island_closure(current, broadphase, seeds);
    ComponentStepResult step = step_component_active(current, closure.bodies, inputs, options);
    BroadphaseStats broadphase_stats = closure.stats;
    GridBroadphaseState next_broadphase = update_grid_broadphase(
        broadphase, step.state, step.dirty, &broadphase_stats);
    return IslandStepResult{
        .state = std::move(step.state),
        .broadphase = std::move(next_broadphase),
        .active = std::move(step.active),
        .island_candidates = std::move(closure.bodies),
        .dirty = std::move(step.dirty),
        .overlaps = std::move(closure.overlaps),
        .component_stats = step.allocation,
        .broadphase_stats = broadphase_stats,
    };
}

} // namespace neoeng::core
