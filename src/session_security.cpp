#include "neoeng/core/session_security.hpp"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace neoeng::core {
namespace {

template <typename T>
void append_little_endian(std::vector<std::uint8_t>& output, T value) {
    using U = std::make_unsigned_t<T>;
    std::uint64_t bits = static_cast<std::uint64_t>(static_cast<U>(value));
    for (std::size_t index = 0; index < sizeof(U); ++index) {
        output.push_back(static_cast<std::uint8_t>(bits & 0xFFU));
        bits >>= 8U;
    }
}

template <typename T>
[[nodiscard]] T read_little_endian(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept {
    using U = std::make_unsigned_t<T>;
    std::uint64_t value{};
    for (std::size_t index = 0; index < sizeof(U); ++index) {
        value |= static_cast<std::uint64_t>(bytes[offset + index]) << (index * 8U);
    }
    return static_cast<T>(static_cast<U>(value));
}

void append_nonce(std::vector<std::uint8_t>& output, const HandshakeNonce& nonce) {
    output.insert(output.end(), nonce.begin(), nonce.end());
}

[[nodiscard]] HandshakeNonce read_nonce(
    std::span<const std::uint8_t> bytes,
    std::size_t offset) noexcept {
    HandshakeNonce nonce{};
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), nonce.size(), nonce.begin());
    return nonce;
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

[[nodiscard]] bool role_is_defined(SessionRole role) noexcept {
    switch (role) {
    case SessionRole::Player:
    case SessionRole::Spectator:
    case SessionRole::Operator:
    case SessionRole::Service:
        return true;
    }
    return false;
}

[[nodiscard]] AuthenticationKey derive_key(
    const AuthenticationKey& root_key,
    std::string_view label,
    OriginId origin,
    std::uint64_t session_id,
    RootKeyId key_id,
    RootKeyEpoch key_epoch,
    SessionRole role,
    std::uint16_t protocol,
    std::uint16_t cipher_suite,
    std::uint64_t expires_at_ms,
    const HandshakeNonce& client_nonce,
    const HandshakeNonce& server_nonce) {
    std::vector<std::uint8_t> salt;
    salt.reserve(client_nonce.size() + server_nonce.size());
    append_nonce(salt, client_nonce);
    append_nonce(salt, server_nonce);
    const AuthenticationTag pseudorandom_key = hmac_sha256(salt, root_key);

    std::vector<std::uint8_t> info;
    constexpr std::string_view domain = "NeoEng D-Core session key schedule v1";
    info.reserve(domain.size() + label.size() + 64U);
    info.insert(info.end(), domain.begin(), domain.end());
    info.push_back(0U);
    info.insert(info.end(), label.begin(), label.end());
    info.push_back(0U);
    append_little_endian(info, origin);
    append_little_endian(info, session_id);
    append_little_endian(info, key_id);
    append_little_endian(info, key_epoch);
    append_little_endian(info, static_cast<std::uint8_t>(role));
    append_little_endian(info, protocol);
    append_little_endian(info, cipher_suite);
    append_little_endian(info, expires_at_ms);
    append_nonce(info, client_nonce);
    append_nonce(info, server_nonce);
    info.push_back(1U);
    return hmac_sha256(pseudorandom_key, info);
}

[[nodiscard]] SessionSecrets derive_session_secrets(
    const AuthenticationKey& root_key,
    OriginId origin,
    std::uint64_t session_id,
    SessionRole role,
    RootKeyId key_id,
    RootKeyEpoch key_epoch,
    std::uint16_t protocol,
    std::uint16_t cipher_suite,
    std::uint64_t issued_at_ms,
    std::uint64_t expires_at_ms,
    const HandshakeNonce& client_nonce,
    const HandshakeNonce& server_nonce) {
    return {
        .origin = origin,
        .session_id = session_id,
        .role = role,
        .key_id = key_id,
        .key_epoch = key_epoch,
        .protocol_version = protocol,
        .cipher_suite = cipher_suite,
        .issued_at_ms = issued_at_ms,
        .expires_at_ms = expires_at_ms,
        .client_to_server_key = derive_key(root_key, "client-to-server", origin, session_id,
            key_id, key_epoch, role, protocol, cipher_suite, expires_at_ms,
            client_nonce, server_nonce),
        .server_to_client_key = derive_key(root_key, "server-to-client", origin, session_id,
            key_id, key_epoch, role, protocol, cipher_suite, expires_at_ms,
            client_nonce, server_nonce),
    };
}

[[nodiscard]] std::uint64_t saturating_add(
    std::uint64_t lhs,
    std::uint64_t rhs) noexcept {
    return rhs > std::numeric_limits<std::uint64_t>::max() - lhs
        ? std::numeric_limits<std::uint64_t>::max()
        : lhs + rhs;
}

[[nodiscard]] bool reserved_bytes_are_zero(
    std::span<const std::uint8_t> bytes,
    std::size_t offset,
    std::size_t count) noexcept {
    return std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(offset + count),
        [](std::uint8_t byte) { return byte == 0U; });
}

} // namespace

SessionKeyRing::SessionKeyRing(std::size_t maximum_keys)
    : maximum_keys_(maximum_keys) {
    if (maximum_keys == 0U) {
        throw std::invalid_argument("SessionKeyRing capacity must be greater than zero");
    }
    records_.reserve(maximum_keys);
}

SessionKeyRing::~SessionKeyRing() {
    for (RootKeyRecord& record : records_) {
        securely_erase(record.key);
    }
}

bool SessionKeyRing::upsert(RootKeyRecord record) noexcept {
    if (record.key_id == 0U || record.epoch == 0U
        || record.allowed_role_mask == 0U
        || record.not_after_ms <= record.not_before_ms
        || !authentication_key_is_valid(record.key)) {
        return false;
    }
    if (record.lifecycle == RootKeyLifecycle::Revoked) {
        securely_erase(record.key);
    }
    try {
        auto iterator = std::find_if(records_.begin(), records_.end(),
            [&record](const RootKeyRecord& existing) {
                return existing.key_id == record.key_id && existing.epoch == record.epoch;
            });
        if (iterator != records_.end()) {
            securely_erase(iterator->key);
            *iterator = record;
            securely_erase(record.key);
            return true;
        }
        if (records_.size() >= maximum_keys_) {
            securely_erase(record.key);
            return false;
        }
        records_.push_back(record);
        securely_erase(record.key);
        return true;
    } catch (const std::bad_alloc&) {
        securely_erase(record.key);
        return false;
    }
}

bool SessionKeyRing::rotate(
    RootKeyId current_key_id,
    RootKeyEpoch current_epoch,
    RootKeyRecord replacement) noexcept {
    const RootKeyRecord* current = find(current_key_id, current_epoch);
    if (current == nullptr || current->lifecycle != RootKeyLifecycle::Active
        || replacement.key_id != current_key_id
        || replacement.epoch <= current_epoch
        || replacement.lifecycle != RootKeyLifecycle::Active) {
        securely_erase(replacement.key);
        return false;
    }
    const bool installed = upsert(replacement);
    securely_erase(replacement.key);
    if (!installed) {
        return false;
    }
    return retire(current_key_id, current_epoch);
}

bool SessionKeyRing::retire(RootKeyId key_id, RootKeyEpoch epoch) noexcept {
    auto iterator = std::find_if(records_.begin(), records_.end(),
        [key_id, epoch](const RootKeyRecord& record) {
            return record.key_id == key_id && record.epoch == epoch;
        });
    if (iterator == records_.end() || iterator->lifecycle == RootKeyLifecycle::Revoked) {
        return false;
    }
    iterator->lifecycle = RootKeyLifecycle::Retired;
    return true;
}

bool SessionKeyRing::revoke(RootKeyId key_id, RootKeyEpoch epoch) noexcept {
    auto iterator = std::find_if(records_.begin(), records_.end(),
        [key_id, epoch](const RootKeyRecord& record) {
            return record.key_id == key_id && record.epoch == epoch;
        });
    if (iterator == records_.end()) {
        return false;
    }
    iterator->lifecycle = RootKeyLifecycle::Revoked;
    securely_erase(iterator->key);
    return true;
}

const RootKeyRecord* SessionKeyRing::find(
    RootKeyId key_id,
    RootKeyEpoch epoch) const noexcept {
    const auto iterator = std::find_if(records_.begin(), records_.end(),
        [key_id, epoch](const RootKeyRecord& record) {
            return record.key_id == key_id && record.epoch == epoch;
        });
    return iterator == records_.end() ? nullptr : &*iterator;
}

std::uint32_t session_role_mask(SessionRole role) noexcept {
    if (!role_is_defined(role)) {
        return 0U;
    }
    const std::uint32_t bit = static_cast<std::uint32_t>(role) - 1U;
    return std::uint32_t{1} << bit;
}

bool handshake_nonce_is_valid(const HandshakeNonce& nonce) noexcept {
    return std::any_of(nonce.begin(), nonce.end(), [](std::uint8_t byte) { return byte != 0U; });
}

ClientHello create_client_hello(
    const AuthenticationKey& root_key,
    ClientHandshakeContext context) {
    if (!authentication_key_is_valid(root_key)
        || context.origin == 0U
        || context.key_id == 0U
        || context.key_epoch == 0U
        || !role_is_defined(context.role)
        || !handshake_nonce_is_valid(context.client_nonce)
        || context.minimum_protocol == 0U
        || context.maximum_protocol < context.minimum_protocol
        || context.cipher_suite == 0U) {
        throw std::invalid_argument("Client handshake context is invalid");
    }

    std::vector<std::uint8_t> datagram;
    datagram.reserve(kClientHelloBytes);
    append_little_endian(datagram, kClientHelloMagic);
    append_little_endian(datagram, kHandshakeFormatVersion);
    append_little_endian(datagram, context.minimum_protocol);
    append_little_endian(datagram, context.maximum_protocol);
    append_little_endian(datagram, context.cipher_suite);
    append_little_endian(datagram, context.key_id);
    append_little_endian(datagram, context.key_epoch);
    append_little_endian(datagram, context.origin);
    append_little_endian(datagram, static_cast<std::uint8_t>(context.role));
    for (std::size_t index = 0; index < 7U; ++index) {
        datagram.push_back(0U);
    }
    append_little_endian(datagram, context.sent_at_ms);
    append_nonce(datagram, context.client_nonce);
    const AuthenticationTag tag = hmac_sha256(root_key, datagram);
    datagram.insert(datagram.end(), tag.begin(), tag.end());
    return {.context = context, .datagram = std::move(datagram)};
}

SessionHandshakeServer::SessionHandshakeServer(
    SessionKeyRing& key_ring,
    SessionSecurityLimits limits)
    : key_ring_(&key_ring), limits_(limits) {
    if (limits.maximum_clock_skew_ms == 0U
        || limits.session_lifetime_ms == 0U
        || limits.maximum_root_keys == 0U
        || limits.maximum_handshake_replay_entries == 0U
        || limits.maximum_root_keys < key_ring.capacity()) {
        throw std::invalid_argument("SessionSecurityLimits is invalid or smaller than key-ring capacity");
    }
    replay_entries_.reserve(limits.maximum_handshake_replay_entries);
}

ServerHandshakeDecision SessionHandshakeServer::accept(
    std::uint64_t now_ms,
    std::span<const std::uint8_t> client_hello,
    std::uint64_t session_id,
    const HandshakeNonce& server_nonce) noexcept {
    try {
        if (client_hello.size() != kClientHelloBytes) {
            return {.reason = HandshakeRejectReason::InvalidLength};
        }
        if (read_little_endian<std::uint32_t>(client_hello, 0U) != kClientHelloMagic) {
            return {.reason = HandshakeRejectReason::InvalidMagic};
        }
        if (read_little_endian<std::uint16_t>(client_hello, 4U) != kHandshakeFormatVersion) {
            return {.reason = HandshakeRejectReason::UnsupportedHandshakeFormat};
        }
        const std::uint16_t minimum_protocol = read_little_endian<std::uint16_t>(client_hello, 6U);
        const std::uint16_t maximum_protocol = read_little_endian<std::uint16_t>(client_hello, 8U);
        if (minimum_protocol > kSessionProtocolVersion
            || maximum_protocol < kSessionProtocolVersion
            || minimum_protocol == 0U
            || maximum_protocol < minimum_protocol) {
            return {.reason = HandshakeRejectReason::UnsupportedProtocolRange};
        }
        const std::uint16_t cipher_suite = read_little_endian<std::uint16_t>(client_hello, 10U);
        if (cipher_suite != kSessionCipherSuiteHmacSha256) {
            return {.reason = HandshakeRejectReason::UnsupportedCipherSuite};
        }
        const RootKeyId key_id = read_little_endian<RootKeyId>(client_hello, 12U);
        const RootKeyEpoch key_epoch = read_little_endian<RootKeyEpoch>(client_hello, 16U);
        const OriginId origin = read_little_endian<OriginId>(client_hello, 20U);
        const SessionRole role = static_cast<SessionRole>(client_hello[28U]);
        if (!reserved_bytes_are_zero(client_hello, 29U, 7U)) {
            return {.reason = HandshakeRejectReason::InvalidReservedField};
        }
        const std::uint64_t sent_at_ms = read_little_endian<std::uint64_t>(client_hello, 36U);
        const HandshakeNonce client_nonce = read_nonce(client_hello, 44U);
        if (origin == 0U) {
            return {.reason = HandshakeRejectReason::InvalidOrigin};
        }
        if (!role_is_defined(role)) {
            return {.reason = HandshakeRejectReason::InvalidRole};
        }
        if (!handshake_nonce_is_valid(client_nonce)) {
            return {.reason = HandshakeRejectReason::InvalidNonce};
        }
        if (timestamp_is_too_old(now_ms, sent_at_ms, limits_.maximum_clock_skew_ms)) {
            return {.reason = HandshakeRejectReason::TimestampTooOld};
        }
        if (timestamp_is_in_future(now_ms, sent_at_ms, limits_.maximum_clock_skew_ms)) {
            return {.reason = HandshakeRejectReason::TimestampInFuture};
        }

        const RootKeyRecord* record = key_ring_->find(key_id, key_epoch);
        if (record == nullptr) {
            return {.reason = HandshakeRejectReason::UnknownKey};
        }
        if (record->lifecycle == RootKeyLifecycle::Retired) {
            return {.reason = HandshakeRejectReason::KeyRetired};
        }
        if (record->lifecycle == RootKeyLifecycle::Revoked) {
            return {.reason = HandshakeRejectReason::KeyRevoked};
        }
        if (now_ms < record->not_before_ms) {
            return {.reason = HandshakeRejectReason::KeyNotYetValid};
        }
        if (now_ms > record->not_after_ms) {
            return {.reason = HandshakeRejectReason::KeyExpired};
        }
        if ((record->allowed_role_mask & session_role_mask(role)) == 0U) {
            return {.reason = HandshakeRejectReason::RoleNotAuthorized};
        }

        const std::span<const std::uint8_t> supplied_tag = client_hello.last(kSecurePacketTagBytes);
        const AuthenticationTag computed_tag = hmac_sha256(
            record->key, client_hello.first(kClientHelloAuthenticatedBytes));
        if (!authentication_tags_equal(computed_tag, supplied_tag)) {
            return {.reason = HandshakeRejectReason::AuthenticationFailed};
        }

        expire_replay_entries(now_ms);
        const bool replay = std::any_of(replay_entries_.begin(), replay_entries_.end(),
            [key_id, key_epoch, origin, &client_nonce](const ReplayEntry& entry) {
                return entry.key_id == key_id && entry.key_epoch == key_epoch
                    && entry.origin == origin && entry.client_nonce == client_nonce;
            });
        if (replay) {
            return {.reason = HandshakeRejectReason::ReplayDetected};
        }
        if (replay_entries_.size() >= limits_.maximum_handshake_replay_entries) {
            return {.reason = HandshakeRejectReason::CapacityReached};
        }
        if (session_id == 0U || !handshake_nonce_is_valid(server_nonce)) {
            return {.reason = HandshakeRejectReason::InvalidSessionParameters};
        }

        const std::uint64_t expires_at_ms = std::min(
            saturating_add(now_ms, limits_.session_lifetime_ms), record->not_after_ms);
        if (expires_at_ms <= now_ms) {
            return {.reason = HandshakeRejectReason::KeyExpired};
        }

        std::vector<std::uint8_t> response;
        response.reserve(kServerHelloBytes);
        append_little_endian(response, kServerHelloMagic);
        append_little_endian(response, kHandshakeFormatVersion);
        append_little_endian(response, kSessionProtocolVersion);
        append_little_endian(response, kSessionCipherSuiteHmacSha256);
        append_little_endian(response, std::uint16_t{0U});
        append_little_endian(response, key_id);
        append_little_endian(response, key_epoch);
        append_little_endian(response, origin);
        append_little_endian(response, static_cast<std::uint8_t>(role));
        for (std::size_t index = 0; index < 7U; ++index) {
            response.push_back(0U);
        }
        append_little_endian(response, session_id);
        append_little_endian(response, now_ms);
        append_little_endian(response, expires_at_ms);
        append_nonce(response, client_nonce);
        append_nonce(response, server_nonce);
        const AuthenticationTag response_tag = hmac_sha256(record->key, response);
        response.insert(response.end(), response_tag.begin(), response_tag.end());

        SessionSecrets secrets = derive_session_secrets(record->key, origin, session_id, role,
            key_id, key_epoch, kSessionProtocolVersion, kSessionCipherSuiteHmacSha256,
            now_ms, expires_at_ms, client_nonce, server_nonce);
        replay_entries_.push_back({
            .key_id = key_id,
            .key_epoch = key_epoch,
            .origin = origin,
            .client_nonce = client_nonce,
            .expires_at_ms = expires_at_ms,
        });
        return {
            .reason = HandshakeRejectReason::None,
            .secrets = secrets,
            .response = std::move(response),
        };
    } catch (const std::bad_alloc&) {
        return {.reason = HandshakeRejectReason::ResourceExhausted};
    }
}

void SessionHandshakeServer::expire_replay_entries(std::uint64_t now_ms) noexcept {
    std::erase_if(replay_entries_, [now_ms](const ReplayEntry& entry) {
        return now_ms > entry.expires_at_ms;
    });
}

KeyRevocationResult revoke_key_and_sessions(
    SessionKeyRing& key_ring,
    NetworkSecurityGateway& gateway,
    RootKeyId key_id,
    RootKeyEpoch key_epoch) noexcept {
    const bool root_revoked = key_ring.revoke(key_id, key_epoch);
    const std::size_t sessions_revoked = gateway.revoke_sessions_for_key(key_id, key_epoch);
    return {
        .root_key_revoked = root_revoked,
        .sessions_revoked = sessions_revoked,
    };
}

ClientHandshakeDecision finalize_client_handshake(
    const AuthenticationKey& root_key,
    const ClientHandshakeContext& expected,
    std::uint64_t now_ms,
    std::span<const std::uint8_t> server_hello,
    std::uint64_t maximum_clock_skew_ms) noexcept {
    try {
        if (!authentication_key_is_valid(root_key)
            || server_hello.size() != kServerHelloBytes) {
            return {.reason = HandshakeRejectReason::InvalidLength};
        }
        if (read_little_endian<std::uint32_t>(server_hello, 0U) != kServerHelloMagic) {
            return {.reason = HandshakeRejectReason::InvalidMagic};
        }
        if (read_little_endian<std::uint16_t>(server_hello, 4U) != kHandshakeFormatVersion) {
            return {.reason = HandshakeRejectReason::UnsupportedHandshakeFormat};
        }
        const std::uint16_t protocol = read_little_endian<std::uint16_t>(server_hello, 6U);
        const std::uint16_t cipher_suite = read_little_endian<std::uint16_t>(server_hello, 8U);
        if (protocol < expected.minimum_protocol || protocol > expected.maximum_protocol) {
            return {.reason = HandshakeRejectReason::UnsupportedProtocolRange};
        }
        if (cipher_suite != expected.cipher_suite
            || cipher_suite != kSessionCipherSuiteHmacSha256) {
            return {.reason = HandshakeRejectReason::UnsupportedCipherSuite};
        }
        if (read_little_endian<std::uint16_t>(server_hello, 10U) != 0U
            || !reserved_bytes_are_zero(server_hello, 29U, 7U)) {
            return {.reason = HandshakeRejectReason::InvalidReservedField};
        }
        const RootKeyId key_id = read_little_endian<RootKeyId>(server_hello, 12U);
        const RootKeyEpoch key_epoch = read_little_endian<RootKeyEpoch>(server_hello, 16U);
        const OriginId origin = read_little_endian<OriginId>(server_hello, 20U);
        const SessionRole role = static_cast<SessionRole>(server_hello[28U]);
        const std::uint64_t session_id = read_little_endian<std::uint64_t>(server_hello, 36U);
        const std::uint64_t issued_at_ms = read_little_endian<std::uint64_t>(server_hello, 44U);
        const std::uint64_t expires_at_ms = read_little_endian<std::uint64_t>(server_hello, 52U);
        const HandshakeNonce client_nonce = read_nonce(server_hello, 60U);
        const HandshakeNonce server_nonce = read_nonce(server_hello, 76U);
        if (key_id != expected.key_id || key_epoch != expected.key_epoch
            || origin != expected.origin || role != expected.role
            || client_nonce != expected.client_nonce) {
            return {.reason = HandshakeRejectReason::AuthenticationFailed};
        }
        if (session_id == 0U || !handshake_nonce_is_valid(server_nonce)
            || expires_at_ms <= issued_at_ms || now_ms > expires_at_ms) {
            return {.reason = HandshakeRejectReason::InvalidSessionParameters};
        }
        if (timestamp_is_in_future(now_ms, issued_at_ms, maximum_clock_skew_ms)) {
            return {.reason = HandshakeRejectReason::TimestampInFuture};
        }
        const AuthenticationTag computed_tag = hmac_sha256(
            root_key, server_hello.first(kServerHelloAuthenticatedBytes));
        const std::span<const std::uint8_t> supplied_tag = server_hello.last(kSecurePacketTagBytes);
        if (!authentication_tags_equal(computed_tag, supplied_tag)) {
            return {.reason = HandshakeRejectReason::AuthenticationFailed};
        }
        return {
            .reason = HandshakeRejectReason::None,
            .secrets = derive_session_secrets(root_key, origin, session_id, role,
                key_id, key_epoch, protocol, cipher_suite, issued_at_ms, expires_at_ms,
                client_nonce, server_nonce),
        };
    } catch (const std::bad_alloc&) {
        return {.reason = HandshakeRejectReason::ResourceExhausted};
    }
}

const char* to_string(SessionRole role) noexcept {
    switch (role) {
    case SessionRole::Player: return "player";
    case SessionRole::Spectator: return "spectator";
    case SessionRole::Operator: return "operator";
    case SessionRole::Service: return "service";
    }
    return "unknown";
}

const char* to_string(RootKeyLifecycle lifecycle) noexcept {
    switch (lifecycle) {
    case RootKeyLifecycle::Active: return "active";
    case RootKeyLifecycle::Retired: return "retired";
    case RootKeyLifecycle::Revoked: return "revoked";
    }
    return "unknown";
}

const char* to_string(HandshakeRejectReason reason) noexcept {
    switch (reason) {
    case HandshakeRejectReason::None: return "none";
    case HandshakeRejectReason::InvalidLength: return "invalid_length";
    case HandshakeRejectReason::InvalidMagic: return "invalid_magic";
    case HandshakeRejectReason::UnsupportedHandshakeFormat: return "unsupported_handshake_format";
    case HandshakeRejectReason::UnsupportedProtocolRange: return "unsupported_protocol_range";
    case HandshakeRejectReason::UnsupportedCipherSuite: return "unsupported_cipher_suite";
    case HandshakeRejectReason::InvalidReservedField: return "invalid_reserved_field";
    case HandshakeRejectReason::InvalidOrigin: return "invalid_origin";
    case HandshakeRejectReason::InvalidRole: return "invalid_role";
    case HandshakeRejectReason::InvalidNonce: return "invalid_nonce";
    case HandshakeRejectReason::TimestampTooOld: return "timestamp_too_old";
    case HandshakeRejectReason::TimestampInFuture: return "timestamp_in_future";
    case HandshakeRejectReason::UnknownKey: return "unknown_key";
    case HandshakeRejectReason::KeyNotYetValid: return "key_not_yet_valid";
    case HandshakeRejectReason::KeyExpired: return "key_expired";
    case HandshakeRejectReason::KeyRetired: return "key_retired";
    case HandshakeRejectReason::KeyRevoked: return "key_revoked";
    case HandshakeRejectReason::RoleNotAuthorized: return "role_not_authorized";
    case HandshakeRejectReason::AuthenticationFailed: return "authentication_failed";
    case HandshakeRejectReason::ReplayDetected: return "replay_detected";
    case HandshakeRejectReason::CapacityReached: return "capacity_reached";
    case HandshakeRejectReason::InvalidSessionParameters: return "invalid_session_parameters";
    case HandshakeRejectReason::ResourceExhausted: return "resource_exhausted";
    }
    return "unknown";
}

} // namespace neoeng::core
