#include "neoeng/core/network_security.hpp"

#include "neoeng/core/crypto_hash.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace neoeng::core {
namespace {

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

[[nodiscard]] bool constant_time_equal_impl(
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
        Sha256Builder key_hash;
        key_hash.update(key);
        const Sha256Digest digest = key_hash.finish();
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

    Sha256Builder inner;
    inner.update(inner_pad);
    inner.update(message);
    const Sha256Digest inner_digest = inner.finish();

    Sha256Builder outer;
    outer.update(outer_pad);
    outer.update(inner_digest);
    return outer.finish();
}


bool authentication_tags_equal(
    std::span<const std::uint8_t> lhs,
    std::span<const std::uint8_t> rhs) noexcept {
    return constant_time_equal_impl(lhs, rhs);
}

bool authentication_key_is_valid(const AuthenticationKey& key) noexcept {
    return std::any_of(key.begin(), key.end(), [](std::uint8_t byte) { return byte != 0U; });
}

void securely_erase(AuthenticationKey& key) noexcept {
    volatile std::uint8_t* bytes = key.data();
    for (std::size_t index = 0; index < key.size(); ++index) {
        bytes[index] = 0U;
    }
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
    AuthenticationKey fallback_key,
    NetworkSecurityLimits limits)
    : fallback_key_(fallback_key), limits_(limits) {
    if (limits_.maximum_payload_bytes == 0U
        || limits_.maximum_tracked_origins == 0U
        || limits_.rate_limit_packets_per_second == 0U
        || limits_.rate_limit_burst_packets == 0U) {
        throw std::invalid_argument("NetworkSecurityLimits contains an invalid zero or burst value");
    }
    if (!authentication_key_is_valid(fallback_key_)) {
        throw std::invalid_argument("Authentication key must not be all zero");
    }
    origins_.reserve(limits_.maximum_tracked_origins);
}

NetworkSecurityGateway::~NetworkSecurityGateway() {
    securely_erase(fallback_key_);
    for (OriginState& state : origins_) {
        erase_state_key(state);
    }
}

bool NetworkSecurityGateway::state_is_expired(
    const OriginState& state,
    const NetworkSecurityLimits& limits,
    std::uint64_t now_ms) noexcept {
    const bool idle_expired = now_ms > state.last_seen_ms
        && now_ms - state.last_seen_ms > limits.session_timeout_ms;
    const bool absolute_expired = state.established
        && state.absolute_expiry_ms != 0U
        && now_ms > state.absolute_expiry_ms;
    return idle_expired || absolute_expired;
}

void NetworkSecurityGateway::erase_state_key(OriginState& state) noexcept {
    securely_erase(state.session_key);
}

bool NetworkSecurityGateway::install_session(
    OriginId origin,
    const SecureSessionBinding& binding,
    std::uint64_t now_ms) noexcept {
    if (origin == 0U || binding.session_id == 0U
        || binding.expires_at_ms <= now_ms
        || !authentication_key_is_valid(binding.client_to_server_key)) {
        return false;
    }
    try {
        auto iterator = std::find_if(origins_.begin(), origins_.end(),
            [origin](const OriginState& state) { return state.origin == origin; });
        if (iterator == origins_.end()) {
            expire_idle_origins(now_ms);
            if (origins_.size() >= limits_.maximum_tracked_origins) {
                return false;
            }
            origins_.push_back(OriginState{});
            iterator = std::prev(origins_.end());
        } else {
            erase_state_key(*iterator);
        }
        *iterator = OriginState{
            .origin = origin,
            .session_id = binding.session_id,
            .highest_sequence = 0U,
            .replay_bitmap = 0U,
            .last_seen_ms = now_ms,
            .token_timestamp_ms = now_ms,
            .milli_tokens = static_cast<std::uint64_t>(
                limits_.rate_limit_burst_packets) * 1'000U,
            .session_key = binding.client_to_server_key,
            .key_id = binding.key_id,
            .key_epoch = binding.key_epoch,
            .authorized_role = binding.authorized_role,
            .absolute_expiry_ms = binding.expires_at_ms,
            .initialized = false,
            .established = true,
            .revoked = false,
        };
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

bool NetworkSecurityGateway::revoke_session(
    OriginId origin,
    std::uint64_t session_id) noexcept {
    const auto iterator = std::find_if(origins_.begin(), origins_.end(),
        [origin, session_id](const OriginState& state) {
            return state.origin == origin && state.session_id == session_id && state.established;
        });
    if (iterator == origins_.end()) {
        return false;
    }
    iterator->revoked = true;
    erase_state_key(*iterator);
    return true;
}

std::size_t NetworkSecurityGateway::revoke_sessions_for_key(
    std::uint32_t key_id,
    std::uint32_t key_epoch) noexcept {
    std::size_t revoked{};
    for (OriginState& state : origins_) {
        if (state.established && state.key_id == key_id && state.key_epoch == key_epoch
            && !state.revoked) {
            state.revoked = true;
            erase_state_key(state);
            ++revoked;
        }
    }
    return revoked;
}

bool NetworkSecurityGateway::has_session(
    OriginId origin,
    std::uint64_t session_id) const noexcept {
    return std::any_of(origins_.begin(), origins_.end(),
        [origin, session_id](const OriginState& state) {
            return state.origin == origin && state.session_id == session_id
                && state.established && !state.revoked;
        });
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

        auto iterator = std::find_if(origins_.begin(), origins_.end(),
            [origin](const OriginState& state) { return state.origin == origin; });
        const AuthenticationKey* authentication_key = &fallback_key_;
        if (iterator != origins_.end() && iterator->established) {
            if (iterator->revoked) {
                return {.reason = PacketRejectReason::SessionRevoked};
            }
            if (state_is_expired(*iterator, limits_, now_ms)) {
                return {.reason = PacketRejectReason::SessionExpired};
            }
            if (iterator->session_id != session_id) {
                return {.reason = PacketRejectReason::SessionMismatch};
            }
            authentication_key = &iterator->session_key;
        } else if (limits_.require_established_session) {
            return {.reason = PacketRejectReason::UnknownSession};
        }

        const std::span<const std::uint8_t> authenticated_bytes = datagram.first(
            kSecurePacketHeaderBytes + payload_size);
        const std::span<const std::uint8_t> supplied_tag = datagram.last(kSecurePacketTagBytes);
        const AuthenticationTag computed_tag = hmac_sha256(*authentication_key, authenticated_bytes);
        if (!authentication_tags_equal(computed_tag, supplied_tag)) {
            return {.reason = PacketRejectReason::AuthenticationFailed};
        }

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
                .milli_tokens = static_cast<std::uint64_t>(
                    limits_.rate_limit_burst_packets) * 1'000U,
                .initialized = false,
                .established = false,
            });
            iterator = std::prev(origins_.end());
        } else if (!iterator->established) {
            OriginState& state = *iterator;
            if (state_is_expired(state, limits_, now_ms)) {
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
    auto iterator = origins_.begin();
    while (iterator != origins_.end()) {
        if (state_is_expired(*iterator, limits_, now_ms)) {
            erase_state_key(*iterator);
            iterator = origins_.erase(iterator);
        } else {
            ++iterator;
        }
    }
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
    case PacketRejectReason::UnknownSession: return "unknown_session";
    case PacketRejectReason::SessionMismatch: return "session_mismatch";
    case PacketRejectReason::SessionExpired: return "session_expired";
    case PacketRejectReason::SessionRevoked: return "session_revoked";
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
