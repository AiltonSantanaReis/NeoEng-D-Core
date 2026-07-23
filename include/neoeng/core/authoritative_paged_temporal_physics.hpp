#pragma once

#include "neoeng/core/dynamic_island_pair_history.hpp"
#include "neoeng/core/paged_atomic_temporal_physics.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct AuthoritativePagedTemporalConfig final {
    PagedAtomicTemporalConfig physics{};
    DynamicIslandPairHistoryConfig pair_history{};
};

struct AuthoritativePagedTemporalStats final {
    PagedAtomicTemporalStats physics{};
    DynamicIslandPairHistoryStats pair_history{};
};

// v0.21 wrapper: physical pages retain body/cache payloads while the dynamic island
// history is the authoritative retained source of broadphase pairs and island topology.
// On restore, the physical kernel pair array is overwritten from the authoritative frame.
class AuthoritativePagedTemporalPhysicsEngine final {
public:
    explicit AuthoritativePagedTemporalPhysicsEngine(AuthoritativePagedTemporalConfig config);

    void initialize(
        std::span<const Fixed::rep> position_x,
        std::span<const Fixed::rep> position_y,
        std::span<const Fixed::rep> velocity_x,
        std::span<const Fixed::rep> velocity_y,
        std::span<const std::uint32_t> masses,
        std::span<const NormalContact> contacts);

    void set_input(std::uint64_t frame, AtomicPhysicsFrameInput input);
    void simulate_to(std::uint64_t target_frame);
    void restore(std::uint64_t frame);
    void truncate_after(std::uint64_t frame);
    void correct_and_resimulate(
        std::uint64_t frame,
        AtomicPhysicsFrameInput corrected,
        std::uint64_t target_frame);

    [[nodiscard]] std::uint64_t frame() const noexcept { return physics_.frame(); }
    [[nodiscard]] std::uint64_t physical_hash() const noexcept { return physics_.physical_hash(); }
    [[nodiscard]] std::uint64_t authoritative_pair_hash() const { return pairs_.hash(frame()); }
    [[nodiscard]] bool physically_equivalent_to(
        const AuthoritativePagedTemporalPhysicsEngine& other) const noexcept {
        return physics_.physically_equivalent_to(other.physics_);
    }
    [[nodiscard]] AtomicTemporalStateView state_view() const noexcept { return physics_.state_view(); }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept {
        return physics_.reserved_bytes() + pairs_.reserved_bytes()
            + restored_pairs_.capacity() * sizeof(BroadphasePair);
    }
    [[nodiscard]] AuthoritativePagedTemporalStats stats() const noexcept {
        return {physics_.stats(), pairs_.stats()};
    }

private:
    void step_one();
    void capture_pairs();
    void synchronize_pairs(std::uint64_t frame);

    AuthoritativePagedTemporalConfig config_{};
    PagedAtomicTemporalPhysicsEngine physics_;
    DynamicIslandPairHistory pairs_;
    std::vector<BroadphasePair> restored_pairs_{};
    bool initialized_{};
};

} // namespace neoeng::core
