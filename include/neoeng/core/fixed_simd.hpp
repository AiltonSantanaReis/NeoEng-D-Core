#pragma once

#include "neoeng/core/fixed.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace neoeng::core {

enum class FixedKernelMode : std::uint8_t {
    Scalar,
    Auto,
};

struct FixedKernelStats final {
    std::uint64_t lanes{};
    std::uint64_t scalar_lanes{};
    std::uint64_t simd_lanes{};
    std::uint64_t overflow_guard_fallback_lanes{};
    bool avx2_available{};
};

[[nodiscard]] bool fixed_simd_available() noexcept;

void multiply_simulation_delta_exact(
    std::span<const Fixed> input,
    std::span<Fixed> output,
    FixedKernelMode mode = FixedKernelMode::Auto,
    FixedKernelStats* stats = nullptr);

} // namespace neoeng::core
