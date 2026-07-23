#include "neoeng/core/atomic_temporal_physics.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <optional>
#include <stdexcept>

namespace neoeng::core {
namespace {
constexpr std::uint64_t kEmptyFrame = ~std::uint64_t{0};

[[nodiscard]] Fixed::rep narrow_raw(WideInteger value) {
    constexpr WideInteger minimum = static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::min());
    constexpr WideInteger maximum = static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::max());
    if (value < minimum || value > maximum) {
        throw std::overflow_error("Atomic temporal fixed-point range overflow");
    }
    return static_cast<Fixed::rep>(value);
}

[[nodiscard]] Fixed::rep integrate_raw(Fixed::rep position, Fixed::rep velocity) {
    const WideInteger displacement = static_cast<WideInteger>(velocity)
        * static_cast<WideInteger>(kSimulationDelta.raw())
        / static_cast<WideInteger>(Fixed::scale);
    return narrow_raw(static_cast<WideInteger>(position) + displacement);
}

[[nodiscard]] Fixed::rep future_raw(Fixed::rep position, Fixed::rep velocity, std::size_t frames) {
    const WideInteger displacement = static_cast<WideInteger>(velocity)
        * static_cast<WideInteger>(kSimulationDelta.raw())
        / static_cast<WideInteger>(Fixed::scale)
        * static_cast<WideInteger>(frames);
    return narrow_raw(static_cast<WideInteger>(position) + displacement);
}

[[nodiscard]] FatAabb make_bound_range(
    Fixed::rep position_x,
    Fixed::rep position_y,
    Fixed::rep minimum_velocity_x,
    Fixed::rep maximum_velocity_x,
    Fixed::rep minimum_velocity_y,
    Fixed::rep maximum_velocity_y,
    Fixed half_extent,
    std::size_t horizon_frames) {
    const Fixed::rep future_min_x = future_raw(position_x, minimum_velocity_x, horizon_frames);
    const Fixed::rep future_max_x = future_raw(position_x, maximum_velocity_x, horizon_frames);
    const Fixed::rep future_min_y = future_raw(position_y, minimum_velocity_y, horizon_frames);
    const Fixed::rep future_max_y = future_raw(position_y, maximum_velocity_y, horizon_frames);
    const WideInteger half = half_extent.raw();
    return FatAabb{
        .minimum_x = Fixed::from_raw(narrow_raw(std::min<WideInteger>(position_x, future_min_x) - half)),
        .maximum_x = Fixed::from_raw(narrow_raw(std::max<WideInteger>(position_x, future_max_x) + half)),
        .minimum_y = Fixed::from_raw(narrow_raw(std::min<WideInteger>(position_y, future_min_y) - half)),
        .maximum_y = Fixed::from_raw(narrow_raw(std::max<WideInteger>(position_y, future_max_y) + half)),
    };
}

[[nodiscard]] FatAabb make_transition_bound(
    Fixed::rep current_x,
    Fixed::rep current_y,
    Fixed::rep predicted_x,
    Fixed::rep predicted_y,
    Fixed::rep projected_velocity_x,
    Fixed::rep projected_velocity_y,
    Fixed half_extent,
    std::size_t remaining_frames) {
    const Fixed::rep horizon_x = future_raw(predicted_x, projected_velocity_x, remaining_frames);
    const Fixed::rep horizon_y = future_raw(predicted_y, projected_velocity_y, remaining_frames);
    const WideInteger half = half_extent.raw();
    return FatAabb{
        .minimum_x = Fixed::from_raw(narrow_raw(
            std::min({static_cast<WideInteger>(current_x), static_cast<WideInteger>(predicted_x),
                      static_cast<WideInteger>(horizon_x)}) - half)),
        .maximum_x = Fixed::from_raw(narrow_raw(
            std::max({static_cast<WideInteger>(current_x), static_cast<WideInteger>(predicted_x),
                      static_cast<WideInteger>(horizon_x)}) + half)),
        .minimum_y = Fixed::from_raw(narrow_raw(
            std::min({static_cast<WideInteger>(current_y), static_cast<WideInteger>(predicted_y),
                      static_cast<WideInteger>(horizon_y)}) - half)),
        .maximum_y = Fixed::from_raw(narrow_raw(
            std::max({static_cast<WideInteger>(current_y), static_cast<WideInteger>(predicted_y),
                      static_cast<WideInteger>(horizon_y)}) + half)),
    };
}

[[nodiscard]] bool contains(const FatAabb& outer, const FatAabb& inner) noexcept {
    return inner.minimum_x >= outer.minimum_x && inner.maximum_x <= outer.maximum_x
        && inner.minimum_y >= outer.minimum_y && inner.maximum_y <= outer.maximum_y;
}

[[nodiscard]] bool overlaps(const FatAabb& first, const FatAabb& second) noexcept {
    return first.minimum_x <= second.maximum_x && second.minimum_x <= first.maximum_x
        && first.minimum_y <= second.maximum_y && second.minimum_y <= first.maximum_y;
}

struct AxisInterval final {
    bool possible{true};
    std::optional<Fixed::rep> entry{};
    std::optional<Fixed::rep> exit{};
};

[[nodiscard]] WideInteger absolute(WideInteger value) noexcept {
    return value < 0 ? -value : value;
}

[[nodiscard]] Fixed::rep bounded_ratio_raw(Fixed::rep numerator, Fixed::rep denominator) {
    if (denominator == 0) throw std::domain_error("Atomic temporal time division by zero");
    const WideInteger quotient = static_cast<WideInteger>(numerator)
        * static_cast<WideInteger>(Fixed::scale)
        / static_cast<WideInteger>(denominator);
    const WideInteger lower = -static_cast<WideInteger>(Fixed::scale) * 2;
    const WideInteger upper = static_cast<WideInteger>(Fixed::scale) * 2;
    return static_cast<Fixed::rep>(std::clamp(quotient, lower, upper));
}

[[nodiscard]] AxisInterval interval_for_axis(
    Fixed::rep relative_start,
    Fixed::rep relative_delta,
    Fixed::rep diameter) {
    if (relative_delta == 0) {
        if (absolute(relative_start) > static_cast<WideInteger>(diameter)) {
            return AxisInterval{.possible = false};
        }
        return AxisInterval{};
    }
    Fixed::rep first = bounded_ratio_raw(-diameter - relative_start, relative_delta);
    Fixed::rep second = bounded_ratio_raw(diameter - relative_start, relative_delta);
    if (second < first) std::swap(first, second);
    return AxisInterval{.possible = true, .entry = first, .exit = second};
}

[[nodiscard]] NormalContact canonicalize(NormalContact contact) {
    if (contact.first == contact.second) throw std::invalid_argument("Self contact is invalid");
    if (contact.second < contact.first) {
        std::swap(contact.first, contact.second);
        contact.normal.x = static_cast<std::int32_t>(-static_cast<std::int64_t>(contact.normal.x));
        contact.normal.y = static_cast<std::int32_t>(-static_cast<std::int64_t>(contact.normal.y));
    }
    if (contact.normal.x == 0 && contact.normal.y == 0) {
        throw std::invalid_argument("Contact normal cannot be zero");
    }
    return contact;
}

[[nodiscard]] bool violates_contact_velocity(
    const NormalContact& contact,
    std::span<const Fixed::rep> velocity_x,
    std::span<const Fixed::rep> velocity_y,
    std::uint64_t tolerance_raw) noexcept {
    const WideInteger relative_x = static_cast<WideInteger>(velocity_x[contact.first])
        - velocity_x[contact.second];
    const WideInteger relative_y = static_cast<WideInteger>(velocity_y[contact.first])
        - velocity_y[contact.second];
    const WideInteger projected_q30 = relative_x * contact.normal.x + relative_y * contact.normal.y;
    return projected_q30 > static_cast<WideInteger>(tolerance_raw) * (WideInteger{1} << 30U);
}

std::size_t island_find(std::vector<std::size_t>& parent, std::size_t value) {
    while (parent[value] != value) {
        parent[value] = parent[parent[value]];
        value = parent[value];
    }
    return value;
}

void island_unite(std::vector<std::size_t>& parent, std::size_t first, std::size_t second) {
    first = island_find(parent, first);
    second = island_find(parent, second);
    if (first == second) return;
    if (second < first) std::swap(first, second);
    parent[second] = first;
}

void fnv_mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

} // namespace

AtomicTemporalExternalState::AtomicTemporalExternalState(
    std::size_t bodies, std::size_t contacts, std::size_t maximum_pairs)
    : position_x(bodies), position_y(bodies), velocity_x(bodies), velocity_y(bodies),
      masses(bodies), dual(contacts), manifold(contacts), contact_stable(contacts),
      contact_candidate(contacts), fat_bounds(bodies), pairs(maximum_pairs) {}

AtomicTemporalPhysicsEngine::AtomicTemporalPhysicsEngine(AtomicTemporalPhysicsConfig config)
    : config_(config), position_x_(config.bodies), position_y_(config.bodies),
      velocity_x_(config.bodies), velocity_y_(config.bodies),
      predicted_x_(config.bodies), predicted_y_(config.bodies),
      guard_velocity_x_(config.bodies), guard_velocity_y_(config.bodies), masses_(config.bodies),
      dual_(config.contacts), manifold_(config.contacts), fat_bounds_(config.bodies),
      pair_cache_(config.maximum_candidate_pairs), broadphase_order_(config.bodies),
      contact_lookup_(config.contacts), body_to_contact_(config.bodies, -1),
      contact_stable_(config.contacts), contact_candidate_(config.contacts),
      contact_dirty_(config.contacts), contact_active_(config.contacts),
      body_check_mask_(config.bodies), body_check_indices_(config.bodies),
      position_changed_mask_(config.bodies), position_changed_indices_(config.bodies),
      active_contact_indices_(config.contacts),
      island_uf_(config.bodies), body_island_(config.bodies), island_offsets_(config.bodies + 1U),
      island_bodies_(config.bodies), island_counts_(config.bodies), island_cursors_(config.bodies),
      island_mark_(config.bodies), active_contacts_(config.contacts),
      snapshots_(config.disable_internal_history ? 1U : config.history_capacity),
      cache_slots_(config.disable_internal_history ? 1U : config.history_capacity * 2U + 1U),
      inputs_(config.history_capacity), scratch_(config.bodies, config.contacts),
      tree_scratch_(config.bodies), axis_forest_scratch_(config.bodies, config.contacts),
      tree_global_bodies_(config.bodies),
      tree_local_index_(config.bodies, std::numeric_limits<std::size_t>::max()),
      tree_velocity_x_(config.bodies), tree_velocity_y_(config.bodies),
      tree_masses_(config.bodies), tree_edges_(config.contacts),
      tree_contact_indices_(config.contacts) {
    if (config.bodies == 0U || config.history_capacity < 2U || config.horizon_frames == 0U
        || config.maximum_candidate_pairs < config.contacts || config.half_extent.raw() < 0) {
        throw std::invalid_argument("Atomic temporal configuration is invalid");
    }
    for (SnapshotSlot& slot : snapshots_) {
        if (!config.disable_internal_history) {
            slot.position_x.resize(config.bodies); slot.position_y.resize(config.bodies);
            slot.velocity_x.resize(config.bodies); slot.velocity_y.resize(config.bodies);
            slot.masses.resize(config.bodies); slot.dual.resize(config.contacts);
            slot.manifold.resize(config.contacts); slot.contact_stable.resize(config.contacts);
        }
        slot.frame = kEmptyFrame;
    }
    for (CacheSlot& slot : cache_slots_) {
        if (!config.disable_internal_history) {
            slot.fat_bounds.resize(config.bodies);
            slot.pairs.resize(config.maximum_candidate_pairs);
        }
    }
    for (InputSlot& slot : inputs_) {
        slot.velocity.resize(config.maximum_velocity_mutations);
        slot.mass.resize(config.maximum_mass_mutations);
        slot.contact.resize(config.maximum_contact_mutations);
        slot.frame = kEmptyFrame;
    }
}

void AtomicTemporalPhysicsEngine::initialize(
    std::span<const Fixed::rep> position_x,
    std::span<const Fixed::rep> position_y,
    std::span<const Fixed::rep> velocity_x,
    std::span<const Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts) {
    if (position_x.size() != config_.bodies || position_y.size() != config_.bodies
        || velocity_x.size() != config_.bodies || velocity_y.size() != config_.bodies
        || masses.size() != config_.bodies || contacts.size() != config_.contacts) {
        throw std::invalid_argument("Atomic temporal initialization shape mismatch");
    }
    std::copy(position_x.begin(), position_x.end(), position_x_.begin());
    std::copy(position_y.begin(), position_y.end(), position_y_.begin());
    std::copy(velocity_x.begin(), velocity_x.end(), velocity_x_.begin());
    std::copy(velocity_y.begin(), velocity_y.end(), velocity_y_.begin());
    std::copy(masses.begin(), masses.end(), masses_.begin());
    for (std::uint32_t mass : masses_) if (mass == 0U) throw std::invalid_argument("Mass must be positive");
    for (std::size_t i = 0U; i < contacts.size(); ++i) manifold_[i] = canonicalize(contacts[i]);
    std::fill(dual_.begin(), dual_.end(), 0);
    std::fill(contact_stable_.begin(), contact_stable_.end(), 0U);
    std::fill(contact_candidate_.begin(), contact_candidate_.end(), 0U);
    for (SnapshotSlot& slot : snapshots_) slot.frame = kEmptyFrame;
    for (InputSlot& slot : inputs_) {
        slot.frame = kEmptyFrame; slot.velocity_count = 0U; slot.mass_count = 0U; slot.contact_count = 0U;
    }
    frame_ = 0U; valid_until_frame_ = 0U; pair_count_ = 0U;
    current_cache_slot_ = 0U; current_cache_generation_ = 0U; next_cache_generation_ = 1U;
    for (CacheSlot& slot : cache_slots_) slot.generation = 0U;
    stats_ = {}; initialized_ = true; last_cache_rebuilt_ = false; last_topology_changed_ = false;
    rebuild_contact_lookup();
    rebuild_island_index();
    rebuild_temporal_cache();
    capture();
}

AtomicTemporalPhysicsEngine::SnapshotSlot& AtomicTemporalPhysicsEngine::snapshot_slot(std::uint64_t frame) noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}
const AtomicTemporalPhysicsEngine::SnapshotSlot& AtomicTemporalPhysicsEngine::snapshot_slot(std::uint64_t frame) const noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}
AtomicTemporalPhysicsEngine::InputSlot& AtomicTemporalPhysicsEngine::input_slot(std::uint64_t frame) noexcept {
    return inputs_[static_cast<std::size_t>(frame % inputs_.size())];
}
const AtomicTemporalPhysicsEngine::InputSlot* AtomicTemporalPhysicsEngine::find_input(std::uint64_t frame) const noexcept {
    const InputSlot& slot = inputs_[static_cast<std::size_t>(frame % inputs_.size())];
    return slot.frame == frame ? &slot : nullptr;
}

void AtomicTemporalPhysicsEngine::set_input(std::uint64_t frame, AtomicPhysicsFrameInput input) {
    if (!initialized_) throw std::logic_error("Atomic temporal physics is not initialized");
    if (input.velocity.size() > config_.maximum_velocity_mutations
        || input.mass.size() > config_.maximum_mass_mutations
        || input.contact.size() > config_.maximum_contact_mutations) {
        throw std::length_error("Atomic temporal input exceeds configured capacity");
    }
    InputSlot& slot = input_slot(frame);
    slot.frame = frame;
    slot.velocity_count = input.velocity.size();
    slot.mass_count = input.mass.size();
    slot.contact_count = input.contact.size();
    std::copy(input.velocity.begin(), input.velocity.end(), slot.velocity.begin());
    std::copy(input.mass.begin(), input.mass.end(), slot.mass.begin());
    std::copy(input.contact.begin(), input.contact.end(), slot.contact.begin());
}

void AtomicTemporalPhysicsEngine::rebuild_contact_lookup() {
    std::fill(body_to_contact_.begin(), body_to_contact_.end(), -1);
    matching_ = true;
    for (std::size_t contact = 0U; contact < manifold_.size(); ++contact) {
        const NormalContact& item = manifold_[contact];
        if (item.first >= config_.bodies || item.second >= config_.bodies) {
            throw std::out_of_range("Contact body index is outside atomic temporal state");
        }
        if (body_to_contact_[item.first] >= 0 || body_to_contact_[item.second] >= 0) matching_ = false;
        body_to_contact_[item.first] = static_cast<std::int64_t>(contact);
        body_to_contact_[item.second] = static_cast<std::int64_t>(contact);
        contact_lookup_[contact] = ContactLookup{
            .pair = BroadphasePair{.first = item.first, .second = item.second},
            .contact = contact,
        };
    }
    std::sort(contact_lookup_.begin(), contact_lookup_.end(), [](const ContactLookup& lhs, const ContactLookup& rhs) {
        if (lhs.pair != rhs.pair) return lhs.pair < rhs.pair;
        return lhs.contact < rhs.contact;
    });
    std::uint64_t signature = 0xCBF29CE484222325ULL;
    const auto mix_topology = [&signature](std::uint64_t value) {
        for (unsigned byte = 0U; byte < 8U; ++byte) {
            signature ^= (value >> (byte * 8U)) & 0xFFU;
            signature *= 0x100000001B3ULL;
        }
    };
    mix_topology(manifold_.size());
    for (const NormalContact& item : manifold_) {
        mix_topology(item.first); mix_topology(item.second);
        mix_topology(static_cast<std::uint32_t>(item.normal.x));
        mix_topology(static_cast<std::uint32_t>(item.normal.y));
    }
    topology_signature_ = signature;
}


void AtomicTemporalPhysicsEngine::rebuild_island_index() {
    std::iota(island_uf_.begin(), island_uf_.end(), 0U);
    for (const NormalContact& contact : manifold_) {
        island_unite(island_uf_, contact.first, contact.second);
    }
    const std::size_t missing = std::numeric_limits<std::size_t>::max();
    std::fill(body_island_.begin(), body_island_.end(), missing);
    std::fill(island_counts_.begin(), island_counts_.end(), 0U);
    island_count_ = 0U;
    for (std::size_t body = 0U; body < config_.bodies; ++body) {
        const std::size_t root = island_find(island_uf_, body);
        if (body_island_[root] == missing) body_island_[root] = island_count_++;
        body_island_[body] = body_island_[root];
        ++island_counts_[body_island_[body]];
    }
    island_offsets_[0] = 0U;
    for (std::size_t island = 0U; island < island_count_; ++island) {
        island_offsets_[island + 1U] = island_offsets_[island] + island_counts_[island];
        island_cursors_[island] = island_offsets_[island];
    }
    for (std::size_t body = 0U; body < config_.bodies; ++body) {
        const std::size_t island = body_island_[body];
        island_bodies_[island_cursors_[island]++] = body;
    }
    ++stats_.island_index_rebuilds;
    stats_.island_count_peak = std::max<std::uint64_t>(stats_.island_count_peak, island_count_);
}

void AtomicTemporalPhysicsEngine::expand_cache_checks_to_islands() {
    if (body_check_count_ == 0U) return;
    std::fill(island_mark_.begin(), island_mark_.begin() + static_cast<std::ptrdiff_t>(island_count_), 0U);
    const std::size_t seed_count = body_check_count_;
    for (std::size_t index = 0U; index < seed_count; ++index) {
        const std::size_t island = body_island_[body_check_indices_[index]];
        if (island_mark_[island] != 0U) continue;
        island_mark_[island] = 1U;
        ++stats_.islands_expanded;
        for (std::size_t cursor = island_offsets_[island]; cursor < island_offsets_[island + 1U]; ++cursor) {
            const std::size_t body = island_bodies_[cursor];
            if (body_check_mask_[body] != 0U) continue;
            body_check_mask_[body] = 1U;
            body_check_indices_[body_check_count_++] = body;
            ++stats_.island_bodies_added;
        }
    }
}

std::size_t AtomicTemporalPhysicsEngine::contact_for_pair(const BroadphasePair& pair) const noexcept {
    if (matching_) {
        const std::int64_t contact = body_to_contact_[pair.first];
        if (contact >= 0 && body_to_contact_[pair.second] == contact) return static_cast<std::size_t>(contact);
        return config_.contacts;
    }
    const auto iterator = std::lower_bound(contact_lookup_.begin(), contact_lookup_.end(), pair,
        [](const ContactLookup& item, const BroadphasePair& value) { return item.pair < value; });
    return iterator != contact_lookup_.end() && iterator->pair == pair
        ? iterator->contact : config_.contacts;
}

void AtomicTemporalPhysicsEngine::rebuild_candidate_map() {
    std::fill(contact_candidate_.begin(), contact_candidate_.end(), 0U);
    for (std::size_t index = 0U; index < pair_count_; ++index) {
        const std::size_t contact = contact_for_pair(pair_cache_[index]);
        if (contact != config_.contacts) contact_candidate_[contact] = 1U;
    }
}

void AtomicTemporalPhysicsEngine::rebuild_temporal_cache() {
    last_cache_rebuilt_ = true;
    std::copy(velocity_x_.begin(), velocity_x_.end(), guard_velocity_x_.begin());
    std::copy(velocity_y_.begin(), velocity_y_.end(), guard_velocity_y_.begin());
    if (matching_ && !manifold_.empty()) {
        static_cast<void>(project_verified_matching_inplace(
            guard_velocity_x_, guard_velocity_y_, masses_, manifold_, config_.projection,
            std::span<Fixed::rep>(scratch_.dual_impulses_.data(), manifold_.size())));
    } else if (!manifold_.empty()) {
        bool guard_projected = false;
        if (config_.enable_single_tree_solver) guard_projected = project_tree_guard_velocities();
        if (!guard_projected && config_.enable_axis_forest_solver) {
            const AxisForestStats axis = project_axis_forest_inplace(
                guard_velocity_x_, guard_velocity_y_, masses_, manifold_,
                AxisForestConfig{.tree = WeightedTreeConfig{
                    .maximum_active_set_iterations = 4096U,
                    .feasibility_tolerance_raw = config_.projection.feasibility_tolerance_raw,
                    .stationarity_tolerance_raw = 16U}},
                axis_forest_scratch_);
            guard_projected = axis.certified;
        }
        static_cast<void>(guard_projected);
    }
    for (std::size_t body = 0U; body < config_.bodies; ++body) {
        const Fixed::rep minimum_velocity_x = narrow_raw(
            static_cast<WideInteger>(std::min(velocity_x_[body], guard_velocity_x_[body]))
            - config_.velocity_mutation_guard_raw);
        const Fixed::rep maximum_velocity_x = narrow_raw(
            static_cast<WideInteger>(std::max(velocity_x_[body], guard_velocity_x_[body]))
            + config_.velocity_mutation_guard_raw);
        const Fixed::rep minimum_velocity_y = narrow_raw(
            static_cast<WideInteger>(std::min(velocity_y_[body], guard_velocity_y_[body]))
            - config_.velocity_mutation_guard_raw);
        const Fixed::rep maximum_velocity_y = narrow_raw(
            static_cast<WideInteger>(std::max(velocity_y_[body], guard_velocity_y_[body]))
            + config_.velocity_mutation_guard_raw);
        fat_bounds_[body] = make_bound_range(position_x_[body], position_y_[body],
            minimum_velocity_x, maximum_velocity_x, minimum_velocity_y, maximum_velocity_y,
            config_.half_extent, config_.horizon_frames);
        broadphase_order_[body] = body;
    }
    std::sort(broadphase_order_.begin(), broadphase_order_.end(), [&](std::size_t lhs, std::size_t rhs) {
        if (fat_bounds_[lhs].minimum_x != fat_bounds_[rhs].minimum_x) {
            return fat_bounds_[lhs].minimum_x < fat_bounds_[rhs].minimum_x;
        }
        return lhs < rhs;
    });
    pair_count_ = 0U;
    for (std::size_t left = 0U; left < broadphase_order_.size(); ++left) {
        const std::size_t first = broadphase_order_[left];
        for (std::size_t right = left + 1U; right < broadphase_order_.size(); ++right) {
            const std::size_t second = broadphase_order_[right];
            if (fat_bounds_[second].minimum_x > fat_bounds_[first].maximum_x) break;
            ++stats_.broadphase_pair_tests;
            if (!overlaps(fat_bounds_[first], fat_bounds_[second])) continue;
            if (pair_count_ == pair_cache_.size()) throw std::length_error("Atomic temporal pair cache capacity exceeded");
            pair_cache_[pair_count_++] = BroadphasePair{
                .first = std::min(first, second), .second = std::max(first, second)};
        }
    }
    std::sort(pair_cache_.begin(), pair_cache_.begin() + static_cast<std::ptrdiff_t>(pair_count_));
    const auto last = std::unique(pair_cache_.begin(), pair_cache_.begin() + static_cast<std::ptrdiff_t>(pair_count_));
    pair_count_ = static_cast<std::size_t>(std::distance(pair_cache_.begin(), last));
    rebuild_candidate_map();
    if (frame_ > std::numeric_limits<std::uint64_t>::max() - config_.horizon_frames) {
        throw std::overflow_error("Atomic temporal horizon would wrap frame counter");
    }
    valid_until_frame_ = frame_ + config_.horizon_frames;
    ++stats_.broadphase_builds;
    stats_.candidate_pairs_peak = std::max<std::uint64_t>(stats_.candidate_pairs_peak, pair_count_);
    persist_current_cache();
}

bool AtomicTemporalPhysicsEngine::project_tree_guard_velocities() {
    const std::size_t active_count = manifold_.size();
    if (active_count == 0U) return true;
    const NormalQ30 common = manifold_[0].normal;
    const std::size_t missing = std::numeric_limits<std::size_t>::max();
    std::size_t body_count = 0U;
    const auto local_of = [&](std::size_t global) -> std::size_t {
        std::size_t& local = tree_local_index_[global];
        if (local == missing) {
            local = body_count;
            tree_global_bodies_[body_count++] = global;
        }
        return local;
    };
    bool shape_valid = true;
    for (std::size_t edge = 0U; edge < active_count; ++edge) {
        const NormalContact& contact = manifold_[edge];
        if (contact.normal != common) { shape_valid = false; break; }
        tree_edges_[edge] = DirectedTreeEdge{.parent = local_of(contact.first), .child = local_of(contact.second)};
    }
    if (shape_valid) shape_valid = body_count == active_count + 1U;
    bool projected = false;
    if (shape_valid) {
        for (std::size_t local = 0U; local < body_count; ++local) {
            const std::size_t global = tree_global_bodies_[local];
            tree_velocity_x_[local] = guard_velocity_x_[global];
            tree_velocity_y_[local] = guard_velocity_y_[global];
            tree_masses_[local] = masses_[global];
        }
        WeightedTreeStats tree = project_weighted_chain_common_normal_inplace(
            std::span<Fixed::rep>(tree_velocity_x_.data(), body_count),
            std::span<Fixed::rep>(tree_velocity_y_.data(), body_count),
            std::span<const std::uint32_t>(tree_masses_.data(), body_count),
            std::span<const DirectedTreeEdge>(tree_edges_.data(), active_count),
            common, WeightedTreeConfig{}, tree_scratch_);
        if (!tree.residuals.certified) {
            std::fill(tree_scratch_.dual.begin(),
                tree_scratch_.dual.begin() + static_cast<std::ptrdiff_t>(active_count), 0);
            tree = project_weighted_tree_common_normal_inplace(
                std::span<Fixed::rep>(tree_velocity_x_.data(), body_count),
                std::span<Fixed::rep>(tree_velocity_y_.data(), body_count),
                std::span<const std::uint32_t>(tree_masses_.data(), body_count),
                std::span<const DirectedTreeEdge>(tree_edges_.data(), active_count),
                common, WeightedTreeConfig{}, tree_scratch_);
        }
        if (tree.residuals.certified) {
            for (std::size_t local = 0U; local < body_count; ++local) {
                const std::size_t global = tree_global_bodies_[local];
                guard_velocity_x_[global] = tree_velocity_x_[local];
                guard_velocity_y_[global] = tree_velocity_y_[local];
            }
            projected = true;
        }
    }
    for (std::size_t local = 0U; local < body_count; ++local) {
        tree_local_index_[tree_global_bodies_[local]] = missing;
    }
    return projected;
}

bool AtomicTemporalPhysicsEngine::try_project_single_tree(std::size_t active_count) {
    if (active_count == 0U) return true;
    ++stats_.tree_solver_calls;
    const NormalQ30 common = active_contacts_[0].normal;
    std::size_t body_count = 0U;
    const std::size_t missing = std::numeric_limits<std::size_t>::max();
    const auto local_of = [&](std::size_t global) -> std::size_t {
        std::size_t& local = tree_local_index_[global];
        if (local == missing) {
            local = body_count;
            tree_global_bodies_[body_count++] = global;
        }
        return local;
    };
    bool shape_valid = true;
    for (std::size_t edge = 0U; edge < active_count; ++edge) {
        const NormalContact& contact = active_contacts_[edge];
        if (contact.normal != common) { shape_valid = false; break; }
        tree_edges_[edge] = DirectedTreeEdge{
            .parent = local_of(contact.first), .child = local_of(contact.second)};
        tree_contact_indices_[edge] = active_contact_indices_[edge];
    }
    if (shape_valid) shape_valid = body_count == active_count + 1U;
    bool projected = false;
    if (shape_valid) {
        for (std::size_t local = 0U; local < body_count; ++local) {
            const std::size_t global = tree_global_bodies_[local];
            tree_velocity_x_[local] = velocity_x_[global];
            tree_velocity_y_[local] = velocity_y_[global];
            tree_masses_[local] = masses_[global];
        }
        for (std::size_t edge = 0U; edge < active_count; ++edge) {
            tree_scratch_.dual[edge] = dual_[tree_contact_indices_[edge]];
        }
        try {
            WeightedTreeStats tree = project_weighted_chain_common_normal_inplace(
                std::span<Fixed::rep>(tree_velocity_x_.data(), body_count),
                std::span<Fixed::rep>(tree_velocity_y_.data(), body_count),
                std::span<const std::uint32_t>(tree_masses_.data(), body_count),
                std::span<const DirectedTreeEdge>(tree_edges_.data(), active_count),
                common, WeightedTreeConfig{}, tree_scratch_);
            const bool chain_certified = tree.residuals.certified;
            if (!tree.residuals.certified) {
                tree = project_weighted_tree_common_normal_inplace(
                    std::span<Fixed::rep>(tree_velocity_x_.data(), body_count),
                    std::span<Fixed::rep>(tree_velocity_y_.data(), body_count),
                    std::span<const std::uint32_t>(tree_masses_.data(), body_count),
                    std::span<const DirectedTreeEdge>(tree_edges_.data(), active_count),
                    common, WeightedTreeConfig{.use_warm_start = true}, tree_scratch_);
            }
            if (tree.residuals.certified) {
                for (std::size_t local = 0U; local < body_count; ++local) {
                    const std::size_t global = tree_global_bodies_[local];
                    velocity_x_[global] = tree_velocity_x_[local];
                    velocity_y_[global] = tree_velocity_y_[local];
                }
                for (std::size_t edge = 0U; edge < active_count; ++edge) {
                    dual_[tree_contact_indices_[edge]] = tree_scratch_.dual[edge];
                }
                ++stats_.tree_solver_certified;
                if (chain_certified) ++stats_.tree_chain_certified;
                else ++stats_.tree_general_certified;
                projected = true;
            }
        } catch (const std::invalid_argument&) {
            projected = false;
        }
    }
    for (std::size_t local = 0U; local < body_count; ++local) {
        tree_local_index_[tree_global_bodies_[local]] = missing;
    }
    if (!projected) ++stats_.tree_solver_fallbacks;
    return projected;
}

std::size_t AtomicTemporalPhysicsEngine::acquire_cache_slot() const {
    for (std::size_t candidate = 0U; candidate < cache_slots_.size(); ++candidate) {
        bool referenced = false;
        for (const SnapshotSlot& snapshot : snapshots_) {
            if (snapshot.frame == kEmptyFrame || snapshot.cache_slot != candidate) continue;
            if (cache_slots_[candidate].generation == snapshot.cache_generation) {
                referenced = true;
                break;
            }
        }
        if (!referenced) return candidate;
    }
    throw std::length_error("Atomic temporal cache pool exhausted");
}

void AtomicTemporalPhysicsEngine::persist_current_cache() {
    if (config_.disable_internal_history) {
        current_cache_slot_ = 0U;
        current_cache_generation_ = next_cache_generation_++;
        return;
    }
    const std::size_t slot_index = acquire_cache_slot();
    CacheSlot& slot = cache_slots_[slot_index];
    slot.generation = next_cache_generation_++;
    slot.valid_until_frame = valid_until_frame_;
    slot.pair_count = pair_count_;
    std::copy(fat_bounds_.begin(), fat_bounds_.end(), slot.fat_bounds.begin());
    std::copy(pair_cache_.begin(), pair_cache_.begin() + static_cast<std::ptrdiff_t>(pair_count_),
              slot.pairs.begin());
    current_cache_slot_ = slot_index;
    current_cache_generation_ = slot.generation;
}

bool AtomicTemporalPhysicsEngine::cache_contains_transition_and_horizon(
    std::span<const std::size_t> bodies_to_check) {
    const std::size_t remaining = valid_until_frame_ > frame_ + 1U
        ? static_cast<std::size_t>(valid_until_frame_ - (frame_ + 1U)) : 0U;
    bool contained = true;
    for (const std::size_t body : bodies_to_check) {
        const FatAabb required = make_transition_bound(
            position_x_[body], position_y_[body], predicted_x_[body], predicted_y_[body],
            velocity_x_[body], velocity_y_[body], config_.half_extent, remaining);
        if (!contains(fat_bounds_[body], required)) {
            ++stats_.escaped_bodies;
            contained = false;
        }
    }
    return contained;
}

bool AtomicTemporalPhysicsEngine::narrowphase_pair(const BroadphasePair& pair) const {
    const Fixed::rep diameter = narrow_raw(static_cast<WideInteger>(config_.half_extent.raw()) * 2);
    const Fixed::rep relative_x = narrow_raw(static_cast<WideInteger>(position_x_[pair.second]) - position_x_[pair.first]);
    const Fixed::rep relative_y = narrow_raw(static_cast<WideInteger>(position_y_[pair.second]) - position_y_[pair.first]);
    if (absolute(relative_x) <= static_cast<WideInteger>(diameter)
        && absolute(relative_y) <= static_cast<WideInteger>(diameter)) {
        return true;
    }
    const Fixed::rep predicted_first_x = integrate_raw(position_x_[pair.first], velocity_x_[pair.first]);
    const Fixed::rep predicted_first_y = integrate_raw(position_y_[pair.first], velocity_y_[pair.first]);
    const Fixed::rep predicted_second_x = integrate_raw(position_x_[pair.second], velocity_x_[pair.second]);
    const Fixed::rep predicted_second_y = integrate_raw(position_y_[pair.second], velocity_y_[pair.second]);
    const Fixed::rep delta_x = narrow_raw(
        (static_cast<WideInteger>(predicted_second_x) - position_x_[pair.second])
        - (static_cast<WideInteger>(predicted_first_x) - position_x_[pair.first]));
    const Fixed::rep delta_y = narrow_raw(
        (static_cast<WideInteger>(predicted_second_y) - position_y_[pair.second])
        - (static_cast<WideInteger>(predicted_first_y) - position_y_[pair.first]));
    const AxisInterval x = interval_for_axis(relative_x, delta_x, diameter);
    const AxisInterval y = interval_for_axis(relative_y, delta_y, diameter);
    if (!x.possible || !y.possible) return false;
    Fixed::rep lower = 0;
    Fixed::rep upper = Fixed::scale;
    if (x.entry.has_value()) lower = std::max(lower, *x.entry);
    if (y.entry.has_value()) lower = std::max(lower, *y.entry);
    if (x.exit.has_value()) upper = std::min(upper, *x.exit);
    if (y.exit.has_value()) upper = std::min(upper, *y.exit);
    return lower <= upper && upper >= 0 && lower <= Fixed::scale;
}

void AtomicTemporalPhysicsEngine::capture() {
    if (config_.disable_internal_history) return;
    SnapshotSlot& slot = snapshot_slot(frame_);
    slot.frame = frame_; slot.cache_slot = current_cache_slot_;
    slot.cache_generation = current_cache_generation_;
    std::copy(position_x_.begin(), position_x_.end(), slot.position_x.begin());
    std::copy(position_y_.begin(), position_y_.end(), slot.position_y.begin());
    std::copy(velocity_x_.begin(), velocity_x_.end(), slot.velocity_x.begin());
    std::copy(velocity_y_.begin(), velocity_y_.end(), slot.velocity_y.begin());
    std::copy(masses_.begin(), masses_.end(), slot.masses.begin());
    std::copy(dual_.begin(), dual_.end(), slot.dual.begin());
    std::copy(manifold_.begin(), manifold_.end(), slot.manifold.begin());
    std::copy(contact_stable_.begin(), contact_stable_.end(), slot.contact_stable.begin());
    ++stats_.snapshots_captured;
    stats_.payload_values_copied += position_x_.size() * 4U + masses_.size() + dual_.size()
        + manifold_.size() + contact_stable_.size();
}

void AtomicTemporalPhysicsEngine::step_one() {
    const std::uint64_t next_frame = frame_ + 1U;
    last_cache_rebuilt_ = false;
    last_topology_changed_ = false;
    std::fill(contact_dirty_.begin(), contact_dirty_.end(), 0U);
    for (std::size_t index = 0U; index < body_check_count_; ++index) {
        body_check_mask_[body_check_indices_[index]] = 0U;
    }
    body_check_count_ = 0U;
    for (std::size_t index = 0U; index < position_changed_count_; ++index) {
        position_changed_mask_[position_changed_indices_[index]] = 0U;
    }
    position_changed_count_ = 0U;
    const auto mark_position_changed = [&](std::size_t body) {
        if (position_changed_mask_[body] != 0U) return;
        position_changed_mask_[body] = 1U;
        position_changed_indices_[position_changed_count_++] = body;
    };
    const auto mark_body_for_cache_check = [&](std::size_t body) {
        if (body_check_mask_[body] != 0U) return;
        body_check_mask_[body] = 1U;
        body_check_indices_[body_check_count_++] = body;
    };
    bool topology_changed = false;
    if (const InputSlot* input = find_input(next_frame)) {
        for (std::size_t i = 0U; i < input->velocity_count; ++i) {
            const VelocityMutation& mutation = input->velocity[i];
            if (mutation.body >= config_.bodies) throw std::out_of_range("Velocity mutation body is invalid");
            velocity_x_[mutation.body] = narrow_raw(static_cast<WideInteger>(velocity_x_[mutation.body]) + mutation.delta_x);
            velocity_y_[mutation.body] = narrow_raw(static_cast<WideInteger>(velocity_y_[mutation.body]) + mutation.delta_y);
            mark_body_for_cache_check(mutation.body);
            const std::int64_t contact = body_to_contact_[mutation.body];
            if (contact >= 0) contact_dirty_[static_cast<std::size_t>(contact)] = 1U;
        }
        for (std::size_t i = 0U; i < input->mass_count; ++i) {
            const MassMutation& mutation = input->mass[i];
            if (mutation.body >= config_.bodies || mutation.mass == 0U) throw std::out_of_range("Mass mutation is invalid");
            masses_[mutation.body] = mutation.mass;
            mark_body_for_cache_check(mutation.body);
            const std::int64_t contact = body_to_contact_[mutation.body];
            if (contact >= 0) contact_dirty_[static_cast<std::size_t>(contact)] = 1U;
        }
        for (std::size_t i = 0U; i < input->contact_count; ++i) {
            const ContactMutation& mutation = input->contact[i];
            if (mutation.contact >= config_.contacts) throw std::out_of_range("Contact mutation index is invalid");
            mark_body_for_cache_check(manifold_[mutation.contact].first);
            mark_body_for_cache_check(manifold_[mutation.contact].second);
            manifold_[mutation.contact] = canonicalize(mutation.value);
            mark_body_for_cache_check(manifold_[mutation.contact].first);
            mark_body_for_cache_check(manifold_[mutation.contact].second);
            contact_dirty_[mutation.contact] = 1U;
            contact_stable_[mutation.contact] = 0U;
            topology_changed = true;
        }
    }
    last_topology_changed_ = topology_changed;
    if (topology_changed) {
        rebuild_contact_lookup();
        rebuild_island_index();
        rebuild_candidate_map();
    }
    expand_cache_checks_to_islands();

    if (config_.force_rebuild_each_frame || next_frame > valid_until_frame_) {
        rebuild_temporal_cache();
    } else {
        ++stats_.broadphase_reuses;
    }

    std::fill(contact_active_.begin(), contact_active_.end(), 0U);
    std::size_t active_count = 0U;
    for (std::size_t contact = 0U; contact < config_.contacts; ++contact) {
        if (contact_candidate_[contact] == 0U) {
            if (contact_stable_[contact] != 0U) contact_dirty_[contact] = 1U;
            contact_stable_[contact] = 0U;
            continue;
        }
        const bool certified_reuse = contact_stable_[contact] != 0U && contact_dirty_[contact] == 0U;
        bool active = certified_reuse;
        if (!certified_reuse) {
            ++stats_.narrowphase_pair_tests;
            active = narrowphase_pair(BroadphasePair{
                .first = manifold_[contact].first, .second = manifold_[contact].second});
        }
        if (!active) {
            if (contact_stable_[contact] != 0U) contact_dirty_[contact] = 1U;
            contact_stable_[contact] = 0U;
            continue;
        }
        if (!certified_reuse) contact_dirty_[contact] = 1U;
        contact_active_[contact] = 1U;
        ++stats_.narrowphase_contacts;
        if (!certified_reuse) {
            const bool tree_needs_projection = !matching_ && config_.enable_single_tree_solver
                && (contact_dirty_[contact] != 0U || violates_contact_velocity(
                    manifold_[contact], velocity_x_, velocity_y_,
                    config_.projection.feasibility_tolerance_raw));
            if (matching_ || !config_.enable_single_tree_solver || tree_needs_projection) {
                active_contact_indices_[active_count] = contact;
                active_contacts_[active_count] = manifold_[contact];
                ++active_count;
            }
        }
    }

    if (!matching_ && config_.enable_single_tree_solver && active_count != 0U) {
        active_count = 0U;
        for (std::size_t contact = 0U; contact < config_.contacts; ++contact) {
            if (contact_active_[contact] == 0U) continue;
            active_contact_indices_[active_count] = contact;
            active_contacts_[active_count] = manifold_[contact];
            ++active_count;
        }
    }

    if (active_count != 0U) {
        ArbitraryNormalStats projection{};
        bool structured_projected = false;
        if (matching_) {
            projection = project_verified_matching_inplace(
                velocity_x_, velocity_y_, masses_,
                std::span<const NormalContact>(active_contacts_.data(), active_count),
                config_.projection,
                std::span<Fixed::rep>(scratch_.dual_impulses_.data(), active_count));
        } else {
            if (config_.enable_single_tree_solver) {
                structured_projected = try_project_single_tree(active_count);
            }
            if (!structured_projected && config_.enable_axis_forest_solver) {
                ++stats_.axis_forest_calls;
                const AxisForestStats axis = project_axis_forest_inplace(
                    velocity_x_, velocity_y_, masses_,
                    std::span<const NormalContact>(active_contacts_.data(), active_count),
                    AxisForestConfig{.tree = WeightedTreeConfig{
                        .maximum_active_set_iterations = 4096U,
                        .feasibility_tolerance_raw = config_.projection.feasibility_tolerance_raw,
                        .stationarity_tolerance_raw = 16U,
                        .use_warm_start = false}},
                    axis_forest_scratch_);
                structured_projected = axis.certified;
                if (structured_projected) ++stats_.axis_forest_certified;
                else ++stats_.axis_forest_fallbacks;
            }
            if (!structured_projected) {
                projection = project_arbitrary_normals_inplace(
                    velocity_x_, velocity_y_, masses_,
                    std::span<const NormalContact>(active_contacts_.data(), active_count),
                    config_.projection, scratch_);
            } else {
                projection.contacts_processed = active_count;
                std::fill_n(scratch_.dual_impulses_.begin(),
                    static_cast<std::ptrdiff_t>(active_count), Fixed::rep{0});
            }
        }
        for (std::size_t local = 0U; local < active_count; ++local) {
            if (!structured_projected) dual_[active_contact_indices_[local]] = scratch_.dual_impulses_[local];
            mark_body_for_cache_check(active_contacts_[local].first);
            mark_body_for_cache_check(active_contacts_[local].second);
        }
        stats_.contacts_projected += projection.contacts_processed;
    }

    for (std::size_t body = 0U; body < config_.bodies; ++body) {
        predicted_x_[body] = integrate_raw(position_x_[body], velocity_x_[body]);
        predicted_y_[body] = integrate_raw(position_y_[body], velocity_y_[body]);
        if (predicted_x_[body] != position_x_[body] || predicted_y_[body] != position_y_[body]) {
            mark_position_changed(body);
        }
    }
    for (std::size_t contact = 0U; contact < config_.contacts; ++contact) {
        if (contact_active_[contact] == 0U) {
            contact_stable_[contact] = 0U;
            continue;
        }
        const NormalContact& item = manifold_[contact];
        contact_stable_[contact] = static_cast<std::uint8_t>(
            velocity_x_[item.first] == velocity_x_[item.second]
            && velocity_y_[item.first] == velocity_y_[item.second]);
    }

    const bool cache_valid = !config_.force_rebuild_each_frame
        && next_frame <= valid_until_frame_ && cache_contains_transition_and_horizon(
            std::span<const std::size_t>(body_check_indices_.data(), body_check_count_));
    position_x_.swap(predicted_x_); position_y_.swap(predicted_y_);
    ++frame_; ++stats_.frames_simulated; stats_.matching_fast_path = matching_;
    if (!cache_valid) rebuild_temporal_cache();
    capture();
}

void AtomicTemporalPhysicsEngine::simulate_to(std::uint64_t target_frame) {
    if (target_frame < frame_) throw std::invalid_argument("simulate_to cannot move backwards");
    while (frame_ < target_frame) step_one();
}

void AtomicTemporalPhysicsEngine::restore(std::uint64_t frame) {
    if (config_.disable_internal_history) throw std::logic_error("Internal temporal history is disabled");
    const SnapshotSlot& slot = snapshot_slot(frame);
    if (slot.frame != frame) throw std::out_of_range("Requested atomic temporal snapshot is not retained");
    std::copy(slot.position_x.begin(), slot.position_x.end(), position_x_.begin());
    std::copy(slot.position_y.begin(), slot.position_y.end(), position_y_.begin());
    std::copy(slot.velocity_x.begin(), slot.velocity_x.end(), velocity_x_.begin());
    std::copy(slot.velocity_y.begin(), slot.velocity_y.end(), velocity_y_.begin());
    std::copy(slot.masses.begin(), slot.masses.end(), masses_.begin());
    std::copy(slot.dual.begin(), slot.dual.end(), dual_.begin());
    std::copy(slot.manifold.begin(), slot.manifold.end(), manifold_.begin());
    std::copy(slot.contact_stable.begin(), slot.contact_stable.end(), contact_stable_.begin());
    if (slot.cache_slot >= cache_slots_.size()) throw std::logic_error("Atomic temporal snapshot cache slot is invalid");
    const CacheSlot& cache = cache_slots_[slot.cache_slot];
    if (cache.generation != slot.cache_generation) {
        throw std::out_of_range("Atomic temporal snapshot cache generation was overwritten");
    }
    std::copy(cache.fat_bounds.begin(), cache.fat_bounds.end(), fat_bounds_.begin());
    std::copy(cache.pairs.begin(), cache.pairs.begin() + static_cast<std::ptrdiff_t>(cache.pair_count), pair_cache_.begin());
    frame_ = frame; valid_until_frame_ = cache.valid_until_frame; pair_count_ = cache.pair_count;
    current_cache_slot_ = slot.cache_slot; current_cache_generation_ = slot.cache_generation;
    rebuild_contact_lookup();
    rebuild_candidate_map();
}

void AtomicTemporalPhysicsEngine::truncate_after(std::uint64_t frame) {
    if (config_.disable_internal_history) return;
    for (SnapshotSlot& slot : snapshots_) if (slot.frame != kEmptyFrame && slot.frame > frame) slot.frame = kEmptyFrame;
}

void AtomicTemporalPhysicsEngine::correct_and_resimulate(
    std::uint64_t frame, AtomicPhysicsFrameInput corrected, std::uint64_t target_frame) {
    if (frame == 0U || target_frame < frame) throw std::invalid_argument("Invalid correction range");
    set_input(frame, corrected);
    restore(frame - 1U);
    truncate_after(frame - 1U);
    simulate_to(target_frame);
}

AtomicTemporalStateView AtomicTemporalPhysicsEngine::state_view() const noexcept {
    return AtomicTemporalStateView{
        .frame = frame_, .valid_until_frame = valid_until_frame_,
        .topology_signature = topology_signature_,
        .position_x = position_x_, .position_y = position_y_,
        .velocity_x = velocity_x_, .velocity_y = velocity_y_, .masses = masses_,
        .dual = dual_, .manifold = manifold_, .contact_stable = contact_stable_,
        .contact_candidate = contact_candidate_, .fat_bounds = fat_bounds_,
        .pairs = std::span<const BroadphasePair>(pair_cache_.data(), pair_count_),
    };
}

AtomicTemporalMutableStateView AtomicTemporalPhysicsEngine::mutable_state_view() noexcept {
    return AtomicTemporalMutableStateView{
        .position_x = position_x_, .position_y = position_y_,
        .velocity_x = velocity_x_, .velocity_y = velocity_y_, .masses = masses_,
        .dual = dual_, .manifold = manifold_, .contact_stable = contact_stable_,
        .contact_candidate = contact_candidate_, .fat_bounds = fat_bounds_, .pairs = pair_cache_,
    };
}

AtomicTemporalCaptureHints AtomicTemporalPhysicsEngine::capture_hints() const noexcept {
    return AtomicTemporalCaptureHints{
        .changed_position_bodies = std::span<const std::size_t>(
            position_changed_indices_.data(), position_changed_count_),
        .position_hints_complete = true,
        .changed_bodies = std::span<const std::size_t>(body_check_indices_.data(), body_check_count_),
        .changed_contacts = contact_dirty_,
        .cache_rebuilt = last_cache_rebuilt_,
        .topology_changed = last_topology_changed_,
    };
}

void AtomicTemporalPhysicsEngine::export_state(AtomicTemporalExternalState& output) const {
    if (output.position_x.size() != config_.bodies || output.position_y.size() != config_.bodies
        || output.velocity_x.size() != config_.bodies || output.velocity_y.size() != config_.bodies
        || output.masses.size() != config_.bodies || output.dual.size() != config_.contacts
        || output.manifold.size() != config_.contacts || output.contact_stable.size() != config_.contacts
        || output.contact_candidate.size() != config_.contacts
        || output.fat_bounds.size() != config_.bodies || output.pairs.size() < pair_count_) {
        throw std::invalid_argument("Atomic temporal external state shape mismatch");
    }
    output.frame = frame_;
    output.valid_until_frame = valid_until_frame_;
    output.topology_signature = topology_signature_;
    output.pair_count = pair_count_;
    std::copy(position_x_.begin(), position_x_.end(), output.position_x.begin());
    std::copy(position_y_.begin(), position_y_.end(), output.position_y.begin());
    std::copy(velocity_x_.begin(), velocity_x_.end(), output.velocity_x.begin());
    std::copy(velocity_y_.begin(), velocity_y_.end(), output.velocity_y.begin());
    std::copy(masses_.begin(), masses_.end(), output.masses.begin());
    std::copy(dual_.begin(), dual_.end(), output.dual.begin());
    std::copy(manifold_.begin(), manifold_.end(), output.manifold.begin());
    std::copy(contact_stable_.begin(), contact_stable_.end(), output.contact_stable.begin());
    std::copy(contact_candidate_.begin(), contact_candidate_.end(), output.contact_candidate.begin());
    std::copy(fat_bounds_.begin(), fat_bounds_.end(), output.fat_bounds.begin());
    std::copy(pair_cache_.begin(), pair_cache_.begin() + static_cast<std::ptrdiff_t>(pair_count_), output.pairs.begin());
}

void AtomicTemporalPhysicsEngine::import_state(const AtomicTemporalExternalState& input) {
    if (input.position_x.size() != config_.bodies || input.position_y.size() != config_.bodies
        || input.velocity_x.size() != config_.bodies || input.velocity_y.size() != config_.bodies
        || input.masses.size() != config_.bodies || input.dual.size() != config_.contacts
        || input.manifold.size() != config_.contacts || input.contact_stable.size() != config_.contacts
        || input.contact_candidate.size() != config_.contacts
        || input.fat_bounds.size() != config_.bodies || input.pair_count > pair_cache_.size()
        || input.pairs.size() < input.pair_count) {
        throw std::invalid_argument("Atomic temporal imported state shape mismatch");
    }
    std::copy(input.position_x.begin(), input.position_x.end(), position_x_.begin());
    std::copy(input.position_y.begin(), input.position_y.end(), position_y_.begin());
    std::copy(input.velocity_x.begin(), input.velocity_x.end(), velocity_x_.begin());
    std::copy(input.velocity_y.begin(), input.velocity_y.end(), velocity_y_.begin());
    std::copy(input.masses.begin(), input.masses.end(), masses_.begin());
    std::copy(input.dual.begin(), input.dual.end(), dual_.begin());
    std::copy(input.manifold.begin(), input.manifold.end(), manifold_.begin());
    std::copy(input.contact_stable.begin(), input.contact_stable.end(), contact_stable_.begin());
    std::copy(input.contact_candidate.begin(), input.contact_candidate.end(), contact_candidate_.begin());
    std::copy(input.fat_bounds.begin(), input.fat_bounds.end(), fat_bounds_.begin());
    std::copy(input.pairs.begin(), input.pairs.begin() + static_cast<std::ptrdiff_t>(input.pair_count), pair_cache_.begin());
    finalize_direct_restore(AtomicTemporalRestoreMetadata{
        .frame = input.frame,
        .valid_until_frame = input.valid_until_frame,
        .topology_signature = input.topology_signature,
        .pair_count = input.pair_count,
    });
}

void AtomicTemporalPhysicsEngine::finalize_direct_restore(AtomicTemporalRestoreMetadata metadata) {
    if (metadata.pair_count > pair_cache_.size()) {
        throw std::invalid_argument("Atomic temporal direct restore pair count exceeds capacity");
    }
    const bool topology_reusable = metadata.topology_signature != 0U
        && metadata.topology_signature == topology_signature_;
    frame_ = metadata.frame;
    valid_until_frame_ = metadata.valid_until_frame;
    pair_count_ = metadata.pair_count;
    for (SnapshotSlot& slot : snapshots_) slot.frame = kEmptyFrame;
    for (CacheSlot& slot : cache_slots_) slot.generation = 0U;
    current_cache_slot_ = 0U;
    current_cache_generation_ = 0U;
    next_cache_generation_ = 1U;
    if (!topology_reusable) {
        rebuild_contact_lookup();
        rebuild_island_index();
    } else {
        topology_signature_ = metadata.topology_signature;
        ++stats_.topology_restore_reuses;
    }
    if (!config_.disable_internal_history) {
        persist_current_cache();
        capture();
    } else {
        current_cache_slot_ = 0U;
        current_cache_generation_ = next_cache_generation_++;
    }
    initialized_ = true;
}

std::uint64_t AtomicTemporalPhysicsEngine::hash() const noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    fnv_mix(hash, frame_); fnv_mix(hash, valid_until_frame_); fnv_mix(hash, pair_count_);
    for (std::size_t i = 0U; i < config_.bodies; ++i) {
        fnv_mix(hash, static_cast<std::uint64_t>(position_x_[i]));
        fnv_mix(hash, static_cast<std::uint64_t>(position_y_[i]));
        fnv_mix(hash, static_cast<std::uint64_t>(velocity_x_[i]));
        fnv_mix(hash, static_cast<std::uint64_t>(velocity_y_[i]));
        fnv_mix(hash, masses_[i]);
        fnv_mix(hash, static_cast<std::uint64_t>(fat_bounds_[i].minimum_x.raw()));
        fnv_mix(hash, static_cast<std::uint64_t>(fat_bounds_[i].maximum_x.raw()));
        fnv_mix(hash, static_cast<std::uint64_t>(fat_bounds_[i].minimum_y.raw()));
        fnv_mix(hash, static_cast<std::uint64_t>(fat_bounds_[i].maximum_y.raw()));
    }
    for (std::size_t i = 0U; i < config_.contacts; ++i) {
        fnv_mix(hash, static_cast<std::uint64_t>(dual_[i]));
        fnv_mix(hash, manifold_[i].first); fnv_mix(hash, manifold_[i].second);
        fnv_mix(hash, static_cast<std::uint32_t>(manifold_[i].normal.x));
        fnv_mix(hash, static_cast<std::uint32_t>(manifold_[i].normal.y));
    }
    for (std::size_t i = 0U; i < contact_stable_.size(); ++i) fnv_mix(hash, contact_stable_[i]);
    for (std::size_t i = 0U; i < pair_count_; ++i) {
        fnv_mix(hash, pair_cache_[i].first); fnv_mix(hash, pair_cache_[i].second);
    }
    return hash;
}

std::uint64_t AtomicTemporalPhysicsEngine::physical_hash() const noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    fnv_mix(hash, frame_);
    for (std::size_t i = 0U; i < config_.bodies; ++i) {
        fnv_mix(hash, static_cast<std::uint64_t>(position_x_[i]));
        fnv_mix(hash, static_cast<std::uint64_t>(position_y_[i]));
        fnv_mix(hash, static_cast<std::uint64_t>(velocity_x_[i]));
        fnv_mix(hash, static_cast<std::uint64_t>(velocity_y_[i]));
        fnv_mix(hash, masses_[i]);
    }
    for (std::size_t i = 0U; i < config_.contacts; ++i) {
        fnv_mix(hash, static_cast<std::uint64_t>(dual_[i]));
        fnv_mix(hash, manifold_[i].first); fnv_mix(hash, manifold_[i].second);
        fnv_mix(hash, static_cast<std::uint32_t>(manifold_[i].normal.x));
        fnv_mix(hash, static_cast<std::uint32_t>(manifold_[i].normal.y));
    }
    return hash;
}

bool AtomicTemporalPhysicsEngine::physically_equivalent_to(
    const AtomicTemporalPhysicsEngine& other) const noexcept {
    return frame_ == other.frame_ && position_x_ == other.position_x_
        && position_y_ == other.position_y_ && velocity_x_ == other.velocity_x_
        && velocity_y_ == other.velocity_y_ && masses_ == other.masses_
        && dual_ == other.dual_ && manifold_ == other.manifold_;
}

bool AtomicTemporalPhysicsEngine::equivalent_to(const AtomicTemporalPhysicsEngine& other) const noexcept {
    return frame_ == other.frame_ && valid_until_frame_ == other.valid_until_frame_
        && pair_count_ == other.pair_count_ && position_x_ == other.position_x_
        && position_y_ == other.position_y_ && velocity_x_ == other.velocity_x_
        && velocity_y_ == other.velocity_y_ && masses_ == other.masses_
        && dual_ == other.dual_ && manifold_ == other.manifold_
        && contact_stable_ == other.contact_stable_
        && fat_bounds_ == other.fat_bounds_
        && std::equal(pair_cache_.begin(), pair_cache_.begin() + static_cast<std::ptrdiff_t>(pair_count_),
                      other.pair_cache_.begin());
}

std::size_t AtomicTemporalPhysicsEngine::reserved_bytes() const noexcept {
    std::size_t bytes = (position_x_.capacity() + position_y_.capacity() + velocity_x_.capacity()
        + velocity_y_.capacity() + predicted_x_.capacity() + predicted_y_.capacity()
        + guard_velocity_x_.capacity() + guard_velocity_y_.capacity() + dual_.capacity()) * sizeof(Fixed::rep)
        + masses_.capacity() * sizeof(std::uint32_t)
        + manifold_.capacity() * sizeof(NormalContact)
        + fat_bounds_.capacity() * sizeof(FatAabb)
        + pair_cache_.capacity() * sizeof(BroadphasePair)
        + broadphase_order_.capacity() * sizeof(std::size_t)
        + contact_lookup_.capacity() * sizeof(ContactLookup)
        + (contact_stable_.capacity() + contact_candidate_.capacity()
           + contact_dirty_.capacity() + contact_active_.capacity()
           + body_check_mask_.capacity()) * sizeof(std::uint8_t)
        + body_check_indices_.capacity() * sizeof(std::size_t);
    for (const SnapshotSlot& slot : snapshots_) {
        bytes += (slot.position_x.capacity() + slot.position_y.capacity() + slot.velocity_x.capacity()
            + slot.velocity_y.capacity() + slot.dual.capacity()) * sizeof(Fixed::rep)
            + slot.masses.capacity() * sizeof(std::uint32_t)
            + slot.manifold.capacity() * sizeof(NormalContact)
            + slot.contact_stable.capacity() * sizeof(std::uint8_t);
    }
    for (const CacheSlot& slot : cache_slots_) {
        bytes += slot.fat_bounds.capacity() * sizeof(FatAabb)
            + slot.pairs.capacity() * sizeof(BroadphasePair);
    }
    return bytes;
}

} // namespace neoeng::core
