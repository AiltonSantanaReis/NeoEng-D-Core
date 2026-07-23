#include "neoeng/core/paged_atomic_temporal_physics.hpp"

#include <algorithm>
#include <stdexcept>

namespace neoeng::core {
namespace {
AtomicTemporalPhysicsConfig internal_config(AtomicTemporalPhysicsConfig config) {
    config.history_capacity = 2U;
    config.disable_internal_history = true;
    return config;
}
}

PagedAtomicTemporalPhysicsEngine::PagedAtomicTemporalPhysicsEngine(PagedAtomicTemporalConfig config)
    : config_(config), kernel_(internal_config(config.physics)), history_(config.history),
      inputs_(config.history.history_capacity) {
    if (config.physics.bodies != config.history.bodies
        || config.physics.contacts != config.history.contacts
        || config.physics.maximum_candidate_pairs != config.history.maximum_candidate_pairs
        || config.history.history_capacity < 2U) {
        throw std::invalid_argument("Paged temporal physics/history configuration mismatch");
    }
    for (InputSlot& slot : inputs_) {
        slot.velocity.resize(config.physics.maximum_velocity_mutations);
        slot.mass.resize(config.physics.maximum_mass_mutations);
        slot.contact.resize(config.physics.maximum_contact_mutations);
    }
}

PagedAtomicTemporalPhysicsEngine::InputSlot&
PagedAtomicTemporalPhysicsEngine::input_slot(std::uint64_t frame) noexcept {
    return inputs_[static_cast<std::size_t>(frame % inputs_.size())];
}

const PagedAtomicTemporalPhysicsEngine::InputSlot*
PagedAtomicTemporalPhysicsEngine::find_input(std::uint64_t frame) const noexcept {
    const InputSlot& slot = inputs_[static_cast<std::size_t>(frame % inputs_.size())];
    return slot.frame == frame ? &slot : nullptr;
}

void PagedAtomicTemporalPhysicsEngine::initialize(
    std::span<const Fixed::rep> position_x,
    std::span<const Fixed::rep> position_y,
    std::span<const Fixed::rep> velocity_x,
    std::span<const Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts) {
    history_.clear();
    for (InputSlot& slot : inputs_) {
        slot.frame = ~std::uint64_t{0};
        slot.velocity_count = slot.mass_count = slot.contact_count = 0U;
    }
    kernel_.initialize(position_x, position_y, velocity_x, velocity_y, masses, contacts);
    initialized_ = true;
    capture_external();
}

void PagedAtomicTemporalPhysicsEngine::set_input(
    std::uint64_t frame, AtomicPhysicsFrameInput input) {
    if (!initialized_) throw std::logic_error("Paged temporal physics is not initialized");
    if (input.velocity.size() > config_.physics.maximum_velocity_mutations
        || input.mass.size() > config_.physics.maximum_mass_mutations
        || input.contact.size() > config_.physics.maximum_contact_mutations) {
        throw std::length_error("Paged temporal input exceeds configured capacity");
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

void PagedAtomicTemporalPhysicsEngine::capture_external() {
    history_.capture(kernel_.state_view(), kernel_.capture_hints());
}

void PagedAtomicTemporalPhysicsEngine::step_one() {
    const std::uint64_t next = kernel_.frame() + 1U;
    if (const InputSlot* input = find_input(next)) {
        kernel_.set_input(next, AtomicPhysicsFrameInput{
            .velocity = std::span<const VelocityMutation>(input->velocity.data(), input->velocity_count),
            .mass = std::span<const MassMutation>(input->mass.data(), input->mass_count),
            .contact = std::span<const ContactMutation>(input->contact.data(), input->contact_count),
        });
    } else {
        kernel_.set_input(next, {});
    }
    kernel_.simulate_to(next);
    capture_external();
}

void PagedAtomicTemporalPhysicsEngine::simulate_to(std::uint64_t target_frame) {
    if (target_frame < kernel_.frame()) throw std::invalid_argument("simulate_to cannot move backwards");
    while (kernel_.frame() < target_frame) step_one();
}

void PagedAtomicTemporalPhysicsEngine::restore(std::uint64_t frame) {
    const AtomicTemporalRestoreMetadata metadata =
        history_.restore_direct_from_current(frame, kernel_.frame(), kernel_.mutable_state_view());
    kernel_.finalize_direct_restore(metadata);
}

void PagedAtomicTemporalPhysicsEngine::restore_without_pairs(std::uint64_t frame) {
    const AtomicTemporalRestoreMetadata metadata =
        history_.restore_direct_from_current(frame, kernel_.frame(), kernel_.mutable_state_view(), false);
    kernel_.finalize_direct_restore(metadata);
}

void PagedAtomicTemporalPhysicsEngine::truncate_after(std::uint64_t frame) {
    history_.truncate_after(frame);
}

void PagedAtomicTemporalPhysicsEngine::correct_and_resimulate(
    std::uint64_t frame, AtomicPhysicsFrameInput corrected, std::uint64_t target_frame) {
    if (frame == 0U || target_frame < frame) throw std::invalid_argument("Invalid correction range");
    set_input(frame, corrected);
    restore(frame - 1U);
    truncate_after(frame - 1U);
    simulate_to(target_frame);
}

void PagedAtomicTemporalPhysicsEngine::synchronize_authoritative_pairs(
    std::span<const BroadphasePair> pairs) {
    const AtomicTemporalStateView current = kernel_.state_view();
    if (pairs.size() != current.pairs.size()) {
        throw std::length_error("Authoritative pair count differs from restored physical metadata");
    }
    AtomicTemporalMutableStateView mutable_state = kernel_.mutable_state_view();
    std::copy(pairs.begin(), pairs.end(), mutable_state.pairs.begin());
}

std::size_t PagedAtomicTemporalPhysicsEngine::reserved_bytes() const noexcept {
    std::size_t bytes = kernel_.reserved_bytes() + history_.reserved_bytes();
    for (const InputSlot& slot : inputs_) {
        bytes += slot.velocity.capacity() * sizeof(VelocityMutation)
            + slot.mass.capacity() * sizeof(MassMutation)
            + slot.contact.capacity() * sizeof(ContactMutation);
    }
    return bytes;
}

} // namespace neoeng::core
