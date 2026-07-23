#pragma once

#include "neoeng/core/arbitrary_normal_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct ObliqueTreeGridConfig final {
    Fixed::rep minimum_raw{-2};
    Fixed::rep maximum_raw{2};
    std::size_t maximum_bodies{4096U};
    std::size_t maximum_grid_states{81U};
};

struct ObliqueTreeGridResult final {
    bool tree_valid{};
    bool feasible{};
    bool certified_on_grid{};
    std::uint64_t objective{};
    std::uint64_t state_pairs_tested{};
    std::uint64_t primal_violation_raw{};
    std::vector<Fixed::rep> velocity_x{};
    std::vector<Fixed::rep> velocity_y{};
};

class ObliqueTreeGridScratch final {
public:
    ObliqueTreeGridScratch(std::size_t maximum_bodies, std::size_t maximum_grid_states);
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

    std::vector<std::size_t> adjacency_head{};
    std::vector<std::size_t> adjacency_next{};
    std::vector<std::size_t> adjacency_edge{};
    std::vector<std::size_t> adjacency_other{};
    std::vector<std::size_t> parent{};
    std::vector<std::size_t> parent_edge{};
    std::vector<std::size_t> order{};
    std::vector<std::uint8_t> visited{};
    std::vector<std::uint64_t> dp{};
    std::vector<std::uint32_t> choice{};
    std::vector<std::uint32_t> selected_state{};
    std::vector<Fixed::rep> edge_state_projection{};
    std::vector<std::uint64_t> feasible_mask{};
    std::vector<Fixed::rep> grid_x{};
    std::vector<Fixed::rep> grid_y{};
    std::uint64_t prepared_signature{};
    std::size_t prepared_bodies{};
    std::size_t prepared_states{};
    bool prepared{};
};

// Exact dynamic programming oracle on a finite Cartesian velocity grid.
// It is polynomial in the number of bodies for tree contact graphs:
// O(|E| * K^2), where K is the number of 2D grid states. The certificate is
// exact only for the configured finite grid; it is not a continuous Q32.32 proof.
[[nodiscard]] ObliqueTreeGridResult solve_oblique_tree_grid_dp(
    std::span<const Fixed::rep> input_x,
    std::span<const Fixed::rep> input_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ObliqueTreeGridConfig config,
    ObliqueTreeGridScratch& scratch);

} // namespace neoeng::core
