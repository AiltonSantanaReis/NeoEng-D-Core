#pragma once

#include "neoeng/core/component_world.hpp"
#include "neoeng/core/fixed.hpp"
#include "neoeng/core/indexed_ring.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace neoeng::core {

// Signed Q1.30 contact normal. The implementation supports any non-zero vector;
// unit-length proximity is reported and checked separately.
struct NormalQ30 final {
    std::int32_t x{};
    std::int32_t y{};

    auto operator<=>(const NormalQ30&) const = default;
};

struct NormalContact final {
    std::size_t first{};
    std::size_t second{};
    NormalQ30 normal{};

    auto operator<=>(const NormalContact&) const = default;
};

enum class ArbitraryNormalMethod : std::uint8_t {
    CertifiedMatching,
    CoordinateFallback,
};

[[nodiscard]] const char* to_string(ArbitraryNormalMethod method) noexcept;

struct ArbitraryNormalConfig final {
    std::size_t maximum_iterations{16U};
    std::uint64_t feasibility_tolerance_raw{8U};
    // |nx^2 + ny^2 - 1| in Q2.60. This validates the caller's normalized encoding.
    std::uint64_t unit_norm_tolerance_q60{1ULL << 38U};
};

struct ArbitraryNormalResiduals final {
    std::uint64_t primal_linf_raw{};
    std::uint64_t unit_norm_linf_q60{};
    std::uint64_t weighted_momentum_linf_raw{};
    bool feasible{};
    bool certified{};
};

struct ArbitraryNormalStats final {
    ArbitraryNormalMethod method{ArbitraryNormalMethod::CoordinateFallback};
    std::uint64_t contacts_processed{};
    std::uint64_t coordinate_updates{};
    std::uint64_t iterations{};
    std::uint64_t changed_bodies{};
    std::uint64_t matching_contacts{};
    std::uint64_t fallback_contacts{};
    ArbitraryNormalResiduals residuals{};
};

class ArbitraryNormalScratch final {
public:
    ArbitraryNormalScratch(std::size_t maximum_bodies, std::size_t maximum_contacts);

    [[nodiscard]] std::size_t maximum_bodies() const noexcept { return maximum_bodies_; }
    [[nodiscard]] std::size_t maximum_contacts() const noexcept { return maximum_contacts_; }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

public:
    std::size_t maximum_bodies_{};
    std::size_t maximum_contacts_{};
    std::vector<Fixed::rep> velocity_x_{};
    std::vector<Fixed::rep> velocity_y_{};
    std::vector<std::uint32_t> degree_{};
    std::vector<NormalContact> contacts_{};
    std::vector<Fixed::rep> dual_impulses_{};
    std::vector<ComponentPatch> patches_{};
};

struct ArbitraryNormalProjectionResult final {
    ComponentWorldState state{};
    ArbitraryNormalStats stats{};
    ComponentAllocationStats allocation{};
    std::vector<Fixed::rep> dual_impulses{};
    std::vector<NormalContact> manifold{};
};

// Allocation-free after scratch construction. Velocities and contacts may alias no scratch buffer.
// Matching graphs are independent half-space projections and receive a quantized certificate.
// Connected graphs use deterministic cyclic projections and remain non-certified.
[[nodiscard]] ArbitraryNormalStats project_arbitrary_normals_inplace(
    std::span<Fixed::rep> velocity_x,
    std::span<Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ArbitraryNormalConfig config,
    ArbitraryNormalScratch& scratch);

// Fast path for a contact set whose canonicality, uniqueness and matching degree
// have already been verified by the owning engine. Pair projections are independent,
// so no sorting or degree scan is repeated in the steady-state.
[[nodiscard]] ArbitraryNormalStats project_verified_matching_inplace(
    std::span<Fixed::rep> velocity_x,
    std::span<Fixed::rep> velocity_y,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ArbitraryNormalConfig config,
    std::span<Fixed::rep> dual_impulses);

[[nodiscard]] ArbitraryNormalProjectionResult project_arbitrary_normal_contacts_2d(
    const ComponentWorldState& current,
    std::span<const std::uint32_t> masses,
    std::span<const NormalContact> contacts,
    ArbitraryNormalConfig config,
    ArbitraryNormalScratch& scratch);

struct PhysicsAuxSnapshot final {
    std::uint64_t frame{};
    std::shared_ptr<const std::vector<std::uint32_t>> masses{};
    std::shared_ptr<const std::vector<Fixed::rep>> dual_impulses{};
    std::shared_ptr<const std::vector<NormalContact>> manifold{};
};

class PhysicsAuxHistory final {
public:
    explicit PhysicsAuxHistory(std::size_t capacity) : ring_(capacity) {}

    void capture(PhysicsAuxSnapshot snapshot);
    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept { return ring_.contains(frame); }
    [[nodiscard]] const PhysicsAuxSnapshot& at(std::uint64_t frame) const { return ring_.at(frame); }
    void truncate_after(std::uint64_t frame) { ring_.truncate_after(frame); }
    [[nodiscard]] std::size_t size() const noexcept { return ring_.size(); }

private:
    IndexedFrameRing<PhysicsAuxSnapshot> ring_;
};

[[nodiscard]] std::uint64_t hash_physics_aux(const PhysicsAuxSnapshot& snapshot) noexcept;

} // namespace neoeng::core
