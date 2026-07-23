#pragma once

#include "neoeng/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace neoeng::core {

inline constexpr std::uint32_t kCanonicalWorldMagic = 0x574F454EU; // "NEOW" in LE bytes
inline constexpr std::uint16_t kCanonicalWorldFormatVersion = 1U;

class WorldHashBuilder final {
public:
    WorldHashBuilder(std::uint64_t frame, std::size_t body_count) noexcept;

    void append(const Body& body) noexcept;
    [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

private:
    void append_byte(std::uint8_t byte) noexcept;

    template <typename T>
    void append_little_endian(T value) noexcept;

    std::uint64_t hash_{};
};

[[nodiscard]] std::vector<std::uint8_t> canonical_serialize(const WorldState& state);
[[nodiscard]] std::uint64_t stable_hash(const WorldState& state) noexcept;
[[nodiscard]] std::string hash_hex(std::uint64_t value);

} // namespace neoeng::core
