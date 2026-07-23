#include "neoeng/core/axis_forest_projection.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace neoeng::core {
namespace {
constexpr std::int32_t kAxisOne = 1 << 30;
constexpr std::size_t kMissing = std::numeric_limits<std::size_t>::max();

enum class Axis : std::uint8_t { X, Y };

std::size_t find_root(std::vector<std::size_t>& parent, std::size_t value) {
    while (parent[value] != value) {
        parent[value] = parent[parent[value]];
        value = parent[value];
    }
    return value;
}

bool unite(std::vector<std::size_t>& parent, std::size_t first, std::size_t second) {
    first = find_root(parent, first);
    second = find_root(parent, second);
    if (first == second) return false;
    if (second < first) std::swap(first, second);
    parent[second] = first;
    return true;
}

void merge_residuals(AxisForestStats& total, const WeightedTreeStats& item) {
    total.tree_iterations += item.iterations;
    total.primal_linf_raw = std::max(total.primal_linf_raw, item.residuals.primal_linf_raw);
    total.dual_violation_raw = std::max(total.dual_violation_raw, item.residuals.dual_violation_raw);
    total.stationarity_linf_raw = std::max(total.stationarity_linf_raw,
                                           item.residuals.stationarity_linf_raw);
    total.complementarity_linf_raw = std::max(total.complementarity_linf_raw,
                                              item.residuals.complementarity_linf_raw);
}

bool encode_axis_edge(const NormalContact& contact, Axis axis,
                      std::size_t& parent, std::size_t& child) noexcept {
    if (axis == Axis::X) {
        if (contact.normal.y != 0 || (contact.normal.x != kAxisOne && contact.normal.x != -kAxisOne)) {
            return false;
        }
        if (contact.normal.x == kAxisOne) {
            parent = contact.first; child = contact.second;
        } else {
            parent = contact.second; child = contact.first;
        }
        return true;
    }
    if (contact.normal.x != 0 || (contact.normal.y != kAxisOne && contact.normal.y != -kAxisOne)) {
        return false;
    }
    if (contact.normal.y == kAxisOne) {
        parent = contact.first; child = contact.second;
    } else {
        parent = contact.second; child = contact.first;
    }
    return true;
}

bool belongs_to_axis(const NormalContact& contact, Axis axis) noexcept {
    return axis == Axis::X ? contact.normal.y == 0 : contact.normal.x == 0;
}

bool solve_axis(
    Axis axis,
    std::span<Fixed::rep> working_x,
    std::span<Fixed::rep> working_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    AxisForestConfig config,
    AxisForestScratch& scratch,
    AxisForestStats& total) {
    const std::size_t bodies = working_x.size();
    std::iota(scratch.uf_parent_.begin(), scratch.uf_parent_.begin() + static_cast<std::ptrdiff_t>(bodies), 0U);
    std::fill(scratch.indegree_.begin(), scratch.indegree_.begin() + static_cast<std::ptrdiff_t>(bodies), 0U);
    std::fill(scratch.body_used_.begin(), scratch.body_used_.begin() + static_cast<std::ptrdiff_t>(bodies), 0U);
    std::fill(scratch.component_index_.begin(), scratch.component_index_.begin() + static_cast<std::ptrdiff_t>(bodies), kMissing);

    std::size_t edge_count = 0U;
    for (const NormalContact& contact : contacts) {
        if (!belongs_to_axis(contact, axis)) continue;
        std::size_t parent{}, child{};
        if (!encode_axis_edge(contact, axis, parent, child)
            || parent >= bodies || child >= bodies || parent == child) {
            return false;
        }
        scratch.edge_parent_[edge_count] = parent;
        scratch.edge_child_[edge_count] = child;
        ++edge_count;
        scratch.body_used_[parent] = 1U;
        scratch.body_used_[child] = 1U;
        if (++scratch.indegree_[child] > 1U) return false;
        if (!unite(scratch.uf_parent_, parent, child)) return false; // undirected cycle
    }
    if (axis == Axis::X) total.x_contacts = edge_count;
    else total.y_contacts = edge_count;
    if (edge_count == 0U) return true;

    std::size_t components = 0U;
    std::fill(scratch.body_counts_.begin(), scratch.body_counts_.end(), 0U);
    std::fill(scratch.edge_counts_.begin(), scratch.edge_counts_.end(), 0U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        if (scratch.body_used_[body] == 0U) continue;
        const std::size_t root = find_root(scratch.uf_parent_, body);
        if (scratch.component_index_[root] == kMissing) {
            scratch.component_index_[root] = components;
            scratch.component_roots_[components] = root;
            ++components;
        }
        ++scratch.body_counts_[scratch.component_index_[root]];
    }
    for (std::size_t edge = 0U; edge < edge_count; ++edge) {
        const std::size_t root = find_root(scratch.uf_parent_, scratch.edge_parent_[edge]);
        ++scratch.edge_counts_[scratch.component_index_[root]];
    }

    scratch.body_offsets_[0] = 0U;
    scratch.edge_offsets_[0] = 0U;
    for (std::size_t component = 0U; component < components; ++component) {
        scratch.body_offsets_[component + 1U] = scratch.body_offsets_[component] + scratch.body_counts_[component];
        scratch.edge_offsets_[component + 1U] = scratch.edge_offsets_[component] + scratch.edge_counts_[component];
        scratch.body_cursor_[component] = scratch.body_offsets_[component];
        scratch.edge_cursor_[component] = scratch.edge_offsets_[component];
    }
    for (std::size_t body = 0U; body < bodies; ++body) {
        if (scratch.body_used_[body] == 0U) continue;
        const std::size_t root = find_root(scratch.uf_parent_, body);
        const std::size_t component = scratch.component_index_[root];
        scratch.component_bodies_[scratch.body_cursor_[component]++] = body;
    }
    for (std::size_t edge = 0U; edge < edge_count; ++edge) {
        const std::size_t root = find_root(scratch.uf_parent_, scratch.edge_parent_[edge]);
        const std::size_t component = scratch.component_index_[root];
        scratch.component_edge_indices_[scratch.edge_cursor_[component]++] = edge;
    }

    const NormalQ30 normal = axis == Axis::X ? NormalQ30{kAxisOne, 0} : NormalQ30{0, kAxisOne};
    for (std::size_t component = 0U; component < components; ++component) {
        const std::size_t body_begin = scratch.body_offsets_[component];
        const std::size_t body_end = scratch.body_offsets_[component + 1U];
        const std::size_t local_bodies = body_end - body_begin;
        const std::size_t edge_begin = scratch.edge_offsets_[component];
        const std::size_t edge_end = scratch.edge_offsets_[component + 1U];
        if (edge_end - edge_begin + 1U != local_bodies) return false;

        for (std::size_t local = 0U; local < local_bodies; ++local) {
            const std::size_t global = scratch.component_bodies_[body_begin + local];
            scratch.local_index_[global] = local;
            scratch.local_x_[local] = working_x[global];
            scratch.local_y_[local] = working_y[global];
            scratch.local_masses_[local] = masses[global];
        }
        for (std::size_t local_edge = 0U; local_edge < edge_end - edge_begin; ++local_edge) {
            const std::size_t global_edge = scratch.component_edge_indices_[edge_begin + local_edge];
            scratch.local_edges_[local_edge] = DirectedTreeEdge{
                scratch.local_index_[scratch.edge_parent_[global_edge]],
                scratch.local_index_[scratch.edge_child_[global_edge]]};
        }
        const WeightedTreeStats result = project_weighted_tree_common_normal_inplace(
            std::span<Fixed::rep>(scratch.local_x_.data(), local_bodies),
            std::span<Fixed::rep>(scratch.local_y_.data(), local_bodies),
            std::span<const std::uint32_t>(scratch.local_masses_.data(), local_bodies),
            std::span<const DirectedTreeEdge>(scratch.local_edges_.data(), edge_end - edge_begin),
            normal, config.tree, scratch.tree_scratch_);
        merge_residuals(total, result);
        if (!result.residuals.certified) return false;
        for (std::size_t local = 0U; local < local_bodies; ++local) {
            const std::size_t global = scratch.component_bodies_[body_begin + local];
            working_x[global] = scratch.local_x_[local];
            working_y[global] = scratch.local_y_[local];
            scratch.local_index_[global] = kMissing;
        }
    }
    if (axis == Axis::X) total.x_components = components;
    else total.y_components = components;
    return true;
}
} // namespace

AxisForestScratch::AxisForestScratch(std::size_t maximum_bodies, std::size_t maximum_contacts)
    : maximum_bodies_(maximum_bodies), maximum_contacts_(maximum_contacts),
      working_x_(maximum_bodies), working_y_(maximum_bodies),
      local_x_(maximum_bodies), local_y_(maximum_bodies), local_masses_(maximum_bodies),
      uf_parent_(maximum_bodies), indegree_(maximum_bodies), component_index_(maximum_bodies),
      component_roots_(maximum_bodies), body_counts_(maximum_bodies), edge_counts_(maximum_bodies),
      body_offsets_(maximum_bodies + 1U), edge_offsets_(maximum_bodies + 1U),
      body_cursor_(maximum_bodies), edge_cursor_(maximum_bodies),
      component_bodies_(maximum_bodies), component_edge_indices_(maximum_contacts),
      local_index_(maximum_bodies, kMissing), edge_parent_(maximum_contacts), edge_child_(maximum_contacts),
      body_used_(maximum_bodies), local_edges_(maximum_contacts), tree_scratch_(maximum_bodies) {
    if (maximum_bodies == 0U || maximum_contacts == 0U) {
        throw std::invalid_argument("Axis forest scratch capacity must be non-zero");
    }
}

std::size_t AxisForestScratch::reserved_bytes() const noexcept {
    return (working_x_.capacity() + working_y_.capacity() + local_x_.capacity() + local_y_.capacity())
            * sizeof(Fixed::rep)
        + local_masses_.capacity() * sizeof(std::uint32_t)
        + (uf_parent_.capacity() + indegree_.capacity() + component_index_.capacity()
            + component_roots_.capacity() + body_counts_.capacity() + edge_counts_.capacity()
            + body_offsets_.capacity() + edge_offsets_.capacity() + body_cursor_.capacity()
            + edge_cursor_.capacity() + component_bodies_.capacity()
            + component_edge_indices_.capacity() + local_index_.capacity()
            + edge_parent_.capacity() + edge_child_.capacity()) * sizeof(std::size_t)
        + body_used_.capacity() * sizeof(std::uint8_t)
        + local_edges_.capacity() * sizeof(DirectedTreeEdge)
        + tree_scratch_.reserved_bytes();
}

AxisForestStats project_axis_forest_inplace(
    std::span<Fixed::rep> velocity_x,
    std::span<Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    AxisForestConfig config,
    AxisForestScratch& scratch) {
    AxisForestStats stats{};
    stats.contacts_processed = contacts.size();
    if (velocity_x.size() != velocity_y.size() || velocity_x.size() != masses.size()
        || velocity_x.empty() || velocity_x.size() > scratch.maximum_bodies_
        || contacts.size() > scratch.maximum_contacts_
        || std::any_of(masses.begin(), masses.end(), [](std::uint32_t mass) { return mass == 0U; })) {
        return stats;
    }
    for (const NormalContact& contact : contacts) {
        const bool x = contact.normal.y == 0
            && (contact.normal.x == kAxisOne || contact.normal.x == -kAxisOne);
        const bool y = contact.normal.x == 0
            && (contact.normal.y == kAxisOne || contact.normal.y == -kAxisOne);
        if ((!x && !y) || contact.first >= velocity_x.size() || contact.second >= velocity_x.size()
            || contact.first == contact.second) {
            return stats;
        }
    }
    std::copy(velocity_x.begin(), velocity_x.end(), scratch.working_x_.begin());
    std::copy(velocity_y.begin(), velocity_y.end(), scratch.working_y_.begin());
    const auto work_x = std::span<Fixed::rep>(scratch.working_x_.data(), velocity_x.size());
    const auto work_y = std::span<Fixed::rep>(scratch.working_y_.data(), velocity_y.size());
    stats.supported = true;
    if (!solve_axis(Axis::X, work_x, work_y, masses, contacts, config, scratch, stats)
        || !solve_axis(Axis::Y, work_x, work_y, masses, contacts, config, scratch, stats)) {
        stats.certified = false;
        return stats;
    }
    stats.certified = true;
    std::copy(work_x.begin(), work_x.end(), velocity_x.begin());
    std::copy(work_y.begin(), work_y.end(), velocity_y.begin());
    return stats;
}

} // namespace neoeng::core
