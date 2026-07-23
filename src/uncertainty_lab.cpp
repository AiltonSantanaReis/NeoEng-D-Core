#include "neoeng/core/uncertainty_lab.hpp"

#include "neoeng/core/fixed.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

namespace neoeng::core {
namespace {
using Clock = std::chrono::steady_clock;

struct Interval final { double low{}, high{}; };

[[nodiscard]] double down(double value) noexcept {
    return std::nextafter(value, -std::numeric_limits<double>::infinity());
}
[[nodiscard]] double up(double value) noexcept {
    return std::nextafter(value, std::numeric_limits<double>::infinity());
}
[[nodiscard]] Interval make_interval(double center, double radius) {
    if (!(radius >= 0.0)) throw std::invalid_argument("Interval radius is invalid");
    return {down(center - radius), up(center + radius)};
}
[[nodiscard]] Interval add(Interval first, Interval second) noexcept {
    return {down(first.low + second.low), up(first.high + second.high)};
}
[[nodiscard]] Interval negate(Interval value) noexcept { return {-value.high, -value.low}; }
[[nodiscard]] Interval multiply(Interval first, Interval second) noexcept {
    const double a = first.low * second.low;
    const double b = first.low * second.high;
    const double c = first.high * second.low;
    const double d = first.high * second.high;
    return {down(std::min({a, b, c, d})), up(std::max({a, b, c, d}))};
}
[[nodiscard]] Interval scale(Interval value, double scalar) noexcept {
    return scalar >= 0.0
        ? Interval{down(value.low * scalar), up(value.high * scalar)}
        : Interval{down(value.high * scalar), up(value.low * scalar)};
}

struct AffineTerm final { std::uint32_t id{}; double coefficient{}; };
struct AffineForm final {
    double center{};
    std::vector<AffineTerm> terms{};
};

class AffineContext final {
public:
    explicit AffineContext(std::size_t maximum_terms) : maximum_terms_(maximum_terms) {
        if (maximum_terms_ < 2U) throw std::invalid_argument("Affine term capacity is too small");
    }

    [[nodiscard]] AffineForm constant(double value) const { return {.center = value}; }
    [[nodiscard]] AffineForm uncertain(double center, double radius) {
        AffineForm value{.center = center};
        if (radius > 0.0) value.terms.push_back({next_id_++, radius});
        return value;
    }

    [[nodiscard]] AffineForm add_forms(const AffineForm& first, const AffineForm& second) {
        AffineForm result{.center = first.center + second.center};
        result.terms.reserve(first.terms.size() + second.terms.size());
        std::size_t a = 0U, b = 0U;
        while (a < first.terms.size() || b < second.terms.size()) {
            if (b == second.terms.size() || (a < first.terms.size() && first.terms[a].id < second.terms[b].id)) {
                result.terms.push_back(first.terms[a++]);
            } else if (a == first.terms.size() || second.terms[b].id < first.terms[a].id) {
                result.terms.push_back(second.terms[b++]);
            } else {
                const double coefficient = first.terms[a].coefficient + second.terms[b].coefficient;
                if (coefficient != 0.0) result.terms.push_back({first.terms[a].id, coefficient});
                ++a; ++b;
            }
        }
        reduce(result);
        return result;
    }

    [[nodiscard]] AffineForm negate_form(const AffineForm& value) const {
        AffineForm result = value;
        result.center = -result.center;
        for (AffineTerm& term : result.terms) term.coefficient = -term.coefficient;
        return result;
    }

    [[nodiscard]] AffineForm scale_form(const AffineForm& value, double scalar) {
        AffineForm result = value;
        result.center *= scalar;
        for (AffineTerm& term : result.terms) term.coefficient *= scalar;
        reduce(result);
        return result;
    }

    [[nodiscard]] AffineForm multiply_forms(const AffineForm& first, const AffineForm& second) {
        AffineForm linear_first = scale_form(first, second.center);
        AffineForm linear_second = scale_form(second, first.center);
        AffineForm result = add_forms(linear_first, linear_second);
        result.center = first.center * second.center;
        const double radius_first = radius(first);
        const double radius_second = radius(second);
        const double scale_guard = std::abs(result.center) + std::abs(first.center) * radius_second
            + std::abs(second.center) * radius_first + radius_first * radius_second + 1.0;
        const double rounding_guard = std::numeric_limits<double>::epsilon() * 32.0 * scale_guard;
        const double remainder = up(radius_first * radius_second + rounding_guard);
        if (remainder > 0.0) result.terms.push_back({next_id_++, remainder});
        std::sort(result.terms.begin(), result.terms.end(), [](const AffineTerm& a, const AffineTerm& b) {
            return a.id < b.id;
        });
        reduce(result);
        return result;
    }

    [[nodiscard]] Interval bounds(const AffineForm& value) const noexcept {
        const double r = radius(value);
        return {down(value.center - r), up(value.center + r)};
    }

    [[nodiscard]] std::size_t maximum_observed_terms() const noexcept { return maximum_observed_terms_; }

private:
    [[nodiscard]] static double radius(const AffineForm& value) noexcept {
        long double sum = 0.0L;
        for (const AffineTerm& term : value.terms) sum += std::abs(static_cast<long double>(term.coefficient));
        return up(static_cast<double>(sum));
    }

    void reduce(AffineForm& value) {
        value.terms.erase(std::remove_if(value.terms.begin(), value.terms.end(), [](const AffineTerm& term) {
            return term.coefficient == 0.0;
        }), value.terms.end());
        if (value.terms.size() > maximum_terms_) {
            std::stable_sort(value.terms.begin(), value.terms.end(), [](const AffineTerm& a, const AffineTerm& b) {
                const double aa = std::abs(a.coefficient), bb = std::abs(b.coefficient);
                return aa == bb ? a.id < b.id : aa > bb;
            });
            long double discarded = 0.0L;
            for (std::size_t index = maximum_terms_ - 1U; index < value.terms.size(); ++index) {
                discarded += std::abs(static_cast<long double>(value.terms[index].coefficient));
            }
            value.terms.resize(maximum_terms_ - 1U);
            value.terms.push_back({next_id_++, up(static_cast<double>(discarded))});
            std::sort(value.terms.begin(), value.terms.end(), [](const AffineTerm& a, const AffineTerm& b) {
                return a.id < b.id;
            });
        }
        maximum_observed_terms_ = std::max(maximum_observed_terms_, value.terms.size());
    }

    std::size_t maximum_terms_{};
    std::uint32_t next_id_{1U};
    std::size_t maximum_observed_terms_{};
};

struct ReferenceEnvelope final {
    std::vector<double> x_min, x_max, v_min, v_max;
    double final_x_min{}, final_x_max{}, final_v_min{}, final_v_max{};
};

[[nodiscard]] ReferenceEnvelope monte_carlo(const UncertaintyLabConfig& config) {
    if (config.steps == 0U || config.monte_carlo_samples == 0U) {
        throw std::invalid_argument("Uncertainty laboratory configuration is empty");
    }
    ReferenceEnvelope result;
    result.x_min.assign(config.steps, std::numeric_limits<double>::infinity());
    result.x_max.assign(config.steps, -std::numeric_limits<double>::infinity());
    result.v_min.assign(config.steps, std::numeric_limits<double>::infinity());
    result.v_max.assign(config.steps, -std::numeric_limits<double>::infinity());
    std::mt19937_64 rng(config.seed);
    std::uniform_real_distribution<double> unit(-1.0, 1.0);
    constexpr double dt = 1.0 / 120.0;
    for (std::size_t sample = 0U; sample < config.monte_carlo_samples; ++sample) {
        double x = 0.95 + 0.05 * unit(rng);
        double v = 0.20 + 0.03 * unit(rng);
        const double k = 0.90 + 0.10 * unit(rng);
        const double alpha = 0.08 + 0.02 * unit(rng);
        for (std::size_t step = 0U; step < config.steps; ++step) {
            const double acceleration = -k * x - alpha * x * x * x;
            v += dt * acceleration;
            x += dt * v;
            result.x_min[step] = std::min(result.x_min[step], x);
            result.x_max[step] = std::max(result.x_max[step], x);
            result.v_min[step] = std::min(result.v_min[step], v);
            result.v_max[step] = std::max(result.v_max[step], v);
        }
    }
    result.final_x_min = result.x_min.back(); result.final_x_max = result.x_max.back();
    result.final_v_min = result.v_min.back(); result.final_v_max = result.v_max.back();
    return result;
}

[[nodiscard]] UncertaintyMethodMetrics run_interval(
    const UncertaintyLabConfig& config, const ReferenceEnvelope& reference) {
    std::vector<double> timings; timings.reserve(config.timing_repetitions);
    UncertaintyMethodMetrics result{};
    for (std::size_t repetition = 0U; repetition < config.timing_repetitions; ++repetition) {
        Interval x = make_interval(0.95, 0.05), v = make_interval(0.20, 0.03);
        const Interval k = make_interval(0.90, 0.10), alpha = make_interval(0.08, 0.02);
        double width_sum = 0.0;
        std::size_t violations = 0U;
        const auto begin = Clock::now();
        for (std::size_t step = 0U; step < config.steps; ++step) {
            const Interval x2 = multiply(x, x);
            const Interval x3 = multiply(x2, x);
            const Interval acceleration = add(negate(multiply(k, x)), negate(multiply(alpha, x3)));
            v = add(v, scale(acceleration, 1.0 / 120.0));
            x = add(x, scale(v, 1.0 / 120.0));
            width_sum += (x.high - x.low) + (v.high - v.low);
            if (reference.x_min[step] < x.low || reference.x_max[step] > x.high
                || reference.v_min[step] < v.low || reference.v_max[step] > v.high) ++violations;
        }
        const auto end = Clock::now();
        timings.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        if (repetition + 1U == config.timing_repetitions) {
            result.final_x_lower = x.low; result.final_x_upper = x.high;
            result.final_v_lower = v.low; result.final_v_upper = v.high;
            result.final_width = (x.high - x.low) + (v.high - v.low);
            result.average_width = width_sum / static_cast<double>(config.steps);
            result.empirical_violations = violations;
        }
    }
    std::sort(timings.begin(), timings.end());
    result.runtime_us = timings[timings.size() / 2U];
    return result;
}

[[nodiscard]] UncertaintyMethodMetrics run_affine(
    const UncertaintyLabConfig& config, const ReferenceEnvelope& reference, std::size_t maximum_terms) {
    std::vector<double> timings; timings.reserve(config.timing_repetitions);
    UncertaintyMethodMetrics result{};
    for (std::size_t repetition = 0U; repetition < config.timing_repetitions; ++repetition) {
        AffineContext context(maximum_terms);
        AffineForm x = context.uncertain(0.95, 0.05), v = context.uncertain(0.20, 0.03);
        const AffineForm k = context.uncertain(0.90, 0.10), alpha = context.uncertain(0.08, 0.02);
        double width_sum = 0.0;
        std::size_t violations = 0U;
        const auto begin = Clock::now();
        for (std::size_t step = 0U; step < config.steps; ++step) {
            const AffineForm x2 = context.multiply_forms(x, x);
            const AffineForm x3 = context.multiply_forms(x2, x);
            const AffineForm acceleration = context.add_forms(
                context.negate_form(context.multiply_forms(k, x)),
                context.negate_form(context.multiply_forms(alpha, x3)));
            v = context.add_forms(v, context.scale_form(acceleration, 1.0 / 120.0));
            x = context.add_forms(x, context.scale_form(v, 1.0 / 120.0));
            const Interval xb = context.bounds(x), vb = context.bounds(v);
            width_sum += (xb.high - xb.low) + (vb.high - vb.low);
            if (reference.x_min[step] < xb.low || reference.x_max[step] > xb.high
                || reference.v_min[step] < vb.low || reference.v_max[step] > vb.high) ++violations;
        }
        const auto end = Clock::now();
        timings.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        if (repetition + 1U == config.timing_repetitions) {
            const Interval xb = context.bounds(x), vb = context.bounds(v);
            result.final_x_lower = xb.low; result.final_x_upper = xb.high;
            result.final_v_lower = vb.low; result.final_v_upper = vb.high;
            result.final_width = (xb.high - xb.low) + (vb.high - vb.low);
            result.average_width = width_sum / static_cast<double>(config.steps);
            result.maximum_terms = context.maximum_observed_terms();
            result.empirical_violations = violations;
        }
    }
    std::sort(timings.begin(), timings.end());
    result.runtime_us = timings[timings.size() / 2U];
    return result;
}

void center_trajectories(const UncertaintyLabConfig& config, UncertaintyLabResult& result) {
    constexpr double dt = 1.0 / 120.0;
    std::vector<double> double_timings, fixed_timings;
    double_timings.reserve(config.timing_repetitions); fixed_timings.reserve(config.timing_repetitions);
    for (std::size_t repetition = 0U; repetition < config.timing_repetitions; ++repetition) {
        double x = 0.95, v = 0.20;
        const auto begin = Clock::now();
        for (std::size_t step = 0U; step < config.steps; ++step) {
            const double acceleration = -0.90 * x - 0.08 * x * x * x;
            v += dt * acceleration;
            x += dt * v;
        }
        const auto end = Clock::now();
        double_timings.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        if (repetition + 1U == config.timing_repetitions) {
            result.double_final_x = x; result.double_final_v = v;
        }
    }
    for (std::size_t repetition = 0U; repetition < config.timing_repetitions; ++repetition) {
        Fixed fx = Fixed::from_ratio(95, 100), fv = Fixed::from_ratio(20, 100);
        const Fixed fk = Fixed::from_ratio(90, 100), fa = Fixed::from_ratio(8, 100);
        const Fixed fdt = Fixed::from_ratio(1, 120);
        const auto begin = Clock::now();
        for (std::size_t step = 0U; step < config.steps; ++step) {
            const Fixed acceleration = -(fk * fx) - fa * fx * fx * fx;
            fv += fdt * acceleration;
            fx += fdt * fv;
        }
        const auto end = Clock::now();
        fixed_timings.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        if (repetition + 1U == config.timing_repetitions) {
            result.fixed_final_x = fx.to_double(); result.fixed_final_v = fv.to_double();
        }
    }
    std::sort(double_timings.begin(), double_timings.end());
    std::sort(fixed_timings.begin(), fixed_timings.end());
    result.double_runtime_us = double_timings[double_timings.size() / 2U];
    result.fixed_runtime_us = fixed_timings[fixed_timings.size() / 2U];
    result.fixed_x_error = std::abs(result.fixed_final_x - result.double_final_x);
    result.fixed_v_error = std::abs(result.fixed_final_v - result.double_final_v);
}
void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}
void mix_double(std::uint64_t& hash, double value) noexcept {
    mix_u64(hash, std::bit_cast<std::uint64_t>(value));
}

} // namespace

UncertaintyLabResult run_uncertainty_lab(const UncertaintyLabConfig& config) {
    if (config.steps == 0U || config.monte_carlo_samples == 0U || config.timing_repetitions == 0U
        || config.full_affine_terms < 2U || config.reduced_affine_terms < 2U
        || config.reduced_affine_terms > config.full_affine_terms) {
        throw std::invalid_argument("Uncertainty laboratory configuration is invalid");
    }
    const ReferenceEnvelope reference = monte_carlo(config);
    UncertaintyLabResult result{};
    result.interval = run_interval(config, reference);
    result.affine = run_affine(config, reference, config.full_affine_terms);
    result.reduced_affine = run_affine(config, reference, config.reduced_affine_terms);
    result.monte_carlo_final_x_min = reference.final_x_min;
    result.monte_carlo_final_x_max = reference.final_x_max;
    result.monte_carlo_final_v_min = reference.final_v_min;
    result.monte_carlo_final_v_max = reference.final_v_max;
    center_trajectories(config, result);
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    mix_double(hash, result.interval.final_width);
    mix_double(hash, result.affine.final_width);
    mix_double(hash, result.reduced_affine.final_width);
    mix_double(hash, result.fixed_final_x);
    mix_double(hash, result.fixed_final_v);
    mix_u64(hash, result.interval.empirical_violations);
    mix_u64(hash, result.affine.empirical_violations);
    mix_u64(hash, result.reduced_affine.empirical_violations);
    result.hash = hash;
    return result;
}

} // namespace neoeng::core
