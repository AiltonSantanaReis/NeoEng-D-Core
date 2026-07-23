#pragma once

#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/fixed.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace neoeng::core {

struct ObliqueStarProjectionConfig final {
    std::size_t maximum_iterations{64U};
};

struct ObliqueStarProjectionResult final {
    bool valid_input{};
    bool star_valid{};
    bool certified_continuous{};
    bool active_set_cycle{};
    std::size_t center{};
    std::size_t iterations{};
    std::size_t active_leaves{};
    std::string objective_numerator{};
    std::string objective_denominator{};
    std::vector<std::string> exact_velocity_x_numerator{};
    std::vector<std::string> exact_velocity_x_denominator{};
    std::vector<std::string> exact_velocity_y_numerator{};
    std::vector<std::string> exact_velocity_y_denominator{};
    std::vector<Fixed::rep> rounded_velocity_x{};
    std::vector<Fixed::rep> rounded_velocity_y{};
    std::uint64_t hash{};
};

// Exact rational active-set leaf elimination for an arbitrary-normal star.
// The method is polynomial per iteration and certifies the returned continuous
// solution only when the active set reaches a fixed point. Non-star inputs and
// active-set cycles are rejected explicitly.
[[nodiscard]] ObliqueStarProjectionResult solve_oblique_star_leaf_elimination(
    std::span<const Fixed::rep> input_x,
    std::span<const Fixed::rep> input_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ObliqueStarProjectionConfig config = {});

} // namespace neoeng::core
