#include "neoeng/core/immutable_world.hpp"

#include "neoeng/core/hash.hpp"
#include "neoeng/core/simulation.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace neoeng::core {
namespace {

constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;
constexpr std::uint32_t kImmutableWorldMagic = 0x494F454EU; // "NEOI" in LE bytes
constexpr std::uint16_t kImmutableWorldVersion = 1U;
constexpr std::uint8_t kLeafDomain = 0x4CU;
constexpr std::uint8_t kInternalDomain = 0x49U;
constexpr std::uint8_t kEmptyDomain = 0x45U;
constexpr std::uint8_t kWorldDomain = 0x57U;

class HashBuilder final {
public:
    void byte(std::uint8_t value) noexcept {
        hash_ ^= value;
        hash_ *= kFnvPrime;
    }

    template <typename T>
    void little_endian(T value) noexcept {
        using U = std::make_unsigned_t<T>;
        U bits = static_cast<U>(value);
        for (std::size_t index = 0; index < sizeof(U); ++index) {
            byte(static_cast<std::uint8_t>(bits & U{0xFF}));
            bits >>= 8U;
        }
    }

    [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
    std::uint64_t hash_{kFnvOffset};
};

struct ImmutableBodyChunk final {
    std::size_t index{};
    std::vector<Body> bodies{};
    std::uint64_t hash{};
};

[[nodiscard]] std::uint64_t hash_empty_leaf(std::size_t index) noexcept {
    HashBuilder builder;
    builder.byte(kEmptyDomain);
    builder.little_endian(static_cast<std::uint64_t>(index));
    return builder.value();
}

[[nodiscard]] std::uint64_t hash_chunk(
    std::size_t index, std::span<const Body> bodies) noexcept {
    HashBuilder builder;
    builder.byte(kLeafDomain);
    builder.little_endian(kCanonicalWorldFormatVersion);
    builder.little_endian(static_cast<std::uint64_t>(index));
    builder.little_endian(static_cast<std::uint64_t>(bodies.size()));
    for (const Body& body : bodies) {
        builder.little_endian(body.id);
        builder.little_endian(body.position.x.raw());
        builder.little_endian(body.position.y.raw());
        builder.little_endian(body.velocity.x.raw());
        builder.little_endian(body.velocity.y.raw());
    }
    return builder.value();
}

[[nodiscard]] std::uint64_t hash_internal(
    std::size_t begin,
    std::size_t end,
    std::uint64_t left,
    std::uint64_t right) noexcept {
    HashBuilder builder;
    builder.byte(kInternalDomain);
    builder.little_endian(static_cast<std::uint64_t>(begin));
    builder.little_endian(static_cast<std::uint64_t>(end));
    builder.little_endian(left);
    builder.little_endian(right);
    return builder.value();
}

[[nodiscard]] std::uint64_t hash_world(
    std::uint64_t frame,
    std::size_t body_count,
    std::size_t chunk_size,
    std::uint64_t body_root) noexcept {
    HashBuilder builder;
    builder.byte(kWorldDomain);
    builder.little_endian(kImmutableWorldMagic);
    builder.little_endian(kImmutableWorldVersion);
    builder.little_endian(kCanonicalWorldFormatVersion);
    builder.little_endian(frame);
    builder.little_endian(static_cast<std::uint64_t>(body_count));
    builder.little_endian(static_cast<std::uint64_t>(chunk_size));
    builder.little_endian(body_root);
    return builder.value();
}

[[nodiscard]] std::size_t tree_leaf_capacity(std::size_t chunks) {
    if (chunks <= 1U) return 1U;
    return std::bit_ceil(chunks);
}

} // namespace

namespace detail {

struct ImmutableChunkNode final {
    std::size_t begin{};
    std::size_t end{};
    std::shared_ptr<const ImmutableChunkNode> left{};
    std::shared_ptr<const ImmutableChunkNode> right{};
    std::shared_ptr<const ImmutableBodyChunk> chunk{};
    std::uint64_t hash{};
};

} // namespace detail

namespace {

using Node = detail::ImmutableChunkNode;

[[nodiscard]] std::shared_ptr<const ImmutableBodyChunk> make_chunk(
    std::size_t index,
    std::vector<Body> bodies,
    ImmutableAllocationStats& allocation) {
    auto chunk = std::make_shared<ImmutableBodyChunk>();
    chunk->index = index;
    chunk->hash = hash_chunk(index, bodies);
    chunk->bodies = std::move(bodies);
    ++allocation.chunks_allocated;
    allocation.chunk_payload_bytes_requested += chunk->bodies.capacity() * sizeof(Body);
    allocation.bodies_copied += chunk->bodies.size();
    return chunk;
}

[[nodiscard]] std::shared_ptr<const Node> make_leaf(
    std::size_t index,
    std::shared_ptr<const ImmutableBodyChunk> chunk,
    ImmutableAllocationStats& allocation) {
    auto node = std::make_shared<Node>();
    node->begin = index;
    node->end = index + 1U;
    node->chunk = std::move(chunk);
    node->hash = node->chunk ? node->chunk->hash : hash_empty_leaf(index);
    ++allocation.tree_nodes_allocated;
    allocation.tree_metadata_bytes_requested += sizeof(Node);
    return node;
}

[[nodiscard]] std::shared_ptr<const Node> make_internal(
    std::size_t begin,
    std::size_t end,
    std::shared_ptr<const Node> left,
    std::shared_ptr<const Node> right,
    ImmutableAllocationStats& allocation) {
    auto node = std::make_shared<Node>();
    node->begin = begin;
    node->end = end;
    node->left = std::move(left);
    node->right = std::move(right);
    node->hash = hash_internal(begin, end, node->left->hash, node->right->hash);
    ++allocation.tree_nodes_allocated;
    allocation.tree_metadata_bytes_requested += sizeof(Node);
    return node;
}

[[nodiscard]] std::shared_ptr<const Node> build_tree(
    std::size_t begin,
    std::size_t end,
    const std::vector<std::shared_ptr<const ImmutableBodyChunk>>& chunks,
    ImmutableAllocationStats& allocation) {
    if (end - begin == 1U) {
        return make_leaf(begin, begin < chunks.size() ? chunks[begin] : nullptr, allocation);
    }
    const std::size_t middle = begin + (end - begin) / 2U;
    auto left = build_tree(begin, middle, chunks, allocation);
    auto right = build_tree(middle, end, chunks, allocation);
    return make_internal(begin, end, std::move(left), std::move(right), allocation);
}

[[nodiscard]] const ImmutableBodyChunk* find_chunk(
    const std::shared_ptr<const Node>& node,
    std::size_t index) noexcept {
    const Node* cursor = node.get();
    while (cursor != nullptr && cursor->end - cursor->begin > 1U) {
        const std::size_t middle = cursor->begin + (cursor->end - cursor->begin) / 2U;
        cursor = index < middle ? cursor->left.get() : cursor->right.get();
    }
    return cursor != nullptr && cursor->chunk ? cursor->chunk.get() : nullptr;
}

[[nodiscard]] std::shared_ptr<const Node> replace_chunk(
    const std::shared_ptr<const Node>& node,
    std::size_t index,
    std::shared_ptr<const ImmutableBodyChunk> chunk,
    ImmutableAllocationStats& allocation) {
    if (!node || index < node->begin || index >= node->end) {
        throw std::out_of_range("Immutable chunk index is outside the tree");
    }
    if (node->end - node->begin == 1U) {
        return make_leaf(index, std::move(chunk), allocation);
    }
    const std::size_t middle = node->begin + (node->end - node->begin) / 2U;
    if (index < middle) {
        auto left = replace_chunk(node->left, index, std::move(chunk), allocation);
        return make_internal(node->begin, node->end, std::move(left), node->right, allocation);
    }
    auto right = replace_chunk(node->right, index, std::move(chunk), allocation);
    return make_internal(node->begin, node->end, node->left, std::move(right), allocation);
}


struct ChunkReplacement final {
    std::size_t index{};
    std::shared_ptr<const ImmutableBodyChunk> chunk{};
};

[[nodiscard]] std::shared_ptr<const Node> replace_chunks_batch(
    const std::shared_ptr<const Node>& node,
    std::span<const ChunkReplacement> replacements,
    ImmutableAllocationStats& allocation) {
    if (replacements.empty()) return node;
    if (!node) throw std::logic_error("Immutable batch update encountered a null tree node");
    if (replacements.front().index < node->begin || replacements.back().index >= node->end) {
        throw std::out_of_range("Immutable batch update is outside the tree range");
    }
    if (node->end - node->begin == 1U) {
        if (replacements.size() != 1U || replacements.front().index != node->begin) {
            throw std::logic_error("Immutable batch update has conflicting leaf replacements");
        }
        return make_leaf(node->begin, replacements.front().chunk, allocation);
    }

    const std::size_t middle = node->begin + (node->end - node->begin) / 2U;
    const auto split = std::lower_bound(
        replacements.begin(), replacements.end(), middle,
        [](const ChunkReplacement& replacement, std::size_t value) {
            return replacement.index < value;
        });
    const std::span<const ChunkReplacement> left_replacements(
        replacements.begin(), split);
    const std::span<const ChunkReplacement> right_replacements(
        split, replacements.end());
    auto left = replace_chunks_batch(node->left, left_replacements, allocation);
    auto right = replace_chunks_batch(node->right, right_replacements, allocation);
    if (left == node->left && right == node->right) return node;
    return make_internal(node->begin, node->end, std::move(left), std::move(right), allocation);
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

} // namespace

ImmutableAllocationStats& ImmutableAllocationStats::operator+=(
    const ImmutableAllocationStats& rhs) noexcept {
    chunks_allocated += rhs.chunks_allocated;
    tree_nodes_allocated += rhs.tree_nodes_allocated;
    chunk_payload_bytes_requested += rhs.chunk_payload_bytes_requested;
    tree_metadata_bytes_requested += rhs.tree_metadata_bytes_requested;
    bodies_copied += rhs.bodies_copied;
    bodies_scanned += rhs.bodies_scanned;
    changed_bodies += rhs.changed_bodies;
    return *this;
}

Body ImmutableWorldState::body_at(std::size_t index) const {
    if (index >= body_count_) {
        throw std::out_of_range("Immutable body index is outside the world");
    }
    const std::size_t chunk_index = index / chunk_size_;
    const std::size_t local_index = index % chunk_size_;
    const ImmutableBodyChunk* chunk = find_chunk(root_, chunk_index);
    if (chunk == nullptr || local_index >= chunk->bodies.size()) {
        throw std::logic_error("Immutable tree does not contain the requested body");
    }
    return chunk->bodies[local_index];
}

WorldState ImmutableWorldState::materialize() const {
    WorldState state;
    state.frame = frame_;
    state.bodies.reserve(body_count_);
    for (std::size_t chunk_index = 0; chunk_index < chunk_count_; ++chunk_index) {
        const ImmutableBodyChunk* chunk = find_chunk(root_, chunk_index);
        if (chunk == nullptr) {
            throw std::logic_error("Immutable tree contains a missing body chunk");
        }
        state.bodies.insert(state.bodies.end(), chunk->bodies.begin(), chunk->bodies.end());
    }
    if (state.bodies.size() != body_count_) {
        throw std::logic_error("Immutable materialization produced the wrong body count");
    }
    return state;
}

std::uint64_t ImmutableWorldState::merkle_hash() const noexcept {
    const std::uint64_t root_hash = root_ ? root_->hash : hash_empty_leaf(0U);
    return hash_world(frame_, body_count_, chunk_size_, root_hash);
}

ImmutableWorldState make_immutable_world(
    const WorldState& state,
    std::size_t chunk_size,
    ImmutableAllocationStats* allocation_output) {
    validate_world(state);
    if (chunk_size == 0U) {
        throw std::invalid_argument("Immutable chunk size must be greater than zero");
    }

    ImmutableAllocationStats allocation;
    std::vector<std::shared_ptr<const ImmutableBodyChunk>> chunks;
    chunks.reserve((state.bodies.size() + chunk_size - 1U) / chunk_size);
    for (std::size_t begin = 0; begin < state.bodies.size(); begin += chunk_size) {
        const std::size_t end = std::min(begin + chunk_size, state.bodies.size());
        std::vector<Body> bodies(state.bodies.begin() + static_cast<std::ptrdiff_t>(begin),
                                 state.bodies.begin() + static_cast<std::ptrdiff_t>(end));
        chunks.push_back(make_chunk(chunks.size(), std::move(bodies), allocation));
    }

    ImmutableWorldState result;
    result.frame_ = state.frame;
    result.body_count_ = state.bodies.size();
    result.chunk_size_ = chunk_size;
    result.chunk_count_ = chunks.size();
    const std::size_t leaves = tree_leaf_capacity(result.chunk_count_);
    result.root_ = build_tree(0U, leaves, chunks, allocation);
    if (allocation_output != nullptr) *allocation_output += allocation;
    return result;
}

ImmutableStepResult step_immutable(
    const ImmutableWorldState& current,
    std::span<const InputCommand> inputs) {
    if (!current.root_) {
        throw std::invalid_argument("Immutable world is not initialized");
    }

    const std::vector<InputCommand> canonical_inputs = canonicalize_inputs(inputs);
    ImmutableStepResult result{
        .state = current,
        .dirty = DirtySet(current.body_count_),
        .allocation = {},
    };
    result.state.frame_ = current.frame_ + 1U;

    std::size_t input_index = 0U;
    std::shared_ptr<const Node> root = current.root_;
    for (std::size_t chunk_index = 0; chunk_index < current.chunk_count_; ++chunk_index) {
        const ImmutableBodyChunk* source = find_chunk(current.root_, chunk_index);
        if (source == nullptr) throw std::logic_error("Immutable source chunk is missing");

        std::vector<Body> modified;
        bool copied = false;
        for (std::size_t local_index = 0; local_index < source->bodies.size(); ++local_index) {
            const std::size_t body_index = chunk_index * current.chunk_size_ + local_index;
            const Body& before = source->bodies[local_index];
            ++result.allocation.bodies_scanned;

            while (input_index < canonical_inputs.size()
                   && canonical_inputs[input_index].entity < before.id) {
                ++input_index;
            }
            std::size_t cursor = input_index;
            Vec2 total_acceleration{};
            while (cursor < canonical_inputs.size()
                   && canonical_inputs[cursor].entity == before.id) {
                total_acceleration.x += canonical_inputs[cursor].acceleration.x;
                total_acceleration.y += canonical_inputs[cursor].acceleration.y;
                ++cursor;
            }
            input_index = cursor;

            Body after = before;
            after.velocity.x += total_acceleration.x * kSimulationDelta;
            after.velocity.y += total_acceleration.y * kSimulationDelta;
            after.position.x += after.velocity.x * kSimulationDelta;
            after.position.y += after.velocity.y * kSimulationDelta;

            std::uint8_t mask = 0U;
            if (after.position.x != before.position.x) mask |= component_mask(DirtyComponent::PositionX);
            if (after.position.y != before.position.y) mask |= component_mask(DirtyComponent::PositionY);
            if (after.velocity.x != before.velocity.x) mask |= component_mask(DirtyComponent::VelocityX);
            if (after.velocity.y != before.velocity.y) mask |= component_mask(DirtyComponent::VelocityY);
            if (mask == 0U) continue;

            if (!copied) {
                modified = source->bodies;
                copied = true;
            }
            modified[local_index] = after;
            result.dirty.mark(body_index, static_cast<DirtyComponent>(mask));
            ++result.allocation.changed_bodies;
        }

        if (copied) {
            auto chunk = make_chunk(chunk_index, std::move(modified), result.allocation);
            root = replace_chunk(root, chunk_index, std::move(chunk), result.allocation);
        }
    }
    result.state.root_ = std::move(root);
    return result;
}

ImmutableWorldState apply_immutable_updates(
    const ImmutableWorldState& base,
    std::uint64_t new_frame,
    std::span<const std::size_t> indices,
    std::span<const Body> bodies,
    ImmutableAllocationStats* allocation_output) {
    if (indices.size() != bodies.size()) {
        throw std::invalid_argument("Immutable update index/body spans have different sizes");
    }
    if (new_frame < base.frame_) {
        throw std::invalid_argument("Immutable update frame cannot move backwards");
    }
    if (!std::is_sorted(indices.begin(), indices.end())) {
        throw std::invalid_argument("Immutable update indices must be sorted");
    }
    if (std::adjacent_find(indices.begin(), indices.end()) != indices.end()) {
        throw std::invalid_argument("Immutable update indices must be unique");
    }

    ImmutableAllocationStats allocation;
    ImmutableWorldState result = base;
    result.frame_ = new_frame;
    if (indices.empty()) {
        if (allocation_output != nullptr) *allocation_output += allocation;
        return result;
    }

    std::vector<ChunkReplacement> replacements;
    replacements.reserve(indices.size());
    std::size_t cursor = 0U;
    while (cursor < indices.size()) {
        const std::size_t index = indices[cursor];
        if (index >= base.body_count_) {
            throw std::out_of_range("Immutable update index is outside world");
        }
        const std::size_t chunk_index = index / base.chunk_size_;
        const ImmutableBodyChunk* source = find_chunk(base.root_, chunk_index);
        if (source == nullptr) throw std::logic_error("Immutable update source chunk is missing");
        std::vector<Body> modified = source->bodies;
        while (cursor < indices.size() && indices[cursor] / base.chunk_size_ == chunk_index) {
            const std::size_t local = indices[cursor] % base.chunk_size_;
            modified[local] = bodies[cursor];
            ++allocation.changed_bodies;
            ++cursor;
        }
        replacements.push_back(ChunkReplacement{
            .index = chunk_index,
            .chunk = make_chunk(chunk_index, std::move(modified), allocation),
        });
    }
    result.root_ = replace_chunks_batch(base.root_, replacements, allocation);
    if (allocation_output != nullptr) *allocation_output += allocation;
    return result;
}


ImmutableMemoryFootprint estimate_retained_immutable_memory(
    std::span<const ImmutableWorldState> states) {
    std::unordered_set<const Node*> nodes;
    std::unordered_set<const ImmutableBodyChunk*> chunks;
    std::vector<const Node*> stack;
    for (const ImmutableWorldState& state : states) {
        if (state.root_) stack.push_back(state.root_.get());
    }
    while (!stack.empty()) {
        const Node* node = stack.back();
        stack.pop_back();
        if (node == nullptr || !nodes.insert(node).second) continue;
        if (node->chunk) chunks.insert(node->chunk.get());
        if (node->left) stack.push_back(node->left.get());
        if (node->right) stack.push_back(node->right.get());
    }

    ImmutableMemoryFootprint result;
    result.unique_tree_nodes = nodes.size();
    result.unique_chunks = chunks.size();
    result.metadata_bytes = nodes.size() * sizeof(Node);
    for (const ImmutableBodyChunk* chunk : chunks) {
        result.payload_bytes += sizeof(ImmutableBodyChunk)
            + chunk->bodies.capacity() * sizeof(Body);
    }
    return result;
}

bool immutable_equivalent_to(
    const ImmutableWorldState& immutable,
    const WorldState& materialized) noexcept {
    try {
        return immutable.materialize() == materialized;
    } catch (...) {
        return false;
    }
}

} // namespace neoeng::core
