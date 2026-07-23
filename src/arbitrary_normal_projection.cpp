#include "neoeng/core/arbitrary_normal_projection.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace neoeng::core {
namespace {

constexpr WideInteger kNormalScale = static_cast<WideInteger>(1) << 30U;
constexpr WideInteger kNormalScaleSquared = kNormalScale * kNormalScale;

[[nodiscard]] Fixed::rep checked_rep(WideInteger value) {
    constexpr WideInteger minimum = static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::min());
    constexpr WideInteger maximum = static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::max());
    if (value < minimum || value > maximum) {
        throw std::overflow_error("Arbitrary-normal projection overflow");
    }
    return static_cast<Fixed::rep>(value);
}

[[nodiscard]] std::uint64_t abs_u64(WideInteger value) noexcept {
    const WideInteger magnitude = value < 0 ? -value : value;
    constexpr WideInteger maximum = static_cast<WideInteger>(std::numeric_limits<std::uint64_t>::max());
    return magnitude > maximum ? std::numeric_limits<std::uint64_t>::max()
                               : static_cast<std::uint64_t>(magnitude);
}

[[nodiscard]] WideInteger rounded_div_ties_to_floor(WideInteger numerator, WideInteger denominator) {
    if (denominator <= 0) throw std::domain_error("Projection denominator must be positive");
    WideInteger quotient = numerator / denominator;
    WideInteger remainder = numerator % denominator;
    if (remainder < 0) {
        remainder += denominator;
        --quotient;
    }
    if (remainder * 2 > denominator) ++quotient;
    return quotient;
}

[[nodiscard]] NormalContact canonical_contact(NormalContact contact) {
    const auto component_in_range = [](std::int32_t value) noexcept {
        const std::int64_t wide = value;
        return wide >= -(static_cast<std::int64_t>(1) << 30U)
            && wide <= (static_cast<std::int64_t>(1) << 30U);
    };
    if (!component_in_range(contact.normal.x) || !component_in_range(contact.normal.y)) {
        throw std::invalid_argument("Normal components must be encoded in signed Q1.30");
    }
    if (contact.first == contact.second) {
        throw std::invalid_argument("A contact cannot constrain a body against itself");
    }
    if (contact.second < contact.first) {
        std::swap(contact.first, contact.second);
        contact.normal.x = static_cast<std::int32_t>(-static_cast<std::int64_t>(contact.normal.x));
        contact.normal.y = static_cast<std::int32_t>(-static_cast<std::int64_t>(contact.normal.y));
    }
    if (contact.normal.x == 0 && contact.normal.y == 0) {
        throw std::invalid_argument("A contact normal cannot be zero");
    }
    return contact;
}

[[nodiscard]] WideInteger normal_squared(const NormalQ30& normal) noexcept {
    return static_cast<WideInteger>(normal.x) * normal.x
         + static_cast<WideInteger>(normal.y) * normal.y;
}

[[nodiscard]] Fixed::rep normal_velocity(
    Fixed::rep velocity_x, Fixed::rep velocity_y, const NormalQ30& normal) {
    const WideInteger numerator = static_cast<WideInteger>(velocity_x) * normal.x
                                + static_cast<WideInteger>(velocity_y) * normal.y;
    return checked_rep(rounded_div_ties_to_floor(numerator, kNormalScale));
}

[[nodiscard]] Fixed::rep component_delta(
    Fixed::rep normal_delta, std::int32_t normal_component, WideInteger norm_squared) {
    const WideInteger numerator = static_cast<WideInteger>(normal_delta)
                                * static_cast<WideInteger>(normal_component)
                                * kNormalScale;
    return checked_rep(rounded_div_ties_to_floor(numerator, norm_squared));
}

struct PairUpdate final {
    bool changed{};
    std::uint64_t momentum_error{};
    Fixed::rep dual_impulse{};
};

[[nodiscard]] PairUpdate project_pair(
    std::span<Fixed::rep> vx,
    std::span<Fixed::rep> vy,
    std::span<const std::uint32_t> masses,
    const NormalContact& contact) {
    const std::size_t first = contact.first;
    const std::size_t second = contact.second;
    const Fixed::rep first_normal = normal_velocity(vx[first], vy[first], contact.normal);
    const Fixed::rep second_normal = normal_velocity(vx[second], vy[second], contact.normal);
    if (first_normal <= second_normal) return {};

    const WideInteger first_mass = masses[first];
    const WideInteger second_mass = masses[second];
    const WideInteger mass_sum = first_mass + second_mass;
    const Fixed::rep common = checked_rep(rounded_div_ties_to_floor(
        first_mass * first_normal + second_mass * second_normal, mass_sum));
    const Fixed::rep first_delta = checked_rep(static_cast<WideInteger>(common) - first_normal);
    const Fixed::rep second_delta = checked_rep(static_cast<WideInteger>(common) - second_normal);
    const WideInteger norm_sq = normal_squared(contact.normal);

    const Fixed::rep first_dx = component_delta(first_delta, contact.normal.x, norm_sq);
    const Fixed::rep first_dy = component_delta(first_delta, contact.normal.y, norm_sq);
    const Fixed::rep second_dx = component_delta(second_delta, contact.normal.x, norm_sq);
    const Fixed::rep second_dy = component_delta(second_delta, contact.normal.y, norm_sq);

    vx[first] = checked_rep(static_cast<WideInteger>(vx[first]) + first_dx);
    vy[first] = checked_rep(static_cast<WideInteger>(vy[first]) + first_dy);
    vx[second] = checked_rep(static_cast<WideInteger>(vx[second]) + second_dx);
    vy[second] = checked_rep(static_cast<WideInteger>(vy[second]) + second_dy);

    const WideInteger momentum_x = first_mass * first_dx + second_mass * second_dx;
    const WideInteger momentum_y = first_mass * first_dy + second_mass * second_dy;
    const WideInteger gap = static_cast<WideInteger>(first_normal) - second_normal;
    const Fixed::rep dual = checked_rep(rounded_div_ties_to_floor(
        gap * first_mass * second_mass * kNormalScaleSquared,
        mass_sum * norm_sq));
    return {.changed = true,
            .momentum_error = std::max(abs_u64(momentum_x), abs_u64(momentum_y)),
            .dual_impulse = dual};
}

[[nodiscard]] std::uint64_t maximum_violation(
    std::span<const Fixed::rep> vx,
    std::span<const Fixed::rep> vy,
    std::span<const NormalContact> contacts) {
    std::uint64_t maximum = 0U;
    for (const NormalContact& contact : contacts) {
        const Fixed::rep first = normal_velocity(vx[contact.first], vy[contact.first], contact.normal);
        const Fixed::rep second = normal_velocity(vx[contact.second], vy[contact.second], contact.normal);
        if (first > second) {
            maximum = std::max(maximum, static_cast<std::uint64_t>(first - second));
        }
    }
    return maximum;
}

[[nodiscard]] std::uint64_t maximum_unit_error(std::span<const NormalContact> contacts) noexcept {
    std::uint64_t maximum = 0U;
    for (const NormalContact& contact : contacts) {
        maximum = std::max(maximum, abs_u64(normal_squared(contact.normal) - kNormalScaleSquared));
    }
    return maximum;
}

void validate_arguments(
    std::span<Fixed::rep> vx,
    std::span<Fixed::rep> vy,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    const ArbitraryNormalScratch& scratch) {
    if (vx.size() != vy.size() || vx.size() != masses.size()) {
        throw std::invalid_argument("Velocity and mass spans must have identical sizes");
    }
    if (vx.size() > scratch.maximum_bodies_ || contacts.size() > scratch.maximum_contacts_) {
        throw std::length_error("Arbitrary-normal scratch capacity exceeded");
    }
    if (std::any_of(masses.begin(), masses.end(), [](std::uint32_t mass) { return mass == 0U; })) {
        throw std::invalid_argument("All masses must be positive");
    }
}

[[nodiscard]] bool contact_less(const NormalContact& lhs, const NormalContact& rhs) noexcept {
    return std::tie(lhs.first, lhs.second, lhs.normal.x, lhs.normal.y)
         < std::tie(rhs.first, rhs.second, rhs.normal.x, rhs.normal.y);
}

} // namespace

const char* to_string(ArbitraryNormalMethod method) noexcept {
    switch (method) {
    case ArbitraryNormalMethod::CertifiedMatching: return "certified_matching";
    case ArbitraryNormalMethod::CoordinateFallback: return "coordinate_fallback";
    }
    return "unknown";
}

ArbitraryNormalScratch::ArbitraryNormalScratch(
    std::size_t maximum_bodies, std::size_t maximum_contacts)
    : maximum_bodies_(maximum_bodies), maximum_contacts_(maximum_contacts),
      velocity_x_(maximum_bodies), velocity_y_(maximum_bodies), degree_(maximum_bodies),
      contacts_(maximum_contacts), dual_impulses_(maximum_contacts), patches_(maximum_bodies) {
    if (maximum_bodies == 0U) throw std::invalid_argument("Scratch body capacity must be positive");
}

std::size_t ArbitraryNormalScratch::reserved_bytes() const noexcept {
    return velocity_x_.capacity() * sizeof(Fixed::rep)
         + velocity_y_.capacity() * sizeof(Fixed::rep)
         + degree_.capacity() * sizeof(std::uint32_t)
         + contacts_.capacity() * sizeof(NormalContact)
         + dual_impulses_.capacity() * sizeof(Fixed::rep)
         + patches_.capacity() * sizeof(ComponentPatch);
}

ArbitraryNormalStats project_arbitrary_normals_inplace(
    std::span<Fixed::rep> velocity_x,
    std::span<Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ArbitraryNormalConfig config,
    ArbitraryNormalScratch& scratch) {
    validate_arguments(velocity_x, velocity_y, masses, contacts, scratch);
    if (config.maximum_iterations == 0U) {
        throw std::invalid_argument("Projection requires at least one iteration");
    }

    std::fill_n(scratch.degree_.begin(), static_cast<std::ptrdiff_t>(velocity_x.size()), 0U);
    for (std::size_t index = 0U; index < contacts.size(); ++index) {
        NormalContact canonical = canonical_contact(contacts[index]);
        if (canonical.first >= velocity_x.size() || canonical.second >= velocity_x.size()) {
            throw std::out_of_range("Contact body index is outside the velocity arrays");
        }
        scratch.contacts_[index] = canonical;
        ++scratch.degree_[canonical.first];
        ++scratch.degree_[canonical.second];
    }
    auto canonical = std::span<NormalContact>(scratch.contacts_.data(), contacts.size());
    std::sort(canonical.begin(), canonical.end(), contact_less);
    if (std::adjacent_find(canonical.begin(), canonical.end()) != canonical.end()) {
        throw std::invalid_argument("Duplicate normal contact");
    }

    const bool matching = std::all_of(
        scratch.degree_.begin(), scratch.degree_.begin() + static_cast<std::ptrdiff_t>(velocity_x.size()),
        [](std::uint32_t degree) { return degree <= 1U; });

    ArbitraryNormalStats stats{};
    stats.method = matching ? ArbitraryNormalMethod::CertifiedMatching
                            : ArbitraryNormalMethod::CoordinateFallback;
    stats.contacts_processed = contacts.size();
    stats.matching_contacts = matching ? contacts.size() : 0U;
    stats.fallback_contacts = matching ? 0U : contacts.size();
    stats.residuals.unit_norm_linf_q60 = maximum_unit_error(canonical);
    std::uint64_t momentum_tolerance = 0U;
    for (const NormalContact& contact : canonical) {
        momentum_tolerance = std::max<std::uint64_t>(
            momentum_tolerance,
            static_cast<std::uint64_t>(masses[contact.first]) + masses[contact.second]);
    }

    std::fill_n(scratch.dual_impulses_.begin(), static_cast<std::ptrdiff_t>(contacts.size()), Fixed::rep{0});
    const std::size_t iteration_limit = matching ? 4U : config.maximum_iterations;
    for (std::size_t iteration = 0U; iteration < iteration_limit; ++iteration) {
        bool changed = false;
        for (std::size_t contact_index = 0U; contact_index < canonical.size(); ++contact_index) {
            const NormalContact& contact = canonical[contact_index];
            const PairUpdate update = project_pair(velocity_x, velocity_y, masses, contact);
            scratch.dual_impulses_[contact_index] = checked_rep(
                static_cast<WideInteger>(scratch.dual_impulses_[contact_index]) + update.dual_impulse);
            changed = changed || update.changed;
            stats.coordinate_updates += update.changed ? 1U : 0U;
            stats.residuals.weighted_momentum_linf_raw = std::max(
                stats.residuals.weighted_momentum_linf_raw, update.momentum_error);
        }
        ++stats.iterations;
        if (!changed || maximum_violation(velocity_x, velocity_y, canonical)
                            <= config.feasibility_tolerance_raw) {
            break;
        }
    }

    stats.residuals.primal_linf_raw = maximum_violation(velocity_x, velocity_y, canonical);
    stats.residuals.feasible = stats.residuals.primal_linf_raw <= config.feasibility_tolerance_raw;
    stats.residuals.certified = matching && stats.residuals.feasible
        && stats.residuals.unit_norm_linf_q60 <= config.unit_norm_tolerance_q60
        && stats.residuals.weighted_momentum_linf_raw <= momentum_tolerance;
    return stats;
}

ArbitraryNormalStats project_verified_matching_inplace(
    std::span<Fixed::rep> velocity_x,
    std::span<Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ArbitraryNormalConfig config,
    std::span<Fixed::rep> dual_impulses) {
    if (velocity_x.size() != velocity_y.size() || velocity_x.size() != masses.size()) {
        throw std::invalid_argument("Velocity and mass spans must have identical sizes");
    }
    if (dual_impulses.size() < contacts.size()) {
        throw std::length_error("Verified matching dual span is too small");
    }
    if (std::any_of(masses.begin(), masses.end(), [](std::uint32_t mass) { return mass == 0U; })) {
        throw std::invalid_argument("All masses must be positive");
    }
    ArbitraryNormalStats stats{};
    stats.method = ArbitraryNormalMethod::CertifiedMatching;
    stats.contacts_processed = contacts.size();
    stats.matching_contacts = contacts.size();
    stats.iterations = contacts.empty() ? 0U : 1U;
    stats.residuals.unit_norm_linf_q60 = maximum_unit_error(contacts);
    std::uint64_t momentum_tolerance = 0U;
    for (std::size_t index = 0U; index < contacts.size(); ++index) {
        const NormalContact& contact = contacts[index];
        if (contact.first >= velocity_x.size() || contact.second >= velocity_x.size()
            || contact.first >= contact.second || (contact.normal.x == 0 && contact.normal.y == 0)) {
            throw std::invalid_argument("Verified matching contact contract was violated");
        }
        const PairUpdate update = project_pair(velocity_x, velocity_y, masses, contact);
        dual_impulses[index] = update.dual_impulse;
        stats.coordinate_updates += update.changed ? 1U : 0U;
        stats.residuals.weighted_momentum_linf_raw = std::max(
            stats.residuals.weighted_momentum_linf_raw, update.momentum_error);
        momentum_tolerance = std::max<std::uint64_t>(
            momentum_tolerance, static_cast<std::uint64_t>(masses[contact.first]) + masses[contact.second]);
    }
    stats.residuals.primal_linf_raw = maximum_violation(velocity_x, velocity_y, contacts);
    stats.residuals.feasible = stats.residuals.primal_linf_raw <= config.feasibility_tolerance_raw;
    stats.residuals.certified = stats.residuals.feasible
        && stats.residuals.unit_norm_linf_q60 <= config.unit_norm_tolerance_q60
        && stats.residuals.weighted_momentum_linf_raw <= momentum_tolerance;
    return stats;
}

ArbitraryNormalProjectionResult project_arbitrary_normal_contacts_2d(
    const ComponentWorldState& current,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ArbitraryNormalConfig config,
    ArbitraryNormalScratch& scratch) {
    if (current.body_count() != masses.size()) {
        throw std::invalid_argument("Mass count must match body count");
    }
    if (current.body_count() > scratch.maximum_bodies_) {
        throw std::length_error("Arbitrary-normal scratch body capacity exceeded");
    }
    for (std::size_t body = 0U; body < current.body_count(); ++body) {
        scratch.velocity_x_[body] = current.velocity_x_at(body).raw();
        scratch.velocity_y_[body] = current.velocity_y_at(body).raw();
    }
    const auto vx = std::span<Fixed::rep>(scratch.velocity_x_.data(), current.body_count());
    const auto vy = std::span<Fixed::rep>(scratch.velocity_y_.data(), current.body_count());
    ArbitraryNormalStats stats = project_arbitrary_normals_inplace(
        vx, vy, masses, contacts, config, scratch);

    std::size_t patch_count = 0U;
    for (std::size_t body = 0U; body < current.body_count(); ++body) {
        if (vx[body] == current.velocity_x_at(body).raw()
            && vy[body] == current.velocity_y_at(body).raw()) {
            continue;
        }
        scratch.patches_[patch_count++] = ComponentPatch{
            .index = body,
            .velocity_x = Fixed::from_raw(vx[body]),
            .velocity_y = Fixed::from_raw(vy[body]),
            .mask = static_cast<std::uint8_t>(component_mask(DirtyComponent::VelocityX)
                  | component_mask(DirtyComponent::VelocityY)),
        };
    }
    stats.changed_bodies = patch_count;
    ArbitraryNormalProjectionResult result{};
    result.stats = stats;
    result.dual_impulses.assign(scratch.dual_impulses_.begin(),
                                scratch.dual_impulses_.begin() + static_cast<std::ptrdiff_t>(contacts.size()));
    result.manifold.assign(scratch.contacts_.begin(),
                           scratch.contacts_.begin() + static_cast<std::ptrdiff_t>(contacts.size()));
    result.state = apply_component_patches(
        current, std::span<const ComponentPatch>(scratch.patches_.data(), patch_count),
        &result.allocation);
    return result;
}

void PhysicsAuxHistory::capture(PhysicsAuxSnapshot snapshot) {
    if (!snapshot.masses || !snapshot.dual_impulses || !snapshot.manifold) {
        throw std::invalid_argument("Auxiliary physics snapshot must own all payloads");
    }
    ring_.capture(snapshot.frame, std::move(snapshot));
}

std::uint64_t hash_physics_aux(const PhysicsAuxSnapshot& snapshot) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (unsigned byte = 0U; byte < 8U; ++byte) {
            hash ^= (value >> (byte * 8U)) & 0xFFU;
            hash *= 0x100000001B3ULL;
        }
    };
    mix(snapshot.frame);
    if (snapshot.masses) for (std::uint32_t mass : *snapshot.masses) mix(mass);
    if (snapshot.dual_impulses) {
        for (Fixed::rep impulse : *snapshot.dual_impulses) mix(static_cast<std::uint64_t>(impulse));
    }
    if (snapshot.manifold) {
        for (const NormalContact& contact : *snapshot.manifold) {
            mix(contact.first); mix(contact.second);
            mix(static_cast<std::uint32_t>(contact.normal.x));
            mix(static_cast<std::uint32_t>(contact.normal.y));
        }
    }
    return hash;
}

} // namespace neoeng::core
