#pragma once

#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/fixed.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace neoeng::core {

struct ExactObliqueTreeOracleConfig final {
    std::size_t maximum_bodies{10U};
    std::size_t maximum_contacts{9U};
    // Search an integer Chebyshev neighbourhood around nearest-even rounding.
    // The returned repair is exact only inside this declared finite neighbourhood.
    std::size_t quantized_repair_radius{1U};
    bool perform_quantized_repair{true};
};

struct ExactObliqueTreeOracleResult final {
    bool valid_input{};
    bool tree_valid{};
    bool certified_continuous{};
    std::uint64_t active_mask{};
    std::uint64_t active_sets_tested{};
    std::string objective_numerator{};
    std::string objective_denominator{};
    std::vector<std::string> exact_velocity_x_numerator{};
    std::vector<std::string> exact_velocity_x_denominator{};
    std::vector<std::string> exact_velocity_y_numerator{};
    std::vector<std::string> exact_velocity_y_denominator{};
    std::vector<Fixed::rep> rounded_velocity_x{};
    std::vector<Fixed::rep> rounded_velocity_y{};
    std::uint64_t rounded_primal_violation_raw{};
    bool repaired_quantized{};
    bool repair_certified_neighbourhood{};
    std::size_t repair_radius{};
    std::string repair_error_numerator{};
    std::string repair_error_denominator{};
    std::vector<Fixed::rep> repaired_velocity_x{};
    std::vector<Fixed::rep> repaired_velocity_y{};
    std::uint64_t repaired_primal_violation_raw{};
    std::uint64_t hash{};
};

// Exact continuous oracle for small oblique trees.
// It enumerates active sets and solves the KKT equations using arbitrary-precision
// rational arithmetic. A returned certificate is exact over the rational encoding of
// input velocities, integer masses and Q1.30 normals. The rounded vectors are only a
// display/interop representation and are not themselves the proof. When enabled,
// the repaired vectors are the exact optimum in the declared finite integer
// neighbourhood around nearest-even rounding; this is not a global integer proof.
[[nodiscard]] ExactObliqueTreeOracleResult solve_exact_oblique_tree_active_sets(
    std::span<const Fixed::rep> input_x,
    std::span<const Fixed::rep> input_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ExactObliqueTreeOracleConfig config = {});

struct QuantizedTreeRepairResult final {
    bool valid_input{};
    bool certified_neighbourhood{};
    std::size_t radius{};
    std::string error_numerator{};
    std::string error_denominator{};
    std::vector<Fixed::rep> velocity_x{};
    std::vector<Fixed::rep> velocity_y{};
    std::uint64_t primal_violation_raw{};
    std::uint64_t hash{};
};

// Re-run only the finite integer-neighbourhood repair from an already certified
// continuous oracle result. This separates expensive active-set enumeration from
// the quantized repair benchmark and supports an adaptive radius policy.
[[nodiscard]] QuantizedTreeRepairResult repair_exact_oblique_tree_neighbourhood(
    const ExactObliqueTreeOracleResult& continuous,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    std::size_t radius);

} // namespace neoeng::core
