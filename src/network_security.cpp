#include "neoeng/core/network_security.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace neoeng::core {
namespace {

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants{
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

class Sha256 final {
public:
    void update(std::span<const std::uint8_t> bytes) noexcept {
        for (const std::uint8_t byte : bytes) {
            buffer_[buffer_size_++] = byte;
            ++total_bytes_;
            if (buffer_size_ == buffer_.size()) {
                transform(buffer_);
                buffer_size_ = 0U;
            }
        }
    }

    [[nodiscard]] AuthenticationTag finish() noexcept {
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

        AuthenticationTag digest{};
        for (std::size_t word = 0; word < state_.size(); ++word) {
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                digest[word * 4U + byte] = static_cast<std::uint8_t>(
                    state_[word] >> ((3U - byte) * 8U));
            }
        }
        return digest;
    }

private:
    void transform(const std::array<std::uint8_t, 64>& block) noexcept {
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
                + kSha256RoundConstants[index] + words[index];
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

    std::array<std::uint32_t, 8> state_{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_size_{};
    std::uint64_t total_bytes_{};
};

template <typename T>
void append_little_endian(std::vector<std::uint8_t>& output, T value) {
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(U); ++index) {
        output.push_back(static_cast<std::uint8_t>(bits & U{0xFF}));
        bits >>= 8U;
    }
}

template <typename T>
[[nodiscard]] T read_little_endian(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    using U = std::make_unsigned_t<T>;
    std::uint64_t value{};
    for (std::size_t index = 0; index < sizeof(U); ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return static_cast<T>(static_cast<U>(value));
}

[[nodiscard]] bool constant_time_equal(
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

[[nodiscard]] bool timestamp_is_too_old(
    std::uint64_t now_ms,
    std::uint64_t sent_at_ms,
    std::uint64_t skew_ms) noexcept {
    return now_ms > sent_at_ms && now_ms - sent_at_ms > skew_ms;
}

[[nodiscard]] bool timestamp_is_in_future(
    std::uint64_t now_ms,
    std::uint64_t sent_at_ms,
    std::uint64_t skew_ms) noexcept {
    return sent_at_ms > now_ms && sent_at_ms - now_ms > skew_ms;
}

} // namespace


AuthenticationTag hmac_sha256(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> message) noexcept {
    constexpr std::size_t block_size = 64U;
    std::array<std::uint8_t, block_size> normalized_key{};
    if (key.size() > block_size) {
        Sha256 key_hash;
        key_hash.update(key);
        const AuthenticationTag digest = key_hash.finish();
        std::copy(digest.begin(), digest.end(), normalized_key.begin());
    } else {
        std::copy(key.begin(), key.end(), normalized_key.begin());
    }

    std::array<std::uint8_t, block_size> inner_pad{};
    std::array<std::uint8_t, block_size> outer_pad{};
    for (std::size_t index = 0; index < block_size; ++index) {
        inner_pad[index] = static_cast<std::uint8_t>(normalized_key[index] ^ 0x36U);
        outer_pad[index] = static_cast<std::uint8_t>(normalized_key[index] ^ 0x5cU);
    }

    Sha256 inner;
    inner.update(inner_pad);
    inner.update(message);
    const AuthenticationTag inner_digest = inner.finish();

    Sha256 outer;
    outer.update(outer_pad);
    outer.update(inner_digest);
    return outer.finish();
}

std::vector<std::uint8_t> encode_authenticated_packet(
    const AuthenticationKey& key,
    std::uint64_t session_id,
    std::uint64_t sequence,
    std::uint64_t sent_at_ms,
    std::span<const std::uint8_t> payload) {
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("Secure packet payload exceeds the wire-format limit");
    }
    std::vector<std::uint8_t> datagram;
    datagram.reserve(kSecurePacketHeaderBytes + payload.size() + kSecurePacketTagBytes);
    append_little_endian(datagram, kSecurePacketMagic);
    append_little_endian(datagram, kSecurePacketVersion);
    append_little_endian(datagram, std::uint16_t{0});
    append_little_endian(datagram, session_id);
    append_little_endian(datagram, sequence);
    append_little_endian(datagram, sent_at_ms);
    append_little_endian(datagram, static_cast<std::uint32_t>(payload.size()));
    datagram.insert(datagram.end(), payload.begin(), payload.end());
    const AuthenticationTag tag = hmac_sha256(key, datagram);
    datagram.insert(datagram.end(), tag.begin(), tag.end());
    return datagram;
}

std::vector<std::uint8_t> encode_input_payload(std::span<const InputCommand> commands) {
    if (commands.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::length_error("Input command count exceeds the wire-format limit");
    }
    constexpr std::size_t command_bytes = sizeof(EntityId) + 2U * sizeof(Fixed::rep);
    std::vector<std::uint8_t> payload;
    payload.reserve(sizeof(std::uint16_t) + commands.size() * command_bytes);
    append_little_endian(payload, static_cast<std::uint16_t>(commands.size()));
    for (const InputCommand& command : commands) {
        append_little_endian(payload, command.entity);
        append_little_endian(payload, command.acceleration.x.raw());
        append_little_endian(payload, command.acceleration.y.raw());
    }
    return payload;
}

InputPayloadParseResult parse_input_payload(
    std::span<const std::uint8_t> payload,
    std::span<InputCommand> output,
    const NetworkSecurityLimits& limits) noexcept {
    if (payload.size() < sizeof(std::uint16_t)) {
        return {.reason = InputPayloadRejectReason::HeaderTruncated};
    }
    const std::size_t count = read_little_endian<std::uint16_t>(payload, 0U);
    if (count > limits.maximum_input_commands) {
        return {.reason = InputPayloadRejectReason::TooManyCommands};
    }
    constexpr std::size_t command_bytes = sizeof(EntityId) + 2U * sizeof(Fixed::rep);
    const std::size_t expected = sizeof(std::uint16_t) + count * command_bytes;
    if (payload.size() != expected) {
        return {.reason = InputPayloadRejectReason::InvalidLength};
    }
    if (output.size() < count) {
        return {.reason = InputPayloadRejectReason::OutputTooSmall};
    }

    // Limits individual acceleration to a deliberately conservative signed 31-bit integer
    // in fixed-point whole units. Product profiles may tighten this further at integration.
    constexpr Fixed::rep maximum_acceleration_raw =
        static_cast<Fixed::rep>(2'147'483'647LL) << Fixed::fractional_bits;
    std::size_t offset = sizeof(std::uint16_t);
    for (std::size_t index = 0; index < count; ++index) {
        const EntityId entity = read_little_endian<EntityId>(payload, offset);
        offset += sizeof(EntityId);
        const Fixed::rep acceleration_x = read_little_endian<Fixed::rep>(payload, offset);
        offset += sizeof(Fixed::rep);
        const Fixed::rep acceleration_y = read_little_endian<Fixed::rep>(payload, offset);
        offset += sizeof(Fixed::rep);
        if (entity == 0U) {
            return {.reason = InputPayloadRejectReason::InvalidEntity};
        }
        const auto absolute_exceeds = [](Fixed::rep value, Fixed::rep maximum) noexcept {
            if (value == std::numeric_limits<Fixed::rep>::min()) {
                return true;
            }
            return (value < 0 ? -value : value) > maximum;
        };
        if (absolute_exceeds(acceleration_x, maximum_acceleration_raw)
            || absolute_exceeds(acceleration_y, maximum_acceleration_raw)) {
            return {.reason = InputPayloadRejectReason::AccelerationOutOfRange};
        }
        output[index] = InputCommand{
            .entity = entity,
            .acceleration = {Fixed::from_raw(acceleration_x), Fixed::from_raw(acceleration_y)},
        };
    }
    return {.reason = InputPayloadRejectReason::None, .command_count = count};
}

NetworkSecurityGateway::NetworkSecurityGateway(
    AuthenticationKey key,
    NetworkSecurityLimits limits)
    : key_(key), limits_(limits) {
    if (limits_.maximum_payload_bytes == 0U
        || limits_.maximum_tracked_origins == 0U
        || limits_.rate_limit_packets_per_second == 0U
        || limits_.rate_limit_burst_packets == 0U) {
        throw std::invalid_argument("NetworkSecurityLimits contains an invalid zero or burst value");
    }
    if (std::all_of(key_.begin(), key_.end(), [](std::uint8_t byte) { return byte == 0U; })) {
        throw std::invalid_argument("Authentication key must not be all zero");
    }
    origins_.reserve(limits_.maximum_tracked_origins);
}

PacketDecision NetworkSecurityGateway::process(
    OriginId origin,
    std::uint64_t now_ms,
    std::span<const std::uint8_t> datagram) noexcept {
    try {
        const std::size_t minimum_size = kSecurePacketHeaderBytes + kSecurePacketTagBytes;
        if (datagram.size() < minimum_size) {
            return {.reason = PacketRejectReason::DatagramTooSmall};
        }
        const std::size_t maximum_size = kSecurePacketHeaderBytes
            + limits_.maximum_payload_bytes + kSecurePacketTagBytes;
        if (datagram.size() > maximum_size) {
            return {.reason = PacketRejectReason::DatagramTooLarge};
        }
        if (read_little_endian<std::uint32_t>(datagram, 0U) != kSecurePacketMagic) {
            return {.reason = PacketRejectReason::InvalidMagic};
        }
        if (read_little_endian<std::uint16_t>(datagram, 4U) != kSecurePacketVersion) {
            return {.reason = PacketRejectReason::UnsupportedVersion};
        }
        if (read_little_endian<std::uint16_t>(datagram, 6U) != 0U) {
            return {.reason = PacketRejectReason::UnsupportedFlags};
        }
        const std::uint64_t session_id = read_little_endian<std::uint64_t>(datagram, 8U);
        const std::uint64_t sequence = read_little_endian<std::uint64_t>(datagram, 16U);
        const std::uint64_t sent_at_ms = read_little_endian<std::uint64_t>(datagram, 24U);
        const std::size_t payload_size = read_little_endian<std::uint32_t>(datagram, 32U);
        if (payload_size > limits_.maximum_payload_bytes
            || datagram.size() != kSecurePacketHeaderBytes + payload_size + kSecurePacketTagBytes) {
            return {.reason = PacketRejectReason::InvalidLength};
        }
        if (timestamp_is_too_old(now_ms, sent_at_ms, limits_.maximum_clock_skew_ms)) {
            return {.reason = PacketRejectReason::TimestampTooOld};
        }
        if (timestamp_is_in_future(now_ms, sent_at_ms, limits_.maximum_clock_skew_ms)) {
            return {.reason = PacketRejectReason::TimestampInFuture};
        }
        const std::span<const std::uint8_t> authenticated_bytes = datagram.first(
            kSecurePacketHeaderBytes + payload_size);
        const std::span<const std::uint8_t> supplied_tag = datagram.last(kSecurePacketTagBytes);
        const AuthenticationTag computed_tag = hmac_sha256(key_, authenticated_bytes);
        if (!constant_time_equal(computed_tag, supplied_tag)) {
            return {.reason = PacketRejectReason::AuthenticationFailed};
        }

        auto iterator = std::find_if(origins_.begin(), origins_.end(),
            [origin](const OriginState& state) { return state.origin == origin; });
        if (iterator == origins_.end()) {
            expire_idle_origins(now_ms);
            if (origins_.size() >= limits_.maximum_tracked_origins) {
                return {.reason = PacketRejectReason::OriginCapacityReached};
            }
            origins_.push_back(OriginState{
                .origin = origin,
                .session_id = session_id,
                .highest_sequence = 0U,
                .replay_bitmap = 0U,
                .last_seen_ms = now_ms,
                .token_timestamp_ms = now_ms,
                .milli_tokens = static_cast<std::uint64_t>(limits_.rate_limit_burst_packets) * 1'000U,
                .initialized = false,
            });
            iterator = std::prev(origins_.end());
        } else {
            OriginState& state = *iterator;
            const bool session_expired = now_ms > state.last_seen_ms
                && now_ms - state.last_seen_ms > limits_.session_timeout_ms;
            if (session_expired) {
                state.session_id = session_id;
                state.highest_sequence = 0U;
                state.replay_bitmap = 0U;
                state.initialized = false;
                state.token_timestamp_ms = now_ms;
                state.milli_tokens = static_cast<std::uint64_t>(
                    limits_.rate_limit_burst_packets) * 1'000U;
            } else if (state.session_id != session_id) {
                return {.reason = PacketRejectReason::SessionMismatch};
            }
        }

        OriginState& state = *iterator;
        if (now_ms > state.token_timestamp_ms) {
            const std::uint64_t elapsed_ms = now_ms - state.token_timestamp_ms;
            const std::uint64_t rate = limits_.rate_limit_packets_per_second;
            const std::uint64_t refill = elapsed_ms > std::numeric_limits<std::uint64_t>::max() / rate
                ? std::numeric_limits<std::uint64_t>::max()
                : elapsed_ms * rate;
            const std::uint64_t capacity = static_cast<std::uint64_t>(
                limits_.rate_limit_burst_packets) * 1'000U;
            state.milli_tokens = std::min(capacity,
                refill > capacity - std::min(state.milli_tokens, capacity)
                    ? capacity
                    : state.milli_tokens + refill);
            state.token_timestamp_ms = now_ms;
        }
        if (state.milli_tokens < 1'000U) {
            return {.reason = PacketRejectReason::RateLimited};
        }

        if (!state.initialized) {
            state.highest_sequence = sequence;
            state.replay_bitmap = 1U;
            state.initialized = true;
        } else if (sequence > state.highest_sequence) {
            const std::uint64_t shift = sequence - state.highest_sequence;
            state.replay_bitmap = shift >= 64U ? 1U : (state.replay_bitmap << shift) | 1U;
            state.highest_sequence = sequence;
        } else {
            const std::uint64_t distance = state.highest_sequence - sequence;
            if (distance >= 64U) {
                return {.reason = PacketRejectReason::ReplayTooOld};
            }
            const std::uint64_t mask = std::uint64_t{1} << distance;
            if ((state.replay_bitmap & mask) != 0U) {
                return {.reason = PacketRejectReason::ReplayDuplicate};
            }
            state.replay_bitmap |= mask;
        }

        state.milli_tokens -= 1'000U;
        state.last_seen_ms = now_ms;
        return {
            .reason = PacketRejectReason::None,
            .packet = {
                .session_id = session_id,
                .sequence = sequence,
                .sent_at_ms = sent_at_ms,
                .payload = datagram.subspan(kSecurePacketHeaderBytes, payload_size),
            },
        };
    } catch (const std::bad_alloc&) {
        return {.reason = PacketRejectReason::ResourceExhausted};
    }
}

void NetworkSecurityGateway::expire_idle_origins(std::uint64_t now_ms) noexcept {
    std::erase_if(origins_, [this, now_ms](const OriginState& state) {
        return now_ms > state.last_seen_ms
            && now_ms - state.last_seen_ms > limits_.session_timeout_ms;
    });
}

std::size_t NetworkSecurityGateway::tracked_origins() const noexcept {
    return origins_.size();
}

const char* to_string(PacketRejectReason reason) noexcept {
    switch (reason) {
    case PacketRejectReason::None: return "none";
    case PacketRejectReason::DatagramTooSmall: return "datagram_too_small";
    case PacketRejectReason::DatagramTooLarge: return "datagram_too_large";
    case PacketRejectReason::InvalidMagic: return "invalid_magic";
    case PacketRejectReason::UnsupportedVersion: return "unsupported_version";
    case PacketRejectReason::UnsupportedFlags: return "unsupported_flags";
    case PacketRejectReason::InvalidLength: return "invalid_length";
    case PacketRejectReason::TimestampTooOld: return "timestamp_too_old";
    case PacketRejectReason::TimestampInFuture: return "timestamp_in_future";
    case PacketRejectReason::AuthenticationFailed: return "authentication_failed";
    case PacketRejectReason::RateLimited: return "rate_limited";
    case PacketRejectReason::OriginCapacityReached: return "origin_capacity_reached";
    case PacketRejectReason::SessionMismatch: return "session_mismatch";
    case PacketRejectReason::ReplayDuplicate: return "replay_duplicate";
    case PacketRejectReason::ReplayTooOld: return "replay_too_old";
    case PacketRejectReason::ResourceExhausted: return "resource_exhausted";
    }
    return "unknown";
}

const char* to_string(InputPayloadRejectReason reason) noexcept {
    switch (reason) {
    case InputPayloadRejectReason::None: return "none";
    case InputPayloadRejectReason::HeaderTruncated: return "header_truncated";
    case InputPayloadRejectReason::TooManyCommands: return "too_many_commands";
    case InputPayloadRejectReason::InvalidLength: return "invalid_length";
    case InputPayloadRejectReason::OutputTooSmall: return "output_too_small";
    case InputPayloadRejectReason::InvalidEntity: return "invalid_entity";
    case InputPayloadRejectReason::AccelerationOutOfRange: return "acceleration_out_of_range";
    }
    return "unknown";
}

} // namespace neoeng::core
