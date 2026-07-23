#pragma once

#include <cstdint>

namespace neoeng::core {

// Candidate contract for the Year-1 freeze. These values are versioned now,
// but remain explicitly unfrozen until Y1-G1..Y1-G5 are accepted.
inline constexpr std::uint16_t kYear1AbiCandidateVersion = 1U;
inline constexpr std::uint32_t kYear1ReplayMagic = 0x3152504EU; // "NPR1" LE
inline constexpr std::uint16_t kYear1ReplaySchemaVersion = 1U;
inline constexpr std::uint16_t kYear1LegacyReplaySchemaVersion = 0U;
inline constexpr std::uint32_t kYear1DefaultTickRateHz = 60U;

} // namespace neoeng::core
