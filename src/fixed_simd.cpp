#include "neoeng/core/fixed_simd.hpp"

#include <array>
#include <limits>
#include <stdexcept>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace neoeng::core {
namespace {

[[nodiscard]] Fixed scalar_multiply_delta(Fixed value) {
    return value * kSimulationDelta;
}

#if defined(__x86_64__) || defined(_M_X64)
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("avx2")))
#endif
std::uint64_t multiply_four_avx2(
    const std::int64_t* input,
    std::int64_t* output) {
    const __m256i values = _mm256_loadu_si256(
        reinterpret_cast<const __m256i*>(input));
    const __m256i zero = _mm256_setzero_si256();
    const __m256i sign = _mm256_cmpgt_epi64(zero, values);
    const __m256i magnitude = _mm256_sub_epi64(_mm256_xor_si256(values, sign), sign);
    const __m256i low_mask = _mm256_set1_epi64x(0xFFFF'FFFFLL);
    const __m256i low = _mm256_and_si256(magnitude, low_mask);
    const __m256i high = _mm256_srli_epi64(magnitude, Fixed::fractional_bits);
    const __m256i multiplier = _mm256_set1_epi64x(kSimulationDelta.raw());
    const __m256i high_product = _mm256_mul_epu32(high, multiplier);
    const __m256i low_product = _mm256_mul_epu32(low, multiplier);
    const __m256i quotient_magnitude = _mm256_add_epi64(
        high_product, _mm256_srli_epi64(low_product, Fixed::fractional_bits));

    alignas(32) std::array<std::uint64_t, 4U> magnitudes{};
    _mm256_storeu_si256(
        reinterpret_cast<__m256i*>(magnitudes.data()), quotient_magnitude);
    std::uint64_t fallbacks = 0U;
    constexpr std::uint64_t positive_limit = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    constexpr std::uint64_t negative_limit = std::uint64_t{1} << 63U;
    for (std::size_t lane = 0U; lane < 4U; ++lane) {
        if (input[lane] >= 0) {
            if (magnitudes[lane] > positive_limit) {
                output[lane] = scalar_multiply_delta(Fixed::from_raw(input[lane])).raw();
                ++fallbacks;
            } else {
                output[lane] = static_cast<std::int64_t>(magnitudes[lane]);
            }
        } else {
            if (magnitudes[lane] > negative_limit) {
                output[lane] = scalar_multiply_delta(Fixed::from_raw(input[lane])).raw();
                ++fallbacks;
            } else if (magnitudes[lane] == negative_limit) {
                output[lane] = std::numeric_limits<std::int64_t>::min();
            } else {
                output[lane] = -static_cast<std::int64_t>(magnitudes[lane]);
            }
        }
    }
    return fallbacks;
}
#endif

} // namespace

bool fixed_simd_available() noexcept {
#if (defined(__x86_64__) || defined(_M_X64)) && (defined(__GNUC__) || defined(__clang__))
    return __builtin_cpu_supports("avx2");
#else
    return false;
#endif
}

void multiply_simulation_delta_exact(
    std::span<const Fixed> input,
    std::span<Fixed> output,
    FixedKernelMode mode,
    FixedKernelStats* stats) {
    if (output.size() != input.size()) {
        throw std::invalid_argument("Fixed SIMD input and output spans must have equal length");
    }

    FixedKernelStats local;
    local.lanes = input.size();
    local.avx2_available = fixed_simd_available();
    std::size_t cursor = 0U;

#if defined(__x86_64__) || defined(_M_X64)
    if (mode == FixedKernelMode::Auto && local.avx2_available) {
        alignas(32) std::array<std::int64_t, 4U> raw_input{};
        alignas(32) std::array<std::int64_t, 4U> raw_output{};
        for (; cursor + 4U <= input.size(); cursor += 4U) {
            for (std::size_t lane = 0U; lane < 4U; ++lane) {
                raw_input[lane] = input[cursor + lane].raw();
            }
            const std::uint64_t fallbacks = multiply_four_avx2(
                raw_input.data(), raw_output.data());
            for (std::size_t lane = 0U; lane < 4U; ++lane) {
                output[cursor + lane] = Fixed::from_raw(raw_output[lane]);
            }
            local.simd_lanes += 4U - fallbacks;
            local.scalar_lanes += fallbacks;
            local.overflow_guard_fallback_lanes += fallbacks;
        }
    }
#endif

    for (; cursor < input.size(); ++cursor) {
        output[cursor] = scalar_multiply_delta(input[cursor]);
        ++local.scalar_lanes;
    }
    if (stats != nullptr) {
        stats->lanes += local.lanes;
        stats->scalar_lanes += local.scalar_lanes;
        stats->simd_lanes += local.simd_lanes;
        stats->overflow_guard_fallback_lanes += local.overflow_guard_fallback_lanes;
        stats->avx2_available = stats->avx2_available || local.avx2_available;
    }
}

} // namespace neoeng::core
