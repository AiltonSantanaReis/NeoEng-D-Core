#pragma once

#include "neoeng/core/fixed.hpp"

#include <cstddef>
#include <cstdint>

namespace neoeng::core {

struct FixedRaaMicrokernelConfig final {
    std::size_t bodies{10'000U};
    std::size_t steps{16U};
    std::size_t maximum_terms{12U};
    std::size_t monte_carlo_samples{2'048U};
    std::size_t timing_repetitions{24U};
    std::uint64_t seed{0x2700270027002700ULL};
};


struct FixedRaaOperationProbeResult final {
    std::size_t iterations{};
    std::size_t maximum_terms{};
    std::uint64_t compressions{};
    std::uint64_t rounding_guard_raw{};
    std::uint64_t disjoint_product_cases{};
    std::uint64_t hash{};
};

struct FixedRaaMicrokernelResult final {
    double p50_us{};
    double p95_us{};
    double ns_per_body_step{};
    double ns_per_contact_step{};
    double final_total_width{};
    double average_total_width{};
    std::size_t maximum_terms{};
    std::uint64_t compressions{};
    std::uint64_t rounding_guard_raw{};
    std::size_t empirical_violations{};
    std::uint64_t hash{};
};

// Independent soft-contact pairs in one dimension:
//   g = (x2-x1)-rest
//   r = v2-v1
//   f = -k*g - c*r - alpha*g^3
//   v1' = v1 - dt*f, v2' = v2 + dt*f
//   x'  = x + dt*v'
//
// Every RAA value is fixed-capacity and Q32.32. Discarded affine terms and
// arithmetic truncation guards are accumulated in an explicit residual radius.
// The returned empirical containment result is checked against Monte Carlo; the
// implementation's local rounding model is documented, but the Monte Carlo test
// is not by itself a proof for all real inputs.
[[nodiscard]] FixedRaaMicrokernelResult run_fixed_raa_microkernel(
    const FixedRaaMicrokernelConfig& config = {});

// Stack-only deterministic exercise of the RAA arithmetic paths. This function
// deliberately excludes benchmark setup containers and is intended to be called
// inside an external C/C++ heap-allocation observation window.
[[nodiscard]] FixedRaaOperationProbeResult run_fixed_raa_operation_probe(
    std::size_t iterations = 256U, std::size_t maximum_terms = 8U);

} // namespace neoeng::core
