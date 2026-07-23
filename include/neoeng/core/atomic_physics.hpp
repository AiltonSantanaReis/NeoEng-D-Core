#pragma once

#include "neoeng/core/arbitrary_normal_projection.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct VelocityMutation final {
    std::size_t body{};
    Fixed::rep delta_x{};
    Fixed::rep delta_y{};
};

struct MassMutation final {
    std::size_t body{};
    std::uint32_t mass{};
};

struct ContactMutation final {
    std::size_t contact{};
    NormalContact value{};
};

struct AtomicPhysicsFrameInput final {
    std::span<const VelocityMutation> velocity{};
    std::span<const MassMutation> mass{};
    std::span<const ContactMutation> contact{};
};

struct AtomicPhysicsConfig final {
    std::size_t bodies{};
    std::size_t contacts{};
    std::size_t history_capacity{32U};
    std::size_t maximum_velocity_mutations{16U};
    std::size_t maximum_mass_mutations{4U};
    std::size_t maximum_contact_mutations{4U};
    ArbitraryNormalConfig projection{};
};

struct AtomicPhysicsStats final {
    std::uint64_t frames_simulated{};
    std::uint64_t contacts_projected{};
    std::uint64_t snapshots_captured{};
    std::uint64_t payload_values_copied{};
    bool matching_fast_path{};
};

class AtomicPhysicsEngine final {
public:
    explicit AtomicPhysicsEngine(AtomicPhysicsConfig config);

    void initialize(
        std::span<const Fixed::rep> position_x,
        std::span<const Fixed::rep> position_y,
        std::span<const Fixed::rep> velocity_x,
        std::span<const Fixed::rep> velocity_y,
        std::span<const std::uint32_t> masses,
        std::span<const NormalContact> contacts);

    void set_input(std::uint64_t frame, AtomicPhysicsFrameInput input);
    void simulate_to(std::uint64_t target_frame);
    void correct_and_resimulate(std::uint64_t frame, AtomicPhysicsFrameInput corrected, std::uint64_t target_frame);
    void restore(std::uint64_t frame);
    void truncate_after(std::uint64_t frame);

    [[nodiscard]] std::uint64_t frame() const noexcept { return frame_; }
    [[nodiscard]] std::uint64_t hash() const noexcept;
    [[nodiscard]] bool equivalent_to(const AtomicPhysicsEngine& other) const noexcept;
    [[nodiscard]] const AtomicPhysicsStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

private:
    struct SnapshotSlot final {
        std::uint64_t frame{~std::uint64_t{0}};
        std::vector<Fixed::rep> position_x{};
        std::vector<Fixed::rep> position_y{};
        std::vector<Fixed::rep> velocity_x{};
        std::vector<Fixed::rep> velocity_y{};
        std::vector<std::uint32_t> masses{};
        std::vector<Fixed::rep> dual{};
        std::vector<NormalContact> manifold{};
    };

    struct InputSlot final {
        std::uint64_t frame{~std::uint64_t{0}};
        std::vector<VelocityMutation> velocity{};
        std::vector<MassMutation> mass{};
        std::vector<ContactMutation> contact{};
        std::size_t velocity_count{};
        std::size_t mass_count{};
        std::size_t contact_count{};
    };

    void capture();
    void step_one();
    [[nodiscard]] SnapshotSlot& snapshot_slot(std::uint64_t frame) noexcept;
    [[nodiscard]] const SnapshotSlot& snapshot_slot(std::uint64_t frame) const noexcept;
    [[nodiscard]] InputSlot& input_slot(std::uint64_t frame) noexcept;
    [[nodiscard]] const InputSlot* find_input(std::uint64_t frame) const noexcept;
    void rebuild_matching_map();

    AtomicPhysicsConfig config_{};
    std::uint64_t frame_{};
    std::vector<Fixed::rep> position_x_{};
    std::vector<Fixed::rep> position_y_{};
    std::vector<Fixed::rep> velocity_x_{};
    std::vector<Fixed::rep> velocity_y_{};
    std::vector<std::uint32_t> masses_{};
    std::vector<Fixed::rep> dual_{};
    std::vector<NormalContact> manifold_{};
    std::vector<std::int64_t> body_to_contact_{};
    std::vector<std::uint8_t> contact_dirty_{};
    std::vector<std::size_t> active_contact_indices_{};
    std::vector<NormalContact> active_contacts_{};
    std::vector<SnapshotSlot> snapshots_{};
    std::vector<InputSlot> inputs_{};
    ArbitraryNormalScratch scratch_;
    AtomicPhysicsStats stats_{};
    bool initialized_{};
    bool matching_{};
};

} // namespace neoeng::core
