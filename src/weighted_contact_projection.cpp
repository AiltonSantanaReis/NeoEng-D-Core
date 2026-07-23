#include "neoeng/core/weighted_contact_projection.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

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
        throw std::overflow_error("Weighted contact projection overflow");
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

[[nodiscard]] WideInteger rounded_div_ties_to_floor(
    WideInteger numerator,
    WideInteger denominator) {
    if (denominator <= 0) throw std::domain_error("Weighted projection denominator is non-positive");
    WideInteger quotient = numerator / denominator;
    WideInteger remainder = numerator % denominator;
    if (remainder < 0) {
        remainder += denominator;
        --quotient;
    }
    if (remainder * 2 > denominator) ++quotient;
    return quotient;
}

[[nodiscard]] bool block_mean_greater(
    WideInteger lhs_sum,
    std::uint64_t lhs_weight,
    WideInteger rhs_sum,
    std::uint64_t rhs_weight) noexcept {
    return lhs_sum * static_cast<WideInteger>(rhs_weight)
        > rhs_sum * static_cast<WideInteger>(lhs_weight);
}

[[nodiscard]] bool contains_total_order_chain(
    const ContactIslandDescriptor& island,
    std::span<const std::size_t> bodies,
    std::span<const SweptContact> contacts,
    std::span<const std::size_t> order,
    std::span<std::size_t> chain_edge) {
    if (bodies.size() < 2U || island.contact_count < bodies.size() - 1U) return false;
    std::fill_n(chain_edge.begin(), static_cast<std::ptrdiff_t>(bodies.size() - 1U),
                std::numeric_limits<std::size_t>::max());
    for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
        const std::size_t ordered = island.contact_begin + offset;
        const auto [first, second] = canonical_pair(contacts[order[ordered]]);
        const auto first_it = std::lower_bound(bodies.begin(), bodies.end(), first);
        const auto second_it = std::lower_bound(bodies.begin(), bodies.end(), second);
        if (first_it == bodies.end() || second_it == bodies.end()
            || *first_it != first || *second_it != second || first_it >= second_it) {
            return false;
        }
        const std::size_t first_local = static_cast<std::size_t>(first_it - bodies.begin());
        const std::size_t second_local = static_cast<std::size_t>(second_it - bodies.begin());
        if (second_local == first_local + 1U) chain_edge[first_local] = ordered;
    }
    return std::all_of(
        chain_edge.begin(), chain_edge.begin() + static_cast<std::ptrdiff_t>(bodies.size() - 1U),
        [](std::size_t edge) { return edge != std::numeric_limits<std::size_t>::max(); });
}

struct IslandSolveStats final {
    std::uint64_t iterations{};
    std::uint64_t updates{};
    std::uint64_t reductions{};
    std::uint64_t star_reductions{};
    std::uint64_t fallbacks{};
    std::uint64_t momentum_error{};
    std::uint64_t quantization_bound{};
    WeightedProjectionResiduals residuals{};
};

void weighted_pav(
    std::span<Fixed::rep> values,
    std::span<const Fixed::rep> original,
    std::span<const std::uint32_t> masses,
    std::span<const std::size_t> bodies,
    WeightedProjectionScratch& scratch,
    IslandSolveStats& stats) {
    std::size_t blocks = 0U;
    for (std::size_t local = 0U; local < bodies.size(); ++local) {
        const std::size_t body = bodies[local];
        scratch.block_begin_[blocks] = local;
        scratch.block_end_[blocks] = local + 1U;
        scratch.block_weight_[blocks] = masses[body];
        scratch.block_weighted_sum_[blocks] = static_cast<WideInteger>(masses[body])
            * static_cast<WideInteger>(original[body]);
        ++blocks;
        while (blocks >= 2U && block_mean_greater(
                   scratch.block_weighted_sum_[blocks - 2U],
                   scratch.block_weight_[blocks - 2U],
                   scratch.block_weighted_sum_[blocks - 1U],
                   scratch.block_weight_[blocks - 1U])) {
            scratch.block_end_[blocks - 2U] = scratch.block_end_[blocks - 1U];
            scratch.block_weighted_sum_[blocks - 2U] += scratch.block_weighted_sum_[blocks - 1U];
            scratch.block_weight_[blocks - 2U] += scratch.block_weight_[blocks - 1U];
            --blocks;
        }
    }

    for (std::size_t block = 0U; block < blocks; ++block) {
        const WideInteger projected = rounded_div_ties_to_floor(
            scratch.block_weighted_sum_[block],
            static_cast<WideInteger>(scratch.block_weight_[block]));
        const Fixed::rep common = checked_rep(projected);
        WideInteger weighted_error = 0;
        for (std::size_t local = scratch.block_begin_[block];
             local < scratch.block_end_[block]; ++local) {
            const std::size_t body = bodies[local];
            values[body] = common;
            weighted_error += static_cast<WideInteger>(masses[body])
                * (static_cast<WideInteger>(common) - original[body]);
        }
        const std::uint64_t error = abs_u64(weighted_error);
        stats.momentum_error = std::max(stats.momentum_error, error);
        stats.quantization_bound = std::max(
            stats.quantization_bound, scratch.block_weight_[block] / 2U);
        stats.residuals.quantization_linf_weighted_raw = std::max(
            stats.residuals.quantization_linf_weighted_raw, error);
    }
    ++stats.reductions;
}

[[nodiscard]] bool weighted_star_project(
    std::span<Fixed::rep> values,
    std::span<const Fixed::rep> original,
    std::span<const std::uint32_t> masses,
    const ContactIslandDescriptor& island,
    std::span<const std::size_t> bodies,
    std::span<const SweptContact> contacts,
    std::span<const std::size_t> order,
    WeightedProjectionScratch& scratch,
    IslandSolveStats& stats) {
    if (island.contact_count + 1U != island.body_count
        || bodies.size() < 3U) {
        return false;
    }
    std::size_t center = std::numeric_limits<std::size_t>::max();
    for (std::size_t candidate : bodies) {
        std::size_t degree = 0U;
        for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
            const auto [first, second] = canonical_pair(
                contacts[order[island.contact_begin + offset]]);
            degree += first == candidate || second == candidate ? 1U : 0U;
        }
        if (degree == island.body_count - 1U) {
            center = candidate;
            break;
        }
    }
    if (center == std::numeric_limits<std::size_t>::max()) return false;

    bool outward = true;
    bool inward = true;
    std::size_t leaf_count = 0U;
    for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
        const auto [first, second] = canonical_pair(
            contacts[order[island.contact_begin + offset]]);
        if (first == center) {
            inward = false;
            scratch.star_leaves_[leaf_count++] = second;
        } else if (second == center) {
            outward = false;
            scratch.star_leaves_[leaf_count++] = first;
        } else {
            return false;
        }
    }
    if ((!outward && !inward) || leaf_count + 1U != bodies.size()) return false;

    std::sort(
        scratch.star_leaves_.begin(),
        scratch.star_leaves_.begin() + static_cast<std::ptrdiff_t>(leaf_count),
        [&](std::size_t lhs, std::size_t rhs) {
            if (original[lhs] != original[rhs]) {
                return outward ? original[lhs] < original[rhs]
                                : original[lhs] > original[rhs];
            }
            return lhs < rhs;
        });

    WideInteger weighted_sum = static_cast<WideInteger>(masses[center]) * original[center];
    std::uint64_t total_weight = masses[center];
    std::size_t pooled = 0U;
    while (pooled < leaf_count) {
        const std::size_t leaf = scratch.star_leaves_[pooled];
        const WideInteger leaf_scaled = static_cast<WideInteger>(original[leaf])
            * static_cast<WideInteger>(total_weight);
        const bool violates = outward ? leaf_scaled < weighted_sum
                                      : leaf_scaled > weighted_sum;
        if (!violates) break;
        weighted_sum += static_cast<WideInteger>(masses[leaf]) * original[leaf];
        total_weight += masses[leaf];
        ++pooled;
    }

    const Fixed::rep threshold = checked_rep(
        rounded_div_ties_to_floor(weighted_sum, static_cast<WideInteger>(total_weight)));
    values[center] = threshold;
    WideInteger weighted_error = static_cast<WideInteger>(masses[center])
        * (static_cast<WideInteger>(threshold) - original[center]);
    for (std::size_t index = 0U; index < pooled; ++index) {
        const std::size_t leaf = scratch.star_leaves_[index];
        values[leaf] = threshold;
        weighted_error += static_cast<WideInteger>(masses[leaf])
            * (static_cast<WideInteger>(threshold) - original[leaf]);
    }
    const std::uint64_t error = abs_u64(weighted_error);
    stats.momentum_error = std::max(stats.momentum_error, error);
    stats.quantization_bound = std::max(stats.quantization_bound, total_weight / 2U);
    stats.residuals.quantization_linf_weighted_raw = std::max(
        stats.residuals.quantization_linf_weighted_raw, error);
    ++stats.star_reductions;
    return true;
}

[[nodiscard]] WeightedProjectionResiduals compute_star_residuals(
    std::span<const Fixed::rep> original,
    std::span<const Fixed::rep> values,
    std::span<const std::uint32_t> masses,
    const ContactIslandDescriptor& island,
    std::span<const std::size_t> bodies,
    std::span<const SweptContact> contacts,
    std::span<const std::size_t> order,
    std::uint64_t tolerance,
    std::uint64_t quantization_error,
    std::uint64_t quantization_bound) {
    WeightedProjectionResiduals result;
    result.quantization_linf_weighted_raw = quantization_error;
    std::size_t center = std::numeric_limits<std::size_t>::max();
    for (std::size_t candidate : bodies) {
        std::size_t degree = 0U;
        for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
            const auto [first, second] = canonical_pair(
                contacts[order[island.contact_begin + offset]]);
            degree += first == candidate || second == candidate ? 1U : 0U;
        }
        if (degree == island.body_count - 1U) {
            center = candidate;
            break;
        }
    }
    if (center == std::numeric_limits<std::size_t>::max()) return result;

    WideInteger center_dual_sum = 0;
    bool outward = true;
    for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
        const auto [first, second] = canonical_pair(
            contacts[order[island.contact_begin + offset]]);
        const WideInteger gap = static_cast<WideInteger>(values[first]) - values[second];
        if (gap > 0) result.primal_linf_raw = std::max(result.primal_linf_raw, abs_u64(gap));
        const std::size_t leaf = first == center ? second : first;
        outward = first == center;
        const WideInteger leaf_delta = static_cast<WideInteger>(masses[leaf])
            * (static_cast<WideInteger>(values[leaf]) - original[leaf]);
        const WideInteger lambda = outward ? leaf_delta : -leaf_delta;
        if (lambda < 0) result.dual_linf_weighted_raw = std::max(
            result.dual_linf_weighted_raw, abs_u64(lambda));
        center_dual_sum += lambda;
        result.stationarity_linf_weighted_raw = std::max(
            result.stationarity_linf_weighted_raw,
            abs_u64(outward ? leaf_delta - lambda : leaf_delta + lambda));
        result.complementarity_linf_scaled_raw = std::max(
            result.complementarity_linf_scaled_raw,
            abs_u64(lambda * gap / static_cast<WideInteger>(Fixed::scale)));
    }
    const WideInteger center_delta = static_cast<WideInteger>(masses[center])
        * (static_cast<WideInteger>(values[center]) - original[center]);
    const WideInteger center_stationarity = outward
        ? center_delta + center_dual_sum
        : center_delta - center_dual_sum;
    result.stationarity_linf_weighted_raw = std::max(
        result.stationarity_linf_weighted_raw, abs_u64(center_stationarity));
    const std::uint64_t allowed = std::max(tolerance, quantization_bound);
    result.certified = result.primal_linf_raw == 0U
        && result.dual_linf_weighted_raw <= allowed
        && result.stationarity_linf_weighted_raw <= allowed
        && result.complementarity_linf_scaled_raw <= allowed
        && result.quantization_linf_weighted_raw <= allowed;
    return result;
}

void quantized_dykstra(
    std::span<Fixed::rep> values,
    std::span<const std::uint32_t> masses,
    const ContactIslandDescriptor& island,
    std::span<const SweptContact> contacts,
    std::span<const std::size_t> order,
    WeightedProjectionConfig config,
    WeightedProjectionScratch& scratch,
    IslandSolveStats& stats) {
    std::fill_n(
        scratch.correction_first_.begin() + static_cast<std::ptrdiff_t>(island.contact_begin),
        static_cast<std::ptrdiff_t>(island.contact_count), Fixed::rep{});
    std::fill_n(
        scratch.correction_second_.begin() + static_cast<std::ptrdiff_t>(island.contact_begin),
        static_cast<std::ptrdiff_t>(island.contact_count), Fixed::rep{});

    for (std::size_t iteration = 0U; iteration < config.maximum_iterations; ++iteration) {
        bool changed = false;
        for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
            const std::size_t ordered = island.contact_begin + offset;
            const auto [first, second] = canonical_pair(contacts[order[ordered]]);
            const WideInteger zi = static_cast<WideInteger>(values[first])
                + scratch.correction_first_[ordered];
            const WideInteger zj = static_cast<WideInteger>(values[second])
                + scratch.correction_second_[ordered];
            Fixed::rep projected_first = checked_rep(zi);
            Fixed::rep projected_second = checked_rep(zj);
            if (zi > zj) {
                const WideInteger numerator = static_cast<WideInteger>(masses[first]) * zi
                    + static_cast<WideInteger>(masses[second]) * zj;
                const WideInteger denominator = static_cast<WideInteger>(masses[first])
                    + static_cast<WideInteger>(masses[second]);
                const Fixed::rep common = checked_rep(
                    rounded_div_ties_to_floor(numerator, denominator));
                projected_first = common;
                projected_second = common;
            }
            const Fixed::rep next_first_correction = checked_rep(zi - projected_first);
            const Fixed::rep next_second_correction = checked_rep(zj - projected_second);
            changed = changed || projected_first != values[first]
                || projected_second != values[second]
                || next_first_correction != scratch.correction_first_[ordered]
                || next_second_correction != scratch.correction_second_[ordered];
            values[first] = projected_first;
            values[second] = projected_second;
            scratch.correction_first_[ordered] = next_first_correction;
            scratch.correction_second_[ordered] = next_second_correction;
            ++stats.updates;
        }
        ++stats.iterations;
        if (!changed) break;
    }
    ++stats.fallbacks;
}

[[nodiscard]] WeightedProjectionResiduals compute_fallback_residuals(
    std::span<const Fixed::rep> values,
    const ContactIslandDescriptor& island,
    std::span<const SweptContact> contacts,
    std::span<const std::size_t> order) {
    WeightedProjectionResiduals result;
    for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
        const auto [first, second] = canonical_pair(
            contacts[order[island.contact_begin + offset]]);
        const WideInteger gap = static_cast<WideInteger>(values[first]) - values[second];
        if (gap > 0) result.primal_linf_raw = std::max(result.primal_linf_raw, abs_u64(gap));
    }
    result.certified = false;
    return result;
}

[[nodiscard]] WeightedProjectionResiduals compute_chain_residuals(
    std::span<const Fixed::rep> original,
    std::span<const Fixed::rep> values,
    std::span<const std::uint32_t> masses,
    std::span<const std::size_t> bodies,
    std::uint64_t tolerance,
    std::uint64_t quantization_error,
    std::uint64_t quantization_bound) {
    WeightedProjectionResiduals result;
    result.quantization_linf_weighted_raw = quantization_error;
    if (bodies.size() < 2U) {
        result.certified = quantization_error <= std::max(tolerance, quantization_bound);
        return result;
    }

    WideInteger prefix = 0;
    WideInteger previous_lambda = 0;
    for (std::size_t local = 0U; local < bodies.size(); ++local) {
        const std::size_t body = bodies[local];
        const WideInteger weighted_delta = static_cast<WideInteger>(masses[body])
            * (static_cast<WideInteger>(values[body]) - original[body]);
        prefix += weighted_delta;
        const WideInteger lambda = local + 1U < bodies.size() ? -prefix : 0;
        if (local + 1U < bodies.size()) {
            if (lambda < 0) {
                result.dual_linf_weighted_raw = std::max(
                    result.dual_linf_weighted_raw, abs_u64(lambda));
            }
            const WideInteger gap = static_cast<WideInteger>(values[body])
                - values[bodies[local + 1U]];
            if (gap > 0) {
                result.primal_linf_raw = std::max(result.primal_linf_raw, abs_u64(gap));
            }
            const WideInteger complementarity = lambda * gap;
            result.complementarity_linf_scaled_raw = std::max(
                result.complementarity_linf_scaled_raw,
                abs_u64(complementarity / static_cast<WideInteger>(Fixed::scale)));
        }
        const WideInteger stationarity = weighted_delta - previous_lambda + lambda;
        result.stationarity_linf_weighted_raw = std::max(
            result.stationarity_linf_weighted_raw, abs_u64(stationarity));
        previous_lambda = lambda;
    }
    const std::uint64_t allowed = std::max(tolerance, quantization_bound);
    result.certified = result.primal_linf_raw == 0U
        && result.dual_linf_weighted_raw <= allowed
        && result.stationarity_linf_weighted_raw <= allowed
        && result.complementarity_linf_scaled_raw <= allowed
        && result.quantization_linf_weighted_raw <= allowed;
    return result;
}

void merge_residuals(
    WeightedProjectionResiduals& target,
    const WeightedProjectionResiduals& source) noexcept {
    target.primal_linf_raw = std::max(target.primal_linf_raw, source.primal_linf_raw);
    target.dual_linf_weighted_raw = std::max(
        target.dual_linf_weighted_raw, source.dual_linf_weighted_raw);
    target.stationarity_linf_weighted_raw = std::max(
        target.stationarity_linf_weighted_raw, source.stationarity_linf_weighted_raw);
    target.complementarity_linf_scaled_raw = std::max(
        target.complementarity_linf_scaled_raw, source.complementarity_linf_scaled_raw);
    target.quantization_linf_weighted_raw = std::max(
        target.quantization_linf_weighted_raw, source.quantization_linf_weighted_raw);
    target.certified = target.certified && source.certified;
}

void solve_axis(
    ContactAxis axis,
    const ComponentWorldState& current,
    std::span<const std::uint32_t> masses,
    std::span<const SweptContact> contacts,
    WeightedProjectionMethod method,
    WeightedProjectionConfig config,
    WeightedProjectionScratch& scratch,
    WeightedProjectionStats& stats) {
    scratch.axis_contacts_.clear();
    for (const SweptContact& contact : contacts) {
        if (contact.axis == axis) scratch.axis_contacts_.push_back(contact);
    }
    if (scratch.axis_contacts_.empty()) return;
    scratch.axis_workspace_.classify(current.body_count(), scratch.axis_contacts_);
    for (std::size_t body = 0U; body < current.body_count(); ++body) {
        const Fixed::rep value = axis == ContactAxis::X
            ? current.velocity_x_at(body).raw()
            : current.velocity_y_at(body).raw();
        scratch.original_[body] = value;
        scratch.values_[body] = value;
    }

    const auto islands = scratch.axis_workspace_.islands();
    const auto body_order = scratch.axis_workspace_.body_order();
    const auto contact_order = scratch.axis_workspace_.contact_order();
    for (const ContactIslandDescriptor& island : islands) {
        ++stats.islands_processed;
        stats.contacts_processed += island.contact_count;
        const std::span<const std::size_t> bodies{
            body_order.data() + island.body_begin, island.body_count};
        IslandSolveStats local;
        const bool reducible = method == WeightedProjectionMethod::CertifiedAuto
            && contains_total_order_chain(
                island, bodies, scratch.axis_contacts_, contact_order,
                {scratch.chain_edge_.data(), island.body_count > 0U ? island.body_count - 1U : 0U});
        bool star_reduced = false;
        if (reducible) {
            weighted_pav(
                scratch.values_, scratch.original_, masses, bodies, scratch, local);
        } else if (method == WeightedProjectionMethod::CertifiedAuto) {
            star_reduced = weighted_star_project(
                scratch.values_, scratch.original_, masses, island, bodies,
                scratch.axis_contacts_, contact_order, scratch, local);
            if (!star_reduced) {
                quantized_dykstra(
                    scratch.values_, masses, island, scratch.axis_contacts_, contact_order,
                    config, scratch, local);
            }
        } else {
            quantized_dykstra(
                scratch.values_, masses, island, scratch.axis_contacts_, contact_order,
                config, scratch, local);
        }
        local.residuals = reducible
            ? compute_chain_residuals(
                scratch.original_, scratch.values_, masses, bodies,
                config.certification_tolerance_raw,
                local.momentum_error, local.quantization_bound)
            : star_reduced
                ? compute_star_residuals(
                    scratch.original_, scratch.values_, masses, island, bodies,
                    scratch.axis_contacts_, contact_order,
                    config.certification_tolerance_raw,
                    local.momentum_error, local.quantization_bound)
                : compute_fallback_residuals(
                    scratch.values_, island, scratch.axis_contacts_, contact_order);
        stats.iterations += local.iterations;
        stats.coordinate_updates += local.updates;
        stats.total_order_reductions += local.reductions;
        stats.star_reductions += local.star_reductions;
        stats.iterative_fallbacks += local.fallbacks;
        stats.weighted_momentum_error_raw = std::max(
            stats.weighted_momentum_error_raw, local.momentum_error);
        merge_residuals(stats.residuals, local.residuals);
    }
    ++stats.axes_processed;

    const std::uint8_t target_mask = component_mask(
        axis == ContactAxis::X ? DirtyComponent::VelocityX : DirtyComponent::VelocityY);
    for (std::size_t body = 0U; body < current.body_count(); ++body) {
        if (scratch.values_[body] == scratch.original_[body]) continue;
        ComponentPatch& patch = scratch.patches_[body];
        if (patch.mask == 0U) {
            patch = ComponentPatch{
                .index = body,
                .position_x = current.position_x_at(body),
                .position_y = current.position_y_at(body),
                .velocity_x = current.velocity_x_at(body),
                .velocity_y = current.velocity_y_at(body),
                .mask = 0U,
            };
        }
        if (axis == ContactAxis::X) patch.velocity_x = Fixed::from_raw(scratch.values_[body]);
        else patch.velocity_y = Fixed::from_raw(scratch.values_[body]);
        patch.mask |= target_mask;
    }
}

} // namespace

const char* to_string(WeightedProjectionMethod method) noexcept {
    switch (method) {
    case WeightedProjectionMethod::QuantizedDykstra: return "quantized_dykstra";
    case WeightedProjectionMethod::CertifiedAuto: return "certified_auto";
    }
    return "unknown";
}

WeightedProjectionScratch::WeightedProjectionScratch(
    std::size_t maximum_bodies,
    std::size_t maximum_contacts)
    : maximum_bodies_(maximum_bodies), maximum_contacts_(maximum_contacts),
      axis_workspace_(maximum_bodies, maximum_contacts),
      original_(maximum_bodies), values_(maximum_bodies), other_axis_(maximum_bodies),
      correction_first_(maximum_contacts), correction_second_(maximum_contacts),
      block_begin_(maximum_bodies), block_end_(maximum_bodies),
      block_weighted_sum_(maximum_bodies), block_weight_(maximum_bodies),
      chain_edge_(maximum_bodies), star_leaves_(maximum_bodies),
      patches_(maximum_bodies) {
    if (maximum_bodies == 0U || maximum_contacts == 0U) {
        throw std::invalid_argument("Weighted projection scratch requires nonzero capacities");
    }
    axis_contacts_.reserve(maximum_contacts);
}

std::size_t WeightedProjectionScratch::reserved_bytes() const noexcept {
    return axis_workspace_.reserved_bytes()
        + axis_contacts_.capacity() * sizeof(axis_contacts_[0])
        + original_.capacity() * sizeof(original_[0])
        + values_.capacity() * sizeof(values_[0])
        + other_axis_.capacity() * sizeof(other_axis_[0])
        + correction_first_.capacity() * sizeof(correction_first_[0])
        + correction_second_.capacity() * sizeof(correction_second_[0])
        + block_begin_.capacity() * sizeof(block_begin_[0])
        + block_end_.capacity() * sizeof(block_end_[0])
        + block_weighted_sum_.capacity() * sizeof(block_weighted_sum_[0])
        + block_weight_.capacity() * sizeof(block_weight_[0])
        + chain_edge_.capacity() * sizeof(chain_edge_[0])
        + star_leaves_.capacity() * sizeof(star_leaves_[0])
        + patches_.capacity() * sizeof(patches_[0]);
}

WeightedVelocityProjectionResult project_weighted_contact_velocities_2d(
    const ComponentWorldState& current,
    std::span<const std::uint32_t> masses,
    std::span<const SweptContact> contacts,
    WeightedProjectionMethod method,
    WeightedProjectionConfig config,
    WeightedProjectionScratch& scratch) {
    if (current.empty()) throw std::invalid_argument("Weighted projection requires initialized state");
    if (masses.size() != current.body_count()) {
        throw std::invalid_argument("Weighted projection mass count differs from body count");
    }
    if (current.body_count() > scratch.maximum_bodies_
        || contacts.size() > scratch.maximum_contacts_) {
        throw std::length_error("Weighted projection scratch capacity exceeded");
    }
    if (config.maximum_iterations == 0U) {
        throw std::invalid_argument("Weighted projection requires at least one iteration");
    }
    for (std::uint32_t mass : masses) {
        if (mass == 0U) throw std::invalid_argument("Weighted projection masses must be positive");
    }

    std::fill(scratch.patches_.begin(), scratch.patches_.end(), ComponentPatch{});
    WeightedProjectionStats stats{.method = method};
    stats.residuals.certified = true;
    solve_axis(ContactAxis::X, current, masses, contacts, method, config, scratch, stats);
    solve_axis(ContactAxis::Y, current, masses, contacts, method, config, scratch, stats);

    std::size_t patch_count = 0U;
    for (std::size_t body = 0U; body < current.body_count(); ++body) {
        if (scratch.patches_[body].mask == 0U) continue;
        if (patch_count != body) scratch.patches_[patch_count] = scratch.patches_[body];
        ++patch_count;
    }
    stats.changed_bodies = patch_count;
    ComponentAllocationStats allocation;
    ComponentWorldState projected = patch_count == 0U
        ? current
        : apply_component_patches(
            current, {scratch.patches_.data(), patch_count}, &allocation);
    return WeightedVelocityProjectionResult{
        .state = std::move(projected),
        .stats = stats,
        .allocation = allocation,
    };
}

} // namespace neoeng::core
