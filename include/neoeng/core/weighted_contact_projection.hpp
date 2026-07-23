#pragma once

#include "neoeng/core/component_world.hpp"
#include "neoeng/core/island_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

enum class WeightedProjectionMethod : std::uint8_t {
    QuantizedDykstra,
    CertifiedAuto,
};

[[nodiscard]] const char* to_string(WeightedProjectionMethod method) noexcept;

struct WeightedProjectionConfig final {
    std::size_t maximum_iterations{2'048U};
    std::uint64_t certification_tolerance_raw{1U};
};

struct WeightedProjectionResiduals final {
    std::uint64_t primal_linf_raw{};
    std::uint64_t dual_linf_weighted_raw{};
    std::uint64_t stationarity_linf_weighted_raw{};
    std::uint64_t complementarity_linf_scaled_raw{};
    std::uint64_t quantization_linf_weighted_raw{};
    bool certified{};
};

struct WeightedProjectionStats final {
    WeightedProjectionMethod method{WeightedProjectionMethod::CertifiedAuto};
    std::uint64_t axes_processed{};
    std::uint64_t islands_processed{};
    std::uint64_t contacts_processed{};
    std::uint64_t total_order_reductions{};
    std::uint64_t star_reductions{};
    std::uint64_t iterative_fallbacks{};
    std::uint64_t iterations{};
    std::uint64_t coordinate_updates{};
    std::uint64_t changed_bodies{};
    std::uint64_t weighted_momentum_error_raw{};
    WeightedProjectionResiduals residuals{};
};

struct WeightedVelocityProjectionResult;

class WeightedProjectionScratch final {
public:
    WeightedProjectionScratch(std::size_t maximum_bodies, std::size_t maximum_contacts);

    [[nodiscard]] std::size_t maximum_bodies() const noexcept { return maximum_bodies_; }
    [[nodiscard]] std::size_t maximum_contacts() const noexcept { return maximum_contacts_; }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

public:
    // Fixed-capacity buffers are public to allocation-free implementation helpers.
    std::size_t maximum_bodies_{};
    std::size_t maximum_contacts_{};
    ContactIslandWorkspace axis_workspace_;
    std::vector<SweptContact> axis_contacts_{};
    std::vector<Fixed::rep> original_{};
    std::vector<Fixed::rep> values_{};
    std::vector<Fixed::rep> other_axis_{};
    std::vector<Fixed::rep> correction_first_{};
    std::vector<Fixed::rep> correction_second_{};
    std::vector<std::size_t> block_begin_{};
    std::vector<std::size_t> block_end_{};
    std::vector<WideInteger> block_weighted_sum_{};
    std::vector<std::uint64_t> block_weight_{};
    std::vector<std::size_t> chain_edge_{};
    std::vector<std::size_t> star_leaves_{};
    std::vector<ComponentPatch> patches_{};

};

struct WeightedVelocityProjectionResult final {
    ComponentWorldState state{};
    WeightedProjectionStats stats{};
    ComponentAllocationStats allocation{};
};

// Projects normal velocity components independently by contact axis. The objective is
// 1/2 sum_i mass_i * (v_i - v_i_original)^2. Masses are positive integer weights.
// A certified total-order island is solved by weighted PAV; all other islands use a
// deterministic quantized Dykstra fallback and are certified only when residual bounds pass.
[[nodiscard]] WeightedVelocityProjectionResult project_weighted_contact_velocities_2d(
    const ComponentWorldState& current,
    std::span<const std::uint32_t> masses,
    std::span<const SweptContact> contacts,
    WeightedProjectionMethod method,
    WeightedProjectionConfig config,
    WeightedProjectionScratch& scratch);

} // namespace neoeng::core
