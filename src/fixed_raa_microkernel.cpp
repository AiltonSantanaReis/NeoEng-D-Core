#include "neoeng/core/fixed_raa_microkernel.hpp"
#include "neoeng/core/fixed_raa_selective_lab.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace neoeng::core {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kStorageTerms = 16U;
constexpr Fixed::rep kScale = Fixed::scale;

[[nodiscard]] Fixed::rep checked_raw(WideInteger value) {
    if (value < static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::min())
        || value > static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::max())) {
        throw std::overflow_error("Fixed RAA raw overflow");
    }
    return static_cast<Fixed::rep>(value);
}

[[nodiscard]] Fixed::rep abs_raw(Fixed::rep value) {
    if (value == std::numeric_limits<Fixed::rep>::min()) {
        throw std::overflow_error("Fixed RAA absolute overflow");
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
    const WideInteger result = (product + static_cast<WideInteger>(kScale) - 1) / static_cast<WideInteger>(kScale);
    return checked_raw(result);
}

struct Term final {
    std::uint32_t id{};
    Fixed::rep coefficient{};
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

[[nodiscard]] WideInteger checked_wide_add(WideInteger first, WideInteger second) {
    WideInteger result{};
#if defined(__GNUC__) || defined(__clang__)
    if (__builtin_add_overflow(first, second, &result)) {
        throw std::overflow_error("Fixed RAA wide addition overflow");
    }
#else
#error "NeoEng fixed RAA requires checked signed 128-bit addition support"
#endif
    return result;
}

struct Raa final {
    Fixed::rep center{};
    std::array<Term, kStorageTerms> terms{};
    std::uint8_t count{};
    Fixed::rep residual{};
};

struct RaaContext final {
    std::size_t maximum_terms{};
    std::uint32_t next_id{1U};
    std::size_t maximum_observed{};
    std::uint64_t compressions{};
    std::uint64_t rounding_guard_raw{};
};

[[nodiscard]] std::uint32_t take_error_id(RaaContext& context) {
    if (context.next_id == 0U || context.next_id == std::numeric_limits<std::uint32_t>::max()) {
        throw std::overflow_error("Fixed RAA error ID space exhausted");
    }
    return context.next_id++;
}

[[nodiscard]] Fixed::rep explicit_radius(const Raa& value) {
    WideInteger sum = 0;
    for (std::size_t index = 0U; index < value.count; ++index) sum += abs_raw(value.terms[index].coefficient);
    return checked_raw(sum);
}

[[nodiscard]] Fixed::rep total_radius(const Raa& value) {
    return checked_raw(static_cast<WideInteger>(explicit_radius(value)) + value.residual);
}

void add_residual(Raa& value, Fixed::rep amount) {
    if (amount < 0) throw std::logic_error("Negative RAA residual increment");
    value.residual = checked_raw(static_cast<WideInteger>(value.residual) + amount);
}

void canonical_reduce(Raa& value, RaaContext& context) {
    std::array<Term, kStorageTerms> nonzero{};
    std::size_t count = 0U;
    for (std::size_t index = 0U; index < value.count; ++index) {
        if (value.terms[index].coefficient != 0) nonzero[count++] = value.terms[index];
    }
    if (count > context.maximum_terms) {
        insertion_sort_prefix(nonzero, count, magnitude_precedes);
        WideInteger discarded = 0;
        for (std::size_t index = context.maximum_terms; index < count; ++index) discarded += abs_raw(nonzero[index].coefficient);
        count = context.maximum_terms;
        add_residual(value, checked_raw(discarded));
        ++context.compressions;
    }
    insertion_sort_prefix(nonzero, count, id_precedes);
    value.count = static_cast<std::uint8_t>(count);
    for (std::size_t index = 0U; index < count; ++index) value.terms[index] = nonzero[index];
    context.maximum_observed = std::max(context.maximum_observed, count);
}

[[nodiscard]] Raa constant(Fixed value) { return {.center = value.raw()}; }

[[nodiscard]] Raa uncertain(Fixed center, Fixed radius, RaaContext& context) {
    Raa result{.center = center.raw()};
    if (radius.raw() < 0) throw std::invalid_argument("Negative RAA uncertainty radius");
    if (radius.raw() != 0) {
        result.terms[0] = {take_error_id(context), radius.raw()};
        result.count = 1U;
        context.maximum_observed = std::max<std::size_t>(context.maximum_observed, 1U);
    }
    return result;
}

[[nodiscard]] Raa negate(Raa value) {
    value.center = checked_raw(-static_cast<WideInteger>(value.center));
    for (std::size_t index = 0U; index < value.count; ++index) {
        value.terms[index].coefficient = checked_raw(-static_cast<WideInteger>(value.terms[index].coefficient));
    }
    return value;
}

[[nodiscard]] Raa add(const Raa& first, const Raa& second, RaaContext& context) {
    Raa result{};
    result.center = checked_raw(static_cast<WideInteger>(first.center) + second.center);
    result.residual = checked_raw(static_cast<WideInteger>(first.residual) + second.residual);
    std::array<Term, kStorageTerms * 2U> merged{};
    std::size_t out = 0U, a = 0U, b = 0U;
    while (a < first.count || b < second.count) {
        if (b == second.count || (a < first.count && first.terms[a].id < second.terms[b].id)) {
            merged[out++] = first.terms[a++];
        } else if (a == first.count || second.terms[b].id < first.terms[a].id) {
            merged[out++] = second.terms[b++];
        } else {
            merged[out++] = {first.terms[a].id,
                checked_raw(static_cast<WideInteger>(first.terms[a].coefficient) + second.terms[b].coefficient)};
            ++a; ++b;
        }
    }
    if (out > kStorageTerms) {
        insertion_sort_prefix(merged, out, magnitude_precedes);
        WideInteger discarded = 0;
        for (std::size_t index = kStorageTerms; index < out; ++index) discarded += abs_raw(merged[index].coefficient);
        out = kStorageTerms;
        add_residual(result, checked_raw(discarded));
        ++context.compressions;
        insertion_sort_prefix(merged, out, id_precedes);
    }
    result.count = static_cast<std::uint8_t>(out);
    for (std::size_t index = 0U; index < out; ++index) result.terms[index] = merged[index];
    canonical_reduce(result, context);
    return result;
}

[[nodiscard]] Raa multiply(const Raa& first, const Raa& second, RaaContext& context) {
    Raa result{};
    bool center_rounded = false;
    result.center = mul_trunc(first.center, second.center, center_rounded);
    if (center_rounded) { add_residual(result, 1); ++context.rounding_guard_raw; }

    std::array<Term, kStorageTerms * 2U + 1U> merged{};
    std::size_t out = 0U, a = 0U, b = 0U;
    while (a < first.count || b < second.count) {
        std::uint32_t id{};
        Fixed::rep ac{}, bc{};
        if (b == second.count || (a < first.count && first.terms[a].id < second.terms[b].id)) {
            id = first.terms[a].id; ac = first.terms[a].coefficient; ++a;
        } else if (a == first.count || second.terms[b].id < first.terms[a].id) {
            id = second.terms[b].id; bc = second.terms[b].coefficient; ++b;
        } else {
            id = first.terms[a].id; ac = first.terms[a].coefficient; bc = second.terms[b].coefficient; ++a; ++b;
        }
        const WideInteger first_contribution = static_cast<WideInteger>(first.center) * bc;
        const WideInteger second_contribution = static_cast<WideInteger>(second.center) * ac;
        const WideInteger numerator = checked_wide_add(first_contribution, second_contribution);
        const WideInteger quotient = numerator / static_cast<WideInteger>(kScale);
        const bool rounded = (numerator % static_cast<WideInteger>(kScale)) != 0;
        const Fixed::rep coefficient = checked_raw(quotient);
        if (coefficient != 0) merged[out++] = {id, coefficient};
        if (rounded) { add_residual(result, 1); ++context.rounding_guard_raw; }
    }

    const Fixed::rep radius_first = total_radius(first);
    const Fixed::rep radius_second = total_radius(second);
    const Fixed::rep nonlinear_remainder = checked_raw(
        static_cast<WideInteger>(mul_abs_ceil(first.center, second.residual))
        + mul_abs_ceil(second.center, first.residual)
        + mul_abs_ceil(radius_first, radius_second));
    if (nonlinear_remainder != 0) merged[out++] = {take_error_id(context), nonlinear_remainder};

    if (out > kStorageTerms) {
        insertion_sort_prefix(merged, out, magnitude_precedes);
        WideInteger discarded = 0;
        for (std::size_t index = kStorageTerms; index < out; ++index) discarded += abs_raw(merged[index].coefficient);
        out = kStorageTerms;
        add_residual(result, checked_raw(discarded));
        ++context.compressions;
        insertion_sort_prefix(merged, out, id_precedes);
    }
    result.count = static_cast<std::uint8_t>(out);
    for (std::size_t index = 0U; index < out; ++index) result.terms[index] = merged[index];
    canonical_reduce(result, context);
    return result;
}

[[nodiscard]] Raa scale(const Raa& value, Fixed scalar, RaaContext& context) {
    return multiply(value, constant(scalar), context);
}

struct PairState final { Raa x1{}, x2{}, v1{}, v2{}; };

void step_pair(PairState& pair, const Raa& stiffness, const Raa& damping, const Raa& cubic,
               const Raa& rest, Fixed dt, RaaContext& context) {
    // Every operation that may allocate an affine error ID is sequenced in its
    // own full-expression. C++ does not define argument evaluation order, so
    // nested calls with context side effects would otherwise be toolchain-dependent.
    const Raa negative_x1 = negate(pair.x1);
    const Raa separation = add(pair.x2, negative_x1, context);
    const Raa negative_rest = negate(rest);
    const Raa gap = add(separation, negative_rest, context);
    const Raa negative_v1 = negate(pair.v1);
    const Raa relative_velocity = add(pair.v2, negative_v1, context);
    const Raa gap2 = multiply(gap, gap, context);
    const Raa gap3 = multiply(gap2, gap, context);
    const Raa stiffness_force = multiply(stiffness, gap, context);
    const Raa damping_force = multiply(damping, relative_velocity, context);
    const Raa cubic_force = multiply(cubic, gap3, context);
    const Raa linear_force = add(stiffness_force, damping_force, context);
    const Raa total_force = add(linear_force, cubic_force, context);
    const Raa force = negate(total_force);
    const Raa impulse = scale(force, dt, context);
    const Raa negative_impulse = negate(impulse);
    pair.v1 = add(pair.v1, negative_impulse, context);
    pair.v2 = add(pair.v2, impulse, context);
    const Raa displacement1 = scale(pair.v1, dt, context);
    const Raa displacement2 = scale(pair.v2, dt, context);
    pair.x1 = add(pair.x1, displacement1, context);
    pair.x2 = add(pair.x2, displacement2, context);
}

[[nodiscard]] std::pair<double, double> bounds(const Raa& value) {
    const double c = Fixed::from_raw(value.center).to_double();
    const double r = Fixed::from_raw(total_radius(value)).to_double();
    return {std::nextafter(c - r, -std::numeric_limits<double>::infinity()),
            std::nextafter(c + r, std::numeric_limits<double>::infinity())};
}

void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) { hash ^= (value >> (byte * 8U)) & 0xFFU; hash *= 0x100000001B3ULL; }
}

[[nodiscard]] double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1U))];
}

} // namespace

FixedRaaMicrokernelResult run_fixed_raa_microkernel(const FixedRaaMicrokernelConfig& config) {
    if (config.bodies < 2U || (config.bodies & 1U) != 0U || config.steps == 0U
        || config.maximum_terms < 2U || config.maximum_terms > kStorageTerms
        || config.monte_carlo_samples == 0U || config.timing_repetitions == 0U) {
        throw std::invalid_argument("Fixed RAA microkernel configuration is invalid");
    }
    const std::size_t pairs = config.bodies / 2U;
    std::vector<PairState> states(pairs);
    std::vector<double> samples; samples.reserve(config.timing_repetitions);
    FixedRaaMicrokernelResult result{};
    const Fixed dt = Fixed::from_ratio(1, 240);

    for (std::size_t repetition = 0U; repetition < config.timing_repetitions; ++repetition) {
        RaaContext context{.maximum_terms = config.maximum_terms};
        const Raa stiffness = uncertain(Fixed::from_ratio(3, 2), Fixed::from_ratio(1, 20), context);
        const Raa damping = uncertain(Fixed::from_ratio(1, 5), Fixed::from_ratio(1, 100), context);
        const Raa cubic = uncertain(Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 200), context);
        const Raa rest = uncertain(Fixed::from_integer(1), Fixed::from_ratio(1, 100), context);
        for (std::size_t index = 0U; index < pairs; ++index) {
            const Fixed offset = Fixed::from_ratio(static_cast<Fixed::rep>(index % 17U), 1000);
            states[index].x1 = uncertain(Fixed::from_raw(-offset.raw()), Fixed::from_ratio(1, 200), context);
            states[index].x2 = uncertain(Fixed::from_raw(Fixed::scale + offset.raw()), Fixed::from_ratio(1, 200), context);
            states[index].v1 = uncertain(Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 500), context);
            states[index].v2 = uncertain(Fixed::from_ratio(-1, 20), Fixed::from_ratio(1, 500), context);
        }
        double width_sum = 0.0;
        const auto begin = Clock::now();
        for (std::size_t step = 0U; step < config.steps; ++step) {
            for (PairState& pair : states) step_pair(pair, stiffness, damping, cubic, rest, dt, context);
            if (repetition + 1U == config.timing_repetitions) {
                for (const PairState& pair : states) {
                    const auto x1b = bounds(pair.x1), x2b = bounds(pair.x2), v1b = bounds(pair.v1), v2b = bounds(pair.v2);
                    width_sum += (x1b.second - x1b.first) + (x2b.second - x2b.first)
                        + (v1b.second - v1b.first) + (v2b.second - v2b.first);
                }
            }
        }
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        if (repetition + 1U == config.timing_repetitions) {
            result.maximum_terms = context.maximum_observed;
            result.compressions = context.compressions;
            result.rounding_guard_raw = context.rounding_guard_raw;
            result.average_total_width = width_sum / static_cast<double>(config.steps * pairs);
            result.final_total_width = 0.0;
            for (const PairState& pair : states) {
                for (const Raa* value : {&pair.x1, &pair.x2, &pair.v1, &pair.v2}) {
                    const auto b = bounds(*value); result.final_total_width += b.second - b.first;
                }
            }
        }
    }
    result.p50_us = percentile(samples, 0.50);
    result.p95_us = percentile(samples, 0.95);
    result.ns_per_body_step = result.p50_us * 1000.0 / static_cast<double>(config.bodies * config.steps);
    result.ns_per_contact_step = result.p50_us * 1000.0 / static_cast<double>(pairs * config.steps);

    // Empirical containment for one representative pair. Parameters are sampled
    // from the same independent boxes used by the RAA initialization.
    RaaContext reference_context{.maximum_terms = config.maximum_terms};
    PairState reference{};
    reference.x1 = uncertain(Fixed::from_integer(0), Fixed::from_ratio(1, 200), reference_context);
    reference.x2 = uncertain(Fixed::from_integer(1), Fixed::from_ratio(1, 200), reference_context);
    reference.v1 = uncertain(Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 500), reference_context);
    reference.v2 = uncertain(Fixed::from_ratio(-1, 20), Fixed::from_ratio(1, 500), reference_context);
    const Raa rk = uncertain(Fixed::from_ratio(3, 2), Fixed::from_ratio(1, 20), reference_context);
    const Raa rc = uncertain(Fixed::from_ratio(1, 5), Fixed::from_ratio(1, 100), reference_context);
    const Raa ra = uncertain(Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 200), reference_context);
    const Raa rr = uncertain(Fixed::from_integer(1), Fixed::from_ratio(1, 100), reference_context);
    for (std::size_t step = 0U; step < config.steps; ++step) step_pair(reference, rk, rc, ra, rr, dt, reference_context);
    const auto bx1 = bounds(reference.x1), bx2 = bounds(reference.x2), bv1 = bounds(reference.v1), bv2 = bounds(reference.v2);
    std::mt19937_64 rng(config.seed);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    constexpr double dtd = 1.0 / 240.0;
    for (std::size_t sample = 0U; sample < config.monte_carlo_samples; ++sample) {
        double x1 = 0.005 * unit(rng), x2 = 1.0 + 0.005 * unit(rng);
        double v1 = 0.05 + 0.002 * unit(rng), v2 = -0.05 + 0.002 * unit(rng);
        const double k = 1.5 + 0.05 * unit(rng), c = 0.2 + 0.01 * unit(rng);
        const double alpha = 0.05 + 0.005 * unit(rng), rest = 1.0 + 0.01 * unit(rng);
        for (std::size_t step = 0U; step < config.steps; ++step) {
            const double gap = (x2 - x1) - rest;
            const double relative = v2 - v1;
            const double force = -k * gap - c * relative - alpha * gap * gap * gap;
            v1 -= dtd * force; v2 += dtd * force;
            x1 += dtd * v1; x2 += dtd * v2;
        }
        if (x1 < bx1.first || x1 > bx1.second || x2 < bx2.first || x2 > bx2.second
            || v1 < bv1.first || v1 > bv1.second || v2 < bv2.first || v2 > bv2.second) {
            ++result.empirical_violations;
        }
    }

    std::uint64_t hash = 0xCBF29CE484222325ULL;
    mix_u64(hash, result.maximum_terms); mix_u64(hash, result.compressions); mix_u64(hash, result.rounding_guard_raw);
    mix_u64(hash, result.empirical_violations);
    for (const PairState& pair : states) {
        for (const Raa* value : {&pair.x1, &pair.x2, &pair.v1, &pair.v2}) {
            mix_u64(hash, static_cast<std::uint64_t>(value->center)); mix_u64(hash, static_cast<std::uint64_t>(value->residual));
            mix_u64(hash, value->count);
            for (std::size_t term = 0U; term < value->count; ++term) {
                mix_u64(hash, value->terms[term].id); mix_u64(hash, static_cast<std::uint64_t>(value->terms[term].coefficient));
            }
        }
    }
    result.hash = hash;
    return result;
}


FixedRaaOperationProbeResult run_fixed_raa_operation_probe(
    std::size_t iterations, std::size_t maximum_terms) {
    if (iterations == 0U || maximum_terms < 2U || maximum_terms > kStorageTerms) {
        throw std::invalid_argument("Fixed RAA operation probe configuration is invalid");
    }

    FixedRaaOperationProbeResult result{
        .iterations = iterations,
        .maximum_terms = maximum_terms,
        .hash = 0xCBF29CE484222325ULL,
    };
    constexpr std::size_t pairs = 4U;
    constexpr std::size_t steps = 8U;
    const Fixed dt = Fixed::from_ratio(1, 240);

    for (std::size_t repetition = 0U; repetition < iterations; ++repetition) {
        RaaContext context{.maximum_terms = maximum_terms};
        std::array<PairState, pairs> states{};
        const Raa stiffness = uncertain(Fixed::from_ratio(3, 2), Fixed::from_ratio(1, 20), context);
        const Raa damping = uncertain(Fixed::from_ratio(1, 5), Fixed::from_ratio(1, 100), context);
        const Raa cubic = uncertain(Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 200), context);
        const Raa rest = uncertain(Fixed::from_integer(1), Fixed::from_ratio(1, 100), context);
        for (std::size_t index = 0U; index < pairs; ++index) {
            const Fixed offset = Fixed::from_ratio(static_cast<Fixed::rep>(index), 1000);
            states[index].x1 = uncertain(Fixed::from_raw(-offset.raw()), Fixed::from_ratio(1, 200), context);
            states[index].x2 = uncertain(Fixed::from_raw(Fixed::scale + offset.raw()), Fixed::from_ratio(1, 200), context);
            states[index].v1 = uncertain(Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 500), context);
            states[index].v2 = uncertain(Fixed::from_ratio(-1, 20), Fixed::from_ratio(1, 500), context);
        }
        for (std::size_t step = 0U; step < steps; ++step) {
            for (PairState& pair : states) {
                step_pair(pair, stiffness, damping, cubic, rest, dt, context);
            }
        }
        result.compressions += context.compressions;
        result.rounding_guard_raw += context.rounding_guard_raw;

        // Adversarial capacity case: 16 disjoint symbols from each operand plus
        // one nonlinear-error symbol require 33 temporary entries before the
        // deterministic reduction to the configured logical capacity.
        RaaContext disjoint_context{.maximum_terms = maximum_terms, .next_id = 33U};
        Raa disjoint_left{.center = Fixed::scale};
        Raa disjoint_right{.center = Fixed::scale};
        disjoint_left.count = static_cast<std::uint8_t>(kStorageTerms);
        disjoint_right.count = static_cast<std::uint8_t>(kStorageTerms);
        for (std::size_t term = 0U; term < kStorageTerms; ++term) {
            const Fixed::rep coefficient = static_cast<Fixed::rep>(term + 1U);
            disjoint_left.terms[term] = {static_cast<std::uint32_t>(term + 1U), coefficient};
            disjoint_right.terms[term] = {static_cast<std::uint32_t>(term + 17U), coefficient};
        }
        const Raa disjoint_product = multiply(disjoint_left, disjoint_right, disjoint_context);
        if (disjoint_context.compressions == 0U || disjoint_product.count > maximum_terms) {
            throw std::logic_error("Fixed RAA disjoint product did not exercise temporary capacity reduction");
        }
        result.compressions += disjoint_context.compressions;
        result.rounding_guard_raw += disjoint_context.rounding_guard_raw;
        ++result.disjoint_product_cases;
        mix_u64(result.hash, static_cast<std::uint64_t>(disjoint_product.center));
        mix_u64(result.hash, static_cast<std::uint64_t>(disjoint_product.residual));
        mix_u64(result.hash, disjoint_product.count);
        for (std::size_t term = 0U; term < disjoint_product.count; ++term) {
            mix_u64(result.hash, disjoint_product.terms[term].id);
            mix_u64(result.hash, static_cast<std::uint64_t>(disjoint_product.terms[term].coefficient));
        }

        for (const PairState& pair : states) {
            for (const Raa* value : {&pair.x1, &pair.x2, &pair.v1, &pair.v2}) {
                mix_u64(result.hash, static_cast<std::uint64_t>(value->center));
                mix_u64(result.hash, static_cast<std::uint64_t>(value->residual));
                mix_u64(result.hash, value->count);
                for (std::size_t term = 0U; term < value->count; ++term) {
                    mix_u64(result.hash, value->terms[term].id);
                    mix_u64(result.hash, static_cast<std::uint64_t>(value->terms[term].coefficient));
                }
            }
        }
    }
    if (result.compressions == 0U || result.rounding_guard_raw == 0U
        || result.disjoint_product_cases != iterations) {
        throw std::logic_error("Fixed RAA operation probe did not exercise guarded reduction paths");
    }
    return result;
}


namespace {

struct SelectiveContactSpec final {
    Fixed x1_center{};
    Fixed x2_center{};
    Fixed v1_center{};
    Fixed v2_center{};
    Fixed x1_radius{};
    Fixed x2_radius{};
    Fixed v1_radius{};
    Fixed v2_radius{};
};

struct Interval final {
    Fixed::rep lower{};
    Fixed::rep upper{};
};

struct NominalPairState final {
    Fixed x1{};
    Fixed x2{};
    Fixed v1{};
    Fixed v2{};
};

struct SelectiveRaaRuntime final {
    RaaContext context{};
    Raa stiffness{};
    Raa damping{};
    Raa cubic{};
    Raa rest{};
    PairState pair{};
};

struct IntervalRuntime final {
    Interval stiffness{};
    Interval damping{};
    Interval cubic{};
    Interval rest{};
    Interval x1{};
    Interval x2{};
    Interval v1{};
    Interval v2{};
};

[[nodiscard]] std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    std::uint64_t value = state;
    value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31U);
}

[[nodiscard]] std::int64_t sample_integer(
    std::uint64_t& state, std::int64_t minimum, std::int64_t maximum) {
    if (minimum > maximum) throw std::invalid_argument("Invalid deterministic corpus range");
    const std::uint64_t span = static_cast<std::uint64_t>(maximum - minimum) + 1U;
    return minimum + static_cast<std::int64_t>(splitmix64(state) % span);
}

[[nodiscard]] Fixed milli(std::int64_t value) {
    return Fixed::from_ratio(value, 1'000);
}

[[nodiscard]] SelectiveContactSpec make_contact_spec(
    FixedRaaSelectiveProfile profile, std::size_t index, std::uint64_t seed) {
    std::uint64_t state = seed
        ^ (static_cast<std::uint64_t>(profile) << 56U)
        ^ (static_cast<std::uint64_t>(index) * 0xD6E8FEB86659FD93ULL);

    std::int64_t gap_milli{};
    std::int64_t relative_milli{};
    switch (profile) {
    case FixedRaaSelectiveProfile::Mixed:
        gap_milli = sample_integer(state, -80, 80);
        relative_milli = sample_integer(state, -250, 250);
        break;
    case FixedRaaSelectiveProfile::MostlySafe: {
        const std::int64_t magnitude = sample_integer(state, 45, 140);
        gap_milli = (splitmix64(state) & 1U) == 0U ? magnitude : -magnitude;
        relative_milli = sample_integer(state, -100, 100);
        break;
    }
    case FixedRaaSelectiveProfile::BoundaryDense:
        gap_milli = sample_integer(state, -22, 22);
        relative_milli = sample_integer(state, -140, 140);
        break;
    case FixedRaaSelectiveProfile::Approaching:
        gap_milli = sample_integer(state, 10, 100);
        relative_milli = sample_integer(state, -500, -50);
        break;
    case FixedRaaSelectiveProfile::Separating:
        gap_milli = sample_integer(state, 10, 100);
        relative_milli = sample_integer(state, 50, 500);
        break;
    }

    const std::int64_t anchor_milli = sample_integer(state, -10, 10);
    const std::int64_t v1_milli = -(relative_milli / 2);
    const std::int64_t v2_milli = v1_milli + relative_milli;
    const Fixed x1 = milli(anchor_milli);
    const Fixed x2 = x1 + Fixed::from_integer(1) + milli(gap_milli);

    return {
        .x1_center = x1,
        .x2_center = x2,
        .v1_center = milli(v1_milli),
        .v2_center = milli(v2_milli),
        .x1_radius = milli(sample_integer(state, 1, 5)),
        .x2_radius = milli(sample_integer(state, 1, 5)),
        .v1_radius = milli(sample_integer(state, 1, 4)),
        .v2_radius = milli(sample_integer(state, 1, 4)),
    };
}

[[nodiscard]] WideInteger floor_div_scale(WideInteger numerator) {
    const WideInteger denominator = static_cast<WideInteger>(kScale);
    if (numerator >= 0) return numerator / denominator;
    return -((-numerator + denominator - 1) / denominator);
}

[[nodiscard]] WideInteger ceil_div_scale(WideInteger numerator) {
    const WideInteger denominator = static_cast<WideInteger>(kScale);
    if (numerator >= 0) return (numerator + denominator - 1) / denominator;
    return -((-numerator) / denominator);
}

[[nodiscard]] Interval interval_point(Fixed value) noexcept {
    return {.lower = value.raw(), .upper = value.raw()};
}

[[nodiscard]] Interval interval_around(Fixed center, Fixed radius) {
    if (radius.raw() < 0) throw std::invalid_argument("Negative interval radius");
    return {
        .lower = checked_raw(static_cast<WideInteger>(center.raw()) - radius.raw()),
        .upper = checked_raw(static_cast<WideInteger>(center.raw()) + radius.raw()),
    };
}

[[nodiscard]] Interval interval_add(const Interval& first, const Interval& second) {
    return {
        .lower = checked_raw(static_cast<WideInteger>(first.lower) + second.lower),
        .upper = checked_raw(static_cast<WideInteger>(first.upper) + second.upper),
    };
}

[[nodiscard]] Interval interval_negate(const Interval& value) {
    return {
        .lower = checked_raw(-static_cast<WideInteger>(value.upper)),
        .upper = checked_raw(-static_cast<WideInteger>(value.lower)),
    };
}

[[nodiscard]] Interval interval_subtract(const Interval& first, const Interval& second) {
    return interval_add(first, interval_negate(second));
}

[[nodiscard]] Interval interval_multiply(const Interval& first, const Interval& second) {
    const std::array<WideInteger, 4U> products{
        static_cast<WideInteger>(first.lower) * second.lower,
        static_cast<WideInteger>(first.lower) * second.upper,
        static_cast<WideInteger>(first.upper) * second.lower,
        static_cast<WideInteger>(first.upper) * second.upper,
    };
    const auto [minimum, maximum] = std::minmax_element(products.begin(), products.end());
    return {
        .lower = checked_raw(floor_div_scale(*minimum)),
        .upper = checked_raw(ceil_div_scale(*maximum)),
    };
}

[[nodiscard]] bool interval_contains_zero(const Interval& value) noexcept {
    return value.lower <= 0 && value.upper >= 0;
}

void step_interval(IntervalRuntime& state, const Interval& dt) {
    const Interval gap = interval_subtract(interval_subtract(state.x2, state.x1), state.rest);
    const Interval relative = interval_subtract(state.v2, state.v1);
    const Interval gap2 = interval_multiply(gap, gap);
    const Interval gap3 = interval_multiply(gap2, gap);
    const Interval stiffness_force = interval_multiply(state.stiffness, gap);
    const Interval damping_force = interval_multiply(state.damping, relative);
    const Interval cubic_force = interval_multiply(state.cubic, gap3);
    const Interval force = interval_negate(
        interval_add(interval_add(stiffness_force, damping_force), cubic_force));
    const Interval impulse = interval_multiply(force, dt);
    state.v1 = interval_subtract(state.v1, impulse);
    state.v2 = interval_add(state.v2, impulse);
    state.x1 = interval_add(state.x1, interval_multiply(state.v1, dt));
    state.x2 = interval_add(state.x2, interval_multiply(state.v2, dt));
}

[[nodiscard]] IntervalRuntime make_interval_runtime(const SelectiveContactSpec& spec) {
    return {
        .stiffness = interval_around(Fixed::from_ratio(3, 2), Fixed::from_ratio(1, 20)),
        .damping = interval_around(Fixed::from_ratio(1, 5), Fixed::from_ratio(1, 100)),
        .cubic = interval_around(Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 200)),
        .rest = interval_around(Fixed::from_integer(1), Fixed::from_ratio(1, 100)),
        .x1 = interval_around(spec.x1_center, spec.x1_radius),
        .x2 = interval_around(spec.x2_center, spec.x2_radius),
        .v1 = interval_around(spec.v1_center, spec.v1_radius),
        .v2 = interval_around(spec.v2_center, spec.v2_radius),
    };
}

[[nodiscard]] bool classify_contact_interval(
    const SelectiveContactSpec& spec, std::size_t steps) {
    IntervalRuntime runtime = make_interval_runtime(spec);
    const Interval dt = interval_point(Fixed::from_ratio(1, 240));
    auto gap = interval_subtract(interval_subtract(runtime.x2, runtime.x1), runtime.rest);
    if (interval_contains_zero(gap)) return true;
    for (std::size_t step = 0U; step < steps; ++step) {
        step_interval(runtime, dt);
        gap = interval_subtract(interval_subtract(runtime.x2, runtime.x1), runtime.rest);
        if (interval_contains_zero(gap)) return true;
    }
    return false;
}

[[nodiscard]] NominalPairState make_nominal_state(const SelectiveContactSpec& spec) noexcept {
    return {
        .x1 = spec.x1_center,
        .x2 = spec.x2_center,
        .v1 = spec.v1_center,
        .v2 = spec.v2_center,
    };
}

void step_nominal(NominalPairState& pair, Fixed dt) {
    const Fixed gap = (pair.x2 - pair.x1) - Fixed::from_integer(1);
    const Fixed relative = pair.v2 - pair.v1;
    const Fixed gap2 = gap * gap;
    const Fixed gap3 = gap2 * gap;
    const Fixed force = -((Fixed::from_ratio(3, 2) * gap)
        + (Fixed::from_ratio(1, 5) * relative)
        + (Fixed::from_ratio(1, 20) * gap3));
    const Fixed impulse = force * dt;
    pair.v1 -= impulse;
    pair.v2 += impulse;
    pair.x1 += pair.v1 * dt;
    pair.x2 += pair.v2 * dt;
}

[[nodiscard]] SelectiveRaaRuntime make_selective_raa_runtime(
    const SelectiveContactSpec& spec, std::size_t maximum_terms) {
    SelectiveRaaRuntime runtime{};
    runtime.context.maximum_terms = maximum_terms;
    runtime.stiffness = uncertain(
        Fixed::from_ratio(3, 2), Fixed::from_ratio(1, 20), runtime.context);
    runtime.damping = uncertain(
        Fixed::from_ratio(1, 5), Fixed::from_ratio(1, 100), runtime.context);
    runtime.cubic = uncertain(
        Fixed::from_ratio(1, 20), Fixed::from_ratio(1, 200), runtime.context);
    runtime.rest = uncertain(
        Fixed::from_integer(1), Fixed::from_ratio(1, 100), runtime.context);
    runtime.pair.x1 = uncertain(spec.x1_center, spec.x1_radius, runtime.context);
    runtime.pair.x2 = uncertain(spec.x2_center, spec.x2_radius, runtime.context);
    runtime.pair.v1 = uncertain(spec.v1_center, spec.v1_radius, runtime.context);
    runtime.pair.v2 = uncertain(spec.v2_center, spec.v2_radius, runtime.context);
    return runtime;
}

[[nodiscard]] bool raa_gap_contains_zero(const SelectiveRaaRuntime& runtime) {
    RaaContext observation{.maximum_terms = runtime.context.maximum_terms};
    const Raa negative_x1 = negate(runtime.pair.x1);
    const Raa separation = add(runtime.pair.x2, negative_x1, observation);
    const Raa negative_rest = negate(runtime.rest);
    const Raa gap = add(separation, negative_rest, observation);
    const auto enclosure = bounds(gap);
    return enclosure.first <= 0.0 && enclosure.second >= 0.0;
}

[[nodiscard]] bool same_raa(const Raa& first, const Raa& second) noexcept {
    if (first.center != second.center || first.count != second.count
        || first.residual != second.residual) return false;
    for (std::size_t index = 0U; index < first.count; ++index) {
        if (first.terms[index].id != second.terms[index].id
            || first.terms[index].coefficient != second.terms[index].coefficient) return false;
    }
    return true;
}

[[nodiscard]] bool same_pair(const PairState& first, const PairState& second) noexcept {
    return same_raa(first.x1, second.x1) && same_raa(first.x2, second.x2)
        && same_raa(first.v1, second.v1) && same_raa(first.v2, second.v2);
}

[[nodiscard]] double pair_width(const PairState& pair) {
    double width = 0.0;
    for (const Raa* value : {&pair.x1, &pair.x2, &pair.v1, &pair.v2}) {
        const auto enclosure = bounds(*value);
        width += enclosure.second - enclosure.first;
    }
    return width;
}

void mix_fixed(std::uint64_t& hash, Fixed value) noexcept {
    mix_u64(hash, static_cast<std::uint64_t>(value.raw()));
}

void mix_nominal_pair(std::uint64_t& hash, const NominalPairState& pair) noexcept {
    mix_fixed(hash, pair.x1);
    mix_fixed(hash, pair.x2);
    mix_fixed(hash, pair.v1);
    mix_fixed(hash, pair.v2);
}

void mix_raa(std::uint64_t& hash, const Raa& value) noexcept {
    mix_u64(hash, static_cast<std::uint64_t>(value.center));
    mix_u64(hash, static_cast<std::uint64_t>(value.residual));
    mix_u64(hash, value.count);
    for (std::size_t index = 0U; index < value.count; ++index) {
        mix_u64(hash, value.terms[index].id);
        mix_u64(hash, static_cast<std::uint64_t>(value.terms[index].coefficient));
    }
}

void mix_raa_pair(std::uint64_t& hash, const PairState& pair) noexcept {
    mix_raa(hash, pair.x1);
    mix_raa(hash, pair.x2);
    mix_raa(hash, pair.v1);
    mix_raa(hash, pair.v2);
}

[[nodiscard]] FixedRaaTimingDistribution timing_distribution(
    const std::vector<double>& input) {
    if (input.empty()) throw std::invalid_argument("Empty timing distribution");
    std::vector<double> values = input;
    std::sort(values.begin(), values.end());
    const auto select = [&values](double fraction) {
        const auto index = static_cast<std::size_t>(
            fraction * static_cast<double>(values.size() - 1U));
        return values[index];
    };
    return {
        .p50_us = select(0.50),
        .p95_us = select(0.95),
        .p99_us = select(0.99),
        .maximum_us = values.back(),
    };
}

[[nodiscard]] std::vector<std::uint8_t> classify_corpus(
    const std::vector<SelectiveContactSpec>& corpus, std::size_t steps) {
    std::vector<std::uint8_t> selected(corpus.size(), 0U);
    for (std::size_t index = 0U; index < corpus.size(); ++index) {
        selected[index] = classify_contact_interval(corpus[index], steps) ? 1U : 0U;
    }
    return selected;
}

[[nodiscard]] std::vector<NominalPairState> initialize_nominal(
    const std::vector<SelectiveContactSpec>& corpus) {
    std::vector<NominalPairState> states;
    states.reserve(corpus.size());
    for (const SelectiveContactSpec& spec : corpus) states.push_back(make_nominal_state(spec));
    return states;
}

[[nodiscard]] std::vector<SelectiveRaaRuntime> initialize_raa(
    const std::vector<SelectiveContactSpec>& corpus, std::size_t maximum_terms) {
    std::vector<SelectiveRaaRuntime> states;
    states.reserve(corpus.size());
    for (const SelectiveContactSpec& spec : corpus) {
        states.push_back(make_selective_raa_runtime(spec, maximum_terms));
    }
    return states;
}

void step_all_nominal(std::vector<NominalPairState>& states, std::size_t steps) {
    const Fixed dt = Fixed::from_ratio(1, 240);
    for (std::size_t step = 0U; step < steps; ++step) {
        for (NominalPairState& pair : states) step_nominal(pair, dt);
    }
}

void step_all_raa(std::vector<SelectiveRaaRuntime>& states, std::size_t steps) {
    const Fixed dt = Fixed::from_ratio(1, 240);
    for (std::size_t step = 0U; step < steps; ++step) {
        for (SelectiveRaaRuntime& runtime : states) {
            step_pair(runtime.pair, runtime.stiffness, runtime.damping,
                runtime.cubic, runtime.rest, dt, runtime.context);
        }
    }
}

void step_selective(
    std::vector<SelectiveRaaRuntime>& raa_states,
    std::vector<NominalPairState>& nominal_states,
    const std::vector<std::uint8_t>& selected,
    std::size_t steps) {
    const Fixed dt = Fixed::from_ratio(1, 240);
    for (std::size_t step = 0U; step < steps; ++step) {
        for (std::size_t index = 0U; index < selected.size(); ++index) {
            if (selected[index] != 0U) {
                SelectiveRaaRuntime& runtime = raa_states[index];
                step_pair(runtime.pair, runtime.stiffness, runtime.damping,
                    runtime.cubic, runtime.rest, dt, runtime.context);
            } else {
                step_nominal(nominal_states[index], dt);
            }
        }
    }
}

[[nodiscard]] std::uint64_t hash_mask(const std::vector<std::uint8_t>& mask) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const std::uint8_t value : mask) mix_u64(hash, value);
    return hash;
}

[[nodiscard]] std::uint64_t hash_nominal_centers(
    const std::vector<NominalPairState>& states) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const NominalPairState& state : states) mix_nominal_pair(hash, state);
    return hash;
}

[[nodiscard]] std::uint64_t hash_raa_centers(
    const std::vector<SelectiveRaaRuntime>& states) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const SelectiveRaaRuntime& runtime : states) {
        mix_u64(hash, static_cast<std::uint64_t>(runtime.pair.x1.center));
        mix_u64(hash, static_cast<std::uint64_t>(runtime.pair.x2.center));
        mix_u64(hash, static_cast<std::uint64_t>(runtime.pair.v1.center));
        mix_u64(hash, static_cast<std::uint64_t>(runtime.pair.v2.center));
    }
    return hash;
}

[[nodiscard]] std::uint64_t hash_full_raa_state(
    const std::vector<SelectiveRaaRuntime>& states) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const SelectiveRaaRuntime& runtime : states) mix_raa_pair(hash, runtime.pair);
    return hash;
}

[[nodiscard]] std::uint64_t hash_selective_state(
    const std::vector<SelectiveRaaRuntime>& raa_states,
    const std::vector<NominalPairState>& nominal_states,
    const std::vector<std::uint8_t>& selected) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (std::size_t index = 0U; index < selected.size(); ++index) {
        mix_u64(hash, selected[index]);
        if (selected[index] != 0U) mix_raa_pair(hash, raa_states[index].pair);
        else mix_nominal_pair(hash, nominal_states[index]);
    }
    return hash;
}

} // namespace

FixedRaaSelectiveBatchResult evaluate_fixed_raa_selective_contacts(
    std::span<const FixedRaaSelectiveContactInput> contacts,
    std::size_t steps,
    std::size_t maximum_terms) {
    if (contacts.empty() || steps == 0U
        || maximum_terms < 2U || maximum_terms > kStorageTerms) {
        throw std::invalid_argument("Selective RAA batch configuration is invalid");
    }

    const auto diagnostic_begin = Clock::now();
    std::vector<SelectiveContactSpec> corpus;
    corpus.reserve(contacts.size());
    FixedRaaSelectiveBatchResult result{};
    result.corpus_hash = 0xCBF29CE484222325ULL;
    for (const FixedRaaSelectiveContactInput& input : contacts) {
        const SelectiveContactSpec spec{
            .x1_center = input.x1_center,
            .x2_center = input.x2_center,
            .v1_center = input.v1_center,
            .v2_center = input.v2_center,
            .x1_radius = input.x1_radius,
            .x2_radius = input.x2_radius,
            .v1_radius = input.v1_radius,
            .v2_radius = input.v2_radius,
        };
        corpus.push_back(spec);
        for (const Fixed value : {spec.x1_center, spec.x2_center,
                 spec.v1_center, spec.v2_center, spec.x1_radius,
                 spec.x2_radius, spec.v1_radius, spec.v2_radius}) {
            mix_fixed(result.corpus_hash, value);
        }
    }

    const auto full_begin = Clock::now();
    std::vector<SelectiveRaaRuntime> full_reference = initialize_raa(corpus, maximum_terms);
    result.oracle_mask.assign(contacts.size(), 0U);
    for (std::size_t index = 0U; index < contacts.size(); ++index) {
        result.oracle_mask[index] = raa_gap_contains_zero(full_reference[index]) ? 1U : 0U;
    }
    const Fixed dt = Fixed::from_ratio(1, 240);
    for (std::size_t step = 0U; step < steps; ++step) {
        for (std::size_t index = 0U; index < contacts.size(); ++index) {
            SelectiveRaaRuntime& runtime = full_reference[index];
            step_pair(runtime.pair, runtime.stiffness, runtime.damping,
                runtime.cubic, runtime.rest, dt, runtime.context);
            if (raa_gap_contains_zero(runtime)) result.oracle_mask[index] = 1U;
        }
    }

    const auto full_end = Clock::now();
    const auto classifier_begin = Clock::now();
    result.classifier_mask = classify_corpus(corpus, steps);
    const auto classifier_end = Clock::now();
    result.oracle_mask_hash = hash_mask(result.oracle_mask);
    result.classifier_mask_hash = hash_mask(result.classifier_mask);
    for (std::size_t index = 0U; index < contacts.size(); ++index) {
        const bool oracle = result.oracle_mask[index] != 0U;
        const bool selected = result.classifier_mask[index] != 0U;
        result.oracle_vulnerable += oracle ? 1U : 0U;
        result.selected += selected ? 1U : 0U;
        result.true_positives += oracle && selected ? 1U : 0U;
        result.false_positives += !oracle && selected ? 1U : 0U;
        result.false_negatives += oracle && !selected ? 1U : 0U;
        result.true_negatives += !oracle && !selected ? 1U : 0U;
    }

    std::vector<NominalPairState> nominal_reference = initialize_nominal(corpus);
    step_all_nominal(nominal_reference, steps);
    result.full_center_hash = hash_raa_centers(full_reference);
    result.full_state_hash = hash_full_raa_state(full_reference);

    const auto selective_begin = Clock::now();
    std::vector<SelectiveRaaRuntime> selective_raa_reference = initialize_raa(
        corpus, maximum_terms);
    std::vector<NominalPairState> selective_nominal_reference = initialize_nominal(corpus);
    step_selective(selective_raa_reference, selective_nominal_reference,
        result.classifier_mask, steps);

    result.selective_center_hash = 0xCBF29CE484222325ULL;
    for (std::size_t index = 0U; index < contacts.size(); ++index) {
        const SelectiveRaaRuntime& full = full_reference[index];
        if (result.classifier_mask[index] != 0U) {
            const SelectiveRaaRuntime& selective = selective_raa_reference[index];
            if (!same_pair(full.pair, selective.pair)
                || full.context.compressions != selective.context.compressions
                || full.context.rounding_guard_raw != selective.context.rounding_guard_raw) {
                ++result.selected_state_mismatches;
            }
            for (const Raa* value : {&selective.pair.x1, &selective.pair.x2,
                     &selective.pair.v1, &selective.pair.v2}) {
                mix_u64(result.selective_center_hash,
                    static_cast<std::uint64_t>(value->center));
            }
        } else {
            mix_nominal_pair(result.selective_center_hash,
                selective_nominal_reference[index]);
        }

        const std::array<Fixed::rep, 4U> full_centers{
            full.pair.x1.center, full.pair.x2.center,
            full.pair.v1.center, full.pair.v2.center};
        const std::array<Fixed::rep, 4U> nominal_centers{
            nominal_reference[index].x1.raw(), nominal_reference[index].x2.raw(),
            nominal_reference[index].v1.raw(), nominal_reference[index].v2.raw()};
        for (std::size_t component = 0U; component < full_centers.size(); ++component) {
            if (full_centers[component] != nominal_centers[component]) {
                ++result.center_mismatches;
            }
        }
    }
    result.selective_state_hash = hash_selective_state(
        selective_raa_reference, selective_nominal_reference,
        result.classifier_mask);
    const auto selective_end = Clock::now();

    if (result.full_center_hash != result.selective_center_hash
        || result.center_mismatches != 0U
        || result.selected_state_mismatches != 0U) {
        throw std::logic_error("Selective RAA batch equivalence failed");
    }
    const auto diagnostic_end = Clock::now();
    result.full_raa_us = std::chrono::duration<double, std::micro>(
        full_end - full_begin).count();
    result.classifier_us = std::chrono::duration<double, std::micro>(
        classifier_end - classifier_begin).count();
    result.selective_kernel_us = std::chrono::duration<double, std::micro>(
        selective_end - selective_begin).count();
    result.selective_total_us = result.classifier_us + result.selective_kernel_us;
    result.diagnostic_total_us = std::chrono::duration<double, std::micro>(
        diagnostic_end - diagnostic_begin).count();
    return result;
}

FixedRaaIntervalArithmeticAuditResult run_fixed_raa_interval_arithmetic_audit(
    std::size_t cases, std::uint64_t seed) {
    if (cases == 0U) throw std::invalid_argument("Interval audit requires at least one case");

    FixedRaaIntervalArithmeticAuditResult result{};
    result.cases = cases;
    result.hash = 0xCBF29CE484222325ULL;
    constexpr Fixed::rep kAuditMagnitude = static_cast<Fixed::rep>(1ULL << 40U);
    constexpr std::uint64_t kAuditSpan = (1ULL << 41U) + 1ULL;

    const auto sample_raw = [&seed]() -> Fixed::rep {
        const std::uint64_t value = splitmix64(seed) % kAuditSpan;
        return static_cast<Fixed::rep>(value) - kAuditMagnitude;
    };
    const auto ordered_interval = [&sample_raw]() {
        const Fixed::rep first = sample_raw();
        const Fixed::rep second = sample_raw();
        return Interval{
            .lower = std::min(first, second),
            .upper = std::max(first, second),
        };
    };

    for (std::size_t index = 0U; index < cases; ++index) {
        const Interval first = ordered_interval();
        const Interval second = ordered_interval();
        const Interval product = interval_multiply(first, second);
        const std::array<WideInteger, 4U> exact_products{
            static_cast<WideInteger>(first.lower) * second.lower,
            static_cast<WideInteger>(first.lower) * second.upper,
            static_cast<WideInteger>(first.upper) * second.lower,
            static_cast<WideInteger>(first.upper) * second.upper,
        };
        const WideInteger scaled_lower = static_cast<WideInteger>(product.lower) * kScale;
        const WideInteger scaled_upper = static_cast<WideInteger>(product.upper) * kScale;
        for (const WideInteger exact : exact_products) {
            ++result.multiplication_corner_checks;
            if (scaled_lower > exact || scaled_upper < exact) ++result.violations;
        }

        const Interval sum = interval_add(first, second);
        const Interval difference = interval_subtract(first, second);
        const Interval negated = interval_negate(first);
        if (sum.lower != checked_raw(static_cast<WideInteger>(first.lower) + second.lower)
            || sum.upper != checked_raw(static_cast<WideInteger>(first.upper) + second.upper)) {
            ++result.violations;
        }
        if (difference.lower != checked_raw(
                static_cast<WideInteger>(first.lower) - second.upper)
            || difference.upper != checked_raw(
                static_cast<WideInteger>(first.upper) - second.lower)) {
            ++result.violations;
        }
        if (negated.lower != -first.upper || negated.upper != -first.lower) {
            ++result.violations;
        }
        if (product.lower > product.upper || sum.lower > sum.upper
            || difference.lower > difference.upper || negated.lower > negated.upper) {
            ++result.violations;
        }

        for (const Fixed::rep value : {first.lower, first.upper, second.lower, second.upper,
                 product.lower, product.upper, sum.lower, sum.upper,
                 difference.lower, difference.upper, negated.lower, negated.upper}) {
            mix_u64(result.hash, static_cast<std::uint64_t>(value));
        }
    }
    mix_u64(result.hash, result.cases);
    mix_u64(result.hash, result.multiplication_corner_checks);
    mix_u64(result.hash, result.violations);
    return result;
}

const char* fixed_raa_selective_profile_name(FixedRaaSelectiveProfile profile) noexcept {
    switch (profile) {
    case FixedRaaSelectiveProfile::Mixed: return "mixed";
    case FixedRaaSelectiveProfile::MostlySafe: return "mostly_safe";
    case FixedRaaSelectiveProfile::BoundaryDense: return "boundary_dense";
    case FixedRaaSelectiveProfile::Approaching: return "approaching";
    case FixedRaaSelectiveProfile::Separating: return "separating";
    }
    return "unknown";
}

FixedRaaSelectiveResult run_fixed_raa_selective_experiment(
    const FixedRaaSelectiveConfig& config) {
    if (config.contacts == 0U || config.steps == 0U
        || config.maximum_terms < 2U || config.maximum_terms > kStorageTerms
        || config.timing_repetitions < 2U) {
        throw std::invalid_argument("Selective RAA experiment configuration is invalid");
    }

    std::vector<SelectiveContactSpec> corpus;
    corpus.reserve(config.contacts);
    for (std::size_t index = 0U; index < config.contacts; ++index) {
        corpus.push_back(make_contact_spec(config.profile, index, config.seed));
    }

    FixedRaaSelectiveResult result{};
    result.contacts = config.contacts;
    result.corpus_hash = 0xCBF29CE484222325ULL;
    for (const SelectiveContactSpec& spec : corpus) {
        for (const Fixed value : {spec.x1_center, spec.x2_center, spec.v1_center, spec.v2_center,
                 spec.x1_radius, spec.x2_radius, spec.v1_radius, spec.v2_radius}) {
            mix_fixed(result.corpus_hash, value);
        }
    }

    std::vector<SelectiveRaaRuntime> full_reference = initialize_raa(corpus, config.maximum_terms);
    std::vector<std::uint8_t> oracle_mask(config.contacts, 0U);
    for (std::size_t index = 0U; index < config.contacts; ++index) {
        oracle_mask[index] = raa_gap_contains_zero(full_reference[index]) ? 1U : 0U;
    }
    const Fixed dt = Fixed::from_ratio(1, 240);
    for (std::size_t step = 0U; step < config.steps; ++step) {
        for (std::size_t index = 0U; index < config.contacts; ++index) {
            SelectiveRaaRuntime& runtime = full_reference[index];
            step_pair(runtime.pair, runtime.stiffness, runtime.damping,
                runtime.cubic, runtime.rest, dt, runtime.context);
            if (raa_gap_contains_zero(runtime)) oracle_mask[index] = 1U;
        }
    }

    const std::vector<std::uint8_t> selected_mask = classify_corpus(corpus, config.steps);
    result.oracle_mask_hash = hash_mask(oracle_mask);
    result.classifier_mask_hash = hash_mask(selected_mask);

    for (std::size_t index = 0U; index < config.contacts; ++index) {
        const bool oracle = oracle_mask[index] != 0U;
        const bool selected = selected_mask[index] != 0U;
        result.oracle_vulnerable += oracle ? 1U : 0U;
        result.selected += selected ? 1U : 0U;
        result.true_positives += oracle && selected ? 1U : 0U;
        result.false_positives += !oracle && selected ? 1U : 0U;
        result.false_negatives += oracle && !selected ? 1U : 0U;
        result.true_negatives += !oracle && !selected ? 1U : 0U;
        const double width = pair_width(full_reference[index].pair);
        result.full_final_width += width;
        if (oracle) result.oracle_vulnerable_final_width += width;
        if (selected) result.selected_final_width += width;
        if (oracle && !selected) result.missed_vulnerable_final_width += width;
        result.full_compressions += full_reference[index].context.compressions;
        result.full_rounding_guard_raw += full_reference[index].context.rounding_guard_raw;
    }

    std::vector<NominalPairState> nominal_reference = initialize_nominal(corpus);
    step_all_nominal(nominal_reference, config.steps);
    result.nominal_center_hash = hash_nominal_centers(nominal_reference);
    result.full_center_hash = hash_raa_centers(full_reference);
    result.full_state_hash = hash_full_raa_state(full_reference);

    std::vector<SelectiveRaaRuntime> selective_raa_reference = initialize_raa(
        corpus, config.maximum_terms);
    std::vector<NominalPairState> selective_nominal_reference = initialize_nominal(corpus);
    step_selective(selective_raa_reference, selective_nominal_reference,
        selected_mask, config.steps);

    result.selective_center_hash = 0xCBF29CE484222325ULL;
    for (std::size_t index = 0U; index < config.contacts; ++index) {
        const SelectiveRaaRuntime& full = full_reference[index];
        if (selected_mask[index] != 0U) {
            const SelectiveRaaRuntime& selective = selective_raa_reference[index];
            if (!same_pair(full.pair, selective.pair)
                || full.context.compressions != selective.context.compressions
                || full.context.rounding_guard_raw != selective.context.rounding_guard_raw) {
                ++result.selected_state_mismatches;
            }
            result.selective_compressions += selective.context.compressions;
            result.selective_rounding_guard_raw += selective.context.rounding_guard_raw;
            for (const Raa* value : {&selective.pair.x1, &selective.pair.x2,
                     &selective.pair.v1, &selective.pair.v2}) {
                mix_u64(result.selective_center_hash, static_cast<std::uint64_t>(value->center));
            }
        } else {
            const NominalPairState& nominal = selective_nominal_reference[index];
            mix_nominal_pair(result.selective_center_hash, nominal);
        }

        const std::array<Fixed::rep, 4U> full_centers{
            full.pair.x1.center, full.pair.x2.center, full.pair.v1.center, full.pair.v2.center};
        const std::array<Fixed::rep, 4U> nominal_centers{
            nominal_reference[index].x1.raw(), nominal_reference[index].x2.raw(),
            nominal_reference[index].v1.raw(), nominal_reference[index].v2.raw()};
        for (std::size_t component = 0U; component < full_centers.size(); ++component) {
            if (full_centers[component] != nominal_centers[component]) ++result.center_mismatches;
        }
    }
    result.selective_state_hash = hash_selective_state(
        selective_raa_reference, selective_nominal_reference, selected_mask);

    if (result.nominal_center_hash != result.full_center_hash
        || result.full_center_hash != result.selective_center_hash) {
        throw std::logic_error("Selective RAA center hash equivalence failed");
    }
    if (result.center_mismatches != 0U || result.selected_state_mismatches != 0U) {
        throw std::logic_error("Selective RAA state equivalence failed");
    }

    std::vector<double> nominal_samples;
    std::vector<double> classifier_samples;
    std::vector<double> full_samples;
    std::vector<double> selective_kernel_samples;
    std::vector<double> selective_total_samples;
    nominal_samples.reserve(config.timing_repetitions);
    classifier_samples.reserve(config.timing_repetitions);
    full_samples.reserve(config.timing_repetitions);
    selective_kernel_samples.reserve(config.timing_repetitions);
    selective_total_samples.reserve(config.timing_repetitions);

    std::uint64_t timing_sink = 0xCBF29CE484222325ULL;
    for (std::size_t repetition = 0U; repetition < config.timing_repetitions; ++repetition) {
        std::vector<std::uint8_t> measured_mask(config.contacts, 0U);
        const auto classifier_begin = Clock::now();
        for (std::size_t index = 0U; index < config.contacts; ++index) {
            measured_mask[index] = classify_contact_interval(corpus[index], config.steps) ? 1U : 0U;
        }
        const auto classifier_end = Clock::now();
        const double classifier_us = std::chrono::duration<double, std::micro>(
            classifier_end - classifier_begin).count();
        classifier_samples.push_back(classifier_us);
        if (measured_mask != selected_mask) {
            throw std::logic_error("Selective RAA classifier changed between repetitions");
        }
        mix_u64(timing_sink, hash_mask(measured_mask));

        std::vector<NominalPairState> nominal_states = initialize_nominal(corpus);
        const auto nominal_begin = Clock::now();
        step_all_nominal(nominal_states, config.steps);
        const auto nominal_end = Clock::now();
        nominal_samples.push_back(std::chrono::duration<double, std::micro>(
            nominal_end - nominal_begin).count());
        mix_u64(timing_sink, hash_nominal_centers(nominal_states));

        std::vector<SelectiveRaaRuntime> full_states = initialize_raa(corpus, config.maximum_terms);
        const auto full_begin = Clock::now();
        step_all_raa(full_states, config.steps);
        const auto full_end = Clock::now();
        full_samples.push_back(std::chrono::duration<double, std::micro>(
            full_end - full_begin).count());
        mix_u64(timing_sink, hash_raa_centers(full_states));

        std::vector<SelectiveRaaRuntime> selective_raa_states = initialize_raa(
            corpus, config.maximum_terms);
        std::vector<NominalPairState> selective_nominal_states = initialize_nominal(corpus);
        const auto selective_begin = Clock::now();
        step_selective(selective_raa_states, selective_nominal_states,
            selected_mask, config.steps);
        const auto selective_end = Clock::now();
        const double selective_kernel_us = std::chrono::duration<double, std::micro>(
            selective_end - selective_begin).count();
        selective_kernel_samples.push_back(selective_kernel_us);
        selective_total_samples.push_back(selective_kernel_us + classifier_us);
        mix_u64(timing_sink, hash_selective_state(
            selective_raa_states, selective_nominal_states, selected_mask));
    }
    if (timing_sink == 0U) throw std::logic_error("Selective RAA timing sink is invalid");

    result.nominal = timing_distribution(nominal_samples);
    result.classifier = timing_distribution(classifier_samples);
    result.full_raa = timing_distribution(full_samples);
    result.selective_kernel = timing_distribution(selective_kernel_samples);
    result.selective_total = timing_distribution(selective_total_samples);
    return result;
}

} // namespace neoeng::core
