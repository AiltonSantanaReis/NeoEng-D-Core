#pragma once

#include "neoeng/core/dirty.hpp"
#include "neoeng/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace neoeng::core {

namespace detail {
struct ImmutableChunkNode;
}

struct ImmutableMemoryFootprint final {
    std::size_t unique_chunks{};
    std::size_t unique_tree_nodes{};
    std::size_t payload_bytes{};
    std::size_t metadata_bytes{};
};

struct ImmutableStepResult;

struct ImmutableAllocationStats final {
    std::uint64_t chunks_allocated{};
    std::uint64_t tree_nodes_allocated{};
    std::uint64_t chunk_payload_bytes_requested{};
    std::uint64_t tree_metadata_bytes_requested{};
    std::uint64_t bodies_copied{};
    std::uint64_t bodies_scanned{};
    std::uint64_t changed_bodies{};

    ImmutableAllocationStats& operator+=(const ImmutableAllocationStats& rhs) noexcept;
};

class ImmutableWorldState final {
public:
    ImmutableWorldState() = default;

    [[nodiscard]] std::uint64_t frame() const noexcept { return frame_; }
    [[nodiscard]] std::size_t body_count() const noexcept { return body_count_; }
    [[nodiscard]] std::size_t chunk_size() const noexcept { return chunk_size_; }
    [[nodiscard]] std::size_t chunk_count() const noexcept { return chunk_count_; }
    [[nodiscard]] bool empty() const noexcept { return body_count_ == 0U; }

    [[nodiscard]] Body body_at(std::size_t index) const;
    [[nodiscard]] WorldState materialize() const;

    // Domain-separated Merkle root over canonical body encodings plus world metadata.
    // This is not claimed to equal stable_hash(WorldState); equality is checked by
    // materializing and applying the canonical serializer.
    [[nodiscard]] std::uint64_t merkle_hash() const noexcept;

    auto operator<=>(const ImmutableWorldState&) const = delete;

private:
    friend ImmutableWorldState make_immutable_world(
        const WorldState&, std::size_t, ImmutableAllocationStats*);
    friend struct ImmutableStepResult;
    friend ImmutableStepResult step_immutable(
        const ImmutableWorldState&, std::span<const InputCommand>);
    friend ImmutableWorldState apply_immutable_updates(
        const ImmutableWorldState&, std::uint64_t, std::span<const std::size_t>,
        std::span<const Body>, ImmutableAllocationStats*);
    friend class ImmutableRollbackEngine;
    friend class PersistentCheckpointHistory;
    friend ImmutableMemoryFootprint estimate_retained_immutable_memory(
        std::span<const ImmutableWorldState>);

    std::uint64_t frame_{};
    std::size_t body_count_{};
    std::size_t chunk_size_{64U};
    std::size_t chunk_count_{};
    std::shared_ptr<const detail::ImmutableChunkNode> root_{};
};

struct ImmutableStepResult final {
    ImmutableWorldState state{};
    DirtySet dirty{};
    ImmutableAllocationStats allocation{};
};

[[nodiscard]] ImmutableWorldState make_immutable_world(
    const WorldState& state,
    std::size_t chunk_size = 64U,
    ImmutableAllocationStats* allocation = nullptr);

[[nodiscard]] ImmutableStepResult step_immutable(
    const ImmutableWorldState& current,
    std::span<const InputCommand> inputs);

[[nodiscard]] ImmutableWorldState apply_immutable_updates(
    const ImmutableWorldState& base,
    std::uint64_t new_frame,
    std::span<const std::size_t> indices,
    std::span<const Body> bodies,
    ImmutableAllocationStats* allocation = nullptr);

[[nodiscard]] bool immutable_equivalent_to(
    const ImmutableWorldState& immutable,
    const WorldState& materialized) noexcept;

[[nodiscard]] ImmutableMemoryFootprint estimate_retained_immutable_memory(
    std::span<const ImmutableWorldState> states);

} // namespace neoeng::core
