#include "neoeng/core/component_world.hpp"

#include "neoeng/core/simulation.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace neoeng::core {
namespace detail {

struct ComponentPage final {
    std::vector<Fixed> values{};
};

struct ComponentDirectory final {
    std::vector<std::shared_ptr<const ComponentPage>> pages{};
};

} // namespace detail
namespace {

using Page = detail::ComponentPage;
using Directory = detail::ComponentDirectory;

enum class ComponentIndex : std::size_t {
    PositionX = 0U,
    PositionY = 1U,
    VelocityX = 2U,
    VelocityY = 3U,
};

struct Update final {
    std::size_t index{};
    Fixed position_x{};
    Fixed position_y{};
    Fixed velocity_x{};
    Fixed velocity_y{};
    std::uint8_t mask{};
};

[[nodiscard]] bool is_active(Fixed velocity_x, Fixed velocity_y) noexcept {
    return velocity_x.raw() != 0 || velocity_y.raw() != 0;
}

[[nodiscard]] std::shared_ptr<const Directory> make_directory(
    std::span<const Body> bodies,
    std::size_t page_size,
    ComponentIndex component,
    ComponentAllocationStats& allocation) {
    auto directory = std::make_shared<Directory>();
    directory->pages.reserve((bodies.size() + page_size - 1U) / page_size);
    for (std::size_t begin = 0; begin < bodies.size(); begin += page_size) {
        const std::size_t end = std::min(begin + page_size, bodies.size());
        auto page = std::make_shared<Page>();
        page->values.reserve(end - begin);
        for (std::size_t index = begin; index < end; ++index) {
            switch (component) {
            case ComponentIndex::PositionX: page->values.push_back(bodies[index].position.x); break;
            case ComponentIndex::PositionY: page->values.push_back(bodies[index].position.y); break;
            case ComponentIndex::VelocityX: page->values.push_back(bodies[index].velocity.x); break;
            case ComponentIndex::VelocityY: page->values.push_back(bodies[index].velocity.y); break;
            }
        }
        ++allocation.component_pages_allocated;
        allocation.component_values_copied += page->values.size();
        directory->pages.push_back(std::move(page));
    }
    ++allocation.directories_allocated;
    allocation.directory_entries_copied += directory->pages.size();
    return directory;
}

[[nodiscard]] Fixed component_at(
    const std::shared_ptr<const Directory>& directory,
    std::size_t page_size,
    std::size_t index) {
    if (!directory) throw std::logic_error("Component directory is not initialized");
    const std::size_t page_index = index / page_size;
    const std::size_t local = index % page_size;
    if (page_index >= directory->pages.size() || local >= directory->pages[page_index]->values.size()) {
        throw std::out_of_range("Component index is outside the world");
    }
    return directory->pages[page_index]->values[local];
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

[[nodiscard]] Fixed select_component(const Update& update, DirtyComponent component) {
    switch (component) {
    case DirtyComponent::PositionX: return update.position_x;
    case DirtyComponent::PositionY: return update.position_y;
    case DirtyComponent::VelocityX: return update.velocity_x;
    case DirtyComponent::VelocityY: return update.velocity_y;
    case DirtyComponent::None:
    case DirtyComponent::Identity:
    case DirtyComponent::All:
        break;
    }
    throw std::logic_error("Identity is not a fixed-point component");
}

[[nodiscard]] std::shared_ptr<const Directory> apply_component_updates(
    const std::shared_ptr<const Directory>& source,
    std::size_t page_size,
    std::span<const Update> updates,
    DirtyComponent component,
    ComponentAllocationStats& allocation) {
    std::vector<const Update*> selected;
    selected.reserve(updates.size());
    for (const Update& update : updates) {
        if ((update.mask & component_mask(component)) != 0U) selected.push_back(&update);
    }
    if (selected.empty()) return source;

    auto directory = std::make_shared<Directory>(*source);
    ++allocation.directories_allocated;
    allocation.directory_entries_copied += directory->pages.size();
    std::size_t cursor = 0U;
    while (cursor < selected.size()) {
        const std::size_t page_index = selected[cursor]->index / page_size;
        auto page = std::make_shared<Page>(*source->pages[page_index]);
        ++allocation.component_pages_allocated;
        allocation.component_values_copied += page->values.size();
        while (cursor < selected.size() && selected[cursor]->index / page_size == page_index) {
            const std::size_t local = selected[cursor]->index % page_size;
            page->values[local] = select_component(*selected[cursor], component);
            ++cursor;
        }
        directory->pages[page_index] = std::move(page);
    }
    return directory;
}

[[nodiscard]] Fixed patch_component_value(
    const ComponentPatch& patch,
    DirtyComponent component) {
    switch (component) {
    case DirtyComponent::PositionX: return patch.position_x;
    case DirtyComponent::PositionY: return patch.position_y;
    case DirtyComponent::VelocityX: return patch.velocity_x;
    case DirtyComponent::VelocityY: return patch.velocity_y;
    case DirtyComponent::None:
    case DirtyComponent::Identity:
    case DirtyComponent::All:
        break;
    }
    throw std::logic_error("Invalid component patch selection");
}

[[nodiscard]] std::shared_ptr<const Directory> apply_component_patches_direct(
    const std::shared_ptr<const Directory>& source,
    std::size_t page_size,
    std::span<const ComponentPatch> patches,
    DirtyComponent component,
    ComponentAllocationStats& allocation) {
    const std::uint8_t target_mask = component_mask(component);
    const auto first_selected = std::find_if(patches.begin(), patches.end(), [&](const ComponentPatch& patch) {
        return (patch.mask & target_mask) != 0U;
    });
    if (first_selected == patches.end()) return source;

    auto directory = std::make_shared<Directory>(*source);
    ++allocation.directories_allocated;
    allocation.directory_entries_copied += directory->pages.size();
    std::size_t cursor = static_cast<std::size_t>(first_selected - patches.begin());
    while (cursor < patches.size()) {
        while (cursor < patches.size() && (patches[cursor].mask & target_mask) == 0U) ++cursor;
        if (cursor == patches.size()) break;
        const std::size_t page_index = patches[cursor].index / page_size;
        auto page = std::make_shared<Page>(*source->pages[page_index]);
        ++allocation.component_pages_allocated;
        allocation.component_values_copied += page->values.size();
        std::size_t page_cursor = cursor;
        while (page_cursor < patches.size()
            && patches[page_cursor].index / page_size == page_index) {
            const ComponentPatch& patch = patches[page_cursor];
            if ((patch.mask & target_mask) != 0U) {
                page->values[patch.index % page_size] = patch_component_value(patch, component);
            }
            ++page_cursor;
        }
        directory->pages[page_index] = std::move(page);
        cursor = page_cursor;
    }
    return directory;
}

[[nodiscard]] std::vector<std::size_t> input_indices_for(
    const ComponentWorldState& current,
    std::span<const InputCommand> canonical) {
    std::vector<std::size_t> input_indices;
    std::size_t cursor = 0U;
    while (cursor < canonical.size()) {
        const EntityId entity = canonical[cursor].entity;
        const std::size_t index = find_entity_index(current, entity);
        if (index != current.body_count()) input_indices.push_back(index);
        while (cursor < canonical.size() && canonical[cursor].entity == entity) ++cursor;
    }
    std::sort(input_indices.begin(), input_indices.end());
    input_indices.erase(std::unique(input_indices.begin(), input_indices.end()), input_indices.end());
    return input_indices;
}

[[nodiscard]] std::vector<std::size_t> candidates_for(
    const DeterministicActiveSet& active,
    std::span<const std::size_t> input_indices) {
    std::vector<std::size_t> candidates;
    candidates.reserve(active.size() + input_indices.size());
    std::set_union(active.indices().begin(), active.indices().end(),
                   input_indices.begin(), input_indices.end(), std::back_inserter(candidates));
    return candidates;
}

void append_direct_update(
    const ComponentWorldState& current,
    std::size_t index,
    Fixed acceleration_x,
    Fixed acceleration_y,
    Fixed position_delta_x,
    Fixed position_delta_y,
    std::vector<Update>& updates,
    std::vector<std::size_t>& next_active,
    DirtySet& dirty,
    ComponentAllocationStats& allocation) {
    const Fixed before_position_x = current.position_x_at(index);
    const Fixed before_position_y = current.position_y_at(index);
    const Fixed before_velocity_x = current.velocity_x_at(index);
    const Fixed before_velocity_y = current.velocity_y_at(index);
    const Fixed after_velocity_x = before_velocity_x + acceleration_x * kSimulationDelta;
    const Fixed after_velocity_y = before_velocity_y + acceleration_y * kSimulationDelta;
    const Fixed after_position_x = before_position_x + position_delta_x;
    const Fixed after_position_y = before_position_y + position_delta_y;

    std::uint8_t mask = 0U;
    if (after_position_x != before_position_x) mask |= component_mask(DirtyComponent::PositionX);
    if (after_position_y != before_position_y) mask |= component_mask(DirtyComponent::PositionY);
    if (after_velocity_x != before_velocity_x) mask |= component_mask(DirtyComponent::VelocityX);
    if (after_velocity_y != before_velocity_y) mask |= component_mask(DirtyComponent::VelocityY);
    if (mask != 0U) {
        updates.push_back(Update{
            .index = index,
            .position_x = after_position_x,
            .position_y = after_position_y,
            .velocity_x = after_velocity_x,
            .velocity_y = after_velocity_y,
            .mask = mask,
        });
        dirty.mark(index, static_cast<DirtyComponent>(mask));
        ++allocation.changed_bodies;
    }
    if (is_active(after_velocity_x, after_velocity_y)) next_active.push_back(index);
}


} // namespace

ComponentAllocationStats& ComponentAllocationStats::operator+=(
    const ComponentAllocationStats& rhs) noexcept {
    component_pages_allocated += rhs.component_pages_allocated;
    directories_allocated += rhs.directories_allocated;
    component_values_copied += rhs.component_values_copied;
    directory_entries_copied += rhs.directory_entries_copied;
    candidate_bodies_scanned += rhs.candidate_bodies_scanned;
    inactive_bodies_skipped += rhs.inactive_bodies_skipped;
    changed_bodies += rhs.changed_bodies;
    body_reconstructions += rhs.body_reconstructions;
    fixed_kernel.lanes += rhs.fixed_kernel.lanes;
    fixed_kernel.scalar_lanes += rhs.fixed_kernel.scalar_lanes;
    fixed_kernel.simd_lanes += rhs.fixed_kernel.simd_lanes;
    fixed_kernel.overflow_guard_fallback_lanes += rhs.fixed_kernel.overflow_guard_fallback_lanes;
    fixed_kernel.avx2_available = fixed_kernel.avx2_available || rhs.fixed_kernel.avx2_available;
    return *this;
}

EntityId ComponentWorldState::entity_at(std::size_t index) const {
    if (index >= body_count_ || !ids_) throw std::out_of_range("Component body index is outside world");
    return (*ids_)[index];
}

Fixed ComponentWorldState::position_x_at(std::size_t index) const {
    return component_at(position_x_, page_size_, index);
}

Fixed ComponentWorldState::position_y_at(std::size_t index) const {
    return component_at(position_y_, page_size_, index);
}

Fixed ComponentWorldState::velocity_x_at(std::size_t index) const {
    return component_at(velocity_x_, page_size_, index);
}

Fixed ComponentWorldState::velocity_y_at(std::size_t index) const {
    return component_at(velocity_y_, page_size_, index);
}

Body ComponentWorldState::body_at(std::size_t index) const {
    return Body{
        .id = entity_at(index),
        .position = {position_x_at(index), position_y_at(index)},
        .velocity = {velocity_x_at(index), velocity_y_at(index)},
    };
}

WorldState ComponentWorldState::materialize() const {
    WorldState state;
    state.frame = frame_;
    state.bodies.reserve(body_count_);
    for (std::size_t index = 0; index < body_count_; ++index) state.bodies.push_back(body_at(index));
    return state;
}

ComponentWorldState make_component_world(
    const WorldState& state,
    std::size_t page_size,
    ComponentAllocationStats* allocation_output) {
    validate_world(state);
    if (page_size == 0U) throw std::invalid_argument("Component page size must be greater than zero");
    ComponentAllocationStats allocation;
    ComponentWorldState result;
    result.frame_ = state.frame;
    result.body_count_ = state.bodies.size();
    result.page_size_ = page_size;
    result.page_count_ = (state.bodies.size() + page_size - 1U) / page_size;
    auto ids = std::make_shared<std::vector<EntityId>>();
    ids->reserve(state.bodies.size());
    for (const Body& body : state.bodies) ids->push_back(body.id);
    result.ids_ = std::move(ids);
    result.position_x_ = make_directory(state.bodies, page_size, ComponentIndex::PositionX, allocation);
    result.position_y_ = make_directory(state.bodies, page_size, ComponentIndex::PositionY, allocation);
    result.velocity_x_ = make_directory(state.bodies, page_size, ComponentIndex::VelocityX, allocation);
    result.velocity_y_ = make_directory(state.bodies, page_size, ComponentIndex::VelocityY, allocation);
    if (allocation_output != nullptr) *allocation_output += allocation;
    return result;
}

ComponentStepResult step_component_active(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs,
    ComponentStepOptions options) {
    ScopedBudgetMeasurement budget_scope(
        options.budget_monitor,
        options.budget_traces,
        {
            .id = BudgetId::EcsMaintenance,
            .subsystem = TraceSubsystem::Simulation,
            .limit_ns = options.budget_limit_ns,
            .exceed_severity = TraceSeverity::Warning,
        },
        options.correlation_id,
        current.frame());
    if (current.empty()) throw std::invalid_argument("Component step requires an initialized world");
    if (!active.indices().empty() && active.indices().back() >= current.body_count_) {
        throw std::invalid_argument("Component active set references a body outside world");
    }
    const std::vector<InputCommand> canonical = canonicalize_inputs(inputs);
    const std::vector<std::size_t> input_indices = input_indices_for(current, canonical);
    const std::vector<std::size_t> candidates = candidates_for(active, input_indices);

    ComponentAllocationStats allocation;
    DirtySet dirty(current.body_count_);

    if (canonical.empty() && candidates.size() == current.body_count_) {
        ComponentWorldState next = current;
        next.frame_ = current.frame_ + 1U;
        auto position_x = std::make_shared<Directory>(*current.position_x_);
        auto position_y = std::make_shared<Directory>(*current.position_y_);
        allocation.directories_allocated += 2U;
        allocation.directory_entries_copied += position_x->pages.size() + position_y->pages.size();
        std::vector<Fixed> velocity_x_buffer(current.page_size_);
        std::vector<Fixed> velocity_y_buffer(current.page_size_);
        std::vector<Fixed> delta_x_buffer(current.page_size_);
        std::vector<Fixed> delta_y_buffer(current.page_size_);
        std::size_t global_index = 0U;
        for (std::size_t page_index = 0U; page_index < current.page_count_; ++page_index) {
            auto page_x = std::make_shared<Page>(*current.position_x_->pages[page_index]);
            auto page_y = std::make_shared<Page>(*current.position_y_->pages[page_index]);
            const auto& velocity_page_x = current.velocity_x_->pages[page_index]->values;
            const auto& velocity_page_y = current.velocity_y_->pages[page_index]->values;
            const std::size_t count = page_x->values.size();
            ++allocation.component_pages_allocated;
            ++allocation.component_pages_allocated;
            allocation.component_values_copied += count * 2U;
            for (std::size_t lane = 0U; lane < count; ++lane) {
                velocity_x_buffer[lane] = velocity_page_x[lane];
                velocity_y_buffer[lane] = velocity_page_y[lane];
            }
            multiply_simulation_delta_exact(
                std::span<const Fixed>(velocity_x_buffer.data(), count),
                std::span<Fixed>(delta_x_buffer.data(), count), options.kernel_mode,
                &allocation.fixed_kernel);
            multiply_simulation_delta_exact(
                std::span<const Fixed>(velocity_y_buffer.data(), count),
                std::span<Fixed>(delta_y_buffer.data(), count), options.kernel_mode,
                &allocation.fixed_kernel);
            for (std::size_t lane = 0U; lane < count; ++lane, ++global_index) {
                std::uint8_t mask = 0U;
                if (delta_x_buffer[lane].raw() != 0) {
                    page_x->values[lane] += delta_x_buffer[lane];
                    mask |= component_mask(DirtyComponent::PositionX);
                }
                if (delta_y_buffer[lane].raw() != 0) {
                    page_y->values[lane] += delta_y_buffer[lane];
                    mask |= component_mask(DirtyComponent::PositionY);
                }
                if (mask != 0U) {
                    dirty.mark(global_index, static_cast<DirtyComponent>(mask));
                    ++allocation.changed_bodies;
                }
            }
            position_x->pages[page_index] = std::move(page_x);
            position_y->pages[page_index] = std::move(page_y);
        }
        allocation.candidate_bodies_scanned = current.body_count_;
        next.position_x_ = std::move(position_x);
        next.position_y_ = std::move(position_y);
        return ComponentStepResult{
            .state = std::move(next),
            .active = active,
            .dirty = std::move(dirty),
            .allocation = allocation,
        };
    }

    std::vector<Update> updates;
    std::vector<std::size_t> next_active;
    updates.reserve(candidates.size());
    next_active.reserve(candidates.size());

    constexpr std::size_t block_size = 128U;
    std::array<Fixed, block_size> velocity_x{};
    std::array<Fixed, block_size> velocity_y{};
    std::array<Fixed, block_size> delta_x{};
    std::array<Fixed, block_size> delta_y{};

    std::size_t candidate_cursor = 0U;
    std::size_t input_cursor = 0U;
    while (candidate_cursor < candidates.size()) {
        const std::size_t count = std::min(block_size, candidates.size() - candidate_cursor);
        for (std::size_t lane = 0U; lane < count; ++lane) {
            const std::size_t index = candidates[candidate_cursor + lane];
            velocity_x[lane] = current.velocity_x_at(index);
            velocity_y[lane] = current.velocity_y_at(index);
        }
        multiply_simulation_delta_exact(
            std::span<const Fixed>(velocity_x.data(), count),
            std::span<Fixed>(delta_x.data(), count), options.kernel_mode, &allocation.fixed_kernel);
        multiply_simulation_delta_exact(
            std::span<const Fixed>(velocity_y.data(), count),
            std::span<Fixed>(delta_y.data(), count), options.kernel_mode, &allocation.fixed_kernel);

        for (std::size_t lane = 0U; lane < count; ++lane) {
            const std::size_t index = candidates[candidate_cursor + lane];
            const EntityId entity = current.entity_at(index);
            ++allocation.candidate_bodies_scanned;
            while (input_cursor < canonical.size() && canonical[input_cursor].entity < entity) {
                ++input_cursor;
            }
            std::size_t cursor = input_cursor;
            Fixed acceleration_x{};
            Fixed acceleration_y{};
            while (cursor < canonical.size() && canonical[cursor].entity == entity) {
                acceleration_x += canonical[cursor].acceleration.x;
                acceleration_y += canonical[cursor].acceleration.y;
                ++cursor;
            }
            input_cursor = cursor;
            Fixed position_delta_x = delta_x[lane];
            Fixed position_delta_y = delta_y[lane];
            if (acceleration_x.raw() != 0 || acceleration_y.raw() != 0) {
                const Fixed after_velocity_x = velocity_x[lane] + acceleration_x * kSimulationDelta;
                const Fixed after_velocity_y = velocity_y[lane] + acceleration_y * kSimulationDelta;
                position_delta_x = after_velocity_x * kSimulationDelta;
                position_delta_y = after_velocity_y * kSimulationDelta;
                allocation.fixed_kernel.scalar_lanes += 2U;
                allocation.fixed_kernel.lanes += 2U;
            }
            append_direct_update(current, index, acceleration_x, acceleration_y,
                position_delta_x, position_delta_y, updates, next_active, dirty, allocation);
        }
        candidate_cursor += count;
    }
    allocation.inactive_bodies_skipped = current.body_count_ - candidates.size();
    ComponentWorldState next = current;
    next.frame_ = current.frame_ + 1U;
    next.position_x_ = apply_component_updates(current.position_x_, current.page_size_, updates,
        DirtyComponent::PositionX, allocation);
    next.position_y_ = apply_component_updates(current.position_y_, current.page_size_, updates,
        DirtyComponent::PositionY, allocation);
    next.velocity_x_ = apply_component_updates(current.velocity_x_, current.page_size_, updates,
        DirtyComponent::VelocityX, allocation);
    next.velocity_y_ = apply_component_updates(current.velocity_y_, current.page_size_, updates,
        DirtyComponent::VelocityY, allocation);
    return ComponentStepResult{
        .state = std::move(next),
        .active = DeterministicActiveSet(std::move(next_active)),
        .dirty = std::move(dirty),
        .allocation = allocation,
    };
}

ComponentStepResult step_component_active_legacy(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs) {
    if (current.empty()) throw std::invalid_argument("Component step requires an initialized world");
    if (!active.indices().empty() && active.indices().back() >= current.body_count_) {
        throw std::invalid_argument("Component active set references a body outside world");
    }
    const std::vector<InputCommand> canonical = canonicalize_inputs(inputs);
    const std::vector<std::size_t> input_indices = input_indices_for(current, canonical);
    const std::vector<std::size_t> candidates = candidates_for(active, input_indices);

    ComponentAllocationStats allocation;
    DirtySet dirty(current.body_count_);
    std::vector<Update> updates;
    std::vector<std::size_t> next_active;
    updates.reserve(candidates.size());
    next_active.reserve(candidates.size());
    std::size_t input_cursor = 0U;
    for (const std::size_t index : candidates) {
        const Body before = current.body_at(index);
        ++allocation.body_reconstructions;
        ++allocation.candidate_bodies_scanned;
        while (input_cursor < canonical.size() && canonical[input_cursor].entity < before.id) {
            ++input_cursor;
        }
        std::size_t cursor = input_cursor;
        Vec2 acceleration{};
        while (cursor < canonical.size() && canonical[cursor].entity == before.id) {
            acceleration.x += canonical[cursor].acceleration.x;
            acceleration.y += canonical[cursor].acceleration.y;
            ++cursor;
        }
        input_cursor = cursor;
        Body after = before;
        after.velocity.x += acceleration.x * kSimulationDelta;
        after.velocity.y += acceleration.y * kSimulationDelta;
        after.position.x += after.velocity.x * kSimulationDelta;
        after.position.y += after.velocity.y * kSimulationDelta;
        allocation.fixed_kernel.lanes += 2U;
        allocation.fixed_kernel.scalar_lanes += 2U;
        std::uint8_t mask = 0U;
        if (after.position.x != before.position.x) mask |= component_mask(DirtyComponent::PositionX);
        if (after.position.y != before.position.y) mask |= component_mask(DirtyComponent::PositionY);
        if (after.velocity.x != before.velocity.x) mask |= component_mask(DirtyComponent::VelocityX);
        if (after.velocity.y != before.velocity.y) mask |= component_mask(DirtyComponent::VelocityY);
        if (mask != 0U) {
            updates.push_back(Update{
                .index = index,
                .position_x = after.position.x,
                .position_y = after.position.y,
                .velocity_x = after.velocity.x,
                .velocity_y = after.velocity.y,
                .mask = mask,
            });
            dirty.mark(index, static_cast<DirtyComponent>(mask));
            ++allocation.changed_bodies;
        }
        if (is_active(after.velocity.x, after.velocity.y)) next_active.push_back(index);
    }
    allocation.inactive_bodies_skipped = current.body_count_ - candidates.size();
    ComponentWorldState next = current;
    next.frame_ = current.frame_ + 1U;
    next.position_x_ = apply_component_updates(current.position_x_, current.page_size_, updates,
        DirtyComponent::PositionX, allocation);
    next.position_y_ = apply_component_updates(current.position_y_, current.page_size_, updates,
        DirtyComponent::PositionY, allocation);
    next.velocity_x_ = apply_component_updates(current.velocity_x_, current.page_size_, updates,
        DirtyComponent::VelocityX, allocation);
    next.velocity_y_ = apply_component_updates(current.velocity_y_, current.page_size_, updates,
        DirtyComponent::VelocityY, allocation);
    return ComponentStepResult{
        .state = std::move(next),
        .active = DeterministicActiveSet(std::move(next_active)),
        .dirty = std::move(dirty),
        .allocation = allocation,
    };
}

ComponentWorldState apply_component_patches(
    const ComponentWorldState& current,
    std::span<const ComponentPatch> patches,
    ComponentAllocationStats* allocation_output) {
    if (current.empty()) throw std::invalid_argument("Component patch requires initialized world");
    if (patches.empty()) return current;

    std::size_t previous_index = current.body_count();
    for (const ComponentPatch& patch : patches) {
        if (patch.index >= current.body_count()) {
            throw std::out_of_range("Component patch index is outside world");
        }
        if (previous_index != current.body_count() && patch.index <= previous_index) {
            throw std::invalid_argument("Component patches must be strictly ordered by index");
        }
        previous_index = patch.index;
        const std::uint8_t mutable_mask = component_mask(DirtyComponent::PositionX)
            | component_mask(DirtyComponent::PositionY)
            | component_mask(DirtyComponent::VelocityX)
            | component_mask(DirtyComponent::VelocityY);
        if ((patch.mask & ~mutable_mask) != 0U || patch.mask == 0U) {
            throw std::invalid_argument("Component patch contains invalid mask");
        }
    }

    ComponentAllocationStats allocation;
    ComponentWorldState next = current;
    next.position_x_ = apply_component_patches_direct(
        current.position_x_, current.page_size_, patches,
        DirtyComponent::PositionX, allocation);
    next.position_y_ = apply_component_patches_direct(
        current.position_y_, current.page_size_, patches,
        DirtyComponent::PositionY, allocation);
    next.velocity_x_ = apply_component_patches_direct(
        current.velocity_x_, current.page_size_, patches,
        DirtyComponent::VelocityX, allocation);
    next.velocity_y_ = apply_component_patches_direct(
        current.velocity_y_, current.page_size_, patches,
        DirtyComponent::VelocityY, allocation);
    allocation.changed_bodies = patches.size();
    if (allocation_output != nullptr) *allocation_output += allocation;
    return next;
}

ComponentWorldState apply_component_patches_next_frame(
    const ComponentWorldState& current,
    std::span<const ComponentPatch> patches,
    ComponentAllocationStats* allocation) {
    ComponentWorldState next = apply_component_patches(current, patches, allocation);
    if (current.frame_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Component world frame counter would wrap");
    }
    next.frame_ = current.frame_ + 1U;
    return next;
}

bool component_equivalent_to(
    const ComponentWorldState& component,
    const WorldState& materialized) noexcept {
    try {
        return component.materialize() == materialized;
    } catch (...) {
        return false;
    }
}

ComponentMemoryFootprint estimate_retained_component_memory(
    std::span<const ComponentWorldState> states) {
    std::unordered_set<const Directory*> directories;
    std::unordered_set<const Page*> pages;
    std::unordered_set<const std::vector<EntityId>*> ids;
    for (const ComponentWorldState& state : states) {
        if (state.ids_) ids.insert(state.ids_.get());
        for (const auto& directory : std::array{
                 state.position_x_, state.position_y_, state.velocity_x_, state.velocity_y_}) {
            if (!directory || !directories.insert(directory.get()).second) continue;
            for (const auto& page : directory->pages) pages.insert(page.get());
        }
    }
    ComponentMemoryFootprint result;
    result.unique_directories = directories.size();
    result.unique_pages = pages.size();
    for (const auto* identifier : ids) {
        result.payload_bytes += identifier->capacity() * sizeof(EntityId);
        result.metadata_bytes += sizeof(*identifier);
    }
    for (const Directory* directory : directories) {
        result.metadata_bytes += sizeof(Directory)
            + directory->pages.capacity() * sizeof(std::shared_ptr<const Page>);
    }
    for (const Page* page : pages) {
        result.payload_bytes += page->values.capacity() * sizeof(Fixed);
        result.metadata_bytes += sizeof(Page);
    }
    return result;
}

} // namespace neoeng::core
