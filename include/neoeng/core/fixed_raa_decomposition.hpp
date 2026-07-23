#pragma once

#include "neoeng/core/fixed_raa_microkernel.hpp"

#include <cstddef>
#include <cstdint>

namespace neoeng::core {

struct FixedRaaWidthBreakdown final {
    double retained_input_width{};
    double retained_nonlinear_width{};
    double condensation_width{};
    double rounding_width{};

    [[nodiscard]] double total() const noexcept {
        return retained_input_width + retained_nonlinear_width
            + condensation_width + rounding_width;
    }
};

struct FixedRaaDecompositionResult final {
    FixedRaaWidthBreakdown final_width{};
    FixedRaaWidthBreakdown average_width{};
    std::size_t maximum_terms{};
    std::uint64_t compressions{};
    std::uint64_t rounding_guard_raw{};
    std::size_t empirical_violations{};
    std::uint64_t hash{};
};

// Shadow implementation used only for diagnostics. It mirrors the fixed-capacity
// Q32.32 RAA arithmetic while preserving provenance for retained input symbols,
// nonlinear error symbols, condensation residual and rounding guards. It is not
// used by the authoritative physics pipeline or by the timing measurement.
[[nodiscard]] FixedRaaDecompositionResult run_fixed_raa_width_decomposition(
    const FixedRaaMicrokernelConfig& config = {});

} // namespace neoeng::core
