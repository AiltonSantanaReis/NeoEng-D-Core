#pragma once

#include "neoeng/core/weighted_tree_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

// v0.18 certified decomposition for a restricted but useful different-normal class.
// Every contact normal must be exactly +/-X or +/-Y in Q1.30. Because kinetic
// energy is separable by velocity component, X and Y constraints become two
// independent weighted isotonic forest problems. Each directed component must
// be an arborescence (one root, at most one incoming edge per non-root body).
struct AxisForestConfig final {
    WeightedTreeConfig tree{};
};

struct AxisForestStats final {
    std::uint64_t contacts_processed{};
    std::uint64_t x_contacts{};
    std::uint64_t y_contacts{};
    std::uint64_t x_components{};
    std::uint64_t y_components{};
    std::uint64_t tree_iterations{};
    std::uint64_t primal_linf_raw{};
    std::uint64_t dual_violation_raw{};
    std::uint64_t stationarity_linf_raw{};
    std::uint64_t complementarity_linf_raw{};
    bool supported{};
    bool certified{};
};

class AxisForestScratch final {
public:
    AxisForestScratch(std::size_t maximum_bodies, std::size_t maximum_contacts);
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

public:
    friend AxisForestStats project_axis_forest_inplace(
        std::span<Fixed::rep>, std::span<Fixed::rep>,
        std::span<const std::uint32_t>, std::span<const NormalContact>,
        AxisForestConfig, AxisForestScratch&);

    std::size_t maximum_bodies_{};
    std::size_t maximum_contacts_{};
    std::vector<Fixed::rep> working_x_{}, working_y_{};
    std::vector<Fixed::rep> local_x_{}, local_y_{};
    std::vector<std::uint32_t> local_masses_{};
    std::vector<std::size_t> uf_parent_{}, indegree_{}, component_index_{};
    std::vector<std::size_t> component_roots_{}, body_counts_{}, edge_counts_{};
    std::vector<std::size_t> body_offsets_{}, edge_offsets_{}, body_cursor_{}, edge_cursor_{};
    std::vector<std::size_t> component_bodies_{}, component_edge_indices_{};
    std::vector<std::size_t> local_index_{}, edge_parent_{}, edge_child_{};
    std::vector<std::uint8_t> body_used_{};
    std::vector<DirectedTreeEdge> local_edges_{};
    WeightedTreeScratch tree_scratch_;
};

// The input velocities are modified only when every component receives a valid
// KKT certificate. Unsupported or uncertified graphs leave the input unchanged.
[[nodiscard]] AxisForestStats project_axis_forest_inplace(
    std::span<Fixed::rep> velocity_x,
    std::span<Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    AxisForestConfig config,
    AxisForestScratch& scratch);

} // namespace neoeng::core
