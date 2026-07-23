#include "neoeng/core/general_lcp_solver.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace neoeng::core {
namespace {

[[nodiscard]] std::pair<std::size_t, std::size_t> canonical_pair(
    const SweptContact& contact) noexcept {
    return contact.first < contact.second
        ? std::pair{contact.first, contact.second}
        : std::pair{contact.second, contact.first};
}

[[nodiscard]] Fixed::rep checked_rep(WideInteger value) {
    constexpr WideInteger minimum = static_cast<WideInteger>(
        std::numeric_limits<Fixed::rep>::min());
    constexpr WideInteger maximum = static_cast<WideInteger>(
        std::numeric_limits<Fixed::rep>::max());
    if (value < minimum || value > maximum) {
        throw std::overflow_error("General projection fixed-point overflow");
    }
    return static_cast<Fixed::rep>(value);
}

[[nodiscard]] std::uint64_t abs_u64(WideInteger value) noexcept {
    const WideInteger magnitude = value < 0 ? -value : value;
    constexpr WideInteger maximum = static_cast<WideInteger>(
        std::numeric_limits<std::uint64_t>::max());
    return magnitude > maximum
        ? std::numeric_limits<std::uint64_t>::max()
        : static_cast<std::uint64_t>(magnitude);
}

[[nodiscard]] WideInteger ceil_div_two(WideInteger value) noexcept {
    WideInteger quotient = value / 2;
    const WideInteger remainder = value % 2;
    if (remainder > 0) ++quotient;
    return quotient;
}

[[nodiscard]] WideInteger rounded_div(WideInteger numerator, WideInteger denominator) {
    if (denominator <= 0) throw std::domain_error("Non-positive projected-CG denominator");
    if (numerator == 0) return 0;
    const bool negative = numerator < 0;
    WideInteger magnitude = negative ? -numerator : numerator;
    const WideInteger quotient = magnitude / denominator;
    const WideInteger remainder = magnitude % denominator;
    const WideInteger rounded = quotient + (remainder * 2 >= denominator ? 1 : 0);
    return negative ? -rounded : rounded;
}

struct OrderedContacts final {
    std::span<const SweptContact> contacts;
    std::span<const std::size_t> order;
};

void copy_values(
    std::span<const Fixed::rep> source,
    std::span<Fixed::rep> destination,
    std::size_t count) {
    std::copy_n(source.begin(), static_cast<std::ptrdiff_t>(count), destination.begin());
}

void compute_values_from_dual(
    std::span<const Fixed::rep> original,
    std::span<Fixed::rep> values,
    std::span<Fixed::rep> body_accumulator,
    std::span<const Fixed::rep> lambda,
    OrderedContacts ordered,
    std::size_t body_count,
    std::size_t contact_count) {
    std::fill_n(body_accumulator.begin(), static_cast<std::ptrdiff_t>(body_count), Fixed::rep{});
    for (std::size_t edge = 0U; edge < contact_count; ++edge) {
        const SweptContact& contact = ordered.contacts[ordered.order[edge]];
        const auto [first, second] = canonical_pair(contact);
        body_accumulator[first] = checked_rep(
            static_cast<WideInteger>(body_accumulator[first]) + lambda[edge]);
        body_accumulator[second] = checked_rep(
            static_cast<WideInteger>(body_accumulator[second]) - lambda[edge]);
    }
    for (std::size_t body = 0U; body < body_count; ++body) {
        values[body] = checked_rep(
            static_cast<WideInteger>(original[body]) - body_accumulator[body]);
    }
}

[[nodiscard]] GeneralProjectionResiduals compute_residuals(
    std::span<const Fixed::rep> original,
    std::span<const Fixed::rep> values,
    std::span<Fixed::rep> stationarity,
    std::span<const Fixed::rep> lambda,
    OrderedContacts ordered,
    std::size_t body_count,
    std::size_t contact_count,
    std::size_t tolerance_raw) {
    std::fill_n(stationarity.begin(), static_cast<std::ptrdiff_t>(body_count), Fixed::rep{});
    for (std::size_t body = 0U; body < body_count; ++body) {
        stationarity[body] = checked_rep(
            static_cast<WideInteger>(values[body]) - original[body]);
    }

    GeneralProjectionResiduals result;
    for (std::size_t edge = 0U; edge < contact_count; ++edge) {
        const SweptContact& contact = ordered.contacts[ordered.order[edge]];
        const auto [first, second] = canonical_pair(contact);
        const Fixed::rep dual = lambda[edge];
        const WideInteger gap = static_cast<WideInteger>(values[first]) - values[second];
        if (gap > 0) result.primal_linf_raw = std::max(result.primal_linf_raw, abs_u64(gap));
        if (dual < 0) result.dual_linf_raw = std::max(
            result.dual_linf_raw, abs_u64(dual));
        const WideInteger projected = dual > 0 || gap > 0 ? gap : 0;
        result.projected_dual_linf_raw = std::max(
            result.projected_dual_linf_raw, abs_u64(projected));
        const WideInteger complementarity = static_cast<WideInteger>(dual) * gap;
        result.complementarity_linf_scaled_raw = std::max(
            result.complementarity_linf_scaled_raw,
            abs_u64(complementarity / static_cast<WideInteger>(Fixed::scale)));
        stationarity[first] = checked_rep(
            static_cast<WideInteger>(stationarity[first]) + dual);
        stationarity[second] = checked_rep(
            static_cast<WideInteger>(stationarity[second]) - dual);
    }
    for (std::size_t body = 0U; body < body_count; ++body) {
        result.stationarity_linf_raw = std::max(
            result.stationarity_linf_raw, abs_u64(stationarity[body]));
    }
    result.certified = result.primal_linf_raw == 0U
        && result.dual_linf_raw == 0U
        && result.stationarity_linf_raw <= tolerance_raw
        && result.complementarity_linf_scaled_raw <= tolerance_raw
        && result.projected_dual_linf_raw <= tolerance_raw;
    return result;
}

struct SolverRunStats final {
    std::uint64_t iterations{};
    std::uint64_t updates{};
    std::uint64_t active_peak{};
    std::uint64_t restarts{};
    std::uint64_t total_order_reductions{};
    std::uint64_t iterative_fallbacks{};
};

[[maybe_unused, nodiscard]] SolverRunStats run_dykstra(
    std::span<const Fixed::rep> original,
    std::span<Fixed::rep> values,
    std::span<Fixed::rep> lambda,
    OrderedContacts ordered,
    std::size_t body_count,
    std::size_t contact_count,
    std::size_t maximum_iterations,
    bool active_set,
    std::span<std::uint8_t> active) {
    (void)body_count;
    SolverRunStats stats;
    if (active_set) {
        std::fill_n(active.begin(), static_cast<std::ptrdiff_t>(contact_count), std::uint8_t{});
        std::uint64_t active_count = 0U;
        for (std::size_t edge = 0U; edge < contact_count; ++edge) {
            const SweptContact& contact = ordered.contacts[ordered.order[edge]];
            const auto [first, second] = canonical_pair(contact);
            if (lambda[edge] > 0 || original[first] > original[second]) {
                active[edge] = 1U;
                ++active_count;
            }
        }
        stats.active_peak = active_count;
    }

    for (std::size_t iteration = 0U; iteration < maximum_iterations; ++iteration) {
        bool changed = false;
        for (std::size_t edge = 0U; edge < contact_count; ++edge) {
            if (active_set && active[edge] == 0U) continue;
            const SweptContact& contact = ordered.contacts[ordered.order[edge]];
            const auto [first, second] = canonical_pair(contact);
            const WideInteger gap = static_cast<WideInteger>(values[first]) - values[second];
            const WideInteger doubled_candidate = static_cast<WideInteger>(lambda[edge]) * 2 + gap;
            const WideInteger candidate_wide = std::max<WideInteger>(0, ceil_div_two(doubled_candidate));
            const Fixed::rep candidate = checked_rep(candidate_wide);
            const Fixed::rep delta = checked_rep(
                static_cast<WideInteger>(candidate) - lambda[edge]);
            if (delta == 0) continue;
            lambda[edge] = candidate;
            values[first] = checked_rep(
                static_cast<WideInteger>(values[first]) - delta);
            values[second] = checked_rep(
                static_cast<WideInteger>(values[second]) + delta);
            changed = true;
            ++stats.updates;
        }
        if (active_set) {
            std::uint64_t active_count = 0U;
            for (std::size_t edge = 0U; edge < contact_count; ++edge) {
                const SweptContact& contact = ordered.contacts[ordered.order[edge]];
                const auto [first, second] = canonical_pair(contact);
                if (values[first] > values[second] && active[edge] == 0U) {
                    active[edge] = 1U;
                    changed = true;
                }
                active_count += active[edge] != 0U ? 1U : 0U;
            }
            stats.active_peak = std::max(stats.active_peak, active_count);
        }
        ++stats.iterations;
        if (!changed) break;
    }
    return stats;
}

void multiply_dual_matrix(
    std::span<const Fixed::rep> direction,
    std::span<Fixed::rep> body_accumulator,
    std::span<Fixed::rep> result,
    OrderedContacts ordered,
    std::size_t body_count,
    std::size_t contact_count) {
    std::fill_n(body_accumulator.begin(), static_cast<std::ptrdiff_t>(body_count), Fixed::rep{});
    for (std::size_t edge = 0U; edge < contact_count; ++edge) {
        const SweptContact& contact = ordered.contacts[ordered.order[edge]];
        const auto [first, second] = canonical_pair(contact);
        body_accumulator[first] = checked_rep(
            static_cast<WideInteger>(body_accumulator[first]) + direction[edge]);
        body_accumulator[second] = checked_rep(
            static_cast<WideInteger>(body_accumulator[second]) - direction[edge]);
    }
    for (std::size_t edge = 0U; edge < contact_count; ++edge) {
        const SweptContact& contact = ordered.contacts[ordered.order[edge]];
        const auto [first, second] = canonical_pair(contact);
        result[edge] = checked_rep(
            static_cast<WideInteger>(body_accumulator[first]) - body_accumulator[second]);
    }
}

[[nodiscard]] WideInteger dot_product(
    std::span<const Fixed::rep> first,
    std::span<const Fixed::rep> second,
    std::size_t count) noexcept {
    WideInteger sum = 0;
    for (std::size_t index = 0U; index < count; ++index) {
        sum += static_cast<WideInteger>(first[index]) * second[index];
    }
    return sum;
}

[[nodiscard]] SolverRunStats run_projected_cg(
    std::span<const Fixed::rep> original,
    std::span<Fixed::rep> values,
    std::span<Fixed::rep> lambda,
    std::span<Fixed::rep> body_accumulator,
    std::span<Fixed::rep> residual,
    std::span<Fixed::rep> direction,
    std::span<Fixed::rep> matrix_direction,
    OrderedContacts ordered,
    std::size_t body_count,
    std::size_t contact_count,
    GeneralProjectionConfig config) {
    SolverRunStats stats;
    compute_values_from_dual(
        original, values, body_accumulator, lambda, ordered, body_count, contact_count);

    auto refresh_projected_residual = [&]() {
        std::uint64_t active_count = 0U;
        for (std::size_t edge = 0U; edge < contact_count; ++edge) {
            const SweptContact& contact = ordered.contacts[ordered.order[edge]];
            const auto [first, second] = canonical_pair(contact);
            const Fixed::rep gap = checked_rep(
                static_cast<WideInteger>(values[first]) - values[second]);
            residual[edge] = (lambda[edge] > 0 || gap > 0) ? gap : 0;
            active_count += residual[edge] != 0 ? 1U : 0U;
        }
        stats.active_peak = std::max(stats.active_peak, active_count);
    };

    refresh_projected_residual();
    std::copy_n(residual.begin(), static_cast<std::ptrdiff_t>(contact_count), direction.begin());
    WideInteger residual_norm = dot_product(residual, residual, contact_count);

    for (std::size_t iteration = 0U;
         iteration < config.maximum_iterations && residual_norm != 0;
         ++iteration) {
        multiply_dual_matrix(
            direction, body_accumulator, matrix_direction,
            ordered, body_count, contact_count);
        const WideInteger denominator = dot_product(
            direction, matrix_direction, contact_count);
        bool projected = false;
        bool changed = false;
        if (denominator > 0) {
            for (std::size_t edge = 0U; edge < contact_count; ++edge) {
                const WideInteger delta_wide = rounded_div(
                    static_cast<WideInteger>(direction[edge]) * residual_norm,
                    denominator);
                if (delta_wide == 0) continue;
                const WideInteger candidate = static_cast<WideInteger>(lambda[edge]) + delta_wide;
                const Fixed::rep next = checked_rep(std::max<WideInteger>(0, candidate));
                projected = projected || candidate < 0;
                changed = changed || next != lambda[edge];
                lambda[edge] = next;
                stats.updates += next != 0 ? 1U : 0U;
            }
        }
        if (!changed) {
            std::size_t best_edge = contact_count;
            Fixed::rep best_gap = 0;
            for (std::size_t edge = 0U; edge < contact_count; ++edge) {
                if (residual[edge] > best_gap) {
                    best_gap = residual[edge];
                    best_edge = edge;
                }
            }
            if (best_edge == contact_count || best_gap <= 0) break;
            const Fixed::rep delta = checked_rep(ceil_div_two(best_gap));
            lambda[best_edge] = checked_rep(
                static_cast<WideInteger>(lambda[best_edge]) + delta);
            ++stats.updates;
            projected = true;
        }

        compute_values_from_dual(
            original, values, body_accumulator, lambda,
            ordered, body_count, contact_count);
        std::copy_n(residual.begin(), static_cast<std::ptrdiff_t>(contact_count), matrix_direction.begin());
        const WideInteger old_norm = residual_norm;
        refresh_projected_residual();
        residual_norm = dot_product(residual, residual, contact_count);
        ++stats.iterations;
        if (residual_norm == 0) break;

        const bool periodic_restart = config.pcg_restart_interval != 0U
            && (iteration + 1U) % config.pcg_restart_interval == 0U;
        if (projected || periodic_restart || old_norm == 0) {
            std::copy_n(residual.begin(), static_cast<std::ptrdiff_t>(contact_count), direction.begin());
            ++stats.restarts;
            continue;
        }
        for (std::size_t edge = 0U; edge < contact_count; ++edge) {
            const WideInteger beta_direction = rounded_div(
                static_cast<WideInteger>(direction[edge]) * residual_norm,
                old_norm);
            const WideInteger candidate = static_cast<WideInteger>(residual[edge]) + beta_direction;
            direction[edge] = checked_rep(candidate);
            if (lambda[edge] == 0 && direction[edge] < 0) direction[edge] = 0;
        }
    }
    return stats;
}


constexpr WideInteger kGuardScale = static_cast<WideInteger>(1) << 16U;
constexpr WideInteger kGuardHalf = kGuardScale / 2;

[[nodiscard]] WideInteger floor_div_positive(WideInteger numerator, WideInteger denominator) {
    if (denominator <= 0) throw std::domain_error("Non-positive guarded denominator");
    WideInteger quotient = numerator / denominator;
    if (numerator < 0 && numerator % denominator != 0) --quotient;
    return quotient;
}

[[nodiscard]] WideInteger guarded_from_raw(Fixed::rep value) noexcept {
    return static_cast<WideInteger>(value) * kGuardScale;
}

[[nodiscard]] Fixed::rep guarded_to_raw(WideInteger value) {
    return checked_rep(floor_div_positive(value + kGuardHalf, kGuardScale));
}

[[nodiscard]] std::uint64_t ceil_abs_guarded_to_raw(WideInteger value) noexcept {
    WideInteger magnitude = value < 0 ? -value : value;
    magnitude = (magnitude + kGuardScale - 1) / kGuardScale;
    constexpr WideInteger maximum = static_cast<WideInteger>(
        std::numeric_limits<std::uint64_t>::max());
    return magnitude > maximum
        ? std::numeric_limits<std::uint64_t>::max()
        : static_cast<std::uint64_t>(magnitude);
}

void compute_values_from_dual_guarded(
    std::span<const WideInteger> original,
    std::span<WideInteger> values,
    std::span<WideInteger> body_accumulator,
    std::span<const WideInteger> lambda,
    OrderedContacts ordered,
    std::size_t body_count,
    std::size_t contact_count) {
    std::fill_n(body_accumulator.begin(), static_cast<std::ptrdiff_t>(body_count), WideInteger{});
    for (std::size_t edge = 0U; edge < contact_count; ++edge) {
        const SweptContact& contact = ordered.contacts[ordered.order[edge]];
        const auto [first, second] = canonical_pair(contact);
        body_accumulator[first] += lambda[edge];
        body_accumulator[second] -= lambda[edge];
    }
    for (std::size_t body = 0U; body < body_count; ++body) {
        values[body] = original[body] - body_accumulator[body];
    }
}

[[nodiscard]] SolverRunStats run_guarded_coordinate(
    std::span<const WideInteger> original,
    std::span<WideInteger> values,
    std::span<WideInteger> lambda,
    std::span<WideInteger> body_accumulator,
    OrderedContacts ordered,
    std::size_t body_count,
    std::size_t contact_count,
    std::size_t maximum_iterations,
    bool active_set,
    std::span<std::uint8_t> active) {
    compute_values_from_dual_guarded(
        original, values, body_accumulator, lambda,
        ordered, body_count, contact_count);
    SolverRunStats stats;
    if (active_set) {
        std::fill_n(active.begin(), static_cast<std::ptrdiff_t>(contact_count), std::uint8_t{});
        std::uint64_t active_count = 0U;
        for (std::size_t edge = 0U; edge < contact_count; ++edge) {
            const SweptContact& contact = ordered.contacts[ordered.order[edge]];
            const auto [first, second] = canonical_pair(contact);
            if (lambda[edge] > 0 || original[first] > original[second]) {
                active[edge] = 1U;
                ++active_count;
            }
        }
        stats.active_peak = active_count;
    }

    for (std::size_t iteration = 0U; iteration < maximum_iterations; ++iteration) {
        bool changed = false;
        for (std::size_t edge = 0U; edge < contact_count; ++edge) {
            if (active_set && active[edge] == 0U) continue;
            const SweptContact& contact = ordered.contacts[ordered.order[edge]];
            const auto [first, second] = canonical_pair(contact);
            const WideInteger gap = values[first] - values[second];
            const WideInteger candidate = std::max<WideInteger>(
                0, ceil_div_two(lambda[edge] * 2 + gap));
            const WideInteger delta = candidate - lambda[edge];
            if (delta == 0) continue;
            lambda[edge] = candidate;
            values[first] -= delta;
            values[second] += delta;
            changed = true;
            ++stats.updates;
        }
        if (active_set) {
            std::uint64_t active_count = 0U;
            for (std::size_t edge = 0U; edge < contact_count; ++edge) {
                const SweptContact& contact = ordered.contacts[ordered.order[edge]];
                const auto [first, second] = canonical_pair(contact);
                if (values[first] > values[second] && active[edge] == 0U) {
                    active[edge] = 1U;
                    changed = true;
                }
                active_count += active[edge] != 0U ? 1U : 0U;
            }
            stats.active_peak = std::max(stats.active_peak, active_count);
        }
        ++stats.iterations;
        if (!changed) break;
    }
    return stats;
}


[[nodiscard]] bool guarded_mean_greater(
    WideInteger left_sum, std::size_t left_count,
    WideInteger right_sum, std::size_t right_count) noexcept {
    return left_sum * static_cast<WideInteger>(right_count)
        > right_sum * static_cast<WideInteger>(left_count);
}

[[nodiscard]] bool try_total_order_reduction(
    std::span<const WideInteger> original,
    std::span<WideInteger> values,
    std::span<WideInteger> lambda,
    std::span<const SweptContact> contacts,
    const ContactIslandWorkspace& workspace,
    GeneralProjectionScratch& scratch,
    std::uint64_t& reductions) {
    const auto islands = workspace.islands();
    const auto body_order = workspace.body_order();
    const auto contact_order = workspace.contact_order();
    std::fill_n(lambda.begin(), static_cast<std::ptrdiff_t>(workspace.contact_count()), WideInteger{});
    std::copy_n(original.begin(), static_cast<std::ptrdiff_t>(workspace.body_count()), values.begin());

    for (const ContactIslandDescriptor& island : islands) {
        if (island.body_count < 2U) return false;
        const std::span<const std::size_t> bodies{
            body_order.data() + island.body_begin, island.body_count};
        const std::size_t required = island.body_count - 1U;
        std::fill_n(
            scratch.chain_edge_.begin(), static_cast<std::ptrdiff_t>(required),
            std::numeric_limits<std::size_t>::max());
        for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
            const std::size_t ordered_index = island.contact_begin + offset;
            const SweptContact& contact = contacts[contact_order[ordered_index]];
            const auto [first, second] = canonical_pair(contact);
            const auto first_it = std::lower_bound(bodies.begin(), bodies.end(), first);
            const auto second_it = std::lower_bound(bodies.begin(), bodies.end(), second);
            if (first_it == bodies.end() || second_it == bodies.end()
                || *first_it != first || *second_it != second) return false;
            const std::size_t first_position = static_cast<std::size_t>(first_it - bodies.begin());
            const std::size_t second_position = static_cast<std::size_t>(second_it - bodies.begin());
            if (second_position == first_position + 1U) {
                scratch.chain_edge_[first_position] = ordered_index;
            }
        }
        if (std::any_of(
                scratch.chain_edge_.begin(),
                scratch.chain_edge_.begin() + static_cast<std::ptrdiff_t>(required),
                [](std::size_t edge) {
                    return edge == std::numeric_limits<std::size_t>::max();
                })) return false;

        std::size_t block_count = 0U;
        for (std::size_t index = 0U; index < island.body_count; ++index) {
            scratch.block_begin_[block_count] = index;
            scratch.block_end_[block_count] = index + 1U;
            scratch.block_sum_[block_count] = original[bodies[index]];
            ++block_count;
            while (block_count >= 2U) {
                const std::size_t left = block_count - 2U;
                const std::size_t right = block_count - 1U;
                const std::size_t left_count = scratch.block_end_[left]
                    - scratch.block_begin_[left];
                const std::size_t right_count = scratch.block_end_[right]
                    - scratch.block_begin_[right];
                if (!guarded_mean_greater(
                        scratch.block_sum_[left], left_count,
                        scratch.block_sum_[right], right_count)) break;
                scratch.block_end_[left] = scratch.block_end_[right];
                scratch.block_sum_[left] += scratch.block_sum_[right];
                --block_count;
            }
        }
        for (std::size_t block = 0U; block < block_count; ++block) {
            const std::size_t begin = scratch.block_begin_[block];
            const std::size_t end = scratch.block_end_[block];
            const std::size_t count = end - begin;
            const WideInteger base = floor_div_positive(
                scratch.block_sum_[block], static_cast<WideInteger>(count));
            const WideInteger remainder_wide = scratch.block_sum_[block]
                - base * static_cast<WideInteger>(count);
            if (remainder_wide < 0 || remainder_wide >= static_cast<WideInteger>(count)) {
                return false;
            }
            const std::size_t remainder = static_cast<std::size_t>(remainder_wide);
            const std::size_t split = end - remainder;
            for (std::size_t index = begin; index < end; ++index) {
                values[bodies[index]] = base + (index >= split ? 1 : 0);
            }
        }

        WideInteger prefix = 0;
        for (std::size_t index = 0U; index + 1U < island.body_count; ++index) {
            prefix += original[bodies[index]] - values[bodies[index]];
            if (prefix < 0) return false;
            lambda[scratch.chain_edge_[index]] = prefix;
        }
        prefix += original[bodies.back()] - values[bodies.back()];
        if (prefix != 0) return false;
        ++reductions;
    }
    return true;
}

[[nodiscard]] SolverRunStats run_guarded_method(
    GeneralProjectionMethod method,
    std::span<const WideInteger> original,
    std::span<WideInteger> values,
    std::span<WideInteger> lambda,
    std::span<WideInteger> body_accumulator,
    OrderedContacts ordered,
    const ContactIslandWorkspace& workspace,
    GeneralProjectionConfig config,
    GeneralProjectionScratch& scratch) {
    if (method == GeneralProjectionMethod::CertifiedAuto) {
        SolverRunStats stats;
        if (try_total_order_reduction(
                original, values, lambda, ordered.contacts, workspace,
                scratch, stats.total_order_reductions)) {
            stats.iterations = 1U;
            stats.updates = workspace.body_count();
            return stats;
        }
        ++stats.iterative_fallbacks;
        std::fill_n(lambda.begin(), static_cast<std::ptrdiff_t>(workspace.contact_count()), WideInteger{});
        stats = run_guarded_coordinate(
            original, values, lambda, body_accumulator, ordered,
            workspace.body_count(), workspace.contact_count(), config.maximum_iterations,
            false, scratch.active_);
        stats.iterative_fallbacks = 1U;
        return stats;
    }
    return run_guarded_coordinate(
        original, values, lambda, body_accumulator, ordered,
        workspace.body_count(), workspace.contact_count(), config.maximum_iterations,
        method == GeneralProjectionMethod::ActiveSetCoordinate,
        scratch.active_);
}

void quantize_guarded_values(
    std::span<const WideInteger> guarded,
    std::span<Fixed::rep> output,
    std::size_t body_count) {
    for (std::size_t body = 0U; body < body_count; ++body) {
        output[body] = guarded_to_raw(guarded[body]);
    }
}

[[nodiscard]] GeneralProjectionResiduals compute_guarded_residuals(
    std::span<const WideInteger> original,
    std::span<const WideInteger> internal_values,
    std::span<const Fixed::rep> output_values,
    std::span<WideInteger> stationarity,
    std::span<const WideInteger> lambda,
    OrderedContacts ordered,
    std::size_t body_count,
    std::size_t contact_count,
    std::size_t tolerance_raw) {
    std::fill_n(stationarity.begin(), static_cast<std::ptrdiff_t>(body_count), WideInteger{});
    for (std::size_t body = 0U; body < body_count; ++body) {
        stationarity[body] = internal_values[body] - original[body];
    }

    GeneralProjectionResiduals result;
    for (std::size_t edge = 0U; edge < contact_count; ++edge) {
        const SweptContact& contact = ordered.contacts[ordered.order[edge]];
        const auto [first, second] = canonical_pair(contact);
        const WideInteger dual = lambda[edge];
        const WideInteger gap = internal_values[first] - internal_values[second];
        const WideInteger output_gap = static_cast<WideInteger>(output_values[first])
            - output_values[second];
        if (output_gap > 0) {
            result.primal_linf_raw = std::max(
                result.primal_linf_raw, abs_u64(output_gap));
        }
        if (dual < 0) {
            result.dual_linf_raw = std::max(
                result.dual_linf_raw, ceil_abs_guarded_to_raw(dual));
        }
        const WideInteger projected = dual > 0 || gap > 0 ? gap : 0;
        result.projected_dual_linf_raw = std::max(
            result.projected_dual_linf_raw,
            ceil_abs_guarded_to_raw(projected));
        const std::uint64_t dual_raw = ceil_abs_guarded_to_raw(dual);
        const std::uint64_t gap_raw = ceil_abs_guarded_to_raw(gap);
        const WideInteger complementarity = static_cast<WideInteger>(dual_raw) * gap_raw;
        result.complementarity_linf_scaled_raw = std::max(
            result.complementarity_linf_scaled_raw,
            abs_u64(complementarity / static_cast<WideInteger>(Fixed::scale)));
        stationarity[first] += dual;
        stationarity[second] -= dual;
    }
    for (std::size_t body = 0U; body < body_count; ++body) {
        result.stationarity_linf_raw = std::max(
            result.stationarity_linf_raw,
            ceil_abs_guarded_to_raw(stationarity[body]));
        const WideInteger quantization = static_cast<WideInteger>(output_values[body])
            * kGuardScale - internal_values[body];
        result.quantization_linf_raw = std::max(
            result.quantization_linf_raw,
            ceil_abs_guarded_to_raw(quantization));
    }
    result.certified = result.primal_linf_raw == 0U
        && result.dual_linf_raw == 0U
        && result.stationarity_linf_raw <= tolerance_raw
        && result.complementarity_linf_scaled_raw <= tolerance_raw
        && result.projected_dual_linf_raw <= tolerance_raw
        && result.quantization_linf_raw <= 1U;
    return result;
}

[[nodiscard]] SolverRunStats run_raw_pcg(
    std::span<const Fixed::rep> original,
    std::span<Fixed::rep> values,
    std::span<Fixed::rep> lambda,
    OrderedContacts ordered,
    std::size_t body_count,
    std::size_t contact_count,
    GeneralProjectionConfig config,
    GeneralProjectionScratch& scratch) {
    return run_projected_cg(
        original, values, lambda,
        scratch.body_accumulator_, scratch.residual_, scratch.direction_,
        scratch.matrix_direction_, ordered, body_count, contact_count, config);
}

[[nodiscard]] bool warm_compatible(
    const GeneralProjectionWarmStart& warm,
    OrderedContacts ordered,
    std::size_t contact_count) noexcept {
    if (!warm.initialized_ || warm.contact_count_ != contact_count) return false;
    for (std::size_t edge = 0U; edge < contact_count; ++edge) {
        const SweptContact& contact = ordered.contacts[ordered.order[edge]];
        const auto [first, second] = canonical_pair(contact);
        if (warm.first_[edge] != first || warm.second_[edge] != second
            || warm.axis_[edge] != contact.axis) return false;
    }
    return true;
}

void refresh_warm_cache(
    GeneralProjectionWarmStart& warm,
    std::span<const WideInteger> lambda,
    OrderedContacts ordered,
    std::size_t contact_count) {
    for (std::size_t edge = 0U; edge < contact_count; ++edge) {
        const SweptContact& contact = ordered.contacts[ordered.order[edge]];
        const auto [first, second] = canonical_pair(contact);
        warm.first_[edge] = first;
        warm.second_[edge] = second;
        warm.axis_[edge] = contact.axis;
        warm.lambda_guarded_[edge] = lambda[edge];
    }
    warm.contact_count_ = contact_count;
    warm.initialized_ = true;
}

} // namespace

const char* to_string(GeneralProjectionMethod method) noexcept {
    switch (method) {
    case GeneralProjectionMethod::DykstraCoordinate: return "dykstra_coordinate";
    case GeneralProjectionMethod::ActiveSetCoordinate: return "active_set_coordinate";
    case GeneralProjectionMethod::ProjectedConjugateGradient: return "projected_cg";
    case GeneralProjectionMethod::CertifiedAuto: return "certified_auto";
    }
    return "unknown";
}

GeneralProjectionScratch::GeneralProjectionScratch(
    std::size_t maximum_bodies,
    std::size_t maximum_contacts)
    : maximum_bodies_(maximum_bodies), maximum_contacts_(maximum_contacts),
      original_(maximum_bodies), cold_values_(maximum_bodies), warm_values_(maximum_bodies),
      body_accumulator_(maximum_bodies), stationarity_(maximum_bodies),
      lambda_(maximum_contacts), cold_lambda_(maximum_contacts), warm_lambda_(maximum_contacts),
      residual_(maximum_contacts), direction_(maximum_contacts),
      matrix_direction_(maximum_contacts), active_(maximum_contacts),
      block_begin_(maximum_bodies), block_end_(maximum_bodies),
      block_sum_(maximum_bodies), chain_edge_(maximum_bodies),
      original_guarded_(maximum_bodies), cold_values_guarded_(maximum_bodies),
      warm_values_guarded_(maximum_bodies), body_accumulator_guarded_(maximum_bodies),
      stationarity_guarded_(maximum_bodies), cold_lambda_guarded_(maximum_contacts),
      warm_lambda_guarded_(maximum_contacts) {
    if (maximum_bodies == 0U || maximum_contacts == 0U) {
        throw std::invalid_argument("General projection scratch requires positive capacities");
    }
}

std::size_t GeneralProjectionScratch::reserved_bytes() const noexcept {
    return original_.capacity() * sizeof(original_[0])
        + cold_values_.capacity() * sizeof(cold_values_[0])
        + warm_values_.capacity() * sizeof(warm_values_[0])
        + body_accumulator_.capacity() * sizeof(body_accumulator_[0])
        + stationarity_.capacity() * sizeof(stationarity_[0])
        + lambda_.capacity() * sizeof(lambda_[0])
        + cold_lambda_.capacity() * sizeof(cold_lambda_[0])
        + warm_lambda_.capacity() * sizeof(warm_lambda_[0])
        + residual_.capacity() * sizeof(residual_[0])
        + direction_.capacity() * sizeof(direction_[0])
        + matrix_direction_.capacity() * sizeof(matrix_direction_[0])
        + active_.capacity() * sizeof(active_[0])
        + block_begin_.capacity() * sizeof(block_begin_[0])
        + block_end_.capacity() * sizeof(block_end_[0])
        + block_sum_.capacity() * sizeof(block_sum_[0])
        + chain_edge_.capacity() * sizeof(chain_edge_[0])
        + original_guarded_.capacity() * sizeof(original_guarded_[0])
        + cold_values_guarded_.capacity() * sizeof(cold_values_guarded_[0])
        + warm_values_guarded_.capacity() * sizeof(warm_values_guarded_[0])
        + body_accumulator_guarded_.capacity() * sizeof(body_accumulator_guarded_[0])
        + stationarity_guarded_.capacity() * sizeof(stationarity_guarded_[0])
        + cold_lambda_guarded_.capacity() * sizeof(cold_lambda_guarded_[0])
        + warm_lambda_guarded_.capacity() * sizeof(warm_lambda_guarded_[0]);
}

GeneralProjectionWarmStart::GeneralProjectionWarmStart(std::size_t maximum_contacts)
    : maximum_contacts_(maximum_contacts), first_(maximum_contacts),
      second_(maximum_contacts), axis_(maximum_contacts), lambda_guarded_(maximum_contacts) {
    if (maximum_contacts == 0U) {
        throw std::invalid_argument("General projection warm cache requires positive capacity");
    }
}

void GeneralProjectionWarmStart::clear() noexcept {
    contact_count_ = 0U;
    initialized_ = false;
    std::fill(lambda_guarded_.begin(), lambda_guarded_.end(), WideInteger{});
}

std::size_t GeneralProjectionWarmStart::reserved_bytes() const noexcept {
    return first_.capacity() * sizeof(first_[0])
        + second_.capacity() * sizeof(second_[0])
        + axis_.capacity() * sizeof(axis_[0])
        + lambda_guarded_.capacity() * sizeof(lambda_guarded_[0]);
}

GeneralProjectionStats project_general_contact_islands(
    std::span<Fixed::rep> values,
    std::span<const SweptContact> contacts,
    const ContactIslandWorkspace& workspace,
    GeneralProjectionMethod method,
    GeneralProjectionConfig config,
    GeneralProjectionScratch& scratch,
    GeneralProjectionWarmStart* warm_start) {
    const std::size_t body_count = workspace.body_count();
    const std::size_t contact_count = workspace.contact_count();
    if (values.size() < body_count) {
        throw std::invalid_argument("General projection values are smaller than classified body count");
    }
    if (contacts.size() != contact_count) {
        throw std::invalid_argument("General projection contacts differ from classification");
    }
    if (body_count > scratch.maximum_bodies_ || contact_count > scratch.maximum_contacts_) {
        throw std::length_error("General projection scratch capacity exceeded");
    }
    if (warm_start != nullptr && contact_count > warm_start->maximum_contacts_) {
        throw std::length_error("General projection warm cache capacity exceeded");
    }
    if (config.maximum_iterations == 0U) {
        throw std::invalid_argument("General projection requires at least one iteration");
    }

    OrderedContacts ordered{contacts, workspace.contact_order()};
    copy_values(values, scratch.original_, body_count);
    copy_values(values, scratch.cold_values_, body_count);

    GeneralProjectionStats result;
    result.method = method;
    result.islands_processed = workspace.islands().size();
    result.contacts_processed = contact_count;

    const bool guarded_coordinate = method != GeneralProjectionMethod::ProjectedConjugateGradient;
    if (guarded_coordinate) {
        for (std::size_t body = 0U; body < body_count; ++body) {
            scratch.original_guarded_[body] = guarded_from_raw(values[body]);
            scratch.cold_values_guarded_[body] = scratch.original_guarded_[body];
        }
        std::fill_n(
            scratch.cold_lambda_guarded_.begin(),
            static_cast<std::ptrdiff_t>(contact_count), WideInteger{});
        const SolverRunStats cold = run_guarded_method(
            method, scratch.original_guarded_, scratch.cold_values_guarded_,
            scratch.cold_lambda_guarded_, scratch.body_accumulator_guarded_,
            ordered, workspace, config, scratch);
        quantize_guarded_values(
            scratch.cold_values_guarded_, scratch.cold_values_, body_count);
        result.iterations = cold.iterations;
        result.coordinate_updates = cold.updates;
        result.active_edges_peak = cold.active_peak;
        result.total_order_reductions = cold.total_order_reductions;
        result.iterative_fallbacks = cold.iterative_fallbacks;
        result.residuals = compute_guarded_residuals(
            scratch.original_guarded_, scratch.cold_values_guarded_, scratch.cold_values_,
            scratch.stationarity_guarded_, scratch.cold_lambda_guarded_,
            ordered, body_count, contact_count, config.certification_tolerance_raw);
    } else {
        std::fill_n(
            scratch.cold_lambda_.begin(),
            static_cast<std::ptrdiff_t>(contact_count), Fixed::rep{});
        const SolverRunStats cold = run_raw_pcg(
            scratch.original_, scratch.cold_values_, scratch.cold_lambda_,
            ordered, body_count, contact_count, config, scratch);
        result.iterations = cold.iterations;
        result.coordinate_updates = cold.updates;
        result.active_edges_peak = cold.active_peak;
        result.pcg_restarts = cold.restarts;
        result.residuals = compute_residuals(
            scratch.original_, scratch.cold_values_, scratch.stationarity_,
            scratch.cold_lambda_, ordered, body_count, contact_count,
            config.certification_tolerance_raw);
        result.residuals.quantization_linf_raw = 0U;
    }

    if (warm_start != nullptr && warm_compatible(*warm_start, ordered, contact_count)) {
        ++result.warm_attempts;
        bool exact = false;
        if (guarded_coordinate) {
            std::copy_n(
                scratch.original_guarded_.begin(), static_cast<std::ptrdiff_t>(body_count),
                scratch.warm_values_guarded_.begin());
            std::copy_n(
                warm_start->lambda_guarded_.begin(), static_cast<std::ptrdiff_t>(contact_count),
                scratch.warm_lambda_guarded_.begin());
            (void)run_guarded_method(
                method, scratch.original_guarded_, scratch.warm_values_guarded_,
                scratch.warm_lambda_guarded_, scratch.body_accumulator_guarded_,
                ordered, workspace, config, scratch);
            quantize_guarded_values(
                scratch.warm_values_guarded_, scratch.warm_values_, body_count);
            exact = std::equal(
                scratch.warm_values_.begin(),
                scratch.warm_values_.begin() + static_cast<std::ptrdiff_t>(body_count),
                scratch.cold_values_.begin());
        } else {
            copy_values(values, scratch.warm_values_, body_count);
            for (std::size_t edge = 0U; edge < contact_count; ++edge) {
                scratch.warm_lambda_[edge] = guarded_to_raw(
                    warm_start->lambda_guarded_[edge]);
            }
            (void)run_raw_pcg(
                scratch.original_, scratch.warm_values_, scratch.warm_lambda_,
                ordered, body_count, contact_count, config, scratch);
            exact = std::equal(
                scratch.warm_values_.begin(),
                scratch.warm_values_.begin() + static_cast<std::ptrdiff_t>(body_count),
                scratch.cold_values_.begin());
        }
        if (exact) ++result.warm_exact_accepts;
        else ++result.warm_rejects;
    }

    copy_values(scratch.cold_values_, values, body_count);
    if (warm_start != nullptr) {
        if (guarded_coordinate) {
            refresh_warm_cache(
                *warm_start, scratch.cold_lambda_guarded_, ordered, contact_count);
        } else {
            for (std::size_t edge = 0U; edge < contact_count; ++edge) {
                scratch.cold_lambda_guarded_[edge] = guarded_from_raw(
                    scratch.cold_lambda_[edge]);
            }
            refresh_warm_cache(
                *warm_start, scratch.cold_lambda_guarded_, ordered, contact_count);
        }
    }
    return result;
}

} // namespace neoeng::core
