#include "neoeng/core/segmented_authoritative_paged_temporal_physics.hpp"

#include <stdexcept>

namespace neoeng::core {

SegmentedAuthoritativePagedTemporalPhysicsEngine::SegmentedAuthoritativePagedTemporalPhysicsEngine(
    SegmentedAuthoritativeTemporalConfig config)
    : config_(config), physics_(config.physics), pairs_(config.pair_history) {
    if (config.physics.physics.bodies != config.pair_history.bodies
        || config.physics.physics.contacts != config.pair_history.maximum_contacts
        || config.physics.physics.maximum_candidate_pairs != config.pair_history.maximum_pairs
        || config.physics.history.history_capacity != config.pair_history.history_capacity) {
        throw std::invalid_argument("Segmented authoritative temporal configuration mismatch");
    }
}

void SegmentedAuthoritativePagedTemporalPhysicsEngine::initialize(
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

void SegmentedAuthoritativePagedTemporalPhysicsEngine::set_input(
    std::uint64_t frame_value, AtomicPhysicsFrameInput input) {
    physics_.set_input(frame_value, input);
}

void SegmentedAuthoritativePagedTemporalPhysicsEngine::capture_pairs() {
    const AtomicTemporalCaptureHints hints = physics_.capture_hints();
    const AtomicTemporalStateView state = physics_.state_view();
    // The kernel's changed_bodies set is complete for pair-cache dependencies.
    // A frame that did not rebuild the cache proves that the retained pair set is unchanged.
    const std::span<const std::size_t> dirty = hints.cache_rebuilt
        ? hints.changed_bodies : std::span<const std::size_t>{};
    pairs_.capture(state.frame, state.manifold, state.pairs, dirty,
        hints.topology_changed, true, true);
}

void SegmentedAuthoritativePagedTemporalPhysicsEngine::step_one() {
    physics_.simulate_to(physics_.frame() + 1U);
    capture_pairs();
}

void SegmentedAuthoritativePagedTemporalPhysicsEngine::simulate_to(std::uint64_t target_frame) {
    if (!initialized_) throw std::logic_error("Segmented authoritative physics is not initialized");
    if (target_frame < frame()) throw std::invalid_argument("simulate_to cannot move backwards");
    while (frame() < target_frame) step_one();
}

void SegmentedAuthoritativePagedTemporalPhysicsEngine::synchronize_pairs(std::uint64_t frame_value) {
    physics_.synchronize_authoritative_pairs_from(pairs_, frame_value);
}

void SegmentedAuthoritativePagedTemporalPhysicsEngine::restore(std::uint64_t frame_value) {
    physics_.restore_without_pairs(frame_value);
    synchronize_pairs(frame_value);
}

void SegmentedAuthoritativePagedTemporalPhysicsEngine::truncate_after(std::uint64_t frame_value) {
    physics_.truncate_after(frame_value);
    pairs_.truncate_after(frame_value);
}

void SegmentedAuthoritativePagedTemporalPhysicsEngine::correct_and_resimulate(
    std::uint64_t frame_value,
    AtomicPhysicsFrameInput corrected,
    std::uint64_t target_frame) {
    if (frame_value == 0U || target_frame < frame_value) {
        throw std::invalid_argument("Invalid segmented authoritative correction range");
    }
    set_input(frame_value, corrected);
    restore(frame_value - 1U);
    truncate_after(frame_value - 1U);
    simulate_to(target_frame);
}

} // namespace neoeng::core
