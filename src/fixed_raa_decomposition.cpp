#include "neoeng/core/fixed_raa_decomposition.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace neoeng::core {
namespace {
constexpr std::size_t kStorageTerms = 16U;
constexpr Fixed::rep kScale = Fixed::scale;

enum class TermOrigin : std::uint8_t {
    Input,
    Nonlinear,
};

enum class ResidualOrigin : std::uint8_t {
    Condensation,
    Rounding,
};

[[nodiscard]] Fixed::rep checked_raw(WideInteger value) {
    if (value < static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::min())
        || value > static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::max())) {
        throw std::overflow_error("Diagnostic RAA raw overflow");
    }
    return static_cast<Fixed::rep>(value);
}

[[nodiscard]] Fixed::rep abs_raw(Fixed::rep value) {
    if (value == std::numeric_limits<Fixed::rep>::min()) {
        throw std::overflow_error("Diagnostic RAA absolute overflow");
    }
    return value < 0 ? -value : value;
}

[[nodiscard]] Fixed::rep mul_trunc(Fixed::rep first, Fixed::rep second, bool& rounded) {
    const WideInteger product = static_cast<WideInteger>(first) * second;
    const WideInteger quotient = product / static_cast<WideInteger>(kScale);
    rounded = (product % static_cast<WideInteger>(kScale)) != 0;
    return checked_raw(quotient);
}

[[nodiscard]] Fixed::rep mul_abs_ceil(Fixed::rep first, Fixed::rep second) {
    const WideInteger a = abs_raw(first);
    const WideInteger b = abs_raw(second);
    const WideInteger product = a * b;
    const WideInteger result = (product + static_cast<WideInteger>(kScale) - 1)
        / static_cast<WideInteger>(kScale);
    return checked_raw(result);
}

[[nodiscard]] WideInteger checked_wide_add(WideInteger first, WideInteger second) {
    WideInteger result{};
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_add_overflow(first, second, &result)) {
        throw std::overflow_error("Diagnostic RAA wide addition overflow");
    }
#else
#error "NeoEng diagnostic RAA requires checked signed 128-bit addition support"
#endif
    return result;
}

struct Term final {
    std::uint32_t id{};
    Fixed::rep coefficient{};
    TermOrigin origin{TermOrigin::Input};
};

template <std::size_t Capacity, typename Less>
void insertion_sort_prefix(std::array<Term, Capacity>& values, std::size_t count, Less less) {
    for (std::size_t index = 1U; index < count; ++index) {
        const Term key = values[index];
        std::size_t position = index;
        while (position > 0U && less(key, values[position - 1U])) {
            values[position] = values[position - 1U];
            --position;
        }
        values[position] = key;
    }
}

[[nodiscard]] bool magnitude_precedes(const Term& first, const Term& second) {
    const Fixed::rep first_abs = abs_raw(first.coefficient);
    const Fixed::rep second_abs = abs_raw(second.coefficient);
    return first_abs == second_abs ? first.id < second.id : first_abs > second_abs;
}

[[nodiscard]] bool id_precedes(const Term& first, const Term& second) noexcept {
    return first.id < second.id;
}

struct DiagnosticRaa final {
    Fixed::rep center{};
    std::array<Term, kStorageTerms> terms{};
    std::uint8_t count{};
    Fixed::rep condensation_residual{};
    Fixed::rep rounding_residual{};
};

struct DiagnosticContext final {
    std::size_t maximum_terms{};
    std::uint32_t next_id{1U};
    std::size_t maximum_observed{};
    std::uint64_t compressions{};
    std::uint64_t rounding_guard_raw{};
};

[[nodiscard]] std::uint32_t take_error_id(DiagnosticContext& context) {
    if (context.next_id == 0U || context.next_id == std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("Diagnostic RAA error ID space exhausted");
    }
    return context.next_id++;
}

[[nodiscard]] Fixed::rep residual(const DiagnosticRaa& value) {
    return checked_raw(static_cast<WideInteger>(value.condensation_residual)
        + value.rounding_residual);
}

[[nodiscard]] Fixed::rep explicit_radius(const DiagnosticRaa& value) {
    WideInteger sum = 0;
    for (std::size_t index = 0U; index < value.count; ++index) {
        sum += abs_raw(value.terms[index].coefficient);
    }
    return checked_raw(sum);
}

[[nodiscard]] Fixed::rep total_radius(const DiagnosticRaa& value) {
    return checked_raw(static_cast<WideInteger>(explicit_radius(value)) + residual(value));
}

void add_residual(DiagnosticRaa& value, Fixed::rep amount, ResidualOrigin origin) {
    if (amount < 0) throw std::logic_error("Negative diagnostic RAA residual increment");
    Fixed::rep& target = origin == ResidualOrigin::Condensation
        ? value.condensation_residual : value.rounding_residual;
    target = checked_raw(static_cast<WideInteger>(target) + amount);
}

void canonical_reduce(DiagnosticRaa& value, DiagnosticContext& context) {
    std::array<Term, kStorageTerms> nonzero{};
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < value.count; ++index) {
        if (value.terms[index].coefficient != 0) nonzero[count++] = value.terms[index];
    }
    if (count > context.maximum_terms) {
        insertion_sort_prefix(nonzero, count, magnitude_precedes);
        WideInteger discarded = 0;
        for (std::size_t index = context.maximum_terms; index < count; ++index) {
            discarded += abs_raw(nonzero[index].coefficient);
        }
        count = context.maximum_terms;
        add_residual(value, checked_raw(discarded), ResidualOrigin::Condensation);
        ++context.compressions;
    }
    insertion_sort_prefix(nonzero, count, id_precedes);
    value.count = static_cast<std::uint8_t>(count);
    for (std::size_t index = 0U; index < count; ++index) value.terms[index] = nonzero[index];
    context.maximum_observed = std::max(context.maximum_observed, count);
}

[[nodiscard]] DiagnosticRaa constant(Fixed value) { return {.center = value.raw()}; }

[[nodiscard]] DiagnosticRaa uncertain(Fixed center, Fixed radius, DiagnosticContext& context) {
    DiagnosticRaa result{.center = center.raw()};
    if (radius.raw() < 0) throw std::invalid_argument("Negative diagnostic RAA radius");
    if (radius.raw() != 0) {
        result.terms[0] = {take_error_id(context), radius.raw(), TermOrigin::Input};
        result.count = 1U;
        context.maximum_observed = std::max<std::size_t>(context.maximum_observed, 1U);
    }
    return result;
}

[[nodiscard]] DiagnosticRaa negate(DiagnosticRaa value) {
    value.center = checked_raw(-static_cast<WideInteger>(value.center));
    for (std::size_t index = 0U; index < value.count; ++index) {
        value.terms[index].coefficient = checked_raw(
            -static_cast<WideInteger>(value.terms[index].coefficient));
    }
    return value;
}

[[nodiscard]] DiagnosticRaa add(
    const DiagnosticRaa& first, const DiagnosticRaa& second, DiagnosticContext& context) {
    DiagnosticRaa result{};
    result.center = checked_raw(static_cast<WideInteger>(first.center) + second.center);
    result.condensation_residual = checked_raw(
        static_cast<WideInteger>(first.condensation_residual) + second.condensation_residual);
    result.rounding_residual = checked_raw(
        static_cast<WideInteger>(first.rounding_residual) + second.rounding_residual);
    std::array<Term, kStorageTerms * 2U> merged{};
    std::size_t out = 0U;
    std::size_t a = 0U;
    std::size_t b = 0U;
    while (a < first.count || b < second.count) {
        if (b == second.count || (a < first.count && first.terms[a].id < second.terms[b].id)) {
            merged[out++] = first.terms[a++];
        } else if (a == first.count || second.terms[b].id < first.terms[a].id) {
            merged[out++] = second.terms[b++];
        } else {
            if (first.terms[a].origin != second.terms[b].origin) {
                throw std::logic_error("Diagnostic RAA term origin mismatch");
            }
            merged[out++] = {
                first.terms[a].id,
                checked_raw(static_cast<WideInteger>(first.terms[a].coefficient)
                    + second.terms[b].coefficient),
                first.terms[a].origin,
            };
            ++a;
            ++b;
        }
    }
    if (out > kStorageTerms) {
        insertion_sort_prefix(merged, out, magnitude_precedes);
        WideInteger discarded = 0;
        for (std::size_t index = kStorageTerms; index < out; ++index) {
            discarded += abs_raw(merged[index].coefficient);
        }
        out = kStorageTerms;
        add_residual(result, checked_raw(discarded), ResidualOrigin::Condensation);
        ++context.compressions;
        insertion_sort_prefix(merged, out, id_precedes);
    }
    result.count = static_cast<std::uint8_t>(out);
    for (std::size_t index = 0U; index < out; ++index) result.terms[index] = merged[index];
    canonical_reduce(result, context);
    return result;
}

[[nodiscard]] DiagnosticRaa multiply(
    const DiagnosticRaa& first, const DiagnosticRaa& second, DiagnosticContext& context) {
    DiagnosticRaa result{};
    bool center_rounded = false;
    result.center = mul_trunc(first.center, second.center, center_rounded);
    if (center_rounded) {
        add_residual(result, 1, ResidualOrigin::Rounding);
        ++context.rounding_guard_raw;
    }

    std::array<Term, kStorageTerms * 2U + 1U> merged{};
    std::size_t out = 0U;
    std::size_t a = 0U;
    std::size_t b = 0U;
    while (a < first.count || b < second.count) {
        std::uint32_t id{};
        Fixed::rep ac{};
        Fixed::rep bc{};
        TermOrigin origin{TermOrigin::Input};
        if (b == second.count || (a < first.count && first.terms[a].id < second.terms[b].id)) {
            id = first.terms[a].id;
            ac = first.terms[a].coefficient;
            origin = first.terms[a].origin;
            ++a;
        } else if (a == first.count || second.terms[b].id < first.terms[a].id) {
            id = second.terms[b].id;
            bc = second.terms[b].coefficient;
            origin = second.terms[b].origin;
            ++b;
        } else {
            if (first.terms[a].origin != second.terms[b].origin) {
                throw std::logic_error("Diagnostic RAA multiply origin mismatch");
            }
            id = first.terms[a].id;
            ac = first.terms[a].coefficient;
            bc = second.terms[b].coefficient;
            origin = first.terms[a].origin;
            ++a;
            ++b;
        }
        const WideInteger first_contribution = static_cast<WideInteger>(first.center) * bc;
        const WideInteger second_contribution = static_cast<WideInteger>(second.center) * ac;
        const WideInteger numerator = checked_wide_add(first_contribution, second_contribution);
        const WideInteger quotient = numerator / static_cast<WideInteger>(kScale);
        const bool rounded = (numerator % static_cast<WideInteger>(kScale)) != 0;
        const Fixed::rep coefficient = checked_raw(quotient);
        if (coefficient != 0) merged[out++] = {id, coefficient, origin};
        if (rounded) {
            add_residual(result, 1, ResidualOrigin::Rounding);
            ++context.rounding_guard_raw;
        }
    }

    const Fixed::rep first_residual = residual(first);
    const Fixed::rep second_residual = residual(second);
    const Fixed::rep radius_first = total_radius(first);
    const Fixed::rep radius_second = total_radius(second);
    const Fixed::rep nonlinear_remainder = checked_raw(
        static_cast<WideInteger>(mul_abs_ceil(first.center, second_residual))
        + mul_abs_ceil(second.center, first_residual)
        + mul_abs_ceil(radius_first, radius_second));
    if (nonlinear_remainder != 0) {
        merged[out++] = {take_error_id(context), nonlinear_remainder, TermOrigin::Nonlinear};
    }

    if (out > kStorageTerms) {
        insertion_sort_prefix(merged, out, magnitude_precedes);
        WideInteger discarded = 0;
        for (std::size_t index = kStorageTerms; index < out; ++index) {
            discarded += abs_raw(merged[index].coefficient);
        }
        out = kStorageTerms;
        add_residual(result, checked_raw(discarded), ResidualOrigin::Condensation);
        ++context.compressions;
        insertion_sort_prefix(merged, out, id_precedes);
    }
    result.count = static_cast<std::uint8_t>(out);
    for (std::size_t index = 0U; index < out; ++index) result.terms[index] = merged[index];
    canonical_reduce(result, context);
    return result;
}

[[nodiscard]] DiagnosticRaa scale(
    const DiagnosticRaa& value, Fixed scalar, DiagnosticContext& context) {
    return multiply(value, constant(scalar), context);
}

struct PairState final {
    DiagnosticRaa x1{};
    DiagnosticRaa x2{};
    DiagnosticRaa v1{};
    DiagnosticRaa v2{};
};

void step_pair(PairState& pair, const DiagnosticRaa& stiffness,
    const DiagnosticRaa& damping, const DiagnosticRaa& cubic,
    const DiagnosticRaa& rest, Fixed dt, DiagnosticContext& context) {
    const DiagnosticRaa negative_x1 = negate(pair.x1);
    const DiagnosticRaa separation = add(pair.x2, negative_x1, context);
    const DiagnosticRaa negative_rest = negate(rest);
    const DiagnosticRaa gap = add(separation, negative_rest, context);
    const DiagnosticRaa negative_v1 = negate(pair.v1);
    const DiagnosticRaa relative_velocity = add(pair.v2, negative_v1, context);
    const DiagnosticRaa gap2 = multiply(gap, gap, context);
    const DiagnosticRaa gap3 = multiply(gap2, gap, context);
    const DiagnosticRaa stiffness_force = multiply(stiffness, gap, context);
    const DiagnosticRaa damping_force = multiply(damping, relative_velocity, context);
    const DiagnosticRaa cubic_force = multiply(cubic, gap3, context);
    const DiagnosticRaa linear_force = add(stiffness_force, damping_force, context);
    const DiagnosticRaa total_force = add(linear_force, cubic_force, context);
    const DiagnosticRaa force = negate(total_force);
    const DiagnosticRaa impulse = scale(force, dt, context);
    const DiagnosticRaa negative_impulse = negate(impulse);
    pair.v1 = add(pair.v1, negative_impulse, context);
    pair.v2 = add(pair.v2, impulse, context);
    const DiagnosticRaa displacement1 = scale(pair.v1, dt, context);
    const DiagnosticRaa displacement2 = scale(pair.v2, dt, context);
    pair.x1 = add(pair.x1, displacement1, context);
    pair.x2 = add(pair.x2, displacement2, context);
}

[[nodiscard]] std::pair<double, double> bounds(const DiagnosticRaa& value) {
    const double center = Fixed::from_raw(value.center).to_double();
    const double radius = Fixed::from_raw(total_radius(value)).to_double();
    return {
        std::nextafter(center - radius, -std::numeric_limits<double>::infinity()),
        std::nextafter(center + radius, std::numeric_limits<double>::infinity()),
    };
}

[[nodiscard]] FixedRaaWidthBreakdown width_breakdown(const DiagnosticRaa& value) {
    WideInteger input_radius = 0;
    WideInteger nonlinear_radius = 0;
    for (std::size_t index = 0U; index < value.count; ++index) {
        const Fixed::rep magnitude = abs_raw(value.terms[index].coefficient);
        if (value.terms[index].origin == TermOrigin::Input) input_radius += magnitude;
        else nonlinear_radius += magnitude;
    }
    const double raw_to_width = 2.0 / static_cast<double>(kScale);
    return {
        .retained_input_width = static_cast<double>(checked_raw(input_radius)) * raw_to_width,
        .retained_nonlinear_width = static_cast<double>(checked_raw(nonlinear_radius)) * raw_to_width,
        .condensation_width = static_cast<double>(value.condensation_residual) * raw_to_width,
        .rounding_width = static_cast<double>(value.rounding_residual) * raw_to_width,
    };
}

void accumulate(FixedRaaWidthBreakdown& target, const FixedRaaWidthBreakdown& source) {
    target.retained_input_width += source.retained_input_width;
    target.retained_nonlinear_width += source.retained_nonlinear_width;
    target.condensation_width += source.condensation_width;
    target.rounding_width += source.rounding_width;
}

void divide(FixedRaaWidthBreakdown& value, double divisor) {
    value.retained_input_width /= divisor;
    value.retained_nonlinear_width /= divisor;
    value.condensation_width /= divisor;
    value.rounding_width /= divisor;
}

void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

} // namespace

FixedRaaDecompositionResult run_fixed_raa_width_decomposition(
    const FixedRaaMicrokernelConfig& config) {
    if (config.bodies < 2U || (config.bodies & 1U) != 0U || config.steps == 0U
        || config.maximum_terms < 2U || config.maximum_terms > kStorageTerms
        || config.monte_carlo_samples == 0U || config.timing_repetitions == 0U) {
        throw std::invalid_argument("Diagnostic RAA configuration is invalid");
    }

    const std::size_t pairs = config.bodies / 2U;
    std::vector<PairState> states(pairs);
    DiagnosticContext context{.maximum_terms = config.maximum_terms};
    const Fixed dt = Fixed::from_ratio(1, 240);
    const DiagnosticRaa stiffness = uncertain(
        Fixed::from_ratio(3, 2), Fixed::from_ratio(1, 20), context);
    const DiagnosticRaa damping = uncertain(
        Fixed::from_ratio(1, 5), Fixed::from_ratio(1, 100), context);
    const DiagnosticRaa cubic = uncertain(
        Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 200), context);
    const DiagnosticRaa rest = uncertain(
        Fixed::from_integer(1), Fixed::from_ratio(1, 100), context);
    for (std::size_t index = 0U; index < pairs; ++index) {
        const Fixed offset = Fixed::from_ratio(static_cast<Fixed::rep>(index % 17U), 1000);
        states[index].x1 = uncertain(
            Fixed::from_raw(-offset.raw()), Fixed::from_ratio(1, 200), context);
        states[index].x2 = uncertain(
            Fixed::from_raw(Fixed::scale + offset.raw()), Fixed::from_ratio(1, 200), context);
        states[index].v1 = uncertain(
            Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 500), context);
        states[index].v2 = uncertain(
            Fixed::from_ratio(-1, 20), Fixed::from_ratio(1, 500), context);
    }

    FixedRaaDecompositionResult result{};
    for (std::size_t step = 0U; step < config.steps; ++step) {
        for (PairState& pair : states) {
            step_pair(pair, stiffness, damping, cubic, rest, dt, context);
        }
        for (const PairState& pair : states) {
            for (const DiagnosticRaa* value : {&pair.x1, &pair.x2, &pair.v1, &pair.v2}) {
                accumulate(result.average_width, width_breakdown(*value));
            }
        }
    }
    divide(result.average_width, static_cast<double>(config.steps * pairs));

    for (const PairState& pair : states) {
        for (const DiagnosticRaa* value : {&pair.x1, &pair.x2, &pair.v1, &pair.v2}) {
            accumulate(result.final_width, width_breakdown(*value));
        }
    }
    result.maximum_terms = context.maximum_observed;
    result.compressions = context.compressions;
    result.rounding_guard_raw = context.rounding_guard_raw;

    DiagnosticContext reference_context{.maximum_terms = config.maximum_terms};
    PairState reference{};
    reference.x1 = uncertain(
        Fixed::from_integer(0), Fixed::from_ratio(1, 200), reference_context);
    reference.x2 = uncertain(
        Fixed::from_integer(1), Fixed::from_ratio(1, 200), reference_context);
    reference.v1 = uncertain(
        Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 500), reference_context);
    reference.v2 = uncertain(
        Fixed::from_ratio(-1, 20), Fixed::from_ratio(1, 500), reference_context);
    const DiagnosticRaa rk = uncertain(
        Fixed::from_ratio(3, 2), Fixed::from_ratio(1, 20), reference_context);
    const DiagnosticRaa rc = uncertain(
        Fixed::from_ratio(1, 5), Fixed::from_ratio(1, 100), reference_context);
    const DiagnosticRaa ra = uncertain(
        Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 200), reference_context);
    const DiagnosticRaa rr = uncertain(
        Fixed::from_integer(1), Fixed::from_ratio(1, 100), reference_context);
    for (std::size_t step = 0U; step < config.steps; ++step) {
        step_pair(reference, rk, rc, ra, rr, dt, reference_context);
    }
    const auto bx1 = bounds(reference.x1);
    const auto bx2 = bounds(reference.x2);
    const auto bv1 = bounds(reference.v1);
    const auto bv2 = bounds(reference.v2);
    std::mt19937_64 rng(config.seed);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    constexpr double dtd = 1.0 / 240.0;
    for (std::size_t sample = 0U; sample < config.monte_carlo_samples; ++sample) {
        double x1 = 0.005 * unit(rng);
        double x2 = 1.0 + 0.005 * unit(rng);
        double v1 = 0.05 + 0.002 * unit(rng);
        double v2 = -0.05 + 0.002 * unit(rng);
        const double k = 1.5 + 0.05 * unit(rng);
        const double c = 0.2 + 0.01 * unit(rng);
        const double alpha = 0.05 + 0.005 * unit(rng);
        const double rest_value = 1.0 + 0.01 * unit(rng);
        for (std::size_t step = 0U; step < config.steps; ++step) {
            const double gap = (x2 - x1) - rest_value;
            const double relative = v2 - v1;
            const double force = -k * gap - c * relative - alpha * gap * gap * gap;
            v1 -= dtd * force;
            v2 += dtd * force;
            x1 += dtd * v1;
            x2 += dtd * v2;
        }
        if (x1 < bx1.first || x1 > bx1.second || x2 < bx2.first || x2 > bx2.second
            || v1 < bv1.first || v1 > bv1.second || v2 < bv2.first || v2 > bv2.second) {
            ++result.empirical_violations;
        }
    }

    std::uint64_t hash = 0xCBF29CE484222325ULL;
    mix_u64(hash, result.maximum_terms);
    mix_u64(hash, result.compressions);
    mix_u64(hash, result.rounding_guard_raw);
    mix_u64(hash, result.empirical_violations);
    for (const PairState& pair : states) {
        for (const DiagnosticRaa* value : {&pair.x1, &pair.x2, &pair.v1, &pair.v2}) {
            mix_u64(hash, static_cast<std::uint64_t>(value->center));
            mix_u64(hash, static_cast<std::uint64_t>(residual(*value)));
            mix_u64(hash, value->count);
            for (std::size_t term = 0U; term < value->count; ++term) {
                mix_u64(hash, value->terms[term].id);
                mix_u64(hash, static_cast<std::uint64_t>(value->terms[term].coefficient));
            }
        }
    }
    result.hash = hash;
    return result;
}

} // namespace neoeng::core
