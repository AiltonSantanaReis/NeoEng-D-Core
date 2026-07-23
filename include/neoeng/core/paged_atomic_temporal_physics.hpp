#pragma once

#include "neoeng/core/paged_atomic_history.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct PagedAtomicTemporalConfig final {
    AtomicTemporalPhysicsConfig physics{};
    PagedAtomicHistoryConfig history{};
};

struct PagedAtomicTemporalStats final {
    AtomicTemporalPhysicsStats physics{};
    PagedAtomicHistoryStats history{};
};

// v0.17: the physical kernel keeps only a two-frame private ring. The authoritative
// rollback history is a fixed-capacity, page-shared atomic history outside the kernel.
class PagedAtomicTemporalPhysicsEngine final {
public:
    explicit PagedAtomicTemporalPhysicsEngine(PagedAtomicTemporalConfig config);

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
    // External authoritative pair histories use this variant to avoid restoring
    // the internal pair pages immediately before overwriting them.
    void restore_without_pairs(std::uint64_t frame);
    void truncate_after(std::uint64_t frame);
    void correct_and_resimulate(
        std::uint64_t frame,
        AtomicPhysicsFrameInput corrected,
        std::uint64_t target_frame);

    [[nodiscard]] std::uint64_t frame() const noexcept { return kernel_.frame(); }
    [[nodiscard]] std::uint64_t hash() const noexcept { return kernel_.hash(); }
    [[nodiscard]] std::uint64_t physical_hash() const noexcept { return kernel_.physical_hash(); }
    [[nodiscard]] bool physically_equivalent_to(const PagedAtomicTemporalPhysicsEngine& other) const noexcept {
        return kernel_.physically_equivalent_to(other.kernel_);
    }
    [[nodiscard]] bool equivalent_to(const PagedAtomicTemporalPhysicsEngine& other) const noexcept {
        return kernel_.equivalent_to(other.kernel_);
    }
    [[nodiscard]] bool physically_equivalent_to(const AtomicTemporalPhysicsEngine& other) const noexcept {
        return kernel_.physically_equivalent_to(other);
    }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;
    [[nodiscard]] std::size_t history_live_payload_bytes() const noexcept {
        return history_.live_payload_bytes();
    }
    [[nodiscard]] PagedAtomicTemporalStats stats() const noexcept {
        return PagedAtomicTemporalStats{kernel_.stats(), history_.stats()};
    }
    [[nodiscard]] AtomicTemporalStateView state_view() const noexcept { return kernel_.state_view(); }
    [[nodiscard]] AtomicTemporalCaptureHints capture_hints() const noexcept { return kernel_.capture_hints(); }
    // v0.21: overwrite the retained pair cache from an authoritative external history.
    // The pair count must match the physical snapshot metadata restored by PagedAtomicHistory.
    void synchronize_authoritative_pairs(std::span<const BroadphasePair> pairs);

    // v0.25: restore directly into the kernel-owned pair span. This removes the
    // intermediate contiguous buffer used by external segmented histories while
    // preserving the same count check and canonical ordering contract.
    template <class PairHistory>
    void synchronize_authoritative_pairs_from(const PairHistory& history, std::uint64_t frame_value) {
        const AtomicTemporalStateView current = kernel_.state_view();
        AtomicTemporalMutableStateView mutable_state = kernel_.mutable_state_view();
        const std::size_t restored = history.restore_pairs(frame_value, mutable_state.pairs);
        if (restored != current.pairs.size()) {
            throw std::length_error("Authoritative pair count differs from restored physical metadata");
        }
    }

private:
    struct InputSlot final {
        std::uint64_t frame{~std::uint64_t{0}};
        std::vector<VelocityMutation> velocity{};
        std::vector<MassMutation> mass{};
        std::vector<ContactMutation> contact{};
        std::size_t velocity_count{};
        std::size_t mass_count{};
        std::size_t contact_count{};
    };

    [[nodiscard]] InputSlot& input_slot(std::uint64_t frame) noexcept;
    [[nodiscard]] const InputSlot* find_input(std::uint64_t frame) const noexcept;
    void step_one();
    void capture_external();

    PagedAtomicTemporalConfig config_{};
    AtomicTemporalPhysicsEngine kernel_;
    PagedAtomicHistory history_;
    std::vector<InputSlot> inputs_{};
    bool initialized_{};
};

} // namespace neoeng::core
