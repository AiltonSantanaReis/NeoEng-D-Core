#include "neoeng/core/oblique_tree_grid_dp.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace neoeng::core {
namespace {
constexpr std::size_t missing = std::numeric_limits<std::size_t>::max();
constexpr std::uint64_t infinite_cost = std::numeric_limits<std::uint64_t>::max() / 4U;
constexpr WideInteger normal_scale = WideInteger{1} << 30U;

WideInteger rounded_div_ties_to_floor(WideInteger numerator, WideInteger denominator) noexcept {
    WideInteger quotient = numerator / denominator;
    WideInteger remainder = numerator % denominator;
    if (remainder < 0) { remainder += denominator; --quotient; }
    if (remainder * 2 > denominator) ++quotient;
    return quotient;
}

Fixed::rep normal_velocity(Fixed::rep x, Fixed::rep y, const NormalQ30& normal) noexcept {
    return static_cast<Fixed::rep>(rounded_div_ties_to_floor(
        static_cast<WideInteger>(x) * normal.x + static_cast<WideInteger>(y) * normal.y,
        normal_scale));
}

bool contact_feasible(
    const NormalContact& contact,
    std::size_t traversal_parent,
    std::uint32_t parent_state,
    std::uint32_t child_state,
    std::span<const Fixed::rep> projection) noexcept {
    const bool traversal_matches = contact.first == traversal_parent;
    const std::uint32_t first_state = traversal_matches ? parent_state : child_state;
    const std::uint32_t second_state = traversal_matches ? child_state : parent_state;
    return projection[first_state] <= projection[second_state];
}

std::uint64_t local_cost(
    Fixed::rep x, Fixed::rep y,
    Fixed::rep input_x, Fixed::rep input_y,
    std::uint32_t mass) {
    const WideInteger dx = static_cast<WideInteger>(x) - input_x;
    const WideInteger dy = static_cast<WideInteger>(y) - input_y;
    const WideInteger value = static_cast<WideInteger>(mass) * (dx * dx + dy * dy);
    if (value < 0 || value >= infinite_cost) throw std::overflow_error("Oblique tree grid objective overflow");
    return static_cast<std::uint64_t>(value);
}

std::uint64_t saturating_add(std::uint64_t a, std::uint64_t b) noexcept {
    if (a >= infinite_cost || b >= infinite_cost || a > infinite_cost - b) return infinite_cost;
    return a + b;
}

std::uint64_t topology_signature(std::size_t bodies, std::size_t states,
    Fixed::rep minimum_raw, Fixed::rep maximum_raw,
    std::span<const NormalContact> contacts) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (unsigned byte = 0U; byte < 8U; ++byte) {
            hash ^= (value >> (byte * 8U)) & 0xFFU;
            hash *= 0x100000001B3ULL;
        }
    };
    mix(bodies); mix(states); mix(static_cast<std::uint64_t>(minimum_raw));
    mix(static_cast<std::uint64_t>(maximum_raw));
    for (const NormalContact& contact : contacts) {
        mix(contact.first); mix(contact.second);
        mix(static_cast<std::uint32_t>(contact.normal.x));
        mix(static_cast<std::uint32_t>(contact.normal.y));
    }
    return hash;
}
} // namespace

ObliqueTreeGridScratch::ObliqueTreeGridScratch(
    std::size_t maximum_bodies, std::size_t maximum_grid_states)
    : adjacency_head(maximum_bodies), adjacency_next(maximum_bodies > 0U ? 2U * (maximum_bodies - 1U) : 0U),
      adjacency_edge(adjacency_next.size()), adjacency_other(adjacency_next.size()),
      parent(maximum_bodies), parent_edge(maximum_bodies), order(maximum_bodies),
      visited(maximum_bodies), dp(maximum_bodies * maximum_grid_states),
      choice((maximum_bodies > 0U ? maximum_bodies - 1U : 0U) * maximum_grid_states),
      selected_state(maximum_bodies),
      edge_state_projection((maximum_bodies > 0U ? maximum_bodies - 1U : 0U) * maximum_grid_states),
      feasible_mask((maximum_bodies > 0U ? maximum_bodies - 1U : 0U) * maximum_grid_states),
      grid_x(maximum_grid_states), grid_y(maximum_grid_states) {}

std::size_t ObliqueTreeGridScratch::reserved_bytes() const noexcept {
    return (adjacency_head.capacity() + adjacency_next.capacity() + adjacency_edge.capacity()
        + adjacency_other.capacity() + parent.capacity() + parent_edge.capacity()
        + order.capacity()) * sizeof(std::size_t)
        + visited.capacity() * sizeof(std::uint8_t)
        + dp.capacity() * sizeof(std::uint64_t)
        + (choice.capacity() + selected_state.capacity()) * sizeof(std::uint32_t)
        + edge_state_projection.capacity() * sizeof(Fixed::rep)
        + feasible_mask.capacity() * sizeof(std::uint64_t)
        + (grid_x.capacity() + grid_y.capacity()) * sizeof(Fixed::rep);
}

ObliqueTreeGridResult solve_oblique_tree_grid_dp(
    std::span<const Fixed::rep> input_x,
    std::span<const Fixed::rep> input_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ObliqueTreeGridConfig config,
    ObliqueTreeGridScratch& scratch) {
    const std::size_t bodies = input_x.size();
    ObliqueTreeGridResult result{};
    if (bodies == 0U || input_y.size() != bodies || masses.size() != bodies
        || contacts.size() + 1U != bodies || bodies > config.maximum_bodies
        || config.minimum_raw > config.maximum_raw) return result;
    const std::uint64_t side = static_cast<std::uint64_t>(config.maximum_raw - config.minimum_raw + 1);
    if (side == 0U || side > config.maximum_grid_states
        || side * side > config.maximum_grid_states) return result;
    const std::size_t states = static_cast<std::size_t>(side * side);
    if (scratch.adjacency_head.size() < bodies || scratch.dp.size() < bodies * states
        || scratch.choice.size() < contacts.size() * states) return result;
    for (std::size_t body = 0U; body < bodies; ++body) {
        if (masses[body] == 0U) return result;
    }
    for (const NormalContact& contact : contacts) {
        if (contact.first >= bodies || contact.second >= bodies || contact.first == contact.second
            || (contact.normal.x == 0 && contact.normal.y == 0)) return result;
    }

    const std::uint64_t signature = topology_signature(
        bodies, states, config.minimum_raw, config.maximum_raw, contacts);
    const bool reuse_prepared = scratch.prepared
        && scratch.prepared_signature == signature
        && scratch.prepared_bodies == bodies
        && scratch.prepared_states == states;
    if (!reuse_prepared) {
        std::size_t state = 0U;
        for (Fixed::rep x = config.minimum_raw; x <= config.maximum_raw; ++x) {
            for (Fixed::rep y = config.minimum_raw; y <= config.maximum_raw; ++y) {
                scratch.grid_x[state] = x;
                scratch.grid_y[state] = y;
                ++state;
            }
        }
        for (std::size_t edge = 0U; edge < contacts.size(); ++edge) {
            for (std::size_t grid_state = 0U; grid_state < states; ++grid_state) {
                scratch.edge_state_projection[edge * states + grid_state] = normal_velocity(
                    scratch.grid_x[grid_state], scratch.grid_y[grid_state], contacts[edge].normal);
            }
        }

        std::fill(scratch.adjacency_head.begin(), scratch.adjacency_head.begin()
            + static_cast<std::ptrdiff_t>(bodies), missing);
        for (std::size_t edge = 0U; edge < contacts.size(); ++edge) {
            const NormalContact& contact = contacts[edge];
            const std::size_t first_arc = edge * 2U;
            const std::size_t second_arc = first_arc + 1U;
            scratch.adjacency_other[first_arc] = contact.second;
            scratch.adjacency_edge[first_arc] = edge;
            scratch.adjacency_next[first_arc] = scratch.adjacency_head[contact.first];
            scratch.adjacency_head[contact.first] = first_arc;
            scratch.adjacency_other[second_arc] = contact.first;
            scratch.adjacency_edge[second_arc] = edge;
            scratch.adjacency_next[second_arc] = scratch.adjacency_head[contact.second];
            scratch.adjacency_head[contact.second] = second_arc;
        }

        std::fill(scratch.visited.begin(), scratch.visited.begin()
            + static_cast<std::ptrdiff_t>(bodies), 0U);
        std::size_t count = 0U;
        scratch.order[count++] = 0U;
        scratch.visited[0] = 1U;
        scratch.parent[0] = missing;
        scratch.parent_edge[0] = missing;
        for (std::size_t cursor = 0U; cursor < count; ++cursor) {
            const std::size_t body = scratch.order[cursor];
            for (std::size_t arc = scratch.adjacency_head[body]; arc != missing;
                 arc = scratch.adjacency_next[arc]) {
                const std::size_t other = scratch.adjacency_other[arc];
                if (other == scratch.parent[body]) continue;
                if (scratch.visited[other] != 0U) return result;
                scratch.visited[other] = 1U;
                scratch.parent[other] = body;
                scratch.parent_edge[other] = scratch.adjacency_edge[arc];
                scratch.order[count++] = other;
            }
        }
        if (count != bodies) return result;
        if (states <= 64U) {
            for (std::size_t body = 1U; body < bodies; ++body) {
                const std::size_t edge = scratch.parent_edge[body];
                const std::size_t traversal_parent = scratch.parent[body];
                for (std::uint32_t parent_state = 0U; parent_state < states; ++parent_state) {
                    std::uint64_t mask = 0U;
                    const auto projection = std::span<const Fixed::rep>(
                        scratch.edge_state_projection.data() + edge * states, states);
                    for (std::uint32_t child_state = 0U; child_state < states; ++child_state) {
                        if (contact_feasible(contacts[edge], traversal_parent,
                                parent_state, child_state, projection)) {
                            mask |= std::uint64_t{1} << child_state;
                        }
                    }
                    scratch.feasible_mask[edge * states + parent_state] = mask;
                }
            }
        }
        scratch.prepared = true;
        scratch.prepared_signature = signature;
        scratch.prepared_bodies = bodies;
        scratch.prepared_states = states;
    }
    result.tree_valid = true;

    for (std::size_t reverse = bodies; reverse-- > 0U;) {
        const std::size_t body = scratch.order[reverse];
        for (std::uint32_t parent_state = 0U; parent_state < states; ++parent_state) {
            std::uint64_t cost = local_cost(scratch.grid_x[parent_state], scratch.grid_y[parent_state],
                input_x[body], input_y[body], masses[body]);
            for (std::size_t arc = scratch.adjacency_head[body]; arc != missing;
                 arc = scratch.adjacency_next[arc]) {
                const std::size_t child = scratch.adjacency_other[arc];
                if (scratch.parent[child] != body) continue;
                const std::size_t edge = scratch.adjacency_edge[arc];
                std::uint64_t best = infinite_cost;
                std::uint32_t best_state = 0U;
                if (states <= 64U) {
                    std::uint64_t mask = scratch.feasible_mask[edge * states + parent_state];
                    while (mask != 0U) {
                        const std::uint32_t child_state = static_cast<std::uint32_t>(__builtin_ctzll(mask));
                        mask &= mask - 1U;
                        ++result.state_pairs_tested;
                        const std::uint64_t candidate = scratch.dp[child * states + child_state];
                        if (candidate < best || (candidate == best && child_state < best_state)) {
                            best = candidate;
                            best_state = child_state;
                        }
                    }
                } else {
                    const auto projection = std::span<const Fixed::rep>(
                        scratch.edge_state_projection.data() + edge * states, states);
                    for (std::uint32_t child_state = 0U; child_state < states; ++child_state) {
                        ++result.state_pairs_tested;
                        if (!contact_feasible(contacts[edge], body, parent_state, child_state, projection)) continue;
                        const std::uint64_t candidate = scratch.dp[child * states + child_state];
                        if (candidate < best || (candidate == best && child_state < best_state)) {
                            best = candidate;
                            best_state = child_state;
                        }
                    }
                }
                scratch.choice[edge * states + parent_state] = best_state;
                cost = saturating_add(cost, best);
            }
            scratch.dp[body * states + parent_state] = cost;
        }
    }

    std::uint32_t root_state = 0U;
    std::uint64_t best = infinite_cost;
    for (std::uint32_t candidate = 0U; candidate < states; ++candidate) {
        const std::uint64_t cost = scratch.dp[candidate];
        if (cost < best || (cost == best && candidate < root_state)) {
            best = cost;
            root_state = candidate;
        }
    }
    if (best >= infinite_cost) return result;
    scratch.selected_state[0] = root_state;
    for (std::size_t cursor = 0U; cursor < bodies; ++cursor) {
        const std::size_t body = scratch.order[cursor];
        const std::uint32_t parent_state = scratch.selected_state[body];
        for (std::size_t arc = scratch.adjacency_head[body]; arc != missing;
             arc = scratch.adjacency_next[arc]) {
            const std::size_t child = scratch.adjacency_other[arc];
            if (scratch.parent[child] != body) continue;
            const std::size_t edge = scratch.adjacency_edge[arc];
            scratch.selected_state[child] = scratch.choice[edge * states + parent_state];
        }
    }

    result.velocity_x.resize(bodies);
    result.velocity_y.resize(bodies);
    for (std::size_t body = 0U; body < bodies; ++body) {
        result.velocity_x[body] = scratch.grid_x[scratch.selected_state[body]];
        result.velocity_y[body] = scratch.grid_y[scratch.selected_state[body]];
    }
    std::uint64_t primal = 0U;
    for (const NormalContact& contact : contacts) {
        const Fixed::rep first = normal_velocity(result.velocity_x[contact.first],
            result.velocity_y[contact.first], contact.normal);
        const Fixed::rep second = normal_velocity(result.velocity_x[contact.second],
            result.velocity_y[contact.second], contact.normal);
        if (first > second) primal = std::max(primal, static_cast<std::uint64_t>(first - second));
    }
    result.feasible = primal == 0U;
    result.certified_on_grid = result.tree_valid && result.feasible;
    result.primal_violation_raw = primal;
    result.objective = best;
    return result;
}

} // namespace neoeng::core
