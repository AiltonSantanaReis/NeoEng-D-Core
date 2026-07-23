#pragma once

#include "neoeng/core/arbitrary_normal_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct ObliqueGridOracleConfig final {
    Fixed::rep minimum_raw{-2};
    Fixed::rep maximum_raw{2};
    std::size_t maximum_bodies{4U};
};

struct ObliqueGridOracleResult final {
    bool feasible{};
    std::uint64_t objective{};
    std::uint64_t candidates_tested{};
    std::vector<Fixed::rep> velocity_x{};
    std::vector<Fixed::rep> velocity_y{};
};

// Exact finite-grid oracle used only for verification. It exhaustively searches
// every integer velocity vector in [minimum_raw, maximum_raw]^(2n), minimizes the
// weighted squared distance, and chooses the lexicographically smallest minimizer.
[[nodiscard]] ObliqueGridOracleResult solve_small_oblique_grid_oracle(
    std::span<const Fixed::rep> input_x,
    std::span<const Fixed::rep> input_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ObliqueGridOracleConfig config = {});

} // namespace neoeng::core
