#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace neoeng::core {

inline constexpr std::size_t kSha256DigestBytes = 32U;
using Sha256Digest = std::array<std::uint8_t, kSha256DigestBytes>;

class Sha256Builder final {
public:
    void update(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] Sha256Digest finish() noexcept;

private:
    void transform(const std::array<std::uint8_t, 64>& block) noexcept;

    std::array<std::uint32_t, 8> state_{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_size_{};
    std::uint64_t total_bytes_{};
    bool finished_{};
};

[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> bytes) noexcept;
[[nodiscard]] bool sha256_equal(
    std::span<const std::uint8_t> lhs,
    std::span<const std::uint8_t> rhs) noexcept;
[[nodiscard]] bool sha256_is_zero(const Sha256Digest& digest) noexcept;
[[nodiscard]] std::string sha256_hex(const Sha256Digest& digest);

} // namespace neoeng::core
