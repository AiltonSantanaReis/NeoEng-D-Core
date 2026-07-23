#include "neoeng/core/authoritative_paged_temporal_physics.hpp"

#include <algorithm>
#include <stdexcept>

namespace neoeng::core {

AuthoritativePagedTemporalPhysicsEngine::AuthoritativePagedTemporalPhysicsEngine(
    AuthoritativePagedTemporalConfig config)
    : config_(config), physics_(config.physics), pairs_(config.pair_history),
      restored_pairs_(config.pair_history.maximum_pairs) {
    if (config.physics.physics.bodies != config.pair_history.bodies
        || config.physics.physics.contacts != config.pair_history.maximum_contacts
        || config.physics.physics.maximum_candidate_pairs != config.pair_history.maximum_pairs
        || config.physics.history.history_capacity != config.pair_history.history_capacity) {
        throw std::invalid_argument("Authoritative temporal physics configuration mismatch");
    }
}

void AuthoritativePagedTemporalPhysicsEngine::initialize(
    std::span<const Fixed::rep> position_x,
    std::span<const Fixed::rep> position_y,
    std::span<const Fixed::rep> velocity_x,
    std::span<const Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts) {
    physics_.initialize(position_x, position_y, velocity_x, velocity_y, masses, contacts);
    const AtomicTemporalStateView state = physics_.state_view();
    pairs_.initialize(state.frame, state.manifold, state.pairs);
    initialized_ = true;
}

void AuthoritativePagedTemporalPhysicsEngine::set_input(
    std::uint64_t frame, AtomicPhysicsFrameInput input) {
    physics_.set_input(frame, input);
}

void AuthoritativePagedTemporalPhysicsEngine::capture_pairs() {
    const AtomicTemporalCaptureHints hints = physics_.capture_hints();
    const AtomicTemporalStateView state = physics_.state_view();
    const std::span<const std::size_t> dirty = hints.cache_rebuilt
        ? hints.changed_bodies : std::span<const std::size_t>{};
    pairs_.capture(state.frame, state.manifold, state.pairs, dirty, hints.topology_changed, true);
}

void AuthoritativePagedTemporalPhysicsEngine::step_one() {
    physics_.simulate_to(physics_.frame() + 1U);
    capture_pairs();
}

void AuthoritativePagedTemporalPhysicsEngine::simulate_to(std::uint64_t target_frame) {
    if (!initialized_) throw std::logic_error("Authoritative temporal physics is not initialized");
    if (target_frame < frame()) throw std::invalid_argument("simulate_to cannot move backwards");
    while (frame() < target_frame) step_one();
}

void AuthoritativePagedTemporalPhysicsEngine::synchronize_pairs(std::uint64_t frame_value) {
    const std::size_t count = pairs_.restore_pairs(frame_value, restored_pairs_);
    physics_.synchronize_authoritative_pairs(
        std::span<const BroadphasePair>(restored_pairs_.data(), count));
}

void AuthoritativePagedTemporalPhysicsEngine::restore(std::uint64_t frame_value) {
    physics_.restore(frame_value);
    synchronize_pairs(frame_value);
}

void AuthoritativePagedTemporalPhysicsEngine::truncate_after(std::uint64_t frame_value) {
    physics_.truncate_after(frame_value);
    pairs_.truncate_after(frame_value);
}

void AuthoritativePagedTemporalPhysicsEngine::correct_and_resimulate(
    std::uint64_t frame_value,
    AtomicPhysicsFrameInput corrected,
    std::uint64_t target_frame) {
    if (frame_value == 0U || target_frame < frame_value) {
        throw std::invalid_argument("Invalid authoritative correction range");
    }
    set_input(frame_value, corrected);
    restore(frame_value - 1U);
    truncate_after(frame_value - 1U);
    simulate_to(target_frame);
}

} // namespace neoeng::core
