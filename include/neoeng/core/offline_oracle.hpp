#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

enum class OracleEncoding : std::uint8_t {
    Full = 0,
    Delta = 1,
    Persistent = 2,
};

inline constexpr std::size_t kOracleEncodingCount = 3U;

struct OracleFrameCost final {
    std::array<std::uint64_t, kOracleEncodingCount> operation_cost{};
    std::array<bool, kOracleEncodingCount> allowed{true, true, true};
};

struct OracleConfig final {
    std::array<std::array<std::uint64_t, kOracleEncodingCount>, kOracleEncodingCount>
        transition_cost{};
    std::size_t max_delta_run{16U};
};

struct OracleResult final {
    std::uint64_t total_cost{};
    std::vector<OracleEncoding> sequence{};
};

[[nodiscard]] OracleResult solve_offline_oracle(
    std::span<const OracleFrameCost> frames,
    const OracleConfig& config);

[[nodiscard]] std::uint64_t evaluate_encoding_sequence(
    std::span<const OracleFrameCost> frames,
    std::span<const OracleEncoding> sequence,
    const OracleConfig& config);

} // namespace neoeng::core
