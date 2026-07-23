#pragma once

#include "neoeng/core/active_world.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace neoeng::core {

namespace detail {
struct RadixNode;
}

struct RadixAllocationStats final {
    std::uint64_t chunks_allocated{};
    std::uint64_t nodes_allocated{};
    std::uint64_t child_slots_copied{};
    std::uint64_t bodies_copied{};
    std::uint64_t candidate_bodies_scanned{};
    std::uint64_t inactive_bodies_skipped{};
    std::uint64_t changed_bodies{};

    RadixAllocationStats& operator+=(const RadixAllocationStats& rhs) noexcept;
};

struct RadixMemoryFootprint final {
    std::size_t unique_chunks{};
    std::size_t unique_nodes{};
    std::size_t payload_bytes{};
    std::size_t metadata_bytes{};
};

struct RadixStepResult;

class RadixWorldState final {
public:
    RadixWorldState() = default;

    [[nodiscard]] std::uint64_t frame() const noexcept { return frame_; }
    [[nodiscard]] std::size_t body_count() const noexcept { return body_count_; }
    [[nodiscard]] std::size_t chunk_size() const noexcept { return chunk_size_; }
    [[nodiscard]] std::size_t fanout() const noexcept { return fanout_; }
    [[nodiscard]] std::size_t depth() const noexcept { return depth_; }
    [[nodiscard]] std::size_t chunk_count() const noexcept { return chunk_count_; }
    [[nodiscard]] bool empty() const noexcept { return body_count_ == 0U; }

    [[nodiscard]] Body body_at(std::size_t index) const;
    [[nodiscard]] WorldState materialize() const;

private:
    friend RadixWorldState make_radix_world(
        const WorldState&, std::size_t, std::size_t, RadixAllocationStats*);
    friend RadixStepResult step_radix_active(
        const RadixWorldState&, const DeterministicActiveSet&,
        std::span<const InputCommand>);
    friend RadixMemoryFootprint estimate_retained_radix_memory(
        std::span<const RadixWorldState>);

    std::uint64_t frame_{};
    std::size_t body_count_{};
    std::size_t chunk_size_{64U};
    std::size_t chunk_count_{};
    std::size_t fanout_{16U};
    std::size_t depth_{};
    std::shared_ptr<const detail::RadixNode> root_{};
};

struct RadixStepResult final {
    RadixWorldState state{};
    DeterministicActiveSet active{};
    DirtySet dirty{};
    RadixAllocationStats allocation{};
};

[[nodiscard]] RadixWorldState make_radix_world(
    const WorldState& state,
    std::size_t chunk_size = 64U,
    std::size_t fanout = 16U,
    RadixAllocationStats* allocation = nullptr);

[[nodiscard]] RadixStepResult step_radix_active(
    const RadixWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs);

[[nodiscard]] bool radix_equivalent_to(
    const RadixWorldState& radix,
    const WorldState& materialized) noexcept;

[[nodiscard]] RadixMemoryFootprint estimate_retained_radix_memory(
    std::span<const RadixWorldState> states);

} // namespace neoeng::core
