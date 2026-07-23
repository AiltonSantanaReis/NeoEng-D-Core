#include "neoeng/core/atomic_physics.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace neoeng::core {
namespace {
constexpr std::uint64_t kEmptyFrame = ~std::uint64_t{0};

void fnv_mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

NormalContact canonicalize(NormalContact contact) {
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
}

AtomicPhysicsEngine::AtomicPhysicsEngine(AtomicPhysicsConfig config)
    : config_(config),
      position_x_(config.bodies), position_y_(config.bodies),
      velocity_x_(config.bodies), velocity_y_(config.bodies), masses_(config.bodies),
      dual_(config.contacts), manifold_(config.contacts), body_to_contact_(config.bodies, -1),
      contact_dirty_(config.contacts), active_contact_indices_(config.contacts),
      active_contacts_(config.contacts), snapshots_(config.history_capacity),
      inputs_(config.history_capacity), scratch_(config.bodies, config.contacts) {
    if (config.bodies == 0U || config.history_capacity < 2U) {
        throw std::invalid_argument("Atomic physics requires bodies and at least two history slots");
    }
    for (SnapshotSlot& slot : snapshots_) {
        slot.position_x.resize(config.bodies); slot.position_y.resize(config.bodies);
        slot.velocity_x.resize(config.bodies); slot.velocity_y.resize(config.bodies);
        slot.masses.resize(config.bodies); slot.dual.resize(config.contacts);
        slot.manifold.resize(config.contacts); slot.frame = kEmptyFrame;
    }
    for (InputSlot& slot : inputs_) {
        slot.velocity.resize(config.maximum_velocity_mutations);
        slot.mass.resize(config.maximum_mass_mutations);
        slot.contact.resize(config.maximum_contact_mutations);
        slot.frame = kEmptyFrame;
    }
}

void AtomicPhysicsEngine::initialize(
    std::span<const Fixed::rep> position_x,
    std::span<const Fixed::rep> position_y,
    std::span<const Fixed::rep> velocity_x,
    std::span<const Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts) {
    if (position_x.size() != config_.bodies || position_y.size() != config_.bodies
        || velocity_x.size() != config_.bodies || velocity_y.size() != config_.bodies
        || masses.size() != config_.bodies || contacts.size() != config_.contacts) {
        throw std::invalid_argument("Atomic physics initialization shape mismatch");
    }
    std::copy(position_x.begin(), position_x.end(), position_x_.begin());
    std::copy(position_y.begin(), position_y.end(), position_y_.begin());
    std::copy(velocity_x.begin(), velocity_x.end(), velocity_x_.begin());
    std::copy(velocity_y.begin(), velocity_y.end(), velocity_y_.begin());
    std::copy(masses.begin(), masses.end(), masses_.begin());
    for (std::uint32_t mass : masses_) if (mass == 0U) throw std::invalid_argument("Mass must be positive");
    for (std::size_t i = 0U; i < contacts.size(); ++i) manifold_[i] = canonicalize(contacts[i]);
    std::fill(dual_.begin(), dual_.end(), 0);
    for (SnapshotSlot& slot : snapshots_) slot.frame = kEmptyFrame;
    for (InputSlot& slot : inputs_) {
        slot.frame = kEmptyFrame; slot.velocity_count = 0U; slot.mass_count = 0U; slot.contact_count = 0U;
    }
    frame_ = 0U; stats_ = {}; initialized_ = true;
    rebuild_matching_map();
    capture();
}

AtomicPhysicsEngine::SnapshotSlot& AtomicPhysicsEngine::snapshot_slot(std::uint64_t frame) noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}
const AtomicPhysicsEngine::SnapshotSlot& AtomicPhysicsEngine::snapshot_slot(std::uint64_t frame) const noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}
AtomicPhysicsEngine::InputSlot& AtomicPhysicsEngine::input_slot(std::uint64_t frame) noexcept {
    return inputs_[static_cast<std::size_t>(frame % inputs_.size())];
}
const AtomicPhysicsEngine::InputSlot* AtomicPhysicsEngine::find_input(std::uint64_t frame) const noexcept {
    const InputSlot& slot = inputs_[static_cast<std::size_t>(frame % inputs_.size())];
    return slot.frame == frame ? &slot : nullptr;
}

void AtomicPhysicsEngine::set_input(std::uint64_t frame, AtomicPhysicsFrameInput input) {
    if (!initialized_) throw std::logic_error("Atomic physics is not initialized");
    if (input.velocity.size() > config_.maximum_velocity_mutations
        || input.mass.size() > config_.maximum_mass_mutations
        || input.contact.size() > config_.maximum_contact_mutations) {
        throw std::length_error("Atomic physics input exceeds configured capacity");
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

void AtomicPhysicsEngine::capture() {
    SnapshotSlot& slot = snapshot_slot(frame_);
    slot.frame = frame_;
    std::copy(position_x_.begin(), position_x_.end(), slot.position_x.begin());
    std::copy(position_y_.begin(), position_y_.end(), slot.position_y.begin());
    std::copy(velocity_x_.begin(), velocity_x_.end(), slot.velocity_x.begin());
    std::copy(velocity_y_.begin(), velocity_y_.end(), slot.velocity_y.begin());
    std::copy(masses_.begin(), masses_.end(), slot.masses.begin());
    std::copy(dual_.begin(), dual_.end(), slot.dual.begin());
    std::copy(manifold_.begin(), manifold_.end(), slot.manifold.begin());
    ++stats_.snapshots_captured;
    stats_.payload_values_copied += position_x_.size() * 4U + masses_.size()
        + dual_.size() + manifold_.size();
}

void AtomicPhysicsEngine::rebuild_matching_map() {
    std::fill(body_to_contact_.begin(), body_to_contact_.end(), -1);
    matching_ = true;
    for (std::size_t contact = 0U; contact < manifold_.size(); ++contact) {
        const NormalContact& item = manifold_[contact];
        if (item.first >= config_.bodies || item.second >= config_.bodies) {
            throw std::out_of_range("Contact body index is outside atomic state");
        }
        if (body_to_contact_[item.first] >= 0 || body_to_contact_[item.second] >= 0) matching_ = false;
        body_to_contact_[item.first] = static_cast<std::int64_t>(contact);
        body_to_contact_[item.second] = static_cast<std::int64_t>(contact);
    }
}

void AtomicPhysicsEngine::step_one() {
    const std::uint64_t next_frame = frame_ + 1U;
    std::fill(contact_dirty_.begin(), contact_dirty_.end(), 0U);
    bool topology_changed = false;
    if (const InputSlot* input = find_input(next_frame)) {
        for (std::size_t i = 0U; i < input->velocity_count; ++i) {
            const VelocityMutation& mutation = input->velocity[i];
            if (mutation.body >= config_.bodies) throw std::out_of_range("Velocity mutation body is invalid");
            velocity_x_[mutation.body] += mutation.delta_x;
            velocity_y_[mutation.body] += mutation.delta_y;
            const std::int64_t contact = body_to_contact_[mutation.body];
            if (contact >= 0) contact_dirty_[static_cast<std::size_t>(contact)] = 1U;
        }
        for (std::size_t i = 0U; i < input->mass_count; ++i) {
            const MassMutation& mutation = input->mass[i];
            if (mutation.body >= config_.bodies || mutation.mass == 0U) throw std::out_of_range("Mass mutation is invalid");
            masses_[mutation.body] = mutation.mass;
            const std::int64_t contact = body_to_contact_[mutation.body];
            if (contact >= 0) contact_dirty_[static_cast<std::size_t>(contact)] = 1U;
        }
        for (std::size_t i = 0U; i < input->contact_count; ++i) {
            const ContactMutation& mutation = input->contact[i];
            if (mutation.contact >= config_.contacts) throw std::out_of_range("Contact mutation index is invalid");
            manifold_[mutation.contact] = canonicalize(mutation.value);
            contact_dirty_[mutation.contact] = 1U;
            topology_changed = true;
        }
    }
    if (topology_changed) rebuild_matching_map();

    std::size_t active_count = 0U;
    if (matching_) {
        for (std::size_t i = 0U; i < contact_dirty_.size(); ++i) {
            if (contact_dirty_[i] != 0U) {
                active_contact_indices_[active_count] = i;
                active_contacts_[active_count] = manifold_[i];
                ++active_count;
            }
        }
    } else {
        active_count = manifold_.size();
        for (std::size_t i = 0U; i < active_count; ++i) {
            active_contact_indices_[i] = i; active_contacts_[i] = manifold_[i];
        }
    }

    if (active_count != 0U) {
        const ArbitraryNormalStats projection = project_arbitrary_normals_inplace(
            velocity_x_, velocity_y_, masses_,
            std::span<const NormalContact>(active_contacts_.data(), active_count),
            config_.projection, scratch_);
        for (std::size_t local = 0U; local < active_count; ++local) {
            dual_[active_contact_indices_[local]] = scratch_.dual_impulses_[local];
        }
        stats_.contacts_projected += projection.contacts_processed;
    }
    ++frame_;
    ++stats_.frames_simulated;
    stats_.matching_fast_path = matching_;
    capture();
}

void AtomicPhysicsEngine::simulate_to(std::uint64_t target_frame) {
    if (target_frame < frame_) throw std::invalid_argument("simulate_to cannot move backwards");
    while (frame_ < target_frame) step_one();
}

void AtomicPhysicsEngine::restore(std::uint64_t frame) {
    const SnapshotSlot& slot = snapshot_slot(frame);
    if (slot.frame != frame) throw std::out_of_range("Requested atomic snapshot is not retained");
    std::copy(slot.position_x.begin(), slot.position_x.end(), position_x_.begin());
    std::copy(slot.position_y.begin(), slot.position_y.end(), position_y_.begin());
    std::copy(slot.velocity_x.begin(), slot.velocity_x.end(), velocity_x_.begin());
    std::copy(slot.velocity_y.begin(), slot.velocity_y.end(), velocity_y_.begin());
    std::copy(slot.masses.begin(), slot.masses.end(), masses_.begin());
    std::copy(slot.dual.begin(), slot.dual.end(), dual_.begin());
    std::copy(slot.manifold.begin(), slot.manifold.end(), manifold_.begin());
    frame_ = frame;
    rebuild_matching_map();
}

void AtomicPhysicsEngine::truncate_after(std::uint64_t frame) {
    for (SnapshotSlot& slot : snapshots_) if (slot.frame != kEmptyFrame && slot.frame > frame) slot.frame = kEmptyFrame;
}

void AtomicPhysicsEngine::correct_and_resimulate(
    std::uint64_t frame, AtomicPhysicsFrameInput corrected, std::uint64_t target_frame) {
    if (frame == 0U || target_frame < frame) throw std::invalid_argument("Invalid correction range");
    set_input(frame, corrected);
    restore(frame - 1U);
    truncate_after(frame - 1U);
    simulate_to(target_frame);
}

std::uint64_t AtomicPhysicsEngine::hash() const noexcept {
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

bool AtomicPhysicsEngine::equivalent_to(const AtomicPhysicsEngine& other) const noexcept {
    return frame_ == other.frame_ && position_x_ == other.position_x_ && position_y_ == other.position_y_
        && velocity_x_ == other.velocity_x_ && velocity_y_ == other.velocity_y_
        && masses_ == other.masses_ && dual_ == other.dual_ && manifold_ == other.manifold_;
}

std::size_t AtomicPhysicsEngine::reserved_bytes() const noexcept {
    std::size_t bytes = (position_x_.capacity() + position_y_.capacity() + velocity_x_.capacity()
        + velocity_y_.capacity() + dual_.capacity()) * sizeof(Fixed::rep)
        + masses_.capacity() * sizeof(std::uint32_t)
        + manifold_.capacity() * sizeof(NormalContact);
    for (const SnapshotSlot& slot : snapshots_) {
        bytes += (slot.position_x.capacity() + slot.position_y.capacity() + slot.velocity_x.capacity()
            + slot.velocity_y.capacity() + slot.dual.capacity()) * sizeof(Fixed::rep)
            + slot.masses.capacity() * sizeof(std::uint32_t)
            + slot.manifold.capacity() * sizeof(NormalContact);
    }
    return bytes;
}

} // namespace neoeng::core
