#include "neoeng/core/oblique_star_projection.hpp"

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/rational_adaptor.hpp>

#include <algorithm>
#include <limits>
#include <set>

namespace neoeng::core {
namespace {
using Rational = boost::multiprecision::cpp_rational;
using Integer = boost::multiprecision::cpp_int;

void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

Fixed::rep round_nearest_even(const Rational& value) {
    Integer num = numerator(value), den = denominator(value);
    const bool negative = num < 0;
    if (negative) num = -num;
    Integer quotient = num / den;
    const Integer remainder = num % den;
    const Integer twice = remainder * 2;
    if (twice > den || (twice == den && (quotient & 1) != 0)) ++quotient;
    if (negative) quotient = -quotient;
    if (quotient < std::numeric_limits<Fixed::rep>::min()) return std::numeric_limits<Fixed::rep>::min();
    if (quotient > std::numeric_limits<Fixed::rep>::max()) return std::numeric_limits<Fixed::rep>::max();
    return quotient.convert_to<Fixed::rep>();
}

std::uint64_t mask_hash(std::span<const std::uint8_t> active) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (std::size_t i = 0U; i < active.size(); ++i) if (active[i] != 0U) mix_u64(hash, i + 1U);
    return hash;
}

struct Leaf final {
    std::size_t body{};
    Integer qx{};
    Integer qy{};
    Rational b{};
    Rational norm2{};
};
} // namespace

ObliqueStarProjectionResult solve_oblique_star_leaf_elimination(
    std::span<const Fixed::rep> input_x,
    std::span<const Fixed::rep> input_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ObliqueStarProjectionConfig config) {
    ObliqueStarProjectionResult result{};
    const std::size_t bodies = input_x.size();
    if (bodies == 0U || input_y.size() != bodies || masses.size() != bodies
        || contacts.size() + 1U != bodies || config.maximum_iterations == 0U) return result;
    for (const std::uint32_t mass : masses) if (mass == 0U) return result;
    result.valid_input = true;

    std::vector<std::size_t> degree(bodies, 0U);
    for (const NormalContact& edge : contacts) {
        if (edge.first >= bodies || edge.second >= bodies || edge.first == edge.second
            || (edge.normal.x == 0 && edge.normal.y == 0)) return result;
        ++degree[edge.first]; ++degree[edge.second];
    }
    const auto center_it = std::find(degree.begin(), degree.end(), contacts.size());
    if (center_it == degree.end()) return result;
    const std::size_t center = static_cast<std::size_t>(center_it - degree.begin());
    for (std::size_t body = 0U; body < bodies; ++body) {
        if (body == center) continue;
        if (degree[body] != 1U) return result;
    }
    result.star_valid = true; result.center = center;

    std::vector<Leaf> leaves;
    leaves.reserve(contacts.size());
    for (const NormalContact& edge : contacts) {
        const bool center_first = edge.first == center;
        if (!center_first && edge.second != center) return result;
        const std::size_t leaf = center_first ? edge.second : edge.first;
        Integer qx = center_first ? Integer(edge.normal.x) : -Integer(edge.normal.x);
        Integer qy = center_first ? Integer(edge.normal.y) : -Integer(edge.normal.y);
        const Rational norm2 = Rational(qx * qx + qy * qy);
        const Rational b = Rational(qx) * input_x[leaf] + Rational(qy) * input_y[leaf];
        leaves.push_back({leaf, std::move(qx), std::move(qy), b, norm2});
    }
    std::sort(leaves.begin(), leaves.end(), [](const Leaf& a, const Leaf& b) { return a.body < b.body; });

    std::vector<std::uint8_t> active(leaves.size(), 0U), next(leaves.size(), 0U);
    std::set<std::uint64_t> seen;
    Rational cx = input_x[center], cy = input_y[center];
    for (std::size_t iteration = 0U; iteration < config.maximum_iterations; ++iteration) {
        const std::uint64_t signature = mask_hash(active);
        if (!seen.insert(signature).second) { result.active_set_cycle = true; return result; }

        Rational a00 = masses[center], a01 = 0, a11 = masses[center];
        Rational r0 = Rational(masses[center]) * input_x[center];
        Rational r1 = Rational(masses[center]) * input_y[center];
        for (std::size_t index = 0U; index < leaves.size(); ++index) {
            if (active[index] == 0U) continue;
            const Leaf& leaf = leaves[index];
            const Rational weight = Rational(masses[leaf.body]) / leaf.norm2;
            a00 += weight * leaf.qx * leaf.qx;
            a01 += weight * leaf.qx * leaf.qy;
            a11 += weight * leaf.qy * leaf.qy;
            r0 += weight * leaf.b * leaf.qx;
            r1 += weight * leaf.b * leaf.qy;
        }
        const Rational determinant = a00 * a11 - a01 * a01;
        if (determinant == 0) return result;
        cx = (r0 * a11 - r1 * a01) / determinant;
        cy = (a00 * r1 - a01 * r0) / determinant;

        std::size_t active_count = 0U;
        for (std::size_t index = 0U; index < leaves.size(); ++index) {
            const Leaf& leaf = leaves[index];
            next[index] = (Rational(leaf.qx) * cx + Rational(leaf.qy) * cy > leaf.b) ? 1U : 0U;
            active_count += next[index];
        }
        result.iterations = iteration + 1U;
        if (next == active) {
            result.active_leaves = active_count;
            break;
        }
        active.swap(next);
        if (iteration + 1U == config.maximum_iterations) return result;
    }

    std::vector<Rational> vx(bodies), vy(bodies);
    for (std::size_t body = 0U; body < bodies; ++body) { vx[body] = input_x[body]; vy[body] = input_y[body]; }
    vx[center] = cx; vy[center] = cy;
    Rational objective = Rational(masses[center])
        * ((cx - input_x[center]) * (cx - input_x[center]) + (cy - input_y[center]) * (cy - input_y[center]));
    Rational gradient_x = Rational(masses[center]) * (cx - input_x[center]);
    Rational gradient_y = Rational(masses[center]) * (cy - input_y[center]);
    for (std::size_t index = 0U; index < leaves.size(); ++index) {
        const Leaf& leaf = leaves[index];
        const Rational delta = Rational(leaf.qx) * cx + Rational(leaf.qy) * cy - leaf.b;
        if (active[index] != 0U) {
            if (delta <= 0) return result;
            vx[leaf.body] += delta * leaf.qx / leaf.norm2;
            vy[leaf.body] += delta * leaf.qy / leaf.norm2;
            const Rational lambda = Rational(masses[leaf.body]) * delta / leaf.norm2;
            gradient_x += lambda * leaf.qx;
            gradient_y += lambda * leaf.qy;
        } else if (delta > 0) {
            return result;
        }
        const Rational dx = vx[leaf.body] - input_x[leaf.body];
        const Rational dy = vy[leaf.body] - input_y[leaf.body];
        objective += Rational(masses[leaf.body]) * (dx * dx + dy * dy);
        const Rational constraint = Rational(leaf.qx) * (cx - vx[leaf.body])
            + Rational(leaf.qy) * (cy - vy[leaf.body]);
        if (constraint > 0) return result;
    }
    if (gradient_x != 0 || gradient_y != 0) return result;

    result.certified_continuous = true;
    result.objective_numerator = numerator(objective).convert_to<std::string>();
    result.objective_denominator = denominator(objective).convert_to<std::string>();
    result.exact_velocity_x_numerator.resize(bodies); result.exact_velocity_x_denominator.resize(bodies);
    result.exact_velocity_y_numerator.resize(bodies); result.exact_velocity_y_denominator.resize(bodies);
    result.rounded_velocity_x.resize(bodies); result.rounded_velocity_y.resize(bodies);
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (std::size_t body = 0U; body < bodies; ++body) {
        result.exact_velocity_x_numerator[body] = numerator(vx[body]).convert_to<std::string>();
        result.exact_velocity_x_denominator[body] = denominator(vx[body]).convert_to<std::string>();
        result.exact_velocity_y_numerator[body] = numerator(vy[body]).convert_to<std::string>();
        result.exact_velocity_y_denominator[body] = denominator(vy[body]).convert_to<std::string>();
        result.rounded_velocity_x[body] = round_nearest_even(vx[body]);
        result.rounded_velocity_y[body] = round_nearest_even(vy[body]);
        mix_u64(hash, static_cast<std::uint64_t>(result.rounded_velocity_x[body]));
        mix_u64(hash, static_cast<std::uint64_t>(result.rounded_velocity_y[body]));
    }
    mix_u64(hash, result.active_leaves); mix_u64(hash, result.iterations);
    result.hash = hash;
    return result;
}

} // namespace neoeng::core
