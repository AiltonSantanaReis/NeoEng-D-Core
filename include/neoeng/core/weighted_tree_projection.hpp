#pragma once

#include "neoeng/core/arbitrary_normal_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct DirectedTreeEdge final {
    std::size_t parent{};
    std::size_t child{};
    auto operator<=>(const DirectedTreeEdge&) const = default;
};

struct WeightedTreeConfig final {
    std::size_t maximum_active_set_iterations{4096U};
    std::uint64_t feasibility_tolerance_raw{4U};
    std::uint64_t stationarity_tolerance_raw{8U};
    bool use_warm_start{};
};

struct WeightedTreeResiduals final {
    std::uint64_t primal_linf_raw{};
    std::uint64_t dual_violation_raw{};
    std::uint64_t stationarity_linf_raw{};
    std::uint64_t complementarity_linf_raw{};
    std::uint64_t quantization_stationarity_bound_raw{};
    bool certified{};
};

struct WeightedTreeStats final {
    std::uint64_t active_edges{};
    std::uint64_t edges_added{};
    std::uint64_t edges_removed{};
    std::uint64_t iterations{};
    WeightedTreeResiduals residuals{};
};

class WeightedTreeScratch final {
public:
    explicit WeightedTreeScratch(std::size_t maximum_bodies);
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

    std::vector<Fixed::rep> scalar_input{};
    std::vector<Fixed::rep> scalar_output{};
    std::vector<Fixed::rep> dual{};
    std::vector<Fixed::rep> delta{};
    std::vector<std::size_t> parent{};
    std::vector<std::size_t> child{};
    std::vector<std::size_t> first_child_edge{};
    std::vector<std::size_t> last_child_edge{};
    std::vector<std::size_t> next_sibling_edge{};
    std::vector<std::size_t> block_begin{};
    std::vector<std::size_t> block_end{};
    std::vector<std::size_t> order{};
    std::vector<std::size_t> uf_parent{};
    std::vector<std::size_t> indegree{};
    std::vector<std::size_t> edge_of_child{};
    std::vector<std::uint64_t> component_weight{};
    std::vector<WideInteger> component_weighted_value{};
    std::vector<WideInteger> gradient{};
    std::vector<WideInteger> subtree_gradient{};
    std::vector<std::uint8_t> active{};
};


// Certified O(n) weighted PAV for a directed chain with one common normal.
// Returns certified=false without modifying velocities when the edges are not one chain.
[[nodiscard]] WeightedTreeStats project_weighted_chain_common_normal_inplace(
    std::span<Fixed::rep> velocity_x,
    std::span<Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const DirectedTreeEdge> edges,
    NormalQ30 normal,
    WeightedTreeConfig config,
    WeightedTreeScratch& scratch);

// Certified weighted L2 isotonic projection for a rooted directed tree with one common normal.
// Each edge enforces n·v_parent <= n·v_child. The active-set is finite and deterministic;
// a result is promoted only when primal, dual, stationarity and complementarity residuals pass.
[[nodiscard]] WeightedTreeStats project_weighted_tree_common_normal_inplace(
    std::span<Fixed::rep> velocity_x,
    std::span<Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const DirectedTreeEdge> edges,
    NormalQ30 normal,
    WeightedTreeConfig config,
    WeightedTreeScratch& scratch);

} // namespace neoeng::core
