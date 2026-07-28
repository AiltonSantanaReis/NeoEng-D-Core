#pragma once

#include "neoeng/core/crypto_hash.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace neoeng::distributed_reference {

inline constexpr std::uint32_t kReferenceDatagramMagic = 0x4E445231U; // "NDR1"
inline constexpr std::uint16_t kReferenceDatagramVersion = 1U;
inline constexpr std::uint16_t kCorrectionMessageKind = 1U;

struct TransportLimits final {
    std::size_t maximum_datagram_bytes{1'472U};
    std::size_t maximum_pending_datagrams{64U};
};

enum class TransportStatus : std::uint8_t {
    Accepted,
    NotConnected,
    PayloadTooLarge,
    Backpressure,
    ResourceExhausted,
    SocketError,
    TimedOut,
    Malformed,
    UnsupportedVersion,
    IntegrityMismatch,
    StaleEpoch,
    Replay,
};

struct OpaqueDatagram final {
    std::uint16_t message_kind{};
    std::uint64_t channel_id{};
    std::uint64_t frame{};
    std::vector<std::uint8_t> payload{};
};

struct ReceiveResult final {
    TransportStatus status{TransportStatus::TimedOut};
    OpaqueDatagram datagram{};

    [[nodiscard]] bool accepted() const noexcept {
        return status == TransportStatus::Accepted;
    }
};

struct DecodedReferenceDatagram final {
    TransportStatus status{TransportStatus::Malformed};
    std::uint64_t session_epoch{};
    std::uint64_t sequence{};
    OpaqueDatagram datagram{};

    [[nodiscard]] bool accepted() const noexcept {
        return status == TransportStatus::Accepted;
    }
};

[[nodiscard]] std::vector<std::uint8_t> encode_reference_datagram(
    const OpaqueDatagram& datagram,
    std::uint64_t session_epoch,
    std::uint64_t sequence);
[[nodiscard]] DecodedReferenceDatagram decode_reference_datagram(
    std::span<const std::uint8_t> bytes);

class ReferenceReplayWindow final {
public:
    [[nodiscard]] TransportStatus accept(
        std::uint64_t session_epoch,
        std::uint64_t sequence) noexcept;

    [[nodiscard]] std::uint64_t session_epoch() const noexcept {
        return session_epoch_;
    }
    [[nodiscard]] std::uint64_t highest_sequence() const noexcept {
        return highest_sequence_;
    }

private:
    std::uint64_t session_epoch_{};
    std::uint64_t highest_sequence_{};
};

class UdpReferenceEndpoint final {
public:
    UdpReferenceEndpoint();
    ~UdpReferenceEndpoint();

    UdpReferenceEndpoint(const UdpReferenceEndpoint&) = delete;
    UdpReferenceEndpoint& operator=(const UdpReferenceEndpoint&) = delete;
    UdpReferenceEndpoint(UdpReferenceEndpoint&&) noexcept;
    UdpReferenceEndpoint& operator=(UdpReferenceEndpoint&&) noexcept;

    [[nodiscard]] static std::pair<UdpReferenceEndpoint, UdpReferenceEndpoint>
    make_loopback_pair(TransportLimits limits = {});

    [[nodiscard]] TransportStatus enqueue(const OpaqueDatagram& datagram);
    [[nodiscard]] TransportStatus flush();
    [[nodiscard]] ReceiveResult receive(std::chrono::milliseconds timeout);

    [[nodiscard]] TransportStatus reconnect(std::uint16_t peer_port) noexcept;
    void disconnect() noexcept;

    [[nodiscard]] std::uint16_t local_port() const noexcept;
    [[nodiscard]] std::uint64_t session_epoch() const noexcept;
    [[nodiscard]] std::size_t pending_datagrams() const noexcept;

private:
    class Impl;
    explicit UdpReferenceEndpoint(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

struct StateFingerprint final {
    std::uint64_t schema_id{};
    std::uint64_t frame{};
    core::Sha256Digest canonical_digest{};
};

enum class ReplicaStatus : std::uint8_t {
    Accepted,
    CorrectionUnavailable,
    InvalidCorrection,
    CorrectionOutsideRetention,
    ResourceExhausted,
};

class ReplicaAdapter {
public:
    virtual ~ReplicaAdapter() = default;
    [[nodiscard]] virtual StateFingerprint fingerprint() const = 0;
    [[nodiscard]] virtual std::vector<std::uint8_t> export_authoritative_correction(
        std::uint64_t input_frame) const = 0;
    [[nodiscard]] virtual ReplicaStatus apply_authoritative_correction(
        std::uint64_t input_frame,
        std::span<const std::uint8_t> correction) noexcept = 0;
};

enum class CoordinationStatus : std::uint8_t {
    Converged,
    Divergent,
    SchemaMismatch,
    FrameMismatch,
    CorrectionUnavailable,
    TransportRejected,
    ReplicaRejected,
    ReconciliationMismatch,
};

struct CoordinationResult final {
    CoordinationStatus status{CoordinationStatus::Divergent};
    StateFingerprint authoritative{};
    StateFingerprint follower{};
    TransportStatus transport_status{TransportStatus::Accepted};
    ReplicaStatus replica_status{ReplicaStatus::Accepted};

    [[nodiscard]] bool converged() const noexcept {
        return status == CoordinationStatus::Converged;
    }
};

class TwoInstanceCoordinator final {
public:
    TwoInstanceCoordinator(ReplicaAdapter& authoritative, ReplicaAdapter& follower) noexcept;

    [[nodiscard]] CoordinationResult compare() const;
    [[nodiscard]] CoordinationResult reconcile(
        std::uint64_t input_frame,
        UdpReferenceEndpoint& sender,
        UdpReferenceEndpoint& receiver,
        std::chrono::milliseconds timeout);

private:
    ReplicaAdapter* authoritative_{};
    ReplicaAdapter* follower_{};
};

[[nodiscard]] const char* to_string(TransportStatus status) noexcept;
[[nodiscard]] const char* to_string(ReplicaStatus status) noexcept;
[[nodiscard]] const char* to_string(CoordinationStatus status) noexcept;

} // namespace neoeng::distributed_reference
