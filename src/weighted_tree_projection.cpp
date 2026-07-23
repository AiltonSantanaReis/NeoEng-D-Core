#include "neoeng/core/weighted_tree_projection.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace neoeng::core {
namespace {
constexpr WideInteger kNormalScale = static_cast<WideInteger>(1) << 30U;

Fixed::rep narrow(WideInteger value) {
    constexpr WideInteger lo = static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::min());
    constexpr WideInteger hi = static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::max());
    if (value < lo || value > hi) throw std::overflow_error("Weighted tree projection overflow");
    return static_cast<Fixed::rep>(value);
}

WideInteger rounded_div(WideInteger numerator, WideInteger denominator) {
    if (denominator <= 0) throw std::domain_error("Weighted tree denominator must be positive");
    WideInteger q = numerator / denominator;
    WideInteger r = numerator % denominator;
    if (r < 0) { r += denominator; --q; }
    if (r * 2 > denominator) ++q;
    return q;
}

std::uint64_t magnitude(WideInteger value) noexcept {
    const WideInteger abs = value < 0 ? -value : value;
    const WideInteger limit = static_cast<WideInteger>(std::numeric_limits<std::uint64_t>::max());
    return abs > limit ? std::numeric_limits<std::uint64_t>::max() : static_cast<std::uint64_t>(abs);
}

Fixed::rep normal_velocity(Fixed::rep x, Fixed::rep y, NormalQ30 normal) {
    return narrow(rounded_div(static_cast<WideInteger>(x) * normal.x
        + static_cast<WideInteger>(y) * normal.y, kNormalScale));
}

Fixed::rep component_delta(Fixed::rep scalar_delta, std::int32_t component, WideInteger norm_sq) {
    return narrow(rounded_div(static_cast<WideInteger>(scalar_delta) * component * kNormalScale, norm_sq));
}

std::size_t find_root(std::vector<std::size_t>& parent, std::size_t value) {
    while (parent[value] != value) {
        parent[value] = parent[parent[value]];
        value = parent[value];
    }
    return value;
}

void unite(std::vector<std::size_t>& parent, std::size_t a, std::size_t b) {
    a = find_root(parent, a); b = find_root(parent, b);
    if (a == b) return;
    if (b < a) std::swap(a, b);
    parent[b] = a;
}
}

WeightedTreeScratch::WeightedTreeScratch(std::size_t maximum_bodies)
    : scalar_input(maximum_bodies), scalar_output(maximum_bodies), dual(maximum_bodies),
      delta(maximum_bodies), parent(maximum_bodies), child(maximum_bodies),
      first_child_edge(maximum_bodies), last_child_edge(maximum_bodies),
      next_sibling_edge(maximum_bodies), block_begin(maximum_bodies), block_end(maximum_bodies), order(maximum_bodies),
      uf_parent(maximum_bodies), indegree(maximum_bodies), edge_of_child(maximum_bodies),
      component_weight(maximum_bodies),
      component_weighted_value(maximum_bodies), gradient(maximum_bodies),
      subtree_gradient(maximum_bodies), active(maximum_bodies) {}

std::size_t WeightedTreeScratch::reserved_bytes() const noexcept {
    return (scalar_input.capacity() + scalar_output.capacity() + dual.capacity() + delta.capacity())
        * sizeof(Fixed::rep)
        + (parent.capacity() + child.capacity() + first_child_edge.capacity()
            + last_child_edge.capacity() + next_sibling_edge.capacity()
            + block_begin.capacity() + block_end.capacity()
            + order.capacity() + uf_parent.capacity() + indegree.capacity()
            + edge_of_child.capacity()) * sizeof(std::size_t)
        + component_weight.capacity() * sizeof(std::uint64_t)
        + (component_weighted_value.capacity() + gradient.capacity() + subtree_gradient.capacity())
            * sizeof(WideInteger)
        + active.capacity();
}

WeightedTreeStats project_weighted_chain_common_normal_inplace(
    std::span<Fixed::rep> velocity_x,
    std::span<Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const DirectedTreeEdge> edges,
    NormalQ30 normal,
    WeightedTreeConfig config,
    WeightedTreeScratch& scratch) {
    const std::size_t bodies = velocity_x.size();
    WeightedTreeStats stats{};
    if (velocity_y.size() != bodies || masses.size() != bodies || bodies == 0U
        || edges.size() + 1U != bodies || scratch.scalar_input.size() < bodies
        || (normal.x == 0 && normal.y == 0)) {
        return stats;
    }
    const std::size_t missing = bodies;
    std::fill(scratch.parent.begin(), scratch.parent.begin() + static_cast<std::ptrdiff_t>(bodies), missing);
    std::fill(scratch.child.begin(), scratch.child.begin() + static_cast<std::ptrdiff_t>(bodies), missing);
    std::fill(scratch.indegree.begin(), scratch.indegree.begin() + static_cast<std::ptrdiff_t>(bodies), 0U);
    for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
        const auto item = edges[edge];
        if (item.parent >= bodies || item.child >= bodies || item.parent == item.child
            || scratch.child[item.parent] != missing || ++scratch.indegree[item.child] != 1U) {
            return stats;
        }
        scratch.child[item.parent] = item.child;
        scratch.parent[item.child] = item.parent;
        scratch.edge_of_child[item.child] = edge;
    }
    std::size_t root = missing;
    std::uint64_t total_mass = 0U;
    for (std::size_t body = 0U; body < bodies; ++body) {
        if (scratch.indegree[body] == 0U) {
            if (root != missing) return stats;
            root = body;
        }
        if (masses[body] == 0U) return stats;
        total_mass += masses[body];
        scratch.scalar_input[body] = normal_velocity(velocity_x[body], velocity_y[body], normal);
    }
    if (root == missing) return stats;
    std::size_t current = root;
    for (std::size_t index = 0U; index < bodies; ++index) {
        if (current == missing) return stats;
        scratch.order[index] = current;
        current = scratch.child[current];
    }
    if (current != missing) return stats;

    std::size_t blocks = 0U;
    for (std::size_t index = 0U; index < bodies; ++index) {
        const std::size_t body = scratch.order[index];
        scratch.block_begin[blocks] = index;
        scratch.block_end[blocks] = index + 1U;
        scratch.component_weight[blocks] = masses[body];
        scratch.component_weighted_value[blocks] = static_cast<WideInteger>(masses[body])
            * scratch.scalar_input[body];
        ++blocks;
        while (blocks >= 2U) {
            const std::size_t left = blocks - 2U, right = blocks - 1U;
            const WideInteger lhs = scratch.component_weighted_value[left]
                * scratch.component_weight[right];
            const WideInteger rhs = scratch.component_weighted_value[right]
                * scratch.component_weight[left];
            if (lhs <= rhs) break;
            scratch.block_end[left] = scratch.block_end[right];
            scratch.component_weight[left] += scratch.component_weight[right];
            scratch.component_weighted_value[left] += scratch.component_weighted_value[right];
            --blocks;
        }
    }
    for (std::size_t block = 0U; block < blocks; ++block) {
        const Fixed::rep value = narrow(rounded_div(
            scratch.component_weighted_value[block], scratch.component_weight[block]));
        for (std::size_t index = scratch.block_begin[block]; index < scratch.block_end[block]; ++index) {
            scratch.scalar_output[scratch.order[index]] = value;
        }
    }

    for (std::size_t body = 0U; body < bodies; ++body) {
        scratch.gradient[body] = static_cast<WideInteger>(masses[body])
            * (static_cast<WideInteger>(scratch.scalar_output[body]) - scratch.scalar_input[body]);
    }
    std::fill(scratch.dual.begin(), scratch.dual.begin() + static_cast<std::ptrdiff_t>(edges.size()), 0);
    WideInteger prefix = 0;
    std::uint64_t primal = 0U, dual_violation = 0U, stationarity = 0U, complementarity = 0U;
    WideInteger previous_lambda = 0;
    for (std::size_t index = 0U; index < bodies; ++index) {
        const std::size_t body = scratch.order[index];
        prefix += scratch.gradient[body];
        const WideInteger lambda = index + 1U < bodies ? -prefix : 0;
        if (index + 1U < bodies) {
            const std::size_t child_body = scratch.order[index + 1U];
            const std::size_t edge = scratch.edge_of_child[child_body];
            scratch.dual[edge] = narrow(lambda);
            const WideInteger gap = static_cast<WideInteger>(scratch.scalar_output[body])
                - scratch.scalar_output[child_body];
            if (gap > 0) primal = std::max(primal, magnitude(gap));
            if (lambda < 0) dual_violation = std::max(dual_violation, magnitude(lambda));
            complementarity = std::max(complementarity,
                magnitude(lambda * gap / static_cast<WideInteger>(Fixed::scale)));
            if (lambda > 0) ++stats.active_edges;
        }
        const WideInteger residual = scratch.gradient[body] - previous_lambda + lambda;
        stationarity = std::max(stationarity, magnitude(residual));
        previous_lambda = lambda;
    }
    std::uint64_t quantization_error = 0U;
    for (std::size_t block = 0U; block < blocks; ++block) {
        const std::size_t first_index = scratch.block_begin[block];
        const Fixed::rep value = scratch.scalar_output[scratch.order[first_index]];
        const WideInteger error = static_cast<WideInteger>(value) * scratch.component_weight[block]
            - scratch.component_weighted_value[block];
        quantization_error = std::max(quantization_error, magnitude(error));
    }
    const std::uint64_t quantization_bound = (total_mass + 1U) / 2U;
    const std::uint64_t allowed = quantization_bound + config.stationarity_tolerance_raw;
    stats.iterations = 1U;
    stats.residuals = {
        .primal_linf_raw = primal,
        .dual_violation_raw = dual_violation,
        .stationarity_linf_raw = std::max(stationarity, quantization_error),
        .complementarity_linf_raw = complementarity,
        .quantization_stationarity_bound_raw = quantization_bound,
        .certified = primal == 0U
            && dual_violation <= allowed
            && stationarity <= allowed
            && quantization_error <= allowed
            && complementarity <= allowed,
    };
    if (!stats.residuals.certified) return stats;

    const WideInteger norm_sq = static_cast<WideInteger>(normal.x) * normal.x
                              + static_cast<WideInteger>(normal.y) * normal.y;
    for (std::size_t body = 0U; body < bodies; ++body) {
        scratch.delta[body] = narrow(static_cast<WideInteger>(scratch.scalar_output[body])
            - scratch.scalar_input[body]);
        velocity_x[body] = narrow(static_cast<WideInteger>(velocity_x[body])
            + component_delta(scratch.delta[body], normal.x, norm_sq));
        velocity_y[body] = narrow(static_cast<WideInteger>(velocity_y[body])
            + component_delta(scratch.delta[body], normal.y, norm_sq));
    }
    return stats;
}

WeightedTreeStats project_weighted_tree_common_normal_inplace(
    std::span<Fixed::rep> velocity_x,
    std::span<Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const DirectedTreeEdge> edges,
    NormalQ30 normal,
    WeightedTreeConfig config,
    WeightedTreeScratch& scratch) {
    const std::size_t bodies = velocity_x.size();
    if (velocity_y.size() != bodies || masses.size() != bodies || bodies == 0U
        || edges.size() + 1U != bodies || scratch.scalar_input.size() < bodies) {
        throw std::invalid_argument("Weighted tree projection shape mismatch");
    }
    if (normal.x == 0 && normal.y == 0) throw std::invalid_argument("Tree normal cannot be zero");
    const WideInteger norm_sq = static_cast<WideInteger>(normal.x) * normal.x
                              + static_cast<WideInteger>(normal.y) * normal.y;

    std::fill(scratch.parent.begin(), scratch.parent.begin() + static_cast<std::ptrdiff_t>(bodies), bodies);
    std::fill(scratch.indegree.begin(), scratch.indegree.begin() + static_cast<std::ptrdiff_t>(bodies), 0U);
    std::fill(scratch.edge_of_child.begin(), scratch.edge_of_child.begin() + static_cast<std::ptrdiff_t>(bodies), edges.size());
    std::fill(scratch.first_child_edge.begin(), scratch.first_child_edge.begin() + static_cast<std::ptrdiff_t>(bodies), edges.size());
    std::fill(scratch.last_child_edge.begin(), scratch.last_child_edge.begin() + static_cast<std::ptrdiff_t>(bodies), edges.size());
    for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
        if (edges[edge].parent >= bodies || edges[edge].child >= bodies
            || edges[edge].parent == edges[edge].child) {
            throw std::invalid_argument("Invalid directed tree edge");
        }
        if (++scratch.indegree[edges[edge].child] != 1U) throw std::invalid_argument("Tree child has multiple parents");
        scratch.parent[edges[edge].child] = edges[edge].parent;
        scratch.edge_of_child[edges[edge].child] = edge;
        scratch.next_sibling_edge[edge] = edges.size();
        const std::size_t parent = edges[edge].parent;
        if (scratch.first_child_edge[parent] == edges.size()) {
            scratch.first_child_edge[parent] = edge;
        } else {
            scratch.next_sibling_edge[scratch.last_child_edge[parent]] = edge;
        }
        scratch.last_child_edge[parent] = edge;
    }
    std::size_t root = bodies;
    std::uint64_t total_mass = 0U;
    for (std::size_t body = 0U; body < bodies; ++body) {
        if (scratch.indegree[body] == 0U) {
            if (root != bodies) throw std::invalid_argument("Directed tree has multiple roots");
            root = body;
        }
        if (masses[body] == 0U) throw std::invalid_argument("Tree mass must be positive");
        total_mass += masses[body];
        scratch.scalar_input[body] = normal_velocity(velocity_x[body], velocity_y[body], normal);
    }
    if (root == bodies) throw std::invalid_argument("Directed tree has no root");

    // Deterministic O(n+e) topological order using the preallocated adjacency lists.
    std::size_t order_count = 0U;
    scratch.order[order_count++] = root;
    for (std::size_t cursor = 0U; cursor < order_count; ++cursor) {
        const std::size_t current = scratch.order[cursor];
        for (std::size_t edge = scratch.first_child_edge[current];
             edge != edges.size(); edge = scratch.next_sibling_edge[edge]) {
            scratch.order[order_count++] = edges[edge].child;
        }
    }
    if (order_count != bodies) throw std::invalid_argument("Directed edges are disconnected or cyclic");
    if (config.use_warm_start) {
        for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
            scratch.active[edge] = static_cast<std::uint8_t>(
                scratch.dual[edge] > static_cast<Fixed::rep>(config.stationarity_tolerance_raw));
            if (scratch.active[edge] == 0U) scratch.dual[edge] = 0;
        }
    } else {
        std::fill(scratch.active.begin(), scratch.active.begin() + static_cast<std::ptrdiff_t>(edges.size()), 0U);
        std::fill(scratch.dual.begin(), scratch.dual.begin() + static_cast<std::ptrdiff_t>(edges.size()), 0);
    }

    WeightedTreeStats stats{};
    bool certified = false;
    for (std::size_t iteration = 0U; iteration < config.maximum_active_set_iterations; ++iteration) {
        stats.iterations = iteration + 1U;
        std::iota(scratch.uf_parent.begin(), scratch.uf_parent.begin() + static_cast<std::ptrdiff_t>(bodies), 0U);
        for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
            if (scratch.active[edge] != 0U) unite(scratch.uf_parent, edges[edge].parent, edges[edge].child);
        }
        std::fill(scratch.component_weight.begin(), scratch.component_weight.begin() + static_cast<std::ptrdiff_t>(bodies), 0U);
        std::fill(scratch.component_weighted_value.begin(), scratch.component_weighted_value.begin() + static_cast<std::ptrdiff_t>(bodies), 0);
        for (std::size_t body = 0U; body < bodies; ++body) {
            const std::size_t component = find_root(scratch.uf_parent, body);
            scratch.component_weight[component] += masses[body];
            scratch.component_weighted_value[component] += static_cast<WideInteger>(masses[body])
                * scratch.scalar_input[body];
        }
        for (std::size_t body = 0U; body < bodies; ++body) {
            const std::size_t component = find_root(scratch.uf_parent, body);
            scratch.scalar_output[body] = narrow(rounded_div(
                scratch.component_weighted_value[component], scratch.component_weight[component]));
        }

        std::size_t violations_added = 0U;
        for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
            if (scratch.active[edge] == 0U
                && scratch.scalar_output[edges[edge].parent]
                    > scratch.scalar_output[edges[edge].child]
                        + static_cast<Fixed::rep>(config.feasibility_tolerance_raw)) {
                scratch.active[edge] = 1U;
                ++violations_added;
            }
        }
        if (violations_added != 0U) {
            stats.edges_added += violations_added;
            continue;
        }

        for (std::size_t body = 0U; body < bodies; ++body) {
            scratch.gradient[body] = static_cast<WideInteger>(masses[body])
                * (static_cast<WideInteger>(scratch.scalar_output[body]) - scratch.scalar_input[body]);
            scratch.subtree_gradient[body] = scratch.gradient[body];
        }
        for (std::size_t index = bodies; index-- > 0U;) {
            const std::size_t body = scratch.order[index];
            if (body == root) continue;
            // Only active edges transmit a multiplier across the active forest.
            const std::size_t edge_index = scratch.edge_of_child[body];
            if (edge_index != edges.size() && scratch.active[edge_index] != 0U) {
                scratch.dual[edge_index] = narrow(scratch.subtree_gradient[body]);
                scratch.subtree_gradient[edges[edge_index].parent] += scratch.subtree_gradient[body];
            }
        }
        std::size_t negative_count = 0U;
        for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
            if (scratch.active[edge] != 0U && scratch.dual[edge]
                < -static_cast<Fixed::rep>(config.stationarity_tolerance_raw)) {
                scratch.active[edge] = 0U;
                scratch.dual[edge] = 0;
                ++negative_count;
            }
        }
        if (negative_count != 0U) {
            stats.edges_removed += negative_count;
            continue;
        }
        certified = true; break;
    }

    std::uint64_t primal = 0U, dual_violation = 0U, stationarity = 0U, complementarity = 0U;
    for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
        const WideInteger gap = static_cast<WideInteger>(scratch.scalar_output[edges[edge].parent])
                              - scratch.scalar_output[edges[edge].child];
        if (gap > 0) primal = std::max(primal, magnitude(gap));
        if (scratch.dual[edge] < 0) dual_violation = std::max(dual_violation, magnitude(scratch.dual[edge]));
        complementarity = std::max(complementarity,
            magnitude(static_cast<WideInteger>(scratch.dual[edge]) * gap));
        if (scratch.active[edge] != 0U) ++stats.active_edges;
    }
    for (std::size_t body = 0U; body < bodies; ++body) {
        scratch.gradient[body] = static_cast<WideInteger>(masses[body])
            * (static_cast<WideInteger>(scratch.scalar_output[body]) - scratch.scalar_input[body]);
    }
    for (std::size_t edge = 0U; edge < edges.size(); ++edge) {
        scratch.gradient[edges[edge].parent] += scratch.dual[edge];
        scratch.gradient[edges[edge].child] -= scratch.dual[edge];
    }
    for (std::size_t body = 0U; body < bodies; ++body) {
        stationarity = std::max(stationarity, magnitude(scratch.gradient[body]));
    }
    const std::uint64_t quantization_bound = (total_mass + 1U) / 2U;
    stats.residuals = {
        .primal_linf_raw = primal,
        .dual_violation_raw = dual_violation,
        .stationarity_linf_raw = stationarity,
        .complementarity_linf_raw = complementarity,
        .quantization_stationarity_bound_raw = quantization_bound,
        .certified = certified
            && primal <= config.feasibility_tolerance_raw
            && dual_violation <= config.stationarity_tolerance_raw
            && stationarity <= quantization_bound + config.stationarity_tolerance_raw
            && complementarity == 0U,
    };

    if (stats.residuals.certified) {
        for (std::size_t body = 0U; body < bodies; ++body) {
            scratch.delta[body] = narrow(static_cast<WideInteger>(scratch.scalar_output[body])
                - scratch.scalar_input[body]);
            velocity_x[body] = narrow(static_cast<WideInteger>(velocity_x[body])
                + component_delta(scratch.delta[body], normal.x, norm_sq));
            velocity_y[body] = narrow(static_cast<WideInteger>(velocity_y[body])
                + component_delta(scratch.delta[body], normal.y, norm_sq));
        }
    }
    return stats;
}

} // namespace neoeng::core
