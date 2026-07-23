#include "neoeng/core/small_oblique_grid_oracle.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace neoeng::core {
namespace {
WideInteger rounded_div_ties_to_floor(WideInteger numerator, WideInteger denominator) noexcept {
    WideInteger quotient = numerator / denominator;
    WideInteger remainder = numerator % denominator;
    if (remainder < 0) { remainder += denominator; --quotient; }
    if (remainder * 2 > denominator) ++quotient;
    return quotient;
}

Fixed::rep normal_velocity(Fixed::rep vx, Fixed::rep vy, const NormalQ30& normal) noexcept {
    const WideInteger numerator = static_cast<WideInteger>(vx) * normal.x
        + static_cast<WideInteger>(vy) * normal.y;
    return static_cast<Fixed::rep>(rounded_div_ties_to_floor(numerator, WideInteger{1} << 30U));
}

bool feasible(std::span<const Fixed::rep> vx, std::span<const Fixed::rep> vy,
              std::span<const NormalContact> contacts) noexcept {
    for (const NormalContact& contact : contacts) {
        if (normal_velocity(vx[contact.first], vy[contact.first], contact.normal)
            > normal_velocity(vx[contact.second], vy[contact.second], contact.normal)) return false;
    }
    return true;
}

std::uint64_t objective(std::span<const Fixed::rep> vx, std::span<const Fixed::rep> vy,
               std::span<const Fixed::rep> input_x, std::span<const Fixed::rep> input_y,
               std::span<const std::uint32_t> masses) {
    WideInteger total = 0;
    for (std::size_t body = 0U; body < vx.size(); ++body) {
        const WideInteger dx = static_cast<WideInteger>(vx[body]) - input_x[body];
        const WideInteger dy = static_cast<WideInteger>(vy[body]) - input_y[body];
        total += static_cast<WideInteger>(masses[body]) * (dx * dx + dy * dy);
        if (total > static_cast<WideInteger>(std::numeric_limits<std::uint64_t>::max())) {
            throw std::overflow_error("Small oblique grid oracle objective overflow");
        }
    }
    return static_cast<std::uint64_t>(total);
}

bool lex_less(std::span<const Fixed::rep> ax, std::span<const Fixed::rep> ay,
              std::span<const Fixed::rep> bx, std::span<const Fixed::rep> by) noexcept {
    for (std::size_t body = 0U; body < ax.size(); ++body) {
        if (ax[body] != bx[body]) return ax[body] < bx[body];
        if (ay[body] != by[body]) return ay[body] < by[body];
    }
    return false;
}
}

ObliqueGridOracleResult solve_small_oblique_grid_oracle(
    std::span<const Fixed::rep> input_x, std::span<const Fixed::rep> input_y,
    std::span<const std::uint32_t> masses, std::span<const NormalContact> contacts,
    ObliqueGridOracleConfig config) {
    const std::size_t bodies = input_x.size();
    if (bodies == 0U || input_y.size() != bodies || masses.size() != bodies
        || bodies > config.maximum_bodies || config.minimum_raw > config.maximum_raw) {
        throw std::invalid_argument("Small oblique grid oracle shape/configuration mismatch");
    }
    for (const NormalContact& contact : contacts) {
        if (contact.first >= bodies || contact.second >= bodies || contact.first == contact.second
            || (contact.normal.x == 0 && contact.normal.y == 0)) {
            throw std::invalid_argument("Small oblique grid oracle contact is invalid");
        }
    }
    if (std::any_of(masses.begin(), masses.end(), [](std::uint32_t mass) { return mass == 0U; })) {
        throw std::invalid_argument("Small oblique grid oracle mass must be positive");
    }

    ObliqueGridOracleResult result{};
    std::uint64_t best_objective = std::numeric_limits<std::uint64_t>::max();
    result.velocity_x.resize(bodies);
    result.velocity_y.resize(bodies);
    std::vector<Fixed::rep> candidate_x(bodies), candidate_y(bodies);
    const std::uint64_t radix = static_cast<std::uint64_t>(config.maximum_raw - config.minimum_raw + 1);
    std::uint64_t combinations = 1U;
    for (std::size_t dimension = 0U; dimension < bodies * 2U; ++dimension) {
        combinations *= radix;
        if (combinations > 20'000'000U) {
            throw std::length_error("Small oblique grid oracle search exceeds safety limit");
        }
    }
    const std::uint64_t total = combinations;
    for (std::uint64_t code = 0U; code < total; ++code) {
        std::uint64_t cursor = code;
        for (std::size_t body = 0U; body < bodies; ++body) {
            candidate_x[body] = config.minimum_raw + static_cast<Fixed::rep>(cursor % radix);
            cursor /= radix;
            candidate_y[body] = config.minimum_raw + static_cast<Fixed::rep>(cursor % radix);
            cursor /= radix;
        }
        ++result.candidates_tested;
        if (!feasible(candidate_x, candidate_y, contacts)) continue;
        const std::uint64_t value = objective(candidate_x, candidate_y, input_x, input_y, masses);
        if (!result.feasible || value < best_objective
            || (value == best_objective && lex_less(candidate_x, candidate_y,
                                                      result.velocity_x, result.velocity_y))) {
            result.feasible = true;
            best_objective = value;
            result.velocity_x = candidate_x;
            result.velocity_y = candidate_y;
        }
    }
    if (result.feasible) result.objective = best_objective;
    return result;
}

} // namespace neoeng::core
