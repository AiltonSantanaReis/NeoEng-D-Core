#pragma once

#include "neoeng/core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

inline constexpr std::uint32_t kSecurePacketMagic = 0x4344454EU; // "NEDC" little-endian
inline constexpr std::uint16_t kSecurePacketVersion = 1U;
inline constexpr std::size_t kSecurePacketHeaderBytes = 36U;
inline constexpr std::size_t kSecurePacketTagBytes = 32U;

using AuthenticationKey = std::array<std::uint8_t, 32>;
using AuthenticationTag = std::array<std::uint8_t, kSecurePacketTagBytes>;
using OriginId = std::uint64_t;

struct SecureSessionBinding final {
    std::uint64_t session_id{};
    AuthenticationKey client_to_server_key{};
    std::uint32_t key_id{};
    std::uint32_t key_epoch{};
    std::uint8_t authorized_role{};
    std::uint64_t expires_at_ms{};
};

struct NetworkSecurityLimits final {
    std::size_t maximum_payload_bytes{1'200U};
    std::size_t maximum_input_commands{64U};
    std::size_t maximum_tracked_origins{1'024U};
    std::uint64_t maximum_clock_skew_ms{2'000U};
    std::uint64_t session_timeout_ms{30'000U};
    std::uint32_t rate_limit_packets_per_second{240U};
    std::uint32_t rate_limit_burst_packets{480U};
    bool require_established_session{false};
};

enum class PacketRejectReason : std::uint8_t {
    None,
    DatagramTooSmall,
    DatagramTooLarge,
    InvalidMagic,
    UnsupportedVersion,
    UnsupportedFlags,
    InvalidLength,
    TimestampTooOld,
    TimestampInFuture,
    AuthenticationFailed,
    RateLimited,
    OriginCapacityReached,
    UnknownSession,
    SessionMismatch,
    SessionExpired,
    SessionRevoked,
    ReplayDuplicate,
    ReplayTooOld,
    ResourceExhausted,
};

struct AuthenticatedPacketView final {
    std::uint64_t session_id{};
    std::uint64_t sequence{};
    std::uint64_t sent_at_ms{};
    OriginId origin{};
    std::uint32_t key_id{};
    std::uint32_t key_epoch{};
    std::uint8_t authorized_role{};
    std::span<const std::uint8_t> payload{};
};

struct PacketDecision final {
    PacketRejectReason reason{PacketRejectReason::None};
    AuthenticatedPacketView packet{};

    [[nodiscard]] bool accepted() const noexcept { return reason == PacketRejectReason::None; }
};

enum class InputPayloadRejectReason : std::uint8_t {
    None,
    HeaderTruncated,
    TooManyCommands,
    InvalidLength,
    OutputTooSmall,
    InvalidEntity,
    AccelerationOutOfRange,
};

struct InputPayloadParseResult final {
    InputPayloadRejectReason reason{InputPayloadRejectReason::None};
    std::size_t command_count{};

    [[nodiscard]] bool accepted() const noexcept {
        return reason == InputPayloadRejectReason::None;
    }
};

[[nodiscard]] AuthenticationTag hmac_sha256(
    std::span<const std::uint8_t> key,
    std::span<const std::uint8_t> message) noexcept;

[[nodiscard]] bool authentication_tags_equal(
    std::span<const std::uint8_t> lhs,
    std::span<const std::uint8_t> rhs) noexcept;

[[nodiscard]] bool authentication_key_is_valid(const AuthenticationKey& key) noexcept;

void securely_erase(AuthenticationKey& key) noexcept;

[[nodiscard]] std::vector<std::uint8_t> encode_authenticated_packet(
    const AuthenticationKey& key,
    std::uint64_t session_id,
    std::uint64_t sequence,
    std::uint64_t sent_at_ms,
    std::span<const std::uint8_t> payload);

[[nodiscard]] std::vector<std::uint8_t> encode_input_payload(
    std::span<const InputCommand> commands);

[[nodiscard]] InputPayloadParseResult parse_input_payload(
    std::span<const std::uint8_t> payload,
    std::span<InputCommand> output,
    const NetworkSecurityLimits& limits = {}) noexcept;

class NetworkSecurityGateway final {
public:
    explicit NetworkSecurityGateway(
        AuthenticationKey fallback_key,
        NetworkSecurityLimits limits = {});

    NetworkSecurityGateway(const NetworkSecurityGateway&) = delete;
    NetworkSecurityGateway& operator=(const NetworkSecurityGateway&) = delete;
    NetworkSecurityGateway(NetworkSecurityGateway&&) = default;
    NetworkSecurityGateway& operator=(NetworkSecurityGateway&&) = default;
    ~NetworkSecurityGateway();

    [[nodiscard]] bool install_session(
        OriginId origin,
        const SecureSessionBinding& binding,
        std::uint64_t now_ms) noexcept;
    [[nodiscard]] bool revoke_session(OriginId origin, std::uint64_t session_id) noexcept;
    [[nodiscard]] std::size_t revoke_sessions_for_key(
        std::uint32_t key_id,
        std::uint32_t key_epoch) noexcept;
    [[nodiscard]] bool has_session(OriginId origin, std::uint64_t session_id) const noexcept;

    [[nodiscard]] PacketDecision process(
        OriginId origin,
        std::uint64_t now_ms,
        std::span<const std::uint8_t> datagram) noexcept;

    void expire_idle_origins(std::uint64_t now_ms) noexcept;
    [[nodiscard]] std::size_t tracked_origins() const noexcept;
    [[nodiscard]] const NetworkSecurityLimits& limits() const noexcept { return limits_; }

private:
    struct OriginState final {
        OriginId origin{};
        std::uint64_t session_id{};
        std::uint64_t highest_sequence{};
        std::uint64_t replay_bitmap{};
        std::uint64_t last_seen_ms{};
        std::uint64_t token_timestamp_ms{};
        std::uint64_t milli_tokens{};
        AuthenticationKey session_key{};
        std::uint32_t key_id{};
        std::uint32_t key_epoch{};
        std::uint8_t authorized_role{};
        std::uint64_t absolute_expiry_ms{};
        bool initialized{};
        bool established{};
        bool revoked{};
    };

    [[nodiscard]] static bool state_is_expired(
        const OriginState& state,
        const NetworkSecurityLimits& limits,
        std::uint64_t now_ms) noexcept;
    static void erase_state_key(OriginState& state) noexcept;

    AuthenticationKey fallback_key_{};
    NetworkSecurityLimits limits_{};
    std::vector<OriginState> origins_{};
};

[[nodiscard]] const char* to_string(PacketRejectReason reason) noexcept;
[[nodiscard]] const char* to_string(InputPayloadRejectReason reason) noexcept;

} // namespace neoeng::core
