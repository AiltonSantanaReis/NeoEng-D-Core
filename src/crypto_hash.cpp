#include "neoeng/core/crypto_hash.hpp"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <sstream>

namespace neoeng::core {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

} // namespace

void Sha256Builder::update(std::span<const std::uint8_t> bytes) noexcept {
    if (finished_) {
        return;
    }
    for (const std::uint8_t byte : bytes) {
        buffer_[buffer_size_++] = byte;
        ++total_bytes_;
        if (buffer_size_ == buffer_.size()) {
            transform(buffer_);
            buffer_size_ = 0U;
        }
    }
}

Sha256Digest Sha256Builder::finish() noexcept {
    if (finished_) {
        Sha256Digest digest{};
        for (std::size_t word = 0; word < state_.size(); ++word) {
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                digest[word * 4U + byte] = static_cast<std::uint8_t>(
                    state_[word] >> ((3U - byte) * 8U));
            }
        }
        return digest;
    }

    const std::uint64_t total_bits = total_bytes_ * 8U;
    buffer_[buffer_size_++] = 0x80U;
    if (buffer_size_ > 56U) {
        while (buffer_size_ < buffer_.size()) {
            buffer_[buffer_size_++] = 0U;
        }
        transform(buffer_);
        buffer_size_ = 0U;
    }
    while (buffer_size_ < 56U) {
        buffer_[buffer_size_++] = 0U;
    }
    for (std::size_t index = 0; index < 8U; ++index) {
        const std::size_t shift = (7U - index) * 8U;
        buffer_[buffer_size_++] = static_cast<std::uint8_t>(total_bits >> shift);
    }
    transform(buffer_);
    finished_ = true;

    Sha256Digest digest{};
    for (std::size_t word = 0; word < state_.size(); ++word) {
        for (std::size_t byte = 0; byte < 4U; ++byte) {
            digest[word * 4U + byte] = static_cast<std::uint8_t>(
                state_[word] >> ((3U - byte) * 8U));
        }
    }
    return digest;
}

void Sha256Builder::transform(const std::array<std::uint8_t, 64>& block) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16U; ++index) {
        const std::size_t offset = index * 4U;
        words[index] = (static_cast<std::uint32_t>(block[offset]) << 24U)
            | (static_cast<std::uint32_t>(block[offset + 1U]) << 16U)
            | (static_cast<std::uint32_t>(block[offset + 2U]) << 8U)
            | static_cast<std::uint32_t>(block[offset + 3U]);
    }
    for (std::size_t index = 16U; index < words.size(); ++index) {
        const std::uint32_t s0 = std::rotr(words[index - 15U], 7)
            ^ std::rotr(words[index - 15U], 18) ^ (words[index - 15U] >> 3U);
        const std::uint32_t s1 = std::rotr(words[index - 2U], 17)
            ^ std::rotr(words[index - 2U], 19) ^ (words[index - 2U] >> 10U);
        words[index] = words[index - 16U] + s0 + words[index - 7U] + s1;
    }

    auto [a, b, c, d, e, f, g, h] = state_;
    for (std::size_t index = 0; index < words.size(); ++index) {
        const std::uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const std::uint32_t choose = (e & f) ^ ((~e) & g);
        const std::uint32_t temporary1 = h + sum1 + choose
            + kRoundConstants[index] + words[index];
        const std::uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temporary2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

Sha256Digest sha256(std::span<const std::uint8_t> bytes) noexcept {
    Sha256Builder builder;
    builder.update(bytes);
    return builder.finish();
}

bool sha256_equal(
    std::span<const std::uint8_t> lhs,
    std::span<const std::uint8_t> rhs) noexcept {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    std::uint8_t difference{};
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        difference |= static_cast<std::uint8_t>(lhs[index] ^ rhs[index]);
    }
    return difference == 0U;
}

bool sha256_is_zero(const Sha256Digest& digest) noexcept {
    return std::all_of(digest.begin(), digest.end(), [](std::uint8_t byte) {
        return byte == 0U;
    });
}

std::string sha256_hex(const Sha256Digest& digest) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const std::uint8_t byte : digest) {
        stream << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return stream.str();
}

} // namespace neoeng::core
