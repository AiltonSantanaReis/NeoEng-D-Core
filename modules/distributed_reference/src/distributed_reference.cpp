#include "neoeng/distributed_reference.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace neoeng::distributed_reference {
namespace {

constexpr std::size_t kPrefixBytes = 44U;
constexpr std::size_t kDigestBytes = core::kSha256DigestBytes;
constexpr std::size_t kHeaderBytes = kPrefixBytes + kDigestBytes;

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;

class SocketRuntime final {
public:
    SocketRuntime() {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
    }
    ~SocketRuntime() { WSACleanup(); }
};

void ensure_socket_runtime() {
    static SocketRuntime runtime;
    (void)runtime;
}

void close_socket(NativeSocket socket) noexcept {
    if (socket != kInvalidSocket) {
        closesocket(socket);
    }
}

[[nodiscard]] bool interrupted_socket_error() noexcept {
    return WSAGetLastError() == WSAEINTR;
}
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;

void ensure_socket_runtime() {}

void close_socket(NativeSocket socket) noexcept {
    if (socket != kInvalidSocket) {
        close(socket);
    }
}

[[nodiscard]] bool interrupted_socket_error() noexcept {
    return errno == EINTR;
}
#endif

template <typename T>
void append_big_endian(std::vector<std::uint8_t>& output, T value) {
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t index = 0U; index < sizeof(U); ++index) {
        const std::size_t shift = (sizeof(U) - index - 1U) * 8U;
        output.push_back(static_cast<std::uint8_t>(bits >> shift));
    }
}

template <typename T>
[[nodiscard]] bool read_big_endian(
    std::span<const std::uint8_t> input,
    std::size_t& cursor,
    T& value) noexcept {
    if (cursor > input.size() || input.size() - cursor < sizeof(T)) {
        return false;
    }
    using U = std::make_unsigned_t<T>;
    U bits{};
    for (std::size_t index = 0U; index < sizeof(U); ++index) {
        bits = static_cast<U>((bits << 8U) | input[cursor++]);
    }
    value = static_cast<T>(bits);
    return true;
}

[[nodiscard]] core::Sha256Digest datagram_digest(
    std::span<const std::uint8_t> prefix,
    std::span<const std::uint8_t> payload) noexcept {
    core::Sha256Builder builder;
    builder.update(prefix);
    builder.update(payload);
    return builder.finish();
}

[[nodiscard]] NativeSocket make_bound_socket(std::uint16_t& port) {
    ensure_socket_runtime();
    const NativeSocket socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == kInvalidSocket) {
        throw std::runtime_error("cannot create UDP socket");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0U;
    if (bind(socket_handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        close_socket(socket_handle);
        throw std::runtime_error("cannot bind UDP loopback socket");
    }
    sockaddr_in bound{};
#if defined(_WIN32)
    int bound_size = sizeof(bound);
#else
    socklen_t bound_size = sizeof(bound);
#endif
    if (getsockname(
            socket_handle,
            reinterpret_cast<sockaddr*>(&bound),
            &bound_size) != 0) {
        close_socket(socket_handle);
        throw std::runtime_error("cannot query UDP loopback socket");
    }
    port = ntohs(bound.sin_port);
    return socket_handle;
}

[[nodiscard]] bool connect_socket(NativeSocket socket_handle, std::uint16_t port) noexcept {
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    peer.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    peer.sin_port = htons(port);
    return connect(
        socket_handle,
        reinterpret_cast<const sockaddr*>(&peer),
        sizeof(peer)) == 0;
}

} // namespace

class UdpReferenceEndpoint::Impl final {
public:
    explicit Impl(TransportLimits limits)
        : limits_(limits) {
        if (limits_.maximum_datagram_bytes < kHeaderBytes
            || limits_.maximum_datagram_bytes
                > static_cast<std::size_t>(std::numeric_limits<int>::max())
            || limits_.maximum_pending_datagrams == 0U) {
            throw std::invalid_argument("invalid reference transport limits");
        }
        socket_ = make_bound_socket(local_port_);
    }

    ~Impl() { close_socket(socket_); }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    TransportLimits limits_{};
    NativeSocket socket_{kInvalidSocket};
    std::uint16_t local_port_{};
    std::uint16_t peer_port_{};
    std::uint64_t epoch_{1U};
    std::uint64_t next_sequence_{1U};
    ReferenceReplayWindow replay_window_{};
    bool connected_{};
    std::deque<std::vector<std::uint8_t>> pending_{};
};

UdpReferenceEndpoint::UdpReferenceEndpoint() = default;
UdpReferenceEndpoint::~UdpReferenceEndpoint() = default;
UdpReferenceEndpoint::UdpReferenceEndpoint(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}
UdpReferenceEndpoint::UdpReferenceEndpoint(UdpReferenceEndpoint&&) noexcept = default;
UdpReferenceEndpoint& UdpReferenceEndpoint::operator=(UdpReferenceEndpoint&&) noexcept = default;

std::vector<std::uint8_t> encode_reference_datagram(
    const OpaqueDatagram& datagram,
    std::uint64_t session_epoch,
    std::uint64_t sequence) {
    if (datagram.payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("reference datagram payload exceeds format");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kHeaderBytes + datagram.payload.size());
    append_big_endian(bytes, kReferenceDatagramMagic);
    append_big_endian(bytes, kReferenceDatagramVersion);
    append_big_endian(bytes, datagram.message_kind);
    append_big_endian(bytes, session_epoch);
    append_big_endian(bytes, sequence);
    append_big_endian(bytes, datagram.channel_id);
    append_big_endian(bytes, datagram.frame);
    append_big_endian(bytes, static_cast<std::uint32_t>(datagram.payload.size()));
    const core::Sha256Digest digest = datagram_digest(bytes, datagram.payload);
    bytes.insert(bytes.end(), digest.begin(), digest.end());
    bytes.insert(bytes.end(), datagram.payload.begin(), datagram.payload.end());
    return bytes;
}

DecodedReferenceDatagram decode_reference_datagram(
    std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderBytes) {
        return {};
    }
    std::size_t cursor{};
    std::uint32_t magic{};
    std::uint16_t version{};
    std::uint32_t payload_size{};
    DecodedReferenceDatagram result{};
    if (!read_big_endian(bytes, cursor, magic)
        || !read_big_endian(bytes, cursor, version)
        || !read_big_endian(bytes, cursor, result.datagram.message_kind)
        || !read_big_endian(bytes, cursor, result.session_epoch)
        || !read_big_endian(bytes, cursor, result.sequence)
        || !read_big_endian(bytes, cursor, result.datagram.channel_id)
        || !read_big_endian(bytes, cursor, result.datagram.frame)
        || !read_big_endian(bytes, cursor, payload_size)) {
        return result;
    }
    if (magic != kReferenceDatagramMagic) {
        return result;
    }
    if (version != kReferenceDatagramVersion) {
        result.status = TransportStatus::UnsupportedVersion;
        return result;
    }
    if (payload_size > bytes.size() - kHeaderBytes
        || bytes.size() != kHeaderBytes + payload_size) {
        return result;
    }
    const std::span<const std::uint8_t> encoded_digest =
        bytes.subspan(kPrefixBytes, kDigestBytes);
    const std::span<const std::uint8_t> payload =
        bytes.subspan(kHeaderBytes, payload_size);
    const core::Sha256Digest expected =
        datagram_digest(bytes.first(kPrefixBytes), payload);
    if (!core::sha256_equal(encoded_digest, expected)) {
        result.status = TransportStatus::IntegrityMismatch;
        return result;
    }
    result.datagram.payload.assign(payload.begin(), payload.end());
    result.status = TransportStatus::Accepted;
    return result;
}

TransportStatus ReferenceReplayWindow::accept(
    std::uint64_t session_epoch,
    std::uint64_t sequence) noexcept {
    if (session_epoch < session_epoch_) {
        return TransportStatus::StaleEpoch;
    }
    if (session_epoch > session_epoch_) {
        session_epoch_ = session_epoch;
        highest_sequence_ = 0U;
    }
    if (sequence == 0U || sequence <= highest_sequence_) {
        return TransportStatus::Replay;
    }
    highest_sequence_ = sequence;
    return TransportStatus::Accepted;
}

std::pair<UdpReferenceEndpoint, UdpReferenceEndpoint>
UdpReferenceEndpoint::make_loopback_pair(TransportLimits limits) {
    UdpReferenceEndpoint first(std::make_unique<Impl>(limits));
    UdpReferenceEndpoint second(std::make_unique<Impl>(limits));
    if (first.reconnect(second.local_port()) != TransportStatus::Accepted
        || second.reconnect(first.local_port()) != TransportStatus::Accepted) {
        throw std::runtime_error("cannot connect UDP loopback pair");
    }
    return {std::move(first), std::move(second)};
}

TransportStatus UdpReferenceEndpoint::enqueue(const OpaqueDatagram& datagram) {
    if (!impl_ || !impl_->connected_) {
        return TransportStatus::NotConnected;
    }
    if (datagram.payload.size() > std::numeric_limits<std::uint32_t>::max()
        || datagram.payload.size() + kHeaderBytes > impl_->limits_.maximum_datagram_bytes) {
        return TransportStatus::PayloadTooLarge;
    }
    if (impl_->pending_.size() >= impl_->limits_.maximum_pending_datagrams) {
        return TransportStatus::Backpressure;
    }
    try {
        impl_->pending_.push_back(encode_reference_datagram(
            datagram, impl_->epoch_, impl_->next_sequence_));
    } catch (const std::bad_alloc&) {
        return TransportStatus::ResourceExhausted;
    }
    ++impl_->next_sequence_;
    return TransportStatus::Accepted;
}

TransportStatus UdpReferenceEndpoint::flush() {
    if (!impl_ || !impl_->connected_) {
        return TransportStatus::NotConnected;
    }
    while (!impl_->pending_.empty()) {
        const std::vector<std::uint8_t>& bytes = impl_->pending_.front();
#if defined(_WIN32)
        const int sent = send(
            impl_->socket_,
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<int>(bytes.size()),
            0);
#else
        const ssize_t sent = send(impl_->socket_, bytes.data(), bytes.size(), 0);
#endif
        if (sent < 0 || static_cast<std::size_t>(sent) != bytes.size()) {
            return TransportStatus::SocketError;
        }
        impl_->pending_.pop_front();
    }
    return TransportStatus::Accepted;
}

ReceiveResult UdpReferenceEndpoint::receive(std::chrono::milliseconds timeout) {
    if (!impl_ || !impl_->connected_) {
        return {.status = TransportStatus::NotConnected};
    }
    const auto nonnegative_timeout = std::max(timeout, std::chrono::milliseconds::zero());
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(impl_->socket_, &read_set);
    timeval wait{
        .tv_sec = static_cast<long>(nonnegative_timeout.count() / 1'000),
        .tv_usec = static_cast<long>((nonnegative_timeout.count() % 1'000) * 1'000),
    };
    int selected{};
    do {
#if defined(_WIN32)
        selected = select(0, &read_set, nullptr, nullptr, &wait);
#else
        selected = select(impl_->socket_ + 1, &read_set, nullptr, nullptr, &wait);
#endif
    } while (selected < 0 && interrupted_socket_error());
    if (selected == 0) {
        return {.status = TransportStatus::TimedOut};
    }
    if (selected < 0) {
        return {.status = TransportStatus::SocketError};
    }
    std::vector<std::uint8_t> bytes;
    try {
        bytes.resize(impl_->limits_.maximum_datagram_bytes + 1U);
    } catch (const std::bad_alloc&) {
        return {.status = TransportStatus::ResourceExhausted};
    }
#if defined(_WIN32)
    const int received = recv(
        impl_->socket_,
        reinterpret_cast<char*>(bytes.data()),
        static_cast<int>(bytes.size()),
        0);
#else
    const ssize_t received = recv(impl_->socket_, bytes.data(), bytes.size(), 0);
#endif
    if (received < 0) {
        return {.status = TransportStatus::SocketError};
    }
    bytes.resize(static_cast<std::size_t>(received));
    if (bytes.size() > impl_->limits_.maximum_datagram_bytes) {
        return {.status = TransportStatus::PayloadTooLarge};
    }
    DecodedReferenceDatagram decoded;
    try {
        decoded = decode_reference_datagram(bytes);
    } catch (const std::bad_alloc&) {
        return {.status = TransportStatus::ResourceExhausted};
    }
    if (decoded.status != TransportStatus::Accepted) {
        return {.status = decoded.status};
    }
    const TransportStatus replay_status = impl_->replay_window_.accept(
        decoded.session_epoch, decoded.sequence);
    if (replay_status != TransportStatus::Accepted) {
        return {.status = replay_status};
    }
    return {
        .status = TransportStatus::Accepted,
        .datagram = std::move(decoded.datagram),
    };
}

TransportStatus UdpReferenceEndpoint::reconnect(std::uint16_t peer_port) noexcept {
    if (!impl_ || peer_port == 0U) {
        return TransportStatus::SocketError;
    }
    if (!connect_socket(impl_->socket_, peer_port)) {
        impl_->connected_ = false;
        return TransportStatus::SocketError;
    }
    impl_->peer_port_ = peer_port;
    impl_->connected_ = true;
    impl_->pending_.clear();
    if (impl_->epoch_ != std::numeric_limits<std::uint64_t>::max()) {
        ++impl_->epoch_;
    }
    impl_->next_sequence_ = 1U;
    return TransportStatus::Accepted;
}

void UdpReferenceEndpoint::disconnect() noexcept {
    if (!impl_) {
        return;
    }
    impl_->connected_ = false;
    impl_->pending_.clear();
}

std::uint16_t UdpReferenceEndpoint::local_port() const noexcept {
    return impl_ ? impl_->local_port_ : 0U;
}

std::uint64_t UdpReferenceEndpoint::session_epoch() const noexcept {
    return impl_ ? impl_->epoch_ : 0U;
}

std::size_t UdpReferenceEndpoint::pending_datagrams() const noexcept {
    return impl_ ? impl_->pending_.size() : 0U;
}

TwoInstanceCoordinator::TwoInstanceCoordinator(
    ReplicaAdapter& authoritative,
    ReplicaAdapter& follower) noexcept
    : authoritative_(&authoritative),
      follower_(&follower) {}

CoordinationResult TwoInstanceCoordinator::compare() const {
    CoordinationResult result{
        .authoritative = authoritative_->fingerprint(),
        .follower = follower_->fingerprint(),
    };
    if (result.authoritative.schema_id != result.follower.schema_id) {
        result.status = CoordinationStatus::SchemaMismatch;
    } else if (result.authoritative.frame != result.follower.frame) {
        result.status = CoordinationStatus::FrameMismatch;
    } else if (core::sha256_equal(
            result.authoritative.canonical_digest,
            result.follower.canonical_digest)) {
        result.status = CoordinationStatus::Converged;
    } else {
        result.status = CoordinationStatus::Divergent;
    }
    return result;
}

CoordinationResult TwoInstanceCoordinator::reconcile(
    std::uint64_t input_frame,
    UdpReferenceEndpoint& sender,
    UdpReferenceEndpoint& receiver,
    std::chrono::milliseconds timeout) {
    CoordinationResult before = compare();
    if (before.status == CoordinationStatus::Converged
        || before.status == CoordinationStatus::SchemaMismatch
        || before.status == CoordinationStatus::FrameMismatch) {
        return before;
    }
    std::vector<std::uint8_t> correction;
    try {
        correction = authoritative_->export_authoritative_correction(input_frame);
    } catch (const std::bad_alloc&) {
        before.status = CoordinationStatus::ReplicaRejected;
        before.replica_status = ReplicaStatus::ResourceExhausted;
        return before;
    }
    if (correction.empty()) {
        before.status = CoordinationStatus::CorrectionUnavailable;
        return before;
    }
    before.transport_status = sender.enqueue({
        .message_kind = kCorrectionMessageKind,
        .channel_id = before.authoritative.schema_id,
        .frame = input_frame,
        .payload = std::move(correction),
    });
    if (before.transport_status != TransportStatus::Accepted) {
        before.status = CoordinationStatus::TransportRejected;
        return before;
    }
    before.transport_status = sender.flush();
    if (before.transport_status != TransportStatus::Accepted) {
        before.status = CoordinationStatus::TransportRejected;
        return before;
    }
    ReceiveResult received = receiver.receive(timeout);
    before.transport_status = received.status;
    if (!received.accepted()
        || received.datagram.message_kind != kCorrectionMessageKind
        || received.datagram.channel_id != before.authoritative.schema_id
        || received.datagram.frame != input_frame) {
        before.status = CoordinationStatus::TransportRejected;
        return before;
    }
    before.replica_status = follower_->apply_authoritative_correction(
        input_frame, received.datagram.payload);
    if (before.replica_status != ReplicaStatus::Accepted) {
        before.status = CoordinationStatus::ReplicaRejected;
        return before;
    }
    CoordinationResult after = compare();
    after.transport_status = TransportStatus::Accepted;
    after.replica_status = ReplicaStatus::Accepted;
    if (after.status != CoordinationStatus::Converged) {
        after.status = CoordinationStatus::ReconciliationMismatch;
    }
    return after;
}

const char* to_string(TransportStatus status) noexcept {
    switch (status) {
    case TransportStatus::Accepted: return "accepted";
    case TransportStatus::NotConnected: return "not_connected";
    case TransportStatus::PayloadTooLarge: return "payload_too_large";
    case TransportStatus::Backpressure: return "backpressure";
    case TransportStatus::ResourceExhausted: return "resource_exhausted";
    case TransportStatus::SocketError: return "socket_error";
    case TransportStatus::TimedOut: return "timed_out";
    case TransportStatus::Malformed: return "malformed";
    case TransportStatus::UnsupportedVersion: return "unsupported_version";
    case TransportStatus::IntegrityMismatch: return "integrity_mismatch";
    case TransportStatus::StaleEpoch: return "stale_epoch";
    case TransportStatus::Replay: return "replay";
    }
    return "unknown";
}

const char* to_string(ReplicaStatus status) noexcept {
    switch (status) {
    case ReplicaStatus::Accepted: return "accepted";
    case ReplicaStatus::CorrectionUnavailable: return "correction_unavailable";
    case ReplicaStatus::InvalidCorrection: return "invalid_correction";
    case ReplicaStatus::CorrectionOutsideRetention: return "correction_outside_retention";
    case ReplicaStatus::ResourceExhausted: return "resource_exhausted";
    }
    return "unknown";
}

const char* to_string(CoordinationStatus status) noexcept {
    switch (status) {
    case CoordinationStatus::Converged: return "converged";
    case CoordinationStatus::Divergent: return "divergent";
    case CoordinationStatus::SchemaMismatch: return "schema_mismatch";
    case CoordinationStatus::FrameMismatch: return "frame_mismatch";
    case CoordinationStatus::CorrectionUnavailable: return "correction_unavailable";
    case CoordinationStatus::TransportRejected: return "transport_rejected";
    case CoordinationStatus::ReplicaRejected: return "replica_rejected";
    case CoordinationStatus::ReconciliationMismatch: return "reconciliation_mismatch";
    }
    return "unknown";
}

} // namespace neoeng::distributed_reference
