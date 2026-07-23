#pragma once

#include <cstddef>
#include <cstdint>

namespace neoeng::core {

struct UncertaintyLabConfig final {
    std::size_t steps{120U};
    std::size_t monte_carlo_samples{4096U};
    std::size_t timing_repetitions{32U};
    std::size_t full_affine_terms{512U};
    std::size_t reduced_affine_terms{12U};
    std::uint64_t seed{0x2600260026002600ULL};
};

struct UncertaintyMethodMetrics final {
    double final_x_lower{};
    double final_x_upper{};
    double final_v_lower{};
    double final_v_upper{};
    double final_width{};
    double average_width{};
    double runtime_us{};
    std::size_t maximum_terms{};
    std::size_t empirical_violations{};
};

struct UncertaintyLabResult final {
    UncertaintyMethodMetrics interval{};
    UncertaintyMethodMetrics affine{};
    UncertaintyMethodMetrics reduced_affine{};
    double double_final_x{};
    double double_final_v{};
    double fixed_final_x{};
    double fixed_final_v{};
    double double_runtime_us{};
    double fixed_runtime_us{};
    double fixed_x_error{};
    double fixed_v_error{};
    double monte_carlo_final_x_min{};
    double monte_carlo_final_x_max{};
    double monte_carlo_final_v_min{};
    double monte_carlo_final_v_max{};
    std::uint64_t hash{};
};

// Nonlinear 1D oscillator laboratory:
//   a = -k*x - alpha*x^3
//   v(t+dt) = v(t) + dt*a
//   x(t+dt) = x(t) + dt*v(t+dt)
//
// It is intentionally isolated from the production physics path. The result
// compares deterministic center trajectories (double and Q32.32) with interval,
// affine and reduced-affine enclosures. Monte Carlo containment is empirical;
// it is not a formal proof of floating-point outward rounding.
[[nodiscard]] UncertaintyLabResult run_uncertainty_lab(const UncertaintyLabConfig& config = {});

} // namespace neoeng::core
