#pragma once

#include "neoeng/core/active_world.hpp"
#include "neoeng/core/fixed_simd.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace neoeng::core {

namespace detail {
struct ComponentPage;
struct ComponentDirectory;
}

struct ComponentAllocationStats final {
    std::uint64_t component_pages_allocated{};
    std::uint64_t directories_allocated{};
    std::uint64_t component_values_copied{};
    std::uint64_t directory_entries_copied{};
    std::uint64_t candidate_bodies_scanned{};
    std::uint64_t inactive_bodies_skipped{};
    std::uint64_t changed_bodies{};
    std::uint64_t body_reconstructions{};
    FixedKernelStats fixed_kernel{};

    ComponentAllocationStats& operator+=(const ComponentAllocationStats& rhs) noexcept;
};

struct ComponentStepOptions;
struct ComponentStepResult;

struct ComponentPatch final {
    std::size_t index{};
    Fixed position_x{};
    Fixed position_y{};
    Fixed velocity_x{};
    Fixed velocity_y{};
    std::uint8_t mask{};

    auto operator<=>(const ComponentPatch&) const = default;
};

struct ComponentMemoryFootprint final {
    std::size_t unique_pages{};
    std::size_t unique_directories{};
    std::size_t payload_bytes{};
    std::size_t metadata_bytes{};
};

class ComponentWorldState final {
public:
    ComponentWorldState() = default;

    [[nodiscard]] std::uint64_t frame() const noexcept { return frame_; }
    [[nodiscard]] std::size_t body_count() const noexcept { return body_count_; }
    [[nodiscard]] std::size_t page_size() const noexcept { return page_size_; }
    [[nodiscard]] std::size_t page_count() const noexcept { return page_count_; }
    [[nodiscard]] bool empty() const noexcept { return body_count_ == 0U; }

    [[nodiscard]] EntityId entity_at(std::size_t index) const;
    [[nodiscard]] Fixed position_x_at(std::size_t index) const;
    [[nodiscard]] Fixed position_y_at(std::size_t index) const;
    [[nodiscard]] Fixed velocity_x_at(std::size_t index) const;
    [[nodiscard]] Fixed velocity_y_at(std::size_t index) const;
    [[nodiscard]] Body body_at(std::size_t index) const;
    [[nodiscard]] WorldState materialize() const;

private:
    friend ComponentWorldState make_component_world(
        const WorldState&, std::size_t, ComponentAllocationStats*);
    friend struct ComponentStepResult;
    friend ComponentStepResult step_component_active(
        const ComponentWorldState&, const DeterministicActiveSet&,
        std::span<const InputCommand>, ComponentStepOptions);
    friend ComponentStepResult step_component_active_legacy(
        const ComponentWorldState&, const DeterministicActiveSet&,
        std::span<const InputCommand>);
    friend ComponentMemoryFootprint estimate_retained_component_memory(
        std::span<const ComponentWorldState>);
    friend ComponentWorldState apply_component_patches(
        const ComponentWorldState&, std::span<const ComponentPatch>,
        ComponentAllocationStats*);
    friend ComponentWorldState apply_component_patches_next_frame(
        const ComponentWorldState&, std::span<const ComponentPatch>,
        ComponentAllocationStats*);

    std::uint64_t frame_{};
    std::size_t body_count_{};
    std::size_t page_size_{64U};
    std::size_t page_count_{};
    std::shared_ptr<const std::vector<EntityId>> ids_{};
    std::shared_ptr<const detail::ComponentDirectory> position_x_{};
    std::shared_ptr<const detail::ComponentDirectory> position_y_{};
    std::shared_ptr<const detail::ComponentDirectory> velocity_x_{};
    std::shared_ptr<const detail::ComponentDirectory> velocity_y_{};
};

struct ComponentStepOptions final {
    FixedKernelMode kernel_mode{FixedKernelMode::Auto};
};

struct ComponentStepResult final {
    ComponentWorldState state{};
    DeterministicActiveSet active{};
    DirtySet dirty{};
    ComponentAllocationStats allocation{};
};

[[nodiscard]] ComponentWorldState make_component_world(
    const WorldState& state,
    std::size_t page_size = 64U,
    ComponentAllocationStats* allocation = nullptr);

[[nodiscard]] ComponentStepResult step_component_active(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs,
    ComponentStepOptions options = {});

[[nodiscard]] ComponentStepResult step_component_active_legacy(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs);

[[nodiscard]] ComponentWorldState apply_component_patches(
    const ComponentWorldState& current,
    std::span<const ComponentPatch> patches,
    ComponentAllocationStats* allocation = nullptr);

[[nodiscard]] ComponentWorldState apply_component_patches_next_frame(
    const ComponentWorldState& current,
    std::span<const ComponentPatch> patches,
    ComponentAllocationStats* allocation = nullptr);

[[nodiscard]] bool component_equivalent_to(
    const ComponentWorldState& component,
    const WorldState& materialized) noexcept;

[[nodiscard]] ComponentMemoryFootprint estimate_retained_component_memory(
    std::span<const ComponentWorldState> states);

} // namespace neoeng::core
