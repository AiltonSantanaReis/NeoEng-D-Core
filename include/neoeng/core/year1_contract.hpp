#pragma once

#include <cstdint>

namespace neoeng::core {

// Internal Year-1 canonical/replay contract. This is not the public Host C ABI
// and does not promise C++ object-layout compatibility across toolchains. Replay
// schema version 1 is frozen; incompatible changes require a new schema.
inline constexpr std::uint16_t kYear1InternalContractVersion = 1U;
// Historical source-compatibility alias. See ADR-009.
inline constexpr std::uint16_t kYear1AbiCandidateVersion = kYear1InternalContractVersion;
inline constexpr std::uint32_t kYear1ReplayMagic = 0x3152504EU; // "NPR1" LE
inline constexpr std::uint16_t kYear1ReplaySchemaVersion = 1U;
inline constexpr std::uint16_t kYear1LegacyReplaySchemaVersion = 0U;
inline constexpr std::uint32_t kYear1DefaultTickRateHz = 60U;

} // namespace neoeng::core
