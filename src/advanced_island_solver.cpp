#include "neoeng/core/advanced_island_solver.hpp"

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
        throw std::overflow_error("Advanced island solver fixed-point overflow");
    }
    return static_cast<Fixed::rep>(value);
}

[[nodiscard]] WideInteger floor_div(WideInteger numerator, std::size_t denominator) {
    if (denominator == 0U) throw std::domain_error("Zero isotonic block weight");
    const WideInteger divisor = static_cast<WideInteger>(denominator);
    WideInteger quotient = numerator / divisor;
    if (numerator < 0 && numerator % divisor != 0) --quotient;
    return quotient;
}

[[nodiscard]] bool mean_greater(
    WideInteger lhs_sum, std::size_t lhs_count,
    WideInteger rhs_sum, std::size_t rhs_count) noexcept {
    return lhs_sum * static_cast<WideInteger>(rhs_count)
        > rhs_sum * static_cast<WideInteger>(lhs_count);
}

[[nodiscard]] std::uint64_t count_violations(
    std::span<const Fixed::rep> values,
    std::span<const SweptContact> contacts) noexcept {
    std::uint64_t result = 0U;
    for (const SweptContact& contact : contacts) {
        const auto [first, second] = canonical_pair(contact);
        result += values[first] > values[second] ? 1U : 0U;
    }
    return result;
}

[[nodiscard]] Fixed::rep project_pair(
    Fixed::rep& first,
    Fixed::rep& second) {
    if (first <= second) return 0;
    const WideInteger old_first = first;
    const WideInteger sum = static_cast<WideInteger>(first)
        + static_cast<WideInteger>(second);
    const WideInteger lower = floor_div(sum, 2U);
    first = checked_rep(lower);
    second = checked_rep(sum - lower);
    return checked_rep(old_first - lower);
}

[[nodiscard]] bool canonical_consecutive_chain(
    const ContactIslandDescriptor& island,
    std::span<const std::size_t> bodies,
    std::span<const SweptContact> contacts,
    std::span<const std::size_t> contact_order,
    bool allow_closing_edge) {
    if (bodies.size() < 2U) return false;
    const std::size_t expected = bodies.size() - 1U + (allow_closing_edge ? 1U : 0U);
    if (island.contact_count != expected) return false;
    std::size_t adjacent_edges = 0U;
    bool closing = false;
    for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
        const SweptContact& contact = contacts[contact_order[island.contact_begin + offset]];
        const auto [first, second] = canonical_pair(contact);
        const auto first_it = std::lower_bound(bodies.begin(), bodies.end(), first);
        const auto second_it = std::lower_bound(bodies.begin(), bodies.end(), second);
        if (first_it == bodies.end() || *first_it != first
            || second_it == bodies.end() || *second_it != second) return false;
        const std::size_t first_index = static_cast<std::size_t>(first_it - bodies.begin());
        const std::size_t second_index = static_cast<std::size_t>(second_it - bodies.begin());
        if (second_index == first_index + 1U) {
            ++adjacent_edges;
        } else if (allow_closing_edge && first_index == 0U
                   && second_index + 1U == bodies.size()) {
            if (closing) return false;
            closing = true;
        } else {
            return false;
        }
    }
    return adjacent_edges == bodies.size() - 1U
        && (!allow_closing_edge || closing);
}

[[nodiscard]] std::size_t isotonic_project(
    std::span<Fixed::rep> values,
    std::span<const std::size_t> bodies,
    IslandSolverScratch& scratch,
    WideInteger& rounding_error) {
    const std::size_t count = bodies.size();
    for (std::size_t index = 0U; index < count; ++index) {
        scratch.ordered_values_[index] = values[bodies[index]];
    }
    std::size_t block_count = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
        scratch.block_begin_[block_count] = index;
        scratch.block_end_[block_count] = index + 1U;
        scratch.block_sum_[block_count] = scratch.ordered_values_[index];
        ++block_count;
        while (block_count >= 2U) {
            const std::size_t left = block_count - 2U;
            const std::size_t right = block_count - 1U;
            const std::size_t left_count = scratch.block_end_[left]
                - scratch.block_begin_[left];
            const std::size_t right_count = scratch.block_end_[right]
                - scratch.block_begin_[right];
            if (!mean_greater(
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
        const Fixed::rep projected = checked_rep(
            floor_div(scratch.block_sum_[block], end - begin));
        for (std::size_t index = begin; index < end; ++index) {
            scratch.projected_values_[index] = projected;
        }
    }
    for (std::size_t index = 0U; index < count; ++index) {
        rounding_error += static_cast<WideInteger>(scratch.projected_values_[index])
            - scratch.ordered_values_[index];
        values[bodies[index]] = scratch.projected_values_[index];
    }
    return block_count;
}

[[nodiscard]] bool star_project(
    std::span<Fixed::rep> values,
    const ContactIslandDescriptor& island,
    std::span<const std::size_t> bodies,
    std::span<const SweptContact> contacts,
    std::span<const std::size_t> contact_order,
    IslandSolverScratch& scratch,
    WideInteger& rounding_error) {
    if (bodies.size() < 3U || island.contact_count + 1U != bodies.size()) return false;
    for (const std::size_t body : bodies) scratch.degree_[body] = 0U;
    for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
        const SweptContact& contact = contacts[contact_order[island.contact_begin + offset]];
        ++scratch.degree_[contact.first];
        ++scratch.degree_[contact.second];
    }
    std::size_t center = bodies.front();
    for (const std::size_t body : bodies) {
        if (scratch.degree_[body] > scratch.degree_[center]) center = body;
    }
    if (scratch.degree_[center] != bodies.size() - 1U) return false;
    std::size_t leaf_count = 0U;
    for (const std::size_t body : bodies) {
        if (body == center) continue;
        if (scratch.degree_[body] != 1U || center > body) return false;
        scratch.star_leaves_[leaf_count++] = IslandSolverScratch::StarLeaf{
            .value = values[body], .body = body,
        };
    }
    for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
        const SweptContact& contact = contacts[contact_order[island.contact_begin + offset]];
        const auto [first, second] = canonical_pair(contact);
        if (first != center || second == center) return false;
    }
    std::sort(
        scratch.star_leaves_.begin(),
        scratch.star_leaves_.begin() + static_cast<std::ptrdiff_t>(leaf_count));
    WideInteger sum = values[center];
    std::size_t pooled_count = 1U;
    for (std::size_t leaf = 0U; leaf < leaf_count; ++leaf) {
        const Fixed::rep mean = checked_rep(floor_div(sum, pooled_count));
        if (scratch.star_leaves_[leaf].value >= mean) break;
        sum += scratch.star_leaves_[leaf].value;
        ++pooled_count;
    }
    const Fixed::rep threshold = checked_rep(floor_div(sum, pooled_count));
    rounding_error += static_cast<WideInteger>(threshold) - values[center];
    values[center] = threshold;
    for (std::size_t leaf = 0U; leaf < leaf_count; ++leaf) {
        Fixed::rep& value = values[scratch.star_leaves_[leaf].body];
        if (value < threshold) {
            rounding_error += static_cast<WideInteger>(threshold) - value;
            value = threshold;
        }
    }
    return true;
}

} // namespace

IslandSolverScratch::IslandSolverScratch(
    std::size_t maximum_bodies,
    std::size_t maximum_contacts)
    : maximum_bodies_(maximum_bodies), maximum_contacts_(maximum_contacts),
      ordered_values_(maximum_bodies), projected_values_(maximum_bodies),
      block_begin_(maximum_bodies), block_end_(maximum_bodies),
      block_sum_(maximum_bodies), degree_(maximum_bodies),
      star_leaves_(maximum_bodies) {
    if (maximum_bodies == 0U || maximum_contacts == 0U) {
        throw std::invalid_argument("Island solver scratch requires nonzero capacities");
    }
}

std::size_t IslandSolverScratch::reserved_bytes() const noexcept {
    return ordered_values_.capacity() * sizeof(ordered_values_[0])
        + projected_values_.capacity() * sizeof(projected_values_[0])
        + block_begin_.capacity() * sizeof(block_begin_[0])
        + block_end_.capacity() * sizeof(block_end_[0])
        + block_sum_.capacity() * sizeof(block_sum_[0])
        + degree_.capacity() * sizeof(degree_[0])
        + star_leaves_.capacity() * sizeof(star_leaves_[0]);
}

GeneralImpulseWarmStart::GeneralImpulseWarmStart(std::size_t maximum_contacts)
    : maximum_contacts_(maximum_contacts), first_(maximum_contacts),
      second_(maximum_contacts), axis_(maximum_contacts),
      impulses_(maximum_contacts), next_impulses_(maximum_contacts) {
    if (maximum_contacts == 0U) {
        throw std::invalid_argument("Warm-start cache requires contact capacity");
    }
}

void GeneralImpulseWarmStart::clear() noexcept {
    contact_count_ = 0U;
    initialized_ = false;
    std::fill(impulses_.begin(), impulses_.end(), Fixed::rep{});
    std::fill(next_impulses_.begin(), next_impulses_.end(), Fixed::rep{});
}

std::size_t GeneralImpulseWarmStart::reserved_bytes() const noexcept {
    return first_.capacity() * sizeof(first_[0])
        + second_.capacity() * sizeof(second_[0])
        + axis_.capacity() * sizeof(axis_[0])
        + impulses_.capacity() * sizeof(impulses_[0])
        + next_impulses_.capacity() * sizeof(next_impulses_[0]);
}

SpecializedIslandProjectionStats project_contact_islands_specialized(
    std::span<Fixed::rep> values,
    std::span<const SweptContact> contacts,
    const ContactIslandWorkspace& workspace,
    std::size_t fallback_iterations,
    IslandSolverScratch& scratch,
    GeneralImpulseWarmStart* warm_start) {
    if (values.size() < workspace.body_count()) {
        throw std::invalid_argument("Specialized projection value array is too small");
    }
    if (contacts.size() != workspace.contact_count()) {
        throw std::invalid_argument("Specialized projection contacts differ from classification");
    }
    if (fallback_iterations == 0U) {
        throw std::invalid_argument("Fallback projection requires at least one iteration");
    }
    if (workspace.body_count() > scratch.maximum_bodies_
        || contacts.size() > scratch.maximum_contacts_) {
        throw std::length_error("Specialized island scratch capacity exceeded");
    }
    if (warm_start != nullptr && contacts.size() > warm_start->maximum_contacts_) {
        throw std::length_error("Warm-start contact capacity exceeded");
    }

    SpecializedIslandProjectionStats stats;
    stats.violations_before = count_violations(values, contacts);
    const auto islands = workspace.islands();
    const auto body_order = workspace.body_order();
    const auto contact_order = workspace.contact_order();
    const auto colors = workspace.contact_colors();
    WideInteger rounding_error = 0;

    bool warm_compatible = false;
    if (warm_start != nullptr) {
        warm_compatible = warm_start->initialized_
            && warm_start->contact_count_ == contacts.size();
        for (std::size_t ordered = 0U; warm_compatible && ordered < contacts.size(); ++ordered) {
            const SweptContact& contact = contacts[contact_order[ordered]];
            const auto [first, second] = canonical_pair(contact);
            warm_compatible = warm_start->first_[ordered] == first
                && warm_start->second_[ordered] == second
                && warm_start->axis_[ordered] == contact.axis;
        }
        if (warm_compatible) ++stats.warm_start_attempts;
        std::fill(
            warm_start->next_impulses_.begin(),
            warm_start->next_impulses_.begin() + static_cast<std::ptrdiff_t>(contacts.size()),
            Fixed::rep{});
    }

    for (const ContactIslandDescriptor& island : islands) {
        ++stats.islands_processed;
        const std::span<const std::size_t> bodies{
            body_order.data() + island.body_begin, island.body_count};
        bool solved = false;
        if (island.topology == IslandTopology::Matching) {
            const std::size_t ordered = island.contact_begin;
            const SweptContact& contact = contacts[contact_order[ordered]];
            const auto [first, second] = canonical_pair(contact);
            const Fixed::rep impulse = project_pair(values[first], values[second]);
            stats.pair_adjustments += impulse != 0 ? 1U : 0U;
            ++stats.contacts_processed;
            ++stats.matching_islands;
            solved = true;
        } else if (island.topology == IslandTopology::Chain
                   && canonical_consecutive_chain(
                       island, bodies, contacts, contact_order, false)) {
            stats.isotonic_blocks += isotonic_project(
                values, bodies, scratch, rounding_error);
            stats.contacts_processed += island.contact_count;
            ++stats.chain_islands;
            solved = true;
        } else if (island.topology == IslandTopology::Cycle
                   && canonical_consecutive_chain(
                       island, bodies, contacts, contact_order, true)) {
            stats.isotonic_blocks += isotonic_project(
                values, bodies, scratch, rounding_error);
            stats.contacts_processed += island.contact_count;
            ++stats.reduced_cycle_islands;
            solved = true;
        } else if (island.topology == IslandTopology::Tree
                   && star_project(
                       values, island, bodies, contacts, contact_order,
                       scratch, rounding_error)) {
            stats.contacts_processed += island.contact_count;
            ++stats.star_tree_islands;
            solved = true;
        }
        if (solved) continue;

        ++stats.general_fallback_islands;
        if (warm_compatible && warm_start != nullptr) {
            bool applied_any = false;
            for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
                const std::size_t ordered = island.contact_begin + offset;
                const Fixed::rep cached = warm_start->impulses_[ordered];
                if (cached <= 0) continue;
                const SweptContact& contact = contacts[contact_order[ordered]];
                const auto [first, second] = canonical_pair(contact);
                if (values[first] <= values[second]) continue;
                const WideInteger sum = static_cast<WideInteger>(values[first]) + values[second];
                const WideInteger lower = floor_div(sum, 2U);
                const Fixed::rep needed = checked_rep(
                    static_cast<WideInteger>(values[first]) - lower);
                const Fixed::rep applied = std::min(cached, needed);
                values[first] = checked_rep(static_cast<WideInteger>(values[first]) - applied);
                values[second] = checked_rep(static_cast<WideInteger>(values[second]) + applied);
                ++stats.warm_contacts_applied;
                applied_any = true;
            }
            if (applied_any) ++stats.warm_start_accepts;
        }
        for (std::size_t iteration = 0U; iteration < fallback_iterations; ++iteration) {
            for (std::size_t color = 0U; color < island.color_count; ++color) {
                for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
                    const std::size_t ordered = island.contact_begin + offset;
                    if (colors[ordered] != color) continue;
                    const SweptContact& contact = contacts[contact_order[ordered]];
                    const auto [first, second] = canonical_pair(contact);
                    const Fixed::rep impulse = project_pair(values[first], values[second]);
                    ++stats.contacts_processed;
                    if (impulse == 0) continue;
                    ++stats.pair_adjustments;
                    if (warm_start != nullptr) {
                        warm_start->next_impulses_[ordered] = checked_rep(
                            static_cast<WideInteger>(warm_start->next_impulses_[ordered])
                            + impulse);
                    }
                }
            }
        }
    }

    if (warm_start != nullptr) {
        for (std::size_t ordered = 0U; ordered < contacts.size(); ++ordered) {
            const SweptContact& contact = contacts[contact_order[ordered]];
            const auto [first, second] = canonical_pair(contact);
            warm_start->first_[ordered] = first;
            warm_start->second_[ordered] = second;
            warm_start->axis_[ordered] = contact.axis;
        }
        warm_start->contact_count_ = contacts.size();
        warm_start->initialized_ = true;
        warm_start->impulses_.swap(warm_start->next_impulses_);
    }
    stats.violations_after = count_violations(values, contacts);
    stats.rounding_error_raw = checked_rep(rounding_error);
    return stats;
}

} // namespace neoeng::core
