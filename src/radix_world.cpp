#include "neoeng/core/radix_world.hpp"

#include "neoeng/core/simulation.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace neoeng::core {
namespace {

struct RadixBodyChunk final {
    std::size_t index{};
    std::vector<Body> bodies{};
};

} // namespace
namespace detail {

struct RadixNode final {
    std::size_t level{};
    std::size_t base{};
    std::vector<std::shared_ptr<const RadixNode>> children{};
    std::shared_ptr<const RadixBodyChunk> chunk{};
};

} // namespace detail
namespace {

using Node = detail::RadixNode;

[[nodiscard]] std::size_t checked_power(std::size_t base, std::size_t exponent) {
    std::size_t result = 1U;
    for (std::size_t i = 0; i < exponent; ++i) {
        if (result > std::numeric_limits<std::size_t>::max() / base) {
            throw std::overflow_error("Radix tree capacity overflow");
        }
        result *= base;
    }
    return result;
}

[[nodiscard]] std::size_t required_depth(std::size_t chunk_count, std::size_t fanout) {
    std::size_t depth = 0U;
    std::size_t capacity = 1U;
    while (capacity < std::max<std::size_t>(chunk_count, 1U)) {
        if (capacity > std::numeric_limits<std::size_t>::max() / fanout) {
            throw std::overflow_error("Radix tree depth overflow");
        }
        capacity *= fanout;
        ++depth;
    }
    return depth;
}

[[nodiscard]] std::shared_ptr<const RadixBodyChunk> make_chunk(
    std::size_t index,
    std::vector<Body> bodies,
    RadixAllocationStats& allocation) {
    auto chunk = std::make_shared<RadixBodyChunk>();
    chunk->index = index;
    chunk->bodies = std::move(bodies);
    ++allocation.chunks_allocated;
    allocation.bodies_copied += chunk->bodies.size();
    return chunk;
}

[[nodiscard]] std::shared_ptr<const Node> make_leaf(
    std::size_t index,
    std::shared_ptr<const RadixBodyChunk> chunk,
    RadixAllocationStats& allocation) {
    auto node = std::make_shared<Node>();
    node->level = 0U;
    node->base = index;
    node->chunk = std::move(chunk);
    ++allocation.nodes_allocated;
    return node;
}

[[nodiscard]] std::shared_ptr<const Node> build_tree(
    std::size_t level,
    std::size_t base,
    std::size_t fanout,
    std::span<const std::shared_ptr<const RadixBodyChunk>> chunks,
    RadixAllocationStats& allocation) {
    if (level == 0U) {
        return make_leaf(base, base < chunks.size() ? chunks[base] : nullptr, allocation);
    }
    auto node = std::make_shared<Node>();
    node->level = level;
    node->base = base;
    node->children.resize(fanout);
    const std::size_t child_capacity = checked_power(fanout, level - 1U);
    for (std::size_t slot = 0; slot < fanout; ++slot) {
        const std::size_t child_base = base + slot * child_capacity;
        if (child_base < chunks.size()) {
            node->children[slot] = build_tree(
                level - 1U, child_base, fanout, chunks, allocation);
        }
    }
    ++allocation.nodes_allocated;
    allocation.child_slots_copied += node->children.size();
    return node;
}

[[nodiscard]] const RadixBodyChunk* find_chunk(
    const std::shared_ptr<const Node>& root,
    std::size_t chunk_index,
    std::size_t fanout) noexcept {
    const Node* node = root.get();
    while (node != nullptr && node->level != 0U) {
        const std::size_t child_capacity = checked_power(fanout, node->level - 1U);
        const std::size_t slot = (chunk_index - node->base) / child_capacity;
        if (slot >= node->children.size()) return nullptr;
        node = node->children[slot].get();
    }
    return node != nullptr && node->chunk ? node->chunk.get() : nullptr;
}

struct Replacement final {
    std::size_t index{};
    std::shared_ptr<const RadixBodyChunk> chunk{};
};

[[nodiscard]] std::shared_ptr<const Node> replace_batch(
    const std::shared_ptr<const Node>& node,
    std::size_t level,
    std::size_t base,
    std::size_t fanout,
    std::span<const Replacement> replacements,
    RadixAllocationStats& allocation) {
    if (replacements.empty()) return node;
    if (level == 0U) {
        if (replacements.size() != 1U || replacements.front().index != base) {
            throw std::logic_error("Radix batch replacement has conflicting leaf updates");
        }
        return make_leaf(base, replacements.front().chunk, allocation);
    }
    if (!node) throw std::logic_error("Radix batch replacement encountered missing path");
    auto replacement_node = std::make_shared<Node>(*node);
    ++allocation.nodes_allocated;
    allocation.child_slots_copied += replacement_node->children.size();
    const std::size_t child_capacity = checked_power(fanout, level - 1U);
    std::size_t cursor = 0U;
    while (cursor < replacements.size()) {
        const std::size_t slot = (replacements[cursor].index - base) / child_capacity;
        const std::size_t child_base = base + slot * child_capacity;
        std::size_t end = cursor + 1U;
        while (end < replacements.size()
               && (replacements[end].index - base) / child_capacity == slot) {
            ++end;
        }
        replacement_node->children[slot] = replace_batch(
            replacement_node->children[slot], level - 1U, child_base, fanout,
            replacements.subspan(cursor, end - cursor), allocation);
        cursor = end;
    }
    return replacement_node;
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

[[nodiscard]] std::size_t find_entity_index(const RadixWorldState& state, EntityId entity) {
    std::size_t first = 0U;
    std::size_t count = state.body_count();
    while (count != 0U) {
        const std::size_t step = count / 2U;
        const std::size_t middle = first + step;
        if (state.body_at(middle).id < entity) {
            first = middle + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    if (first < state.body_count() && state.body_at(first).id == entity) return first;
    return state.body_count();
}

[[nodiscard]] bool body_is_active(const Body& body) noexcept {
    return body.velocity.x.raw() != 0 || body.velocity.y.raw() != 0;
}

} // namespace

RadixAllocationStats& RadixAllocationStats::operator+=(const RadixAllocationStats& rhs) noexcept {
    chunks_allocated += rhs.chunks_allocated;
    nodes_allocated += rhs.nodes_allocated;
    child_slots_copied += rhs.child_slots_copied;
    bodies_copied += rhs.bodies_copied;
    candidate_bodies_scanned += rhs.candidate_bodies_scanned;
    inactive_bodies_skipped += rhs.inactive_bodies_skipped;
    changed_bodies += rhs.changed_bodies;
    return *this;
}

Body RadixWorldState::body_at(std::size_t index) const {
    if (index >= body_count_) throw std::out_of_range("Radix body index is outside world");
    const std::size_t chunk_index = index / chunk_size_;
    const std::size_t local = index % chunk_size_;
    const RadixBodyChunk* chunk = find_chunk(root_, chunk_index, fanout_);
    if (!chunk || local >= chunk->bodies.size()) {
        throw std::logic_error("Radix tree is missing a body chunk");
    }
    return chunk->bodies[local];
}

WorldState RadixWorldState::materialize() const {
    WorldState result;
    result.frame = frame_;
    result.bodies.reserve(body_count_);
    for (std::size_t chunk_index = 0; chunk_index < chunk_count_; ++chunk_index) {
        const RadixBodyChunk* chunk = find_chunk(root_, chunk_index, fanout_);
        if (!chunk) throw std::logic_error("Radix materialization found a missing chunk");
        result.bodies.insert(result.bodies.end(), chunk->bodies.begin(), chunk->bodies.end());
    }
    if (result.bodies.size() != body_count_) {
        throw std::logic_error("Radix materialization produced wrong body count");
    }
    return result;
}

RadixWorldState make_radix_world(
    const WorldState& state,
    std::size_t chunk_size,
    std::size_t fanout,
    RadixAllocationStats* allocation_output) {
    validate_world(state);
    if (chunk_size == 0U) throw std::invalid_argument("Radix chunk size must be greater than zero");
    if (fanout < 2U || fanout > 64U) throw std::invalid_argument("Radix fanout must be between 2 and 64");
    RadixAllocationStats allocation;
    std::vector<std::shared_ptr<const RadixBodyChunk>> chunks;
    for (std::size_t begin = 0; begin < state.bodies.size(); begin += chunk_size) {
        const std::size_t end = std::min(begin + chunk_size, state.bodies.size());
        std::vector<Body> bodies(state.bodies.begin() + static_cast<std::ptrdiff_t>(begin),
                                 state.bodies.begin() + static_cast<std::ptrdiff_t>(end));
        chunks.push_back(make_chunk(chunks.size(), std::move(bodies), allocation));
    }
    RadixWorldState result;
    result.frame_ = state.frame;
    result.body_count_ = state.bodies.size();
    result.chunk_size_ = chunk_size;
    result.chunk_count_ = chunks.size();
    result.fanout_ = fanout;
    result.depth_ = required_depth(result.chunk_count_, fanout);
    result.root_ = build_tree(result.depth_, 0U, fanout, chunks, allocation);
    if (allocation_output != nullptr) *allocation_output += allocation;
    return result;
}

RadixStepResult step_radix_active(
    const RadixWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs) {
    if (current.empty()) throw std::invalid_argument("Radix step requires an initialized world");
    if (!active.indices().empty() && active.indices().back() >= current.body_count_) {
        throw std::invalid_argument("Radix active set references a body outside world");
    }
    const std::vector<InputCommand> canonical = canonicalize_inputs(inputs);
    std::vector<std::size_t> input_indices;
    std::size_t cursor = 0U;
    while (cursor < canonical.size()) {
        const EntityId entity = canonical[cursor].entity;
        const std::size_t index = find_entity_index(current, entity);
        if (index != current.body_count_) input_indices.push_back(index);
        while (cursor < canonical.size() && canonical[cursor].entity == entity) ++cursor;
    }
    std::sort(input_indices.begin(), input_indices.end());
    input_indices.erase(std::unique(input_indices.begin(), input_indices.end()), input_indices.end());
    std::vector<std::size_t> candidates;
    candidates.reserve(active.size() + input_indices.size());
    std::set_union(active.indices().begin(), active.indices().end(),
                   input_indices.begin(), input_indices.end(), std::back_inserter(candidates));

    RadixStepResult result{
        .state = current,
        .active = {},
        .dirty = DirtySet(current.body_count_),
        .allocation = {},
    };
    result.state.frame_ = current.frame_ + 1U;
    std::vector<std::size_t> next_active;
    std::vector<std::size_t> changed_indices;
    std::vector<Body> changed_bodies;
    next_active.reserve(candidates.size());
    changed_indices.reserve(candidates.size());
    changed_bodies.reserve(candidates.size());
    cursor = 0U;
    for (const std::size_t index : candidates) {
        const Body before = current.body_at(index);
        ++result.allocation.candidate_bodies_scanned;
        while (cursor < canonical.size() && canonical[cursor].entity < before.id) ++cursor;
        std::size_t command = cursor;
        Vec2 acceleration{};
        while (command < canonical.size() && canonical[command].entity == before.id) {
            acceleration.x += canonical[command].acceleration.x;
            acceleration.y += canonical[command].acceleration.y;
            ++command;
        }
        cursor = command;
        Body after = before;
        after.velocity.x += acceleration.x * kSimulationDelta;
        after.velocity.y += acceleration.y * kSimulationDelta;
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
            result.dirty.mark(index, static_cast<DirtyComponent>(mask));
            ++result.allocation.changed_bodies;
        }
        if (body_is_active(after)) next_active.push_back(index);
    }
    result.allocation.inactive_bodies_skipped = current.body_count_ - candidates.size();

    std::vector<Replacement> replacements;
    std::size_t update_cursor = 0U;
    while (update_cursor < changed_indices.size()) {
        const std::size_t chunk_index = changed_indices[update_cursor] / current.chunk_size_;
        const RadixBodyChunk* source = find_chunk(current.root_, chunk_index, current.fanout_);
        if (!source) throw std::logic_error("Radix update source chunk is missing");
        std::vector<Body> modified = source->bodies;
        while (update_cursor < changed_indices.size()
               && changed_indices[update_cursor] / current.chunk_size_ == chunk_index) {
            modified[changed_indices[update_cursor] % current.chunk_size_] = changed_bodies[update_cursor];
            ++update_cursor;
        }
        replacements.push_back(Replacement{
            .index = chunk_index,
            .chunk = make_chunk(chunk_index, std::move(modified), result.allocation),
        });
    }
    result.state.root_ = replace_batch(current.root_, current.depth_, 0U, current.fanout_,
                                       replacements, result.allocation);
    result.active = DeterministicActiveSet(std::move(next_active));
    return result;
}

bool radix_equivalent_to(const RadixWorldState& radix, const WorldState& materialized) noexcept {
    try {
        return radix.materialize() == materialized;
    } catch (...) {
        return false;
    }
}

RadixMemoryFootprint estimate_retained_radix_memory(std::span<const RadixWorldState> states) {
    std::unordered_set<const Node*> nodes;
    std::unordered_set<const RadixBodyChunk*> chunks;
    std::vector<const Node*> stack;
    for (const RadixWorldState& state : states) if (state.root_) stack.push_back(state.root_.get());
    while (!stack.empty()) {
        const Node* node = stack.back();
        stack.pop_back();
        if (!node || !nodes.insert(node).second) continue;
        if (node->chunk) chunks.insert(node->chunk.get());
        for (const auto& child : node->children) if (child) stack.push_back(child.get());
    }
    RadixMemoryFootprint result;
    result.unique_nodes = nodes.size();
    result.unique_chunks = chunks.size();
    for (const Node* node : nodes) {
        result.metadata_bytes += sizeof(Node)
            + node->children.capacity() * sizeof(std::shared_ptr<const Node>);
    }
    for (const RadixBodyChunk* chunk : chunks) {
        result.metadata_bytes += sizeof(RadixBodyChunk);
        result.payload_bytes += chunk->bodies.capacity() * sizeof(Body);
    }
    return result;
}

} // namespace neoeng::core
