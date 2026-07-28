#include "neoeng/dcore_replica_adapter.hpp"
#include "neoeng/distributed_reference.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace std::chrono_literals;
using neoeng::core::Body;
using neoeng::core::Fixed;
using neoeng::core::InputCommand;
using neoeng::core::Vec2;
using neoeng::core::WorldState;
using neoeng::distributed_reference::CoordinationStatus;
using neoeng::distributed_reference::DCoreReplicaAdapter;
using neoeng::distributed_reference::OpaqueDatagram;
using neoeng::distributed_reference::ReferenceReplayWindow;
using neoeng::distributed_reference::TransportLimits;
using neoeng::distributed_reference::TransportStatus;
using neoeng::distributed_reference::TwoInstanceCoordinator;
using neoeng::distributed_reference::UdpReferenceEndpoint;

void check(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] WorldState initial_world() {
    return {
        .frame = 0U,
        .bodies = {
            Body{
                .id = 1U,
                .position = Vec2{Fixed::from_integer(0), Fixed::from_integer(0)},
                .velocity = Vec2{Fixed::from_integer(1), Fixed::from_integer(0)},
            },
            Body{
                .id = 2U,
                .position = Vec2{Fixed::from_integer(4), Fixed::from_integer(-2)},
                .velocity = Vec2{Fixed::from_integer(0), Fixed::from_integer(1)},
            },
        },
    };
}

[[nodiscard]] InputCommand command_for(std::uint64_t frame) {
    return {
        .entity = static_cast<std::uint32_t>(frame % 2U) + 1U,
        .acceleration = Vec2{
            Fixed::from_raw(static_cast<Fixed::rep>(frame % 7U) - 3),
            Fixed::from_raw(static_cast<Fixed::rep>(frame % 5U) - 2),
        },
    };
}

void test_real_udp_and_backpressure() {
    auto [sender, receiver] = UdpReferenceEndpoint::make_loopback_pair({
        .maximum_datagram_bytes = 256U,
        .maximum_pending_datagrams = 2U,
    });
    const OpaqueDatagram message{
        .message_kind = 7U,
        .channel_id = 42U,
        .frame = 9U,
        .payload = {1U, 2U, 3U},
    };
    check(sender.enqueue(message) == TransportStatus::Accepted, "first enqueue");
    check(sender.enqueue(message) == TransportStatus::Accepted, "second enqueue");
    check(sender.enqueue(message) == TransportStatus::Backpressure, "bounded backpressure");
    check(sender.flush() == TransportStatus::Accepted, "flush");
    const auto first = receiver.receive(500ms);
    const auto second = receiver.receive(500ms);
    check(first.accepted() && second.accepted(), "real UDP receives");
    check(first.datagram.payload == message.payload, "opaque payload preserved");
}

void test_reconnect_epoch() {
    auto [sender, receiver] = UdpReferenceEndpoint::make_loopback_pair();
    const std::uint64_t original_epoch = sender.session_epoch();
    sender.disconnect();
    check(sender.enqueue({}) == TransportStatus::NotConnected, "disconnect fails closed");
    check(sender.reconnect(receiver.local_port()) == TransportStatus::Accepted, "reconnect");
    check(sender.session_epoch() > original_epoch, "reconnect advances epoch");
    check(sender.enqueue({
        .message_kind = 3U,
        .channel_id = 1U,
        .frame = 1U,
        .payload = {9U},
    }) == TransportStatus::Accepted, "post reconnect enqueue");
    check(sender.flush() == TransportStatus::Accepted, "post reconnect flush");
    check(receiver.receive(500ms).accepted(), "post reconnect receive");
}

void test_payload_bound() {
    auto [sender, receiver] = UdpReferenceEndpoint::make_loopback_pair({
        .maximum_datagram_bytes = 96U,
        .maximum_pending_datagrams = 1U,
    });
    (void)receiver;
    check(sender.enqueue({
        .message_kind = 1U,
        .channel_id = 1U,
        .frame = 0U,
        .payload = std::vector<std::uint8_t>(64U, 0xA5U),
    }) == TransportStatus::PayloadTooLarge, "oversized payload rejected");
}

void test_integrity_and_replay_fail_closed() {
    const OpaqueDatagram message{
        .message_kind = 5U,
        .channel_id = 99U,
        .frame = 17U,
        .payload = {0x10U, 0x20U, 0x30U},
    };
    auto encoded = neoeng::distributed_reference::encode_reference_datagram(
        message, 4U, 8U);
    check(neoeng::distributed_reference::decode_reference_datagram(encoded).accepted(),
        "valid encoded datagram");
    encoded.back() ^= 0x80U;
    check(neoeng::distributed_reference::decode_reference_datagram(encoded).status
        == TransportStatus::IntegrityMismatch, "tampering rejected");

    ReferenceReplayWindow window;
    check(window.accept(4U, 8U) == TransportStatus::Accepted, "first sequence");
    check(window.accept(4U, 8U) == TransportStatus::Replay, "replay rejected");
    check(window.accept(5U, 1U) == TransportStatus::Accepted, "new epoch accepted");
    check(window.accept(4U, 9U) == TransportStatus::StaleEpoch, "stale epoch rejected");
}

void test_end_to_end_diverge_and_reconcile() {
    DCoreReplicaAdapter authoritative(initial_world(), 128U);
    DCoreReplicaAdapter follower(initial_world(), 128U);
    constexpr std::uint64_t divergence_frame = 12U;
    for (std::uint64_t frame = 0U; frame < 64U; ++frame) {
        const InputCommand command = command_for(frame);
        authoritative.advance(std::span<const InputCommand>{&command, 1U});
        if (frame == divergence_frame) {
            const InputCommand wrong{
                .entity = command.entity,
                .acceleration = Vec2{Fixed::from_integer(2), Fixed::from_integer(-1)},
            };
            follower.advance(std::span<const InputCommand>{&wrong, 1U});
        } else {
            follower.advance(std::span<const InputCommand>{&command, 1U});
        }
    }

    TwoInstanceCoordinator coordinator(authoritative, follower);
    check(coordinator.compare().status == CoordinationStatus::Divergent,
        "deliberate divergence observed");
    const auto diagnosis = authoritative.diagnose_against(follower, 0xC5010U);
    check(diagnosis.divergent, "semantic divergence diagnosed");
    check(!diagnosis.first_divergent_component.empty(), "component localized");
    auto [sender, receiver] = UdpReferenceEndpoint::make_loopback_pair();
    const auto reconciled = coordinator.reconcile(
        divergence_frame, sender, receiver, 500ms);
    check(reconciled.status == CoordinationStatus::Converged,
        "authoritative correction reconciles");
    check(authoritative.state() == follower.state(), "canonical states equal");
}

void test_invalid_correction_fails_closed() {
    DCoreReplicaAdapter authoritative(initial_world(), 32U);
    DCoreReplicaAdapter follower(initial_world(), 32U);
    const InputCommand command = command_for(0U);
    authoritative.advance(std::span<const InputCommand>{&command, 1U});
    follower.advance({});
    const WorldState before = follower.state();
    const std::uint8_t malformed[]{0xFFU};
    check(follower.apply_authoritative_correction(0U, malformed)
        == neoeng::distributed_reference::ReplicaStatus::InvalidCorrection,
        "malformed correction rejected");
    check(follower.state() == before, "malformed correction cannot mutate state");
}

void test_retention_fails_closed() {
    DCoreReplicaAdapter authoritative(initial_world(), 32U);
    DCoreReplicaAdapter follower(initial_world(), 4U);
    for (std::uint64_t frame = 0U; frame < 12U; ++frame) {
        const InputCommand command = command_for(frame);
        authoritative.advance(std::span<const InputCommand>{&command, 1U});
        follower.advance({});
    }
    const auto correction = authoritative.export_authoritative_correction(0U);
    check(!correction.empty(), "authority retains correction payload");
    check(follower.apply_authoritative_correction(0U, correction)
        == neoeng::distributed_reference::ReplicaStatus::CorrectionOutsideRetention,
        "expired snapshot rejected");
}

} // namespace

int main() {
    try {
        test_real_udp_and_backpressure();
        test_reconnect_epoch();
        test_payload_bound();
        test_integrity_and_replay_fail_closed();
        test_end_to_end_diverge_and_reconcile();
        test_invalid_correction_fails_closed();
        test_retention_fails_closed();
        std::cout << "distributed_reference_tests=passed\n";
        std::cout << "transport=udp_loopback\n";
        std::cout << "canonical_authority=preserved\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "distributed_reference_tests=failed\n";
        std::cerr << "error=" << error.what() << '\n';
        return 1;
    }
}
