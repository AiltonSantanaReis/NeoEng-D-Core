#pragma once

#include "neoeng/core/immutable_world.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace neoeng::core {

enum class PersistentCheckpointPolicy : std::uint8_t {
    Fixed,
    Geometric,
    Adaptive,
};

struct PersistentCheckpointConfig final {
    std::size_t capacity{300U};
    std::size_t max_delta_depth{16U};
    PersistentCheckpointPolicy policy{PersistentCheckpointPolicy::Fixed};
    std::uint32_t adaptive_density_ppm{100'000U};
};

struct PersistentCheckpointStats final {
    std::uint64_t checkpoint_frames{};
    std::uint64_t delta_frames{};
    std::uint64_t delta_bodies_stored{};
    std::uint64_t restore_deltas_applied{};
    std::size_t retained_frames{};
    std::size_t live_delta_payload_bytes{};
    ImmutableMemoryFootprint checkpoint_memory{};
};

class PersistentCheckpointHistory final {
public:
    explicit PersistentCheckpointHistory(PersistentCheckpointConfig config = {});

    void capture(const ImmutableWorldState& state, const DirtySet& dirty);
    [[nodiscard]] ImmutableWorldState restore(std::uint64_t frame) const;
    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept;
    void truncate_after(std::uint64_t frame);

    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] PersistentCheckpointStats stats() const;

private:
    struct Entry final {
        std::uint64_t frame{};
        std::optional<ImmutableWorldState> checkpoint{};
        std::vector<std::size_t> indices{};
        std::vector<Body> bodies{};
    };

    [[nodiscard]] bool should_checkpoint(
        const ImmutableWorldState& state,
        const DirtySet& dirty) const noexcept;
    [[nodiscard]] std::size_t entry_index(std::uint64_t frame) const;
    void enforce_capacity();

    PersistentCheckpointConfig config_{};
    std::deque<Entry> entries_{};
    std::size_t delta_depth_{};
    mutable std::uint64_t restore_deltas_applied_{};
};

} // namespace neoeng::core
