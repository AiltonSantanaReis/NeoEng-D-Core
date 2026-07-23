#pragma once

#include "neoeng/core/network_security.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

inline constexpr std::uint32_t kClientHelloMagic = 0x4843444EU; // "NDCH"
inline constexpr std::uint32_t kServerHelloMagic = 0x4853444EU; // "NDSH"
inline constexpr std::uint16_t kHandshakeFormatVersion = 1U;
inline constexpr std::uint16_t kSessionProtocolVersion = 1U;
inline constexpr std::uint16_t kSessionCipherSuiteHmacSha256 = 1U;
inline constexpr std::size_t kHandshakeNonceBytes = 16U;
inline constexpr std::size_t kClientHelloAuthenticatedBytes = 60U;
inline constexpr std::size_t kClientHelloBytes = kClientHelloAuthenticatedBytes + kSecurePacketTagBytes;
inline constexpr std::size_t kServerHelloAuthenticatedBytes = 92U;
inline constexpr std::size_t kServerHelloBytes = kServerHelloAuthenticatedBytes + kSecurePacketTagBytes;

using HandshakeNonce = std::array<std::uint8_t, kHandshakeNonceBytes>;
using RootKeyId = std::uint32_t;
using RootKeyEpoch = std::uint32_t;

enum class SessionRole : std::uint8_t {
    Player = 1U,
    Spectator = 2U,
    Operator = 3U,
    Service = 4U,
};

enum class RootKeyLifecycle : std::uint8_t {
    Active,
    Retired,
    Revoked,
};

struct RootKeyRecord final {
    RootKeyId key_id{};
    RootKeyEpoch epoch{};
    AuthenticationKey key{};
    std::uint32_t allowed_role_mask{};
    std::uint64_t not_before_ms{};
    std::uint64_t not_after_ms{};
    RootKeyLifecycle lifecycle{RootKeyLifecycle::Active};
};

struct SessionSecurityLimits final {
    std::uint64_t maximum_clock_skew_ms{2'000U};
    std::uint64_t session_lifetime_ms{60'000U};
    std::size_t maximum_root_keys{16U};
    std::size_t maximum_handshake_replay_entries{4'096U};
};

enum class HandshakeRejectReason : std::uint8_t {
    None,
    InvalidLength,
    InvalidMagic,
    UnsupportedHandshakeFormat,
    UnsupportedProtocolRange,
    UnsupportedCipherSuite,
    InvalidReservedField,
    InvalidOrigin,
    InvalidRole,
    InvalidNonce,
    TimestampTooOld,
    TimestampInFuture,
    UnknownKey,
    KeyNotYetValid,
    KeyExpired,
    KeyRetired,
    KeyRevoked,
    RoleNotAuthorized,
    AuthenticationFailed,
    ReplayDetected,
    CapacityReached,
    InvalidSessionParameters,
    ResourceExhausted,
};

struct ClientHandshakeContext final {
    OriginId origin{};
    SessionRole role{SessionRole::Player};
    RootKeyId key_id{};
    RootKeyEpoch key_epoch{};
    std::uint16_t minimum_protocol{kSessionProtocolVersion};
    std::uint16_t maximum_protocol{kSessionProtocolVersion};
    std::uint16_t cipher_suite{kSessionCipherSuiteHmacSha256};
    std::uint64_t sent_at_ms{};
    HandshakeNonce client_nonce{};
};

struct SessionSecrets final {
    OriginId origin{};
    std::uint64_t session_id{};
    SessionRole role{SessionRole::Player};
    RootKeyId key_id{};
    RootKeyEpoch key_epoch{};
    std::uint16_t protocol_version{};
    std::uint16_t cipher_suite{};
    std::uint64_t issued_at_ms{};
    std::uint64_t expires_at_ms{};
    AuthenticationKey client_to_server_key{};
    AuthenticationKey server_to_client_key{};

    [[nodiscard]] SecureSessionBinding gateway_binding() const noexcept {
        return {
            .session_id = session_id,
            .client_to_server_key = client_to_server_key,
            .key_id = key_id,
            .key_epoch = key_epoch,
            .authorized_role = static_cast<std::uint8_t>(role),
            .expires_at_ms = expires_at_ms,
        };
    }
};

struct ClientHello final {
    ClientHandshakeContext context{};
    std::vector<std::uint8_t> datagram{};
};

struct ServerHandshakeDecision final {
    HandshakeRejectReason reason{HandshakeRejectReason::None};
    SessionSecrets secrets{};
    std::vector<std::uint8_t> response{};

    [[nodiscard]] bool accepted() const noexcept {
        return reason == HandshakeRejectReason::None;
    }
};

struct ClientHandshakeDecision final {
    HandshakeRejectReason reason{HandshakeRejectReason::None};
    SessionSecrets secrets{};

    [[nodiscard]] bool accepted() const noexcept {
        return reason == HandshakeRejectReason::None;
    }
};

class SessionKeyRing final {
public:
    explicit SessionKeyRing(std::size_t maximum_keys = 16U);
    SessionKeyRing(const SessionKeyRing&) = delete;
    SessionKeyRing& operator=(const SessionKeyRing&) = delete;
    SessionKeyRing(SessionKeyRing&&) = default;
    SessionKeyRing& operator=(SessionKeyRing&&) = default;
    ~SessionKeyRing();

    [[nodiscard]] bool upsert(RootKeyRecord record) noexcept;
    [[nodiscard]] bool rotate(
        RootKeyId current_key_id,
        RootKeyEpoch current_epoch,
        RootKeyRecord replacement) noexcept;
    [[nodiscard]] bool retire(RootKeyId key_id, RootKeyEpoch epoch) noexcept;
    [[nodiscard]] bool revoke(RootKeyId key_id, RootKeyEpoch epoch) noexcept;
    [[nodiscard]] const RootKeyRecord* find(RootKeyId key_id, RootKeyEpoch epoch) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return maximum_keys_; }

private:
    std::size_t maximum_keys_{};
    std::vector<RootKeyRecord> records_{};
};

[[nodiscard]] std::uint32_t session_role_mask(SessionRole role) noexcept;
[[nodiscard]] bool handshake_nonce_is_valid(const HandshakeNonce& nonce) noexcept;

[[nodiscard]] ClientHello create_client_hello(
    const AuthenticationKey& root_key,
    ClientHandshakeContext context);

class SessionHandshakeServer final {
public:
    SessionHandshakeServer(SessionKeyRing& key_ring, SessionSecurityLimits limits = {});

    [[nodiscard]] ServerHandshakeDecision accept(
        std::uint64_t now_ms,
        std::span<const std::uint8_t> client_hello,
        std::uint64_t session_id,
        const HandshakeNonce& server_nonce) noexcept;

    void expire_replay_entries(std::uint64_t now_ms) noexcept;
    [[nodiscard]] std::size_t replay_entries() const noexcept { return replay_entries_.size(); }

private:
    struct ReplayEntry final {
        RootKeyId key_id{};
        RootKeyEpoch key_epoch{};
        OriginId origin{};
        HandshakeNonce client_nonce{};
        std::uint64_t expires_at_ms{};
    };

    SessionKeyRing* key_ring_{};
    SessionSecurityLimits limits_{};
    std::vector<ReplayEntry> replay_entries_{};
};

struct KeyRevocationResult final {
    bool root_key_revoked{};
    std::size_t sessions_revoked{};
};

[[nodiscard]] KeyRevocationResult revoke_key_and_sessions(
    SessionKeyRing& key_ring,
    NetworkSecurityGateway& gateway,
    RootKeyId key_id,
    RootKeyEpoch key_epoch) noexcept;

[[nodiscard]] ClientHandshakeDecision finalize_client_handshake(
    const AuthenticationKey& root_key,
    const ClientHandshakeContext& expected,
    std::uint64_t now_ms,
    std::span<const std::uint8_t> server_hello,
    std::uint64_t maximum_clock_skew_ms = 2'000U) noexcept;

[[nodiscard]] const char* to_string(SessionRole role) noexcept;
[[nodiscard]] const char* to_string(RootKeyLifecycle lifecycle) noexcept;
[[nodiscard]] const char* to_string(HandshakeRejectReason reason) noexcept;

} // namespace neoeng::core
