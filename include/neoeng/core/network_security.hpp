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

struct NetworkSecurityLimits final {
    std::size_t maximum_payload_bytes{1'200U};
    std::size_t maximum_input_commands{64U};
    std::size_t maximum_tracked_origins{1'024U};
    std::uint64_t maximum_clock_skew_ms{2'000U};
    std::uint64_t session_timeout_ms{30'000U};
    std::uint32_t rate_limit_packets_per_second{240U};
    std::uint32_t rate_limit_burst_packets{480U};
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
    SessionMismatch,
    ReplayDuplicate,
    ReplayTooOld,
    ResourceExhausted,
};

struct AuthenticatedPacketView final {
    std::uint64_t session_id{};
    std::uint64_t sequence{};
    std::uint64_t sent_at_ms{};
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
        AuthenticationKey key,
        NetworkSecurityLimits limits = {});

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
        bool initialized{};
    };

    AuthenticationKey key_{};
    NetworkSecurityLimits limits_{};
    std::vector<OriginState> origins_{};
};

[[nodiscard]] const char* to_string(PacketRejectReason reason) noexcept;
[[nodiscard]] const char* to_string(InputPayloadRejectReason reason) noexcept;

} // namespace neoeng::core
