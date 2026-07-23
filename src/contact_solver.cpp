#include "neoeng/core/contact_solver.hpp"
#include "neoeng/core/chain_solver.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace neoeng::core {
namespace {

struct CellEntry final {
    std::int64_t x{};
    std::int64_t y{};
    std::size_t body{};

    auto operator<=>(const CellEntry&) const = default;
};

struct AxisInterval final {
    bool possible{true};
    std::optional<Fixed> entry{}; // nullopt = -infinity
    std::optional<Fixed> exit{};  // nullopt = +infinity
};

[[nodiscard]] Fixed fixed_from_wide(WideInteger value) {
    constexpr auto minimum = static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::min());
    constexpr auto maximum = static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::max());
    if (value < minimum || value > maximum) {
        throw std::overflow_error("Contact fixed-point range overflow");
    }
    return Fixed::from_raw(static_cast<Fixed::rep>(value));
}

[[nodiscard]] std::int64_t floor_div_raw(std::int64_t value, std::int64_t divisor) {
    if (divisor <= 0) throw std::invalid_argument("Contact grid cell size must be positive");
    std::int64_t quotient = value / divisor;
    const std::int64_t remainder = value % divisor;
    if (remainder != 0 && value < 0) --quotient;
    return quotient;
}

[[nodiscard]] WideInteger absolute(WideInteger value) noexcept {
    return value < 0 ? -value : value;
}

[[nodiscard]] bool overlaps_positions(
    Fixed ax, Fixed ay, Fixed bx, Fixed by, Fixed diameter) noexcept {
    return absolute(static_cast<WideInteger>(bx.raw()) - ax.raw())
            <= static_cast<WideInteger>(diameter.raw())
        && absolute(static_cast<WideInteger>(by.raw()) - ay.raw())
            <= static_cast<WideInteger>(diameter.raw());
}


[[nodiscard]] Fixed bounded_time_ratio(Fixed numerator, Fixed denominator) {
    if (denominator.raw() == 0) throw std::domain_error("Contact time division by zero");
    const WideInteger scaled = static_cast<WideInteger>(numerator.raw())
        * static_cast<WideInteger>(Fixed::scale);
    const WideInteger quotient = scaled / static_cast<WideInteger>(denominator.raw());
    const WideInteger lower = -static_cast<WideInteger>(Fixed::scale) * 2;
    const WideInteger upper = static_cast<WideInteger>(Fixed::scale) * 2;
    if (quotient < lower) return Fixed::from_raw(static_cast<Fixed::rep>(lower));
    if (quotient > upper) return Fixed::from_raw(static_cast<Fixed::rep>(upper));
    return Fixed::from_raw(static_cast<Fixed::rep>(quotient));
}

[[nodiscard]] AxisInterval interval_for_axis(
    Fixed relative_start,
    Fixed relative_delta,
    Fixed diameter) {
    if (relative_delta.raw() == 0) {
        if (absolute(relative_start.raw()) > static_cast<WideInteger>(diameter.raw())) {
            return AxisInterval{.possible = false};
        }
        return AxisInterval{};
    }
    Fixed first = bounded_time_ratio(-diameter - relative_start, relative_delta);
    Fixed second = bounded_time_ratio(diameter - relative_start, relative_delta);
    if (second < first) std::swap(first, second);
    return AxisInterval{.possible = true, .entry = first, .exit = second};
}

[[nodiscard]] ContactAxis minimum_penetration_axis(
    Fixed ax, Fixed ay, Fixed bx, Fixed by, Fixed diameter) noexcept {
    const WideInteger penetration_x = static_cast<WideInteger>(diameter.raw())
        - absolute(static_cast<WideInteger>(bx.raw()) - ax.raw());
    const WideInteger penetration_y = static_cast<WideInteger>(diameter.raw())
        - absolute(static_cast<WideInteger>(by.raw()) - ay.raw());
    return penetration_x <= penetration_y ? ContactAxis::X : ContactAxis::Y;
}

[[nodiscard]] std::optional<SweptContact> narrowphase_values(
    std::size_t first,
    std::size_t second,
    Fixed current_ax,
    Fixed current_ay,
    Fixed current_bx,
    Fixed current_by,
    Fixed predicted_ax,
    Fixed predicted_ay,
    Fixed predicted_bx,
    Fixed predicted_by,
    Fixed half_extent) {
    const Fixed diameter = fixed_from_wide(static_cast<WideInteger>(half_extent.raw()) * 2);
    const bool initial_overlap = overlaps_positions(
        current_ax, current_ay, current_bx, current_by, diameter);
    const bool final_overlap = overlaps_positions(
        predicted_ax, predicted_ay, predicted_bx, predicted_by, diameter);

    const Fixed relative_x = current_bx - current_ax;
    const Fixed relative_y = current_by - current_ay;
    const Fixed delta_x = (predicted_bx - current_bx) - (predicted_ax - current_ax);
    const Fixed delta_y = (predicted_by - current_by) - (predicted_ay - current_ay);
    const AxisInterval x = interval_for_axis(relative_x, delta_x, diameter);
    const AxisInterval y = interval_for_axis(relative_y, delta_y, diameter);
    if (!x.possible || !y.possible) return std::nullopt;

    const Fixed zero{};
    const Fixed one = Fixed::from_integer(1);
    Fixed lower = zero;
    Fixed upper = one;
    std::optional<Fixed> largest_entry;
    ContactAxis axis = ContactAxis::X;

    auto include = [&](const AxisInterval& interval, ContactAxis candidate_axis) {
        if (interval.entry.has_value()) {
            if (!largest_entry.has_value() || *interval.entry > *largest_entry) {
                largest_entry = interval.entry;
                axis = candidate_axis;
            }
            if (*interval.entry > lower) lower = *interval.entry;
        }
        if (interval.exit.has_value() && *interval.exit < upper) upper = *interval.exit;
    };
    include(x, ContactAxis::X);
    include(y, ContactAxis::Y);
    if (lower > upper || upper < zero || lower > one) return std::nullopt;
    if (lower < zero) lower = zero;

    if (initial_overlap) {
        axis = minimum_penetration_axis(
            current_ax, current_ay, current_bx, current_by, diameter);
    }
    return SweptContact{
        .first = first,
        .second = second,
        .axis = axis,
        .toi = lower,
        .initial_overlap = initial_overlap,
        .final_overlap = final_overlap,
    };
}

[[nodiscard]] std::optional<SweptContact> narrowphase_pair(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    std::size_t first,
    std::size_t second,
    Fixed half_extent) {
    return narrowphase_values(
        first, second,
        current.position_x_at(first), current.position_y_at(first),
        current.position_x_at(second), current.position_y_at(second),
        predicted.position_x_at(first), predicted.position_y_at(first),
        predicted.position_x_at(second), predicted.position_y_at(second),
        half_extent);
}

[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> all_pairs(std::size_t body_count) {
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    if (body_count > 1U) pairs.reserve(body_count * (body_count - 1U) / 2U);
    for (std::size_t first = 0U; first < body_count; ++first) {
        for (std::size_t second = first + 1U; second < body_count; ++second) {
            pairs.emplace_back(first, second);
        }
    }
    return pairs;
}

[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> swept_raster_candidates(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    ContactSolverConfig config,
    ContactSolverStats& stats) {
    if (config.cell_size.raw() <= 0 || config.half_extent.raw() < 0) {
        throw std::invalid_argument("Contact solver dimensions are invalid");
    }
    if (current.body_count() != predicted.body_count()) {
        throw std::invalid_argument("Swept broadphase state shape mismatch");
    }

    std::vector<CellEntry> entries;
    entries.reserve(current.body_count() * 4U);
    bool fallback = false;
    for (std::size_t index = 0U; index < current.body_count(); ++index) {
        ++stats.bodies_scanned;
        const WideInteger minimum_x = std::min<WideInteger>(
            current.position_x_at(index).raw(), predicted.position_x_at(index).raw())
            - config.half_extent.raw();
        const WideInteger maximum_x = std::max<WideInteger>(
            current.position_x_at(index).raw(), predicted.position_x_at(index).raw())
            + config.half_extent.raw();
        const WideInteger minimum_y = std::min<WideInteger>(
            current.position_y_at(index).raw(), predicted.position_y_at(index).raw())
            - config.half_extent.raw();
        const WideInteger maximum_y = std::max<WideInteger>(
            current.position_y_at(index).raw(), predicted.position_y_at(index).raw())
            + config.half_extent.raw();
        const Fixed min_x = fixed_from_wide(minimum_x);
        const Fixed max_x = fixed_from_wide(maximum_x);
        const Fixed min_y = fixed_from_wide(minimum_y);
        const Fixed max_y = fixed_from_wide(maximum_y);
        const std::int64_t cell_min_x = floor_div_raw(min_x.raw(), config.cell_size.raw());
        const std::int64_t cell_max_x = floor_div_raw(max_x.raw(), config.cell_size.raw());
        const std::int64_t cell_min_y = floor_div_raw(min_y.raw(), config.cell_size.raw());
        const std::int64_t cell_max_y = floor_div_raw(max_y.raw(), config.cell_size.raw());
        const WideInteger width = static_cast<WideInteger>(cell_max_x) - cell_min_x + 1;
        const WideInteger height = static_cast<WideInteger>(cell_max_y) - cell_min_y + 1;
        const WideInteger cell_count = width * height;
        if (cell_count <= 0
            || cell_count > static_cast<WideInteger>(config.max_cells_per_body)) {
            fallback = true;
            break;
        }
        for (std::int64_t y = cell_min_y; y <= cell_max_y; ++y) {
            for (std::int64_t x = cell_min_x; x <= cell_max_x; ++x) {
                entries.push_back(CellEntry{.x = x, .y = y, .body = index});
            }
        }
    }
    if (fallback) {
        ++stats.fallback_all_pairs;
        return all_pairs(current.body_count());
    }

    stats.cell_entries += entries.size();
    std::sort(entries.begin(), entries.end());
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    std::size_t begin = 0U;
    while (begin < entries.size()) {
        std::size_t end = begin + 1U;
        while (end < entries.size()
            && entries[end].x == entries[begin].x
            && entries[end].y == entries[begin].y) {
            ++end;
        }
        for (std::size_t lhs = begin; lhs < end; ++lhs) {
            for (std::size_t rhs = lhs + 1U; rhs < end; ++rhs) {
                const std::size_t first = std::min(entries[lhs].body, entries[rhs].body);
                const std::size_t second = std::max(entries[lhs].body, entries[rhs].body);
                if (first != second) pairs.emplace_back(first, second);
            }
        }
        begin = end;
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    stats.candidate_pairs += pairs.size();
    return pairs;
}


struct CellRun final {
    std::int64_t x{};
    std::int64_t y{};
    std::size_t begin{};
    std::size_t end{};

    auto operator<=>(const CellRun& other) const noexcept {
        if (const auto x_order = x <=> other.x; x_order != 0) return x_order;
        return y <=> other.y;
    }
};

[[nodiscard]] std::vector<std::pair<std::size_t, std::size_t>> center_grid_candidates(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    ContactSolverConfig config,
    ContactSolverStats& stats) {
    if (config.cell_size.raw() <= 0 || config.half_extent.raw() < 0) {
        throw std::invalid_argument("Contact solver dimensions are invalid");
    }
    const Fixed diameter = fixed_from_wide(
        static_cast<WideInteger>(config.half_extent.raw()) * 2);
    WideInteger maximum_delta_x = 0;
    WideInteger maximum_delta_y = 0;
    std::vector<CellEntry> centers;
    centers.reserve(current.body_count());
    for (std::size_t index = 0U; index < current.body_count(); ++index) {
        ++stats.bodies_scanned;
        const Fixed current_x = current.position_x_at(index);
        const Fixed current_y = current.position_y_at(index);
        const Fixed predicted_x = predicted.position_x_at(index);
        const Fixed predicted_y = predicted.position_y_at(index);
        maximum_delta_x = std::max(maximum_delta_x,
            absolute(static_cast<WideInteger>(predicted_x.raw()) - current_x.raw()));
        maximum_delta_y = std::max(maximum_delta_y,
            absolute(static_cast<WideInteger>(predicted_y.raw()) - current_y.raw()));
        centers.push_back(CellEntry{
            .x = floor_div_raw(current_x.raw(), config.cell_size.raw()),
            .y = floor_div_raw(current_y.raw(), config.cell_size.raw()),
            .body = index,
        });
    }
    stats.cell_entries += centers.size();
    std::sort(centers.begin(), centers.end());
    std::vector<CellRun> runs;
    runs.reserve(centers.size());
    std::size_t begin = 0U;
    while (begin < centers.size()) {
        std::size_t end = begin + 1U;
        while (end < centers.size()
            && centers[end].x == centers[begin].x
            && centers[end].y == centers[begin].y) {
            ++end;
        }
        runs.push_back(CellRun{
            .x = centers[begin].x,
            .y = centers[begin].y,
            .begin = begin,
            .end = end,
        });
        begin = end;
    }

    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    for (std::size_t first = 0U; first < current.body_count(); ++first) {
        const WideInteger minimum_x = std::min<WideInteger>(
            current.position_x_at(first).raw(), predicted.position_x_at(first).raw())
            - diameter.raw() - maximum_delta_x;
        const WideInteger maximum_x = std::max<WideInteger>(
            current.position_x_at(first).raw(), predicted.position_x_at(first).raw())
            + diameter.raw() + maximum_delta_x;
        const WideInteger minimum_y = std::min<WideInteger>(
            current.position_y_at(first).raw(), predicted.position_y_at(first).raw())
            - diameter.raw() - maximum_delta_y;
        const WideInteger maximum_y = std::max<WideInteger>(
            current.position_y_at(first).raw(), predicted.position_y_at(first).raw())
            + diameter.raw() + maximum_delta_y;
        const std::int64_t min_cell_x = floor_div_raw(
            fixed_from_wide(minimum_x).raw(), config.cell_size.raw());
        const std::int64_t max_cell_x = floor_div_raw(
            fixed_from_wide(maximum_x).raw(), config.cell_size.raw());
        const std::int64_t min_cell_y = floor_div_raw(
            fixed_from_wide(minimum_y).raw(), config.cell_size.raw());
        const std::int64_t max_cell_y = floor_div_raw(
            fixed_from_wide(maximum_y).raw(), config.cell_size.raw());
        const WideInteger cell_count =
            (static_cast<WideInteger>(max_cell_x) - min_cell_x + 1)
            * (static_cast<WideInteger>(max_cell_y) - min_cell_y + 1);
        if (cell_count <= 0
            || cell_count > static_cast<WideInteger>(config.max_cells_per_body)) {
            ++stats.fallback_all_pairs;
            return all_pairs(current.body_count());
        }
        for (std::int64_t y = min_cell_y; y <= max_cell_y; ++y) {
            for (std::int64_t x = min_cell_x; x <= max_cell_x; ++x) {
                const CellRun key{.x = x, .y = y};
                const auto run = std::lower_bound(runs.begin(), runs.end(), key);
                if (run == runs.end() || run->x != x || run->y != y) continue;
                for (std::size_t cursor = run->begin; cursor < run->end; ++cursor) {
                    const std::size_t second = centers[cursor].body;
                    if (second > first) pairs.emplace_back(first, second);
                }
            }
        }
    }
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
    stats.candidate_pairs += pairs.size();
    return pairs;
}

[[nodiscard]] Fixed average_fixed(Fixed first, Fixed second) {
    const WideInteger sum = static_cast<WideInteger>(first.raw()) + second.raw();
    return fixed_from_wide(sum / 2);
}

[[nodiscard]] Fixed interpolate(Fixed first, Fixed second, Fixed time) {
    return first + (second - first) * time;
}

[[nodiscard]] int normal_sign(const Body& first, const Body& second, ContactAxis axis) noexcept {
    const Fixed relative = axis == ContactAxis::X
        ? second.position.x - first.position.x
        : second.position.y - first.position.y;
    if (relative.raw() > 0) return 1;
    if (relative.raw() < 0) return -1;
    return first.id < second.id ? 1 : -1;
}

[[nodiscard]] Fixed normal_velocity(const Body& body, ContactAxis axis) noexcept {
    return axis == ContactAxis::X ? body.velocity.x : body.velocity.y;
}

void set_normal_velocity(Body& body, ContactAxis axis, Fixed value) noexcept {
    if (axis == ContactAxis::X) body.velocity.x = value;
    else body.velocity.y = value;
}

[[nodiscard]] bool resolve_velocity(
    Body& first,
    Body& second,
    ContactAxis axis,
    int sign) {
    const Fixed first_velocity = normal_velocity(first, axis);
    const Fixed second_velocity = normal_velocity(second, axis);
    const WideInteger relative = static_cast<WideInteger>(second_velocity.raw())
        - first_velocity.raw();
    if (relative * sign >= 0) return false;
    const Fixed average = average_fixed(first_velocity, second_velocity);
    set_normal_velocity(first, axis, average);
    set_normal_velocity(second, axis, average);
    return true;
}

void resolve_swept_crossing(
    const Body& start_first,
    const Body& start_second,
    Body& first,
    Body& second,
    const SweptContact& contact,
    ContactSolverStats& stats) {
    const Fixed one = Fixed::from_integer(1);
    Body impact_first = first;
    Body impact_second = second;
    impact_first.position.x = interpolate(start_first.position.x, first.position.x, contact.toi);
    impact_first.position.y = interpolate(start_first.position.y, first.position.y, contact.toi);
    impact_second.position.x = interpolate(start_second.position.x, second.position.x, contact.toi);
    impact_second.position.y = interpolate(start_second.position.y, second.position.y, contact.toi);
    const int sign = normal_sign(impact_first, impact_second, contact.axis);
    if (resolve_velocity(impact_first, impact_second, contact.axis, sign)) {
        ++stats.velocity_resolutions;
    }
    const Fixed remaining = one - contact.toi;
    impact_first.position.x += impact_first.velocity.x * kSimulationDelta * remaining;
    impact_first.position.y += impact_first.velocity.y * kSimulationDelta * remaining;
    impact_second.position.x += impact_second.velocity.x * kSimulationDelta * remaining;
    impact_second.position.y += impact_second.velocity.y * kSimulationDelta * remaining;
    first = impact_first;
    second = impact_second;
}

[[nodiscard]] bool project_overlap(
    Body& first,
    Body& second,
    Fixed diameter,
    ContactAxis preferred_axis,
    ContactSolverStats& stats) {
    if (!overlaps_positions(first.position.x, first.position.y,
            second.position.x, second.position.y, diameter)) {
        return false;
    }
    const WideInteger dx = static_cast<WideInteger>(second.position.x.raw())
        - first.position.x.raw();
    const WideInteger dy = static_cast<WideInteger>(second.position.y.raw())
        - first.position.y.raw();
    const WideInteger penetration_x = static_cast<WideInteger>(diameter.raw()) - absolute(dx);
    const WideInteger penetration_y = static_cast<WideInteger>(diameter.raw()) - absolute(dy);
    ContactAxis axis = penetration_x < penetration_y ? ContactAxis::X
        : penetration_y < penetration_x ? ContactAxis::Y : preferred_axis;
    const WideInteger penetration = axis == ContactAxis::X ? penetration_x : penetration_y;
    const int sign = normal_sign(first, second, axis);
    if (resolve_velocity(first, second, axis, sign)) ++stats.velocity_resolutions;
    if (penetration <= 0) return true;

    const WideInteger first_correction = penetration / 2;
    const WideInteger second_correction = penetration - first_correction;
    if (axis == ContactAxis::X) {
        first.position.x = fixed_from_wide(
            static_cast<WideInteger>(first.position.x.raw()) - sign * first_correction);
        second.position.x = fixed_from_wide(
            static_cast<WideInteger>(second.position.x.raw()) + sign * second_correction);
    } else {
        first.position.y = fixed_from_wide(
            static_cast<WideInteger>(first.position.y.raw()) - sign * first_correction);
        second.position.y = fixed_from_wide(
            static_cast<WideInteger>(second.position.y.raw()) + sign * second_correction);
    }
    ++stats.position_projections;
    return true;
}

class DisjointSet final {
public:
    explicit DisjointSet(std::size_t size) : parent_(size), rank_(size, 0U) {
        std::iota(parent_.begin(), parent_.end(), 0U);
    }
    [[nodiscard]] std::size_t find(std::size_t value) {
        while (parent_[value] != value) {
            parent_[value] = parent_[parent_[value]];
            value = parent_[value];
        }
        return value;
    }
    void unite(std::size_t first, std::size_t second) {
        first = find(first);
        second = find(second);
        if (first == second) return;
        if (rank_[first] < rank_[second]
            || (rank_[first] == rank_[second] && second < first)) {
            std::swap(first, second);
        }
        parent_[second] = first;
        if (rank_[first] == rank_[second]) ++rank_[first];
    }
private:
    std::vector<std::size_t> parent_{};
    std::vector<std::uint8_t> rank_{};
};

void merge_dirty(DirtySet& destination, const DirtySet& source) {
    source.for_each_dirty([&](std::size_t index, std::uint8_t mask) {
        destination.mark(index, static_cast<DirtyComponent>(mask));
    });
}

} // namespace

std::vector<SweptContact> swept_aabb_contacts_for_pairs(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    std::span<const BroadphasePair> pairs,
    Fixed half_extent,
    ContactSolverStats* stats_output) {
    if (current.frame() + 1U != predicted.frame()) {
        throw std::invalid_argument("Swept contact states must be consecutive");
    }
    if (current.body_count() != predicted.body_count()) {
        throw std::invalid_argument("Swept contact states must have the same shape");
    }
    ContactSolverStats local;
    local.candidate_pairs = pairs.size();
    std::vector<SweptContact> contacts;
    contacts.reserve(pairs.size());
    BroadphasePair previous{};
    bool has_previous = false;
    for (const BroadphasePair& pair : pairs) {
        if (pair.first >= pair.second || pair.second >= current.body_count()) {
            throw std::invalid_argument("Candidate contact pair is invalid");
        }
        if (has_previous && pair < previous) {
            throw std::invalid_argument("Candidate contact pairs must be canonical");
        }
        previous = pair;
        has_previous = true;
        ++local.narrowphase_tests;
        const auto contact = narrowphase_pair(
            current, predicted, pair.first, pair.second, half_extent);
        if (!contact.has_value()) continue;
        contacts.push_back(*contact);
        ++local.swept_hits;
        if (contact->initial_overlap) ++local.initial_overlaps;
        if (contact->final_overlap) ++local.final_overlaps;
    }
    if (stats_output != nullptr) *stats_output = local;
    return contacts;
}

std::vector<SweptContact> swept_aabb_contacts(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    ContactSolverConfig config,
    ContactSolverStats* stats_output) {
    if (current.frame() + 1U != predicted.frame()) {
        throw std::invalid_argument("Swept contact states must be consecutive");
    }
    ContactSolverStats local;
    const auto raw_candidates = config.broadphase_mode == ContactBroadphaseMode::SweptCellRaster
        ? swept_raster_candidates(current, predicted, config, local)
        : center_grid_candidates(current, predicted, config, local);
    std::vector<BroadphasePair> candidates;
    candidates.reserve(raw_candidates.size());
    for (const auto& [first, second] : raw_candidates) {
        candidates.push_back(BroadphasePair{.first = first, .second = second});
    }
    ContactSolverStats narrow;
    std::vector<SweptContact> contacts = swept_aabb_contacts_for_pairs(
        current, predicted, candidates, config.half_extent, &narrow);
    local.narrowphase_tests += narrow.narrowphase_tests;
    local.swept_hits += narrow.swept_hits;
    local.initial_overlaps += narrow.initial_overlaps;
    local.final_overlaps += narrow.final_overlaps;
    if (stats_output != nullptr) *stats_output = local;
    return contacts;
}

std::vector<SweptContact> brute_force_swept_aabb_contacts(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    Fixed half_extent) {
    std::vector<SweptContact> contacts;
    for (std::size_t first = 0U; first < current.body_count(); ++first) {
        for (std::size_t second = first + 1U; second < current.body_count(); ++second) {
            const auto contact = narrowphase_pair(current, predicted, first, second, half_extent);
            if (contact.has_value()) contacts.push_back(*contact);
        }
    }
    return contacts;
}

namespace {

[[nodiscard]] int normal_sign_components(
    const ComponentWorldState& state,
    std::size_t first,
    std::size_t second,
    ContactAxis axis) {
    const Fixed relative = axis == ContactAxis::X
        ? state.position_x_at(second) - state.position_x_at(first)
        : state.position_y_at(second) - state.position_y_at(first);
    if (relative.raw() > 0) return 1;
    if (relative.raw() < 0) return -1;
    return state.entity_at(first) < state.entity_at(second) ? 1 : -1;
}

[[nodiscard]] bool contact_drives_island(
    const ComponentWorldState& current,
    const ComponentWorldState& predicted,
    const SweptContact& contact,
    Fixed diameter) {
    if (!contact.initial_overlap && !contact.final_overlap) return true;
    const Fixed first_position = contact.axis == ContactAxis::X
        ? predicted.position_x_at(contact.first) : predicted.position_y_at(contact.first);
    const Fixed second_position = contact.axis == ContactAxis::X
        ? predicted.position_x_at(contact.second) : predicted.position_y_at(contact.second);
    const WideInteger separation = absolute(
        static_cast<WideInteger>(second_position.raw()) - first_position.raw());
    if (separation < static_cast<WideInteger>(diameter.raw())) return true;

    const Fixed first_velocity = contact.axis == ContactAxis::X
        ? predicted.velocity_x_at(contact.first) : predicted.velocity_y_at(contact.first);
    const Fixed second_velocity = contact.axis == ContactAxis::X
        ? predicted.velocity_x_at(contact.second) : predicted.velocity_y_at(contact.second);
    const WideInteger relative_velocity = static_cast<WideInteger>(second_velocity.raw())
        - first_velocity.raw();
    const int sign = normal_sign_components(current, contact.first, contact.second, contact.axis);
    return relative_velocity * sign < 0;
}

struct ContactColoring final {
    std::vector<std::vector<std::size_t>> colors{};
};

[[nodiscard]] ContactColoring color_contacts(
    std::span<const SweptContact> contacts,
    std::size_t body_count) {
    ContactColoring result;
    std::vector<std::vector<std::uint8_t>> occupied;
    constexpr std::size_t maximum_dense_colors = 64U;
    for (std::size_t index = 0U; index < contacts.size(); ++index) {
        const SweptContact& contact = contacts[index];
        std::size_t color = 0U;
        while (color < occupied.size()
            && (occupied[color][contact.first] != 0U
                || occupied[color][contact.second] != 0U)) {
            ++color;
        }
        if (color == occupied.size()) {
            if (occupied.size() >= maximum_dense_colors) {
                // The deterministic fallback keeps the remaining constraints in canonical order.
                result.colors.push_back({});
                auto& fallback = result.colors.back();
                fallback.reserve(contacts.size() - index);
                for (std::size_t remaining = index; remaining < contacts.size(); ++remaining) {
                    fallback.push_back(remaining);
                }
                return result;
            }
            occupied.emplace_back(body_count, 0U);
            result.colors.emplace_back();
        }
        occupied[color][contact.first] = 1U;
        occupied[color][contact.second] = 1U;
        result.colors[color].push_back(index);
    }
    return result;
}


void persist_general_manifold(
    std::uint64_t frame,
    std::span<const SweptContact> contacts,
    const PersistentManifoldState* previous,
    PersistentManifoldState& next,
    ContactSolverStats* stats) {
    next.frame = frame;
    next.contacts.assign(contacts.begin(), contacts.end());
    next.points.clear();
    next.points.reserve(contacts.size());
    next.chain_warm_start.reset();
    auto key_less = [](const ContactManifoldPoint& point,
                       const std::tuple<std::size_t, std::size_t, ContactAxis>& key) {
        return std::tuple{point.first, point.second, point.axis} < key;
    };
    for (const SweptContact& contact : contacts) {
        const std::size_t first = std::min(contact.first, contact.second);
        const std::size_t second = std::max(contact.first, contact.second);
        Fixed impulse{};
        bool reused = false;
        if (previous != nullptr) {
            const auto key = std::tuple{first, second, contact.axis};
            const auto found = std::lower_bound(
                previous->points.begin(), previous->points.end(), key, key_less);
            if (found != previous->points.end()
                && found->first == first && found->second == second
                && found->axis == contact.axis) {
                impulse = found->accumulated_normal_impulse;
                reused = true;
            }
        }
        next.points.push_back(ContactManifoldPoint{
            .first = first,
            .second = second,
            .axis = contact.axis,
            .accumulated_normal_impulse = impulse,
        });
        if (stats != nullptr) {
            if (reused) ++stats->manifold_points_reused;
            else ++stats->manifold_points_created;
        }
    }
    std::sort(next.points.begin(), next.points.end(), [](const auto& lhs, const auto& rhs) {
        return std::tuple{lhs.first, lhs.second, lhs.axis}
             < std::tuple{rhs.first, rhs.second, rhs.axis};
    });
}

[[nodiscard]] bool contacts_form_matching(
    std::span<const SweptContact> contacts,
    std::size_t body_count) {
    std::vector<std::uint8_t> used(body_count, 0U);
    for (const SweptContact& contact : contacts) {
        if (used[contact.first] != 0U || used[contact.second] != 0U) return false;
        used[contact.first] = 1U;
        used[contact.second] = 1U;
    }
    return true;
}

[[nodiscard]] ContactStepResult solve_matching_contacts(
    const ComponentWorldState& current,
    ComponentStepResult integrated,
    std::vector<SweptContact> contacts,
    ContactSolverConfig config,
    ContactSolverStats stats) {
    const Fixed diameter = fixed_from_wide(
        static_cast<WideInteger>(config.half_extent.raw()) * 2);
    std::vector<ComponentPatch> patches;
    patches.reserve(contacts.size() * 2U);
    std::vector<std::size_t> involved;
    involved.reserve(contacts.size() * 2U);
    std::vector<std::size_t> still_active;
    still_active.reserve(contacts.size() * 2U);
    std::size_t driving = 0U;

    auto append_patch = [&](std::size_t index,
                            Fixed before_px, Fixed before_py,
                            Fixed before_vx, Fixed before_vy,
                            Fixed after_px, Fixed after_py,
                            Fixed after_vx, Fixed after_vy) {
        std::uint8_t mask = 0U;
        if (after_px != before_px) mask |= component_mask(DirtyComponent::PositionX);
        if (after_py != before_py) mask |= component_mask(DirtyComponent::PositionY);
        if (after_vx != before_vx) mask |= component_mask(DirtyComponent::VelocityX);
        if (after_vy != before_vy) mask |= component_mask(DirtyComponent::VelocityY);
        if (mask != 0U) {
            patches.push_back(ComponentPatch{
                .index = index,
                .position_x = after_px,
                .position_y = after_py,
                .velocity_x = after_vx,
                .velocity_y = after_vy,
                .mask = mask,
            });
        }
        if (after_vx.raw() != 0 || after_vy.raw() != 0) still_active.push_back(index);
    };

    for (const SweptContact& contact : contacts) {
        const std::size_t first_index = contact.first;
        const std::size_t second_index = contact.second;
        Fixed first_px = integrated.state.position_x_at(first_index);
        Fixed first_py = integrated.state.position_y_at(first_index);
        Fixed first_vx = integrated.state.velocity_x_at(first_index);
        Fixed first_vy = integrated.state.velocity_y_at(first_index);
        Fixed second_px = integrated.state.position_x_at(second_index);
        Fixed second_py = integrated.state.position_y_at(second_index);
        Fixed second_vx = integrated.state.velocity_x_at(second_index);
        Fixed second_vy = integrated.state.velocity_y_at(second_index);
        const Fixed before_first_px = first_px;
        const Fixed before_first_py = first_py;
        const Fixed before_first_vx = first_vx;
        const Fixed before_first_vy = first_vy;
        const Fixed before_second_px = second_px;
        const Fixed before_second_py = second_py;
        const Fixed before_second_vx = second_vx;
        const Fixed before_second_vy = second_vy;

        bool is_driving = false;
        if (!contact.initial_overlap && !contact.final_overlap) {
            is_driving = true;
            Body start_first = current.body_at(first_index);
            Body start_second = current.body_at(second_index);
            Body first{
                .id = integrated.state.entity_at(first_index),
                .position = {first_px, first_py},
                .velocity = {first_vx, first_vy},
            };
            Body second{
                .id = integrated.state.entity_at(second_index),
                .position = {second_px, second_py},
                .velocity = {second_vx, second_vy},
            };
            resolve_swept_crossing(
                start_first, start_second, first, second, contact, stats);
            first_px = first.position.x;
            first_py = first.position.y;
            first_vx = first.velocity.x;
            first_vy = first.velocity.y;
            second_px = second.position.x;
            second_py = second.position.y;
            second_vx = second.velocity.x;
            second_vy = second.velocity.y;
        } else if (contact.final_overlap) {
            const Fixed first_axis_position = contact.axis == ContactAxis::X ? first_px : first_py;
            const Fixed second_axis_position = contact.axis == ContactAxis::X ? second_px : second_py;
            const WideInteger relative_position = static_cast<WideInteger>(second_axis_position.raw())
                - first_axis_position.raw();
            const WideInteger separation = absolute(relative_position);
            const WideInteger penetration = static_cast<WideInteger>(diameter.raw()) - separation;
            int sign = relative_position > 0 ? 1 : relative_position < 0 ? -1
                : integrated.state.entity_at(first_index) < integrated.state.entity_at(second_index)
                    ? 1 : -1;
            const Fixed first_axis_velocity = contact.axis == ContactAxis::X ? first_vx : first_vy;
            const Fixed second_axis_velocity = contact.axis == ContactAxis::X ? second_vx : second_vy;
            const WideInteger relative_velocity = static_cast<WideInteger>(second_axis_velocity.raw())
                - first_axis_velocity.raw();
            is_driving = penetration > 0 || relative_velocity * sign < 0;
            if (is_driving && relative_velocity * sign < 0) {
                const Fixed average = average_fixed(first_axis_velocity, second_axis_velocity);
                if (contact.axis == ContactAxis::X) {
                    first_vx = average;
                    second_vx = average;
                } else {
                    first_vy = average;
                    second_vy = average;
                }
                ++stats.velocity_resolutions;
            }
            if (is_driving && penetration > 0) {
                const WideInteger first_correction = penetration / 2;
                const WideInteger second_correction = penetration - first_correction;
                if (contact.axis == ContactAxis::X) {
                    first_px = fixed_from_wide(
                        static_cast<WideInteger>(first_px.raw()) - sign * first_correction);
                    second_px = fixed_from_wide(
                        static_cast<WideInteger>(second_px.raw()) + sign * second_correction);
                } else {
                    first_py = fixed_from_wide(
                        static_cast<WideInteger>(first_py.raw()) - sign * first_correction);
                    second_py = fixed_from_wide(
                        static_cast<WideInteger>(second_py.raw()) + sign * second_correction);
                }
                ++stats.position_projections;
            }
        } else {
            is_driving = contact_drives_island(
                current, integrated.state, contact, diameter);
        }

        if (!is_driving) continue;
        ++driving;
        involved.push_back(first_index);
        involved.push_back(second_index);
        append_patch(first_index,
            before_first_px, before_first_py, before_first_vx, before_first_vy,
            first_px, first_py, first_vx, first_vy);
        append_patch(second_index,
            before_second_px, before_second_py, before_second_vx, before_second_vy,
            second_px, second_py, second_vx, second_vy);
    }

    stats.constraint_islands = driving;
    stats.resting_islands_skipped = contacts.size() - driving;
    stats.graph_colors = driving == 0U ? 0U : 1U;
    if (driving == 0U) {
        return ContactStepResult{
            .state = std::move(integrated.state),
            .active = std::move(integrated.active),
            .dirty = std::move(integrated.dirty),
            .contacts = std::move(contacts),
            .stats = stats,
        };
    }

    std::sort(patches.begin(), patches.end(), [](const ComponentPatch& lhs, const ComponentPatch& rhs) {
        return lhs.index < rhs.index;
    });
    DirtySet dirty(current.body_count());
    merge_dirty(dirty, integrated.dirty);
    for (const ComponentPatch& patch : patches) {
        dirty.mark(patch.index, static_cast<DirtyComponent>(patch.mask));
    }
    ComponentWorldState solved = apply_component_patches(
        integrated.state, patches, &stats.solver_allocation);

    std::sort(involved.begin(), involved.end());
    std::vector<std::uint8_t> involved_mask(current.body_count(), 0U);
    for (const std::size_t index : involved) involved_mask[index] = 1U;
    std::vector<std::size_t> next_active;
    next_active.reserve(integrated.active.size());
    for (const std::size_t index : integrated.active.indices()) {
        if (involved_mask[index] == 0U) next_active.push_back(index);
    }
    next_active.insert(next_active.end(), still_active.begin(), still_active.end());
    std::sort(next_active.begin(), next_active.end());
    next_active.erase(std::unique(next_active.begin(), next_active.end()), next_active.end());

    return ContactStepResult{
        .state = std::move(solved),
        .active = DeterministicActiveSet(std::move(next_active)),
        .dirty = std::move(dirty),
        .contacts = std::move(contacts),
        .stats = stats,
    };
}

} // namespace

std::optional<ContactStepResult> step_component_contacts_matching_fused(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const BroadphasePair> pairs,
    ContactSolverConfig config) {
    if (pairs.empty()) return std::nullopt;
    std::vector<std::uint8_t> matching_mask(current.body_count(), 0U);
    BroadphasePair previous{};
    bool has_previous = false;
    for (const BroadphasePair& pair : pairs) {
        if (pair.first >= pair.second || pair.second >= current.body_count()) {
            throw std::invalid_argument("Fused contact pair is invalid");
        }
        if (has_previous && pair < previous) {
            throw std::invalid_argument("Fused contact pairs must be canonical");
        }
        previous = pair;
        has_previous = true;
        if (matching_mask[pair.first] != 0U || matching_mask[pair.second] != 0U) {
            return std::nullopt;
        }
        matching_mask[pair.first] = 1U;
        matching_mask[pair.second] = 1U;
    }

    std::vector<std::uint8_t> active_mask(current.body_count(), 0U);
    for (const std::size_t index : active.indices()) {
        if (index >= current.body_count()) {
            throw std::invalid_argument("Fused active set references a body outside world");
        }
        active_mask[index] = 1U;
    }
    const std::vector<std::uint8_t>& paired_mask = matching_mask;

    ContactSolverStats stats;
    stats.candidate_pairs = pairs.size();
    stats.narrowphase_tests = pairs.size();
    stats.integration_allocation.candidate_bodies_scanned = active.size();
    const Fixed diameter = fixed_from_wide(
        static_cast<WideInteger>(config.half_extent.raw()) * 2);
    std::vector<ComponentPatch> patches;
    patches.reserve(active.size() + pairs.size());
    std::vector<std::size_t> next_active;
    next_active.reserve(active.size() + pairs.size());
    std::vector<SweptContact> contacts;
    contacts.reserve(pairs.size());
    std::size_t driving = 0U;

    auto append_patch = [&](std::size_t index,
                            Fixed before_px, Fixed before_py,
                            Fixed before_vx, Fixed before_vy,
                            Fixed after_px, Fixed after_py,
                            Fixed after_vx, Fixed after_vy) {
        std::uint8_t mask = 0U;
        if (after_px != before_px) mask |= component_mask(DirtyComponent::PositionX);
        if (after_py != before_py) mask |= component_mask(DirtyComponent::PositionY);
        if (after_vx != before_vx) mask |= component_mask(DirtyComponent::VelocityX);
        if (after_vy != before_vy) mask |= component_mask(DirtyComponent::VelocityY);
        if (mask != 0U) {
            patches.push_back(ComponentPatch{
                .index = index,
                .position_x = after_px,
                .position_y = after_py,
                .velocity_x = after_vx,
                .velocity_y = after_vy,
                .mask = mask,
            });
        }
        if (after_vx.raw() != 0 || after_vy.raw() != 0) next_active.push_back(index);
    };

    for (const BroadphasePair& pair : pairs) {
        const std::size_t first_index = pair.first;
        const std::size_t second_index = pair.second;
        const Fixed current_first_px = current.position_x_at(first_index);
        const Fixed current_first_py = current.position_y_at(first_index);
        const Fixed current_first_vx = current.velocity_x_at(first_index);
        const Fixed current_first_vy = current.velocity_y_at(first_index);
        const Fixed current_second_px = current.position_x_at(second_index);
        const Fixed current_second_py = current.position_y_at(second_index);
        const Fixed current_second_vx = current.velocity_x_at(second_index);
        const Fixed current_second_vy = current.velocity_y_at(second_index);

        Fixed first_px = current_first_px;
        Fixed first_py = current_first_py;
        Fixed first_vx = current_first_vx;
        Fixed first_vy = current_first_vy;
        Fixed second_px = current_second_px;
        Fixed second_py = current_second_py;
        Fixed second_vx = current_second_vx;
        Fixed second_vy = current_second_vy;
        if (active_mask[first_index] != 0U) {
            first_px += first_vx * kSimulationDelta;
            first_py += first_vy * kSimulationDelta;
        }
        if (active_mask[second_index] != 0U) {
            second_px += second_vx * kSimulationDelta;
            second_py += second_vy * kSimulationDelta;
        }
        stats.integration_allocation.fixed_kernel.lanes += 4U;
        stats.integration_allocation.fixed_kernel.scalar_lanes += 4U;

        const auto contact = narrowphase_values(
            first_index, second_index,
            current_first_px, current_first_py,
            current_second_px, current_second_py,
            first_px, first_py, second_px, second_py,
            config.half_extent);
        if (contact.has_value()) {
            contacts.push_back(*contact);
            ++stats.swept_hits;
            if (contact->initial_overlap) ++stats.initial_overlaps;
            if (contact->final_overlap) ++stats.final_overlaps;
            bool is_driving = false;
            if (!contact->initial_overlap && !contact->final_overlap) {
                is_driving = true;
                Body start_first{
                    .id = current.entity_at(first_index),
                    .position = {current_first_px, current_first_py},
                    .velocity = {current_first_vx, current_first_vy},
                };
                Body start_second{
                    .id = current.entity_at(second_index),
                    .position = {current_second_px, current_second_py},
                    .velocity = {current_second_vx, current_second_vy},
                };
                Body predicted_first{
                    .id = start_first.id,
                    .position = {first_px, first_py},
                    .velocity = {first_vx, first_vy},
                };
                Body predicted_second{
                    .id = start_second.id,
                    .position = {second_px, second_py},
                    .velocity = {second_vx, second_vy},
                };
                resolve_swept_crossing(start_first, start_second,
                    predicted_first, predicted_second, *contact, stats);
                first_px = predicted_first.position.x;
                first_py = predicted_first.position.y;
                first_vx = predicted_first.velocity.x;
                first_vy = predicted_first.velocity.y;
                second_px = predicted_second.position.x;
                second_py = predicted_second.position.y;
                second_vx = predicted_second.velocity.x;
                second_vy = predicted_second.velocity.y;
            } else if (contact->final_overlap) {
                const Fixed first_axis_position = contact->axis == ContactAxis::X ? first_px : first_py;
                const Fixed second_axis_position = contact->axis == ContactAxis::X ? second_px : second_py;
                const WideInteger relative_position = static_cast<WideInteger>(second_axis_position.raw())
                    - first_axis_position.raw();
                const WideInteger separation = absolute(relative_position);
                const WideInteger penetration = static_cast<WideInteger>(diameter.raw()) - separation;
                const int sign = relative_position > 0 ? 1 : relative_position < 0 ? -1
                    : current.entity_at(first_index) < current.entity_at(second_index) ? 1 : -1;
                const Fixed first_axis_velocity = contact->axis == ContactAxis::X ? first_vx : first_vy;
                const Fixed second_axis_velocity = contact->axis == ContactAxis::X ? second_vx : second_vy;
                const WideInteger relative_velocity = static_cast<WideInteger>(second_axis_velocity.raw())
                    - first_axis_velocity.raw();
                is_driving = penetration > 0 || relative_velocity * sign < 0;
                if (is_driving && relative_velocity * sign < 0) {
                    const Fixed average = average_fixed(first_axis_velocity, second_axis_velocity);
                    if (contact->axis == ContactAxis::X) {
                        first_vx = average;
                        second_vx = average;
                    } else {
                        first_vy = average;
                        second_vy = average;
                    }
                    ++stats.velocity_resolutions;
                }
                if (is_driving && penetration > 0) {
                    const WideInteger first_correction = penetration / 2;
                    const WideInteger second_correction = penetration - first_correction;
                    if (contact->axis == ContactAxis::X) {
                        first_px = fixed_from_wide(
                            static_cast<WideInteger>(first_px.raw()) - sign * first_correction);
                        second_px = fixed_from_wide(
                            static_cast<WideInteger>(second_px.raw()) + sign * second_correction);
                    } else {
                        first_py = fixed_from_wide(
                            static_cast<WideInteger>(first_py.raw()) - sign * first_correction);
                        second_py = fixed_from_wide(
                            static_cast<WideInteger>(second_py.raw()) + sign * second_correction);
                    }
                    ++stats.position_projections;
                }
            }
            if (is_driving) ++driving;
        }

        append_patch(first_index,
            current_first_px, current_first_py, current_first_vx, current_first_vy,
            first_px, first_py, first_vx, first_vy);
        append_patch(second_index,
            current_second_px, current_second_py, current_second_vx, current_second_vy,
            second_px, second_py, second_vx, second_vy);
    }

    for (const std::size_t index : active.indices()) {
        if (paired_mask[index] != 0U) continue;
        const Fixed before_px = current.position_x_at(index);
        const Fixed before_py = current.position_y_at(index);
        const Fixed before_vx = current.velocity_x_at(index);
        const Fixed before_vy = current.velocity_y_at(index);
        const Fixed after_px = before_px + before_vx * kSimulationDelta;
        const Fixed after_py = before_py + before_vy * kSimulationDelta;
        stats.integration_allocation.fixed_kernel.lanes += 2U;
        stats.integration_allocation.fixed_kernel.scalar_lanes += 2U;
        append_patch(index, before_px, before_py, before_vx, before_vy,
            after_px, after_py, before_vx, before_vy);
    }

    const auto patch_less = [](const ComponentPatch& lhs, const ComponentPatch& rhs) {
        return lhs.index < rhs.index;
    };
    if (!std::is_sorted(patches.begin(), patches.end(), patch_less)) {
        std::sort(patches.begin(), patches.end(), patch_less);
    }
    for (std::size_t index = 1U; index < patches.size(); ++index) {
        if (patches[index - 1U].index == patches[index].index) {
            throw std::logic_error("Fused matching produced duplicate component patches");
        }
    }
    DirtySet dirty(current.body_count());
    for (const ComponentPatch& patch : patches) {
        dirty.mark(patch.index, static_cast<DirtyComponent>(patch.mask));
    }
    ComponentAllocationStats allocation;
    ComponentWorldState solved = apply_component_patches_next_frame(
        current, patches, &allocation);
    stats.solver_allocation += allocation;
    stats.constraint_islands = driving;
    stats.resting_islands_skipped = contacts.size() - driving;
    stats.graph_colors = driving == 0U ? 0U : 1U;
    std::sort(next_active.begin(), next_active.end());
    next_active.erase(std::unique(next_active.begin(), next_active.end()), next_active.end());
    return ContactStepResult{
        .state = std::move(solved),
        .active = DeterministicActiveSet(std::move(next_active)),
        .dirty = std::move(dirty),
        .contacts = std::move(contacts),
        .stats = stats,
    };
}

ContactStepResult solve_component_contact_constraints(
    const ComponentWorldState& current,
    ComponentStepResult integrated,
    std::vector<SweptContact> contacts,
    ContactSolverConfig config,
    ContactSolverStats broadphase_stats,
    const PersistentManifoldState* previous_manifold,
    PersistentManifoldState* next_manifold) {
    if (config.position_iterations == 0U) {
        throw std::invalid_argument("Contact solver requires at least one position iteration");
    }
    ContactSolverStats stats = broadphase_stats;
    stats.integration_allocation += integrated.allocation;
    if (contacts.empty()) {
        if (next_manifold != nullptr) {
            *next_manifold = PersistentManifoldState{
                .frame = integrated.state.frame(),
                .contacts = {},
            };
        }
        return ContactStepResult{
            .state = std::move(integrated.state),
            .active = std::move(integrated.active),
            .dirty = std::move(integrated.dirty),
            .contacts = {},
            .stats = stats,
        };
    }
    if (contacts_form_matching(contacts, current.body_count())) {
        ContactStepResult matching = solve_matching_contacts(
            current, std::move(integrated), std::move(contacts), config, stats);
        if (next_manifold != nullptr) {
            persist_general_manifold(
                matching.state.frame(), matching.contacts, previous_manifold,
                *next_manifold, &matching.stats);
        }
        return matching;
    }
    if (config.connected_solver_mode != ConnectedContactSolverMode::GeneralColored) {
        auto chain = solve_chain_contacts_isotonic(
            current, integrated, contacts, config, stats,
            previous_manifold, next_manifold);
        if (chain.has_value()) return std::move(*chain);
    }

    std::vector<std::size_t> all_involved;
    all_involved.reserve(contacts.size() * 2U);
    for (const SweptContact& contact : contacts) {
        all_involved.push_back(contact.first);
        all_involved.push_back(contact.second);
    }
    std::sort(all_involved.begin(), all_involved.end());
    all_involved.erase(std::unique(all_involved.begin(), all_involved.end()), all_involved.end());

    constexpr std::size_t missing = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> all_local(current.body_count(), missing);
    for (std::size_t local = 0U; local < all_involved.size(); ++local) {
        all_local[all_involved[local]] = local;
    }
    DisjointSet all_disjoint(all_involved.size());
    for (const SweptContact& contact : contacts) {
        all_disjoint.unite(all_local[contact.first], all_local[contact.second]);
    }

    const Fixed diameter = fixed_from_wide(
        static_cast<WideInteger>(config.half_extent.raw()) * 2);
    std::vector<std::size_t> driving_roots;
    for (const SweptContact& contact : contacts) {
        if (!contact_drives_island(current, integrated.state, contact, diameter)) continue;
        driving_roots.push_back(all_disjoint.find(all_local[contact.first]));
    }
    std::sort(driving_roots.begin(), driving_roots.end());
    driving_roots.erase(std::unique(driving_roots.begin(), driving_roots.end()), driving_roots.end());

    std::vector<std::size_t> all_roots;
    all_roots.reserve(all_involved.size());
    for (std::size_t local = 0U; local < all_involved.size(); ++local) {
        all_roots.push_back(all_disjoint.find(local));
    }
    std::sort(all_roots.begin(), all_roots.end());
    all_roots.erase(std::unique(all_roots.begin(), all_roots.end()), all_roots.end());
    stats.resting_islands_skipped = all_roots.size() - driving_roots.size();
    stats.constraint_islands = driving_roots.size();

    if (driving_roots.empty()) {
        if (next_manifold != nullptr) {
            persist_general_manifold(
                integrated.state.frame(), contacts, previous_manifold,
                *next_manifold, &stats);
        }
        return ContactStepResult{
            .state = std::move(integrated.state),
            .active = std::move(integrated.active),
            .dirty = std::move(integrated.dirty),
            .contacts = std::move(contacts),
            .stats = stats,
        };
    }

    std::vector<SweptContact> active_contacts;
    active_contacts.reserve(contacts.size());
    for (const SweptContact& contact : contacts) {
        const std::size_t root = all_disjoint.find(all_local[contact.first]);
        if (std::binary_search(driving_roots.begin(), driving_roots.end(), root)) {
            active_contacts.push_back(contact);
        }
    }

    std::vector<std::size_t> involved;
    involved.reserve(active_contacts.size() * 2U);
    for (const SweptContact& contact : active_contacts) {
        involved.push_back(contact.first);
        involved.push_back(contact.second);
    }
    std::sort(involved.begin(), involved.end());
    involved.erase(std::unique(involved.begin(), involved.end()), involved.end());

    std::vector<std::size_t> local_index(current.body_count(), missing);
    std::vector<Body> mutable_bodies;
    std::vector<Body> start_bodies;
    mutable_bodies.reserve(involved.size());
    start_bodies.reserve(involved.size());
    for (std::size_t local = 0U; local < involved.size(); ++local) {
        local_index[involved[local]] = local;
        start_bodies.push_back(current.body_at(involved[local]));
        mutable_bodies.push_back(integrated.state.body_at(involved[local]));
    }

    const ContactColoring coloring = color_contacts(active_contacts, current.body_count());
    stats.graph_colors = coloring.colors.size();

    for (const auto& color : coloring.colors) {
        for (const std::size_t contact_index : color) {
            const SweptContact& contact = active_contacts[contact_index];
            if (contact.initial_overlap || contact.final_overlap) continue;
            const std::size_t first = local_index[contact.first];
            const std::size_t second = local_index[contact.second];
            resolve_swept_crossing(start_bodies[first], start_bodies[second],
                mutable_bodies[first], mutable_bodies[second], contact, stats);
        }
    }

    for (std::size_t iteration = 0U; iteration < config.position_iterations; ++iteration) {
        for (const auto& color : coloring.colors) {
            for (const std::size_t contact_index : color) {
                const SweptContact& contact = active_contacts[contact_index];
                static_cast<void>(project_overlap(
                    mutable_bodies[local_index[contact.first]],
                    mutable_bodies[local_index[contact.second]],
                    diameter, contact.axis, stats));
            }
        }
    }

    std::vector<ComponentPatch> patches;
    patches.reserve(involved.size());
    DirtySet dirty(current.body_count());
    merge_dirty(dirty, integrated.dirty);
    for (std::size_t local = 0U; local < involved.size(); ++local) {
        const std::size_t index = involved[local];
        const Body before = integrated.state.body_at(index);
        const Body& after = mutable_bodies[local];
        std::uint8_t mask = 0U;
        if (after.position.x != before.position.x) mask |= component_mask(DirtyComponent::PositionX);
        if (after.position.y != before.position.y) mask |= component_mask(DirtyComponent::PositionY);
        if (after.velocity.x != before.velocity.x) mask |= component_mask(DirtyComponent::VelocityX);
        if (after.velocity.y != before.velocity.y) mask |= component_mask(DirtyComponent::VelocityY);
        if (mask == 0U) continue;
        patches.push_back(ComponentPatch{
            .index = index,
            .position_x = after.position.x,
            .position_y = after.position.y,
            .velocity_x = after.velocity.x,
            .velocity_y = after.velocity.y,
            .mask = mask,
        });
        dirty.mark(index, static_cast<DirtyComponent>(mask));
    }
    ComponentWorldState solved = apply_component_patches(
        integrated.state, patches, &stats.solver_allocation);

    std::vector<bool> involved_mask(current.body_count(), false);
    for (const std::size_t index : involved) involved_mask[index] = true;
    std::vector<std::size_t> next_active;
    next_active.reserve(integrated.active.size() + involved.size());
    for (const std::size_t index : integrated.active.indices()) {
        if (!involved_mask[index]) next_active.push_back(index);
    }
    for (std::size_t local = 0U; local < involved.size(); ++local) {
        const Body& body = mutable_bodies[local];
        if (body.velocity.x.raw() != 0 || body.velocity.y.raw() != 0) {
            next_active.push_back(involved[local]);
        }
    }
    std::sort(next_active.begin(), next_active.end());
    next_active.erase(std::unique(next_active.begin(), next_active.end()), next_active.end());

    if (next_manifold != nullptr) {
        persist_general_manifold(
            solved.frame(), contacts, previous_manifold, *next_manifold, &stats);
    }
    return ContactStepResult{
        .state = std::move(solved),
        .active = DeterministicActiveSet(std::move(next_active)),
        .dirty = std::move(dirty),
        .contacts = std::move(contacts),
        .stats = stats,
    };
}

ContactStepResult step_component_contacts(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    std::span<const InputCommand> inputs,
    ContactSolverConfig config,
    ComponentStepOptions options,
    const PersistentManifoldState* previous_manifold,
    PersistentManifoldState* next_manifold) {
    ComponentStepResult integrated = step_component_active(current, active, inputs, options);
    ContactSolverStats stats;
    if (active.empty() && inputs.empty()) {
        stats.integration_allocation = integrated.allocation;
        if (next_manifold != nullptr) {
            *next_manifold = PersistentManifoldState{
                .frame = integrated.state.frame(),
                .contacts = {},
            };
        }
        return ContactStepResult{
            .state = std::move(integrated.state),
            .active = std::move(integrated.active),
            .dirty = std::move(integrated.dirty),
            .contacts = {},
            .stats = stats,
        };
    }
    std::vector<SweptContact> contacts = swept_aabb_contacts(
        current, integrated.state, config, &stats);
    return solve_component_contact_constraints(
        current, std::move(integrated), std::move(contacts), config, stats,
        previous_manifold, next_manifold);
}

} // namespace neoeng::core
