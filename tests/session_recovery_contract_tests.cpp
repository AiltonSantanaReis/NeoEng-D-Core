#include "neoeng/core/network_security.hpp"
#include "neoeng/core/operational_runtime.hpp"
#include "neoeng/core/recovery_contract.hpp"
#include "neoeng/core/session_security.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

using namespace neoeng::core;

namespace {

int failures{};

#define CHECK(name, expression) do { \
    if (!(expression)) { \
        std::cerr << "FAIL " << (name) << ": " << #expression << '\n'; \
        ++failures; \
    } \
} while (false)

AuthenticationKey key_with_seed(std::uint8_t seed) {
    AuthenticationKey key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(seed + index * 7U);
    }
    return key;
}

HandshakeNonce nonce_with_seed(std::uint8_t seed) {
    HandshakeNonce nonce{};
    for (std::size_t index = 0; index < nonce.size(); ++index) {
        nonce[index] = static_cast<std::uint8_t>(seed + index * 11U);
    }
    return nonce;
}

RootKeyRecord active_record(
    const AuthenticationKey& key,
    RootKeyEpoch epoch,
    std::uint32_t roles = session_role_mask(SessionRole::Player)) {
    return {
        .key_id = 7U,
        .epoch = epoch,
        .key = key,
        .allowed_role_mask = roles,
        .not_before_ms = 1'000U,
        .not_after_ms = 100'000U,
        .lifecycle = RootKeyLifecycle::Active,
    };
}

ClientHandshakeContext client_context(RootKeyEpoch epoch, std::uint8_t nonce_seed) {
    return {
        .origin = 42U,
        .role = SessionRole::Player,
        .key_id = 7U,
        .key_epoch = epoch,
        .minimum_protocol = 1U,
        .maximum_protocol = 1U,
        .cipher_suite = 1U,
        .sent_at_ms = 2'000U,
        .client_nonce = nonce_with_seed(nonce_seed),
    };
}

void test_authenticated_handshake_key_rotation_and_session_gateway() {
    constexpr const char* name = "authenticated_handshake_key_rotation_and_session_gateway";
    const AuthenticationKey epoch1_key = key_with_seed(3U);
    const AuthenticationKey epoch2_key = key_with_seed(91U);
    SessionKeyRing ring(4U);
    CHECK(name, ring.upsert(active_record(epoch1_key, 1U)));

    SessionHandshakeServer server(ring, {
        .maximum_clock_skew_ms = 500U,
        .session_lifetime_ms = 10'000U,
        .maximum_root_keys = 4U,
        .maximum_handshake_replay_entries = 8U,
    });
    const ClientHello hello = create_client_hello(epoch1_key, client_context(1U, 5U));
    const ServerHandshakeDecision accepted = server.accept(
        2'100U, hello.datagram, 1001U, nonce_with_seed(31U));
    CHECK(name, accepted.accepted());
    CHECK(name, accepted.response.size() == kServerHelloBytes);

    const ClientHandshakeDecision client = finalize_client_handshake(
        epoch1_key, hello.context, 2'101U, accepted.response, 500U);
    CHECK(name, client.accepted());
    CHECK(name, client.secrets.client_to_server_key == accepted.secrets.client_to_server_key);
    CHECK(name, client.secrets.server_to_client_key == accepted.secrets.server_to_client_key);
    CHECK(name, client.secrets.client_to_server_key != client.secrets.server_to_client_key);
    constexpr AuthenticationKey expected_client_to_server{
        0xbcU, 0x8fU, 0xf7U, 0x15U, 0x35U, 0x5bU, 0x5dU, 0x4aU,
        0x5fU, 0x6aU, 0x62U, 0xffU, 0xe8U, 0x3eU, 0x02U, 0x9fU,
        0xcaU, 0x5aU, 0x94U, 0x12U, 0x40U, 0x54U, 0xfcU, 0x29U,
        0x9eU, 0x84U, 0x2aU, 0x77U, 0xfbU, 0xabU, 0x54U, 0x07U,
    };
    CHECK(name, client.secrets.client_to_server_key == expected_client_to_server);

    CHECK(name, server.accept(2'102U, hello.datagram, 1002U, nonce_with_seed(32U)).reason
        == HandshakeRejectReason::ReplayDetected);

    std::vector<std::uint8_t> downgraded = hello.datagram;
    downgraded.at(8U) = 0U;
    downgraded.at(9U) = 0U;
    CHECK(name, server.accept(2'102U, downgraded, 1002U, nonce_with_seed(32U)).reason
        == HandshakeRejectReason::UnsupportedProtocolRange);

    NetworkSecurityGateway gateway(key_with_seed(201U), {
        .maximum_payload_bytes = 256U,
        .maximum_input_commands = 8U,
        .maximum_tracked_origins = 4U,
        .maximum_clock_skew_ms = 500U,
        .session_timeout_ms = 20'000U,
        .rate_limit_packets_per_second = 100U,
        .rate_limit_burst_packets = 100U,
        .require_established_session = true,
    });
    CHECK(name, gateway.install_session(42U, accepted.secrets.gateway_binding(), 2'100U));
    const std::array<std::uint8_t, 3> payload{1U, 2U, 3U};
    const std::vector<std::uint8_t> packet = encode_authenticated_packet(
        client.secrets.client_to_server_key, client.secrets.session_id, 1U, 2'110U, payload);
    CHECK(name, gateway.process(42U, 2'110U, packet).accepted());
    CHECK(name, gateway.process(42U, 2'111U, packet).reason == PacketRejectReason::ReplayDuplicate);

    const std::vector<std::uint8_t> bootstrap_packet = encode_authenticated_packet(
        key_with_seed(201U), client.secrets.session_id, 2U, 2'112U, payload);
    CHECK(name, gateway.process(42U, 2'112U, bootstrap_packet).reason
        == PacketRejectReason::AuthenticationFailed);

    CHECK(name, ring.rotate(7U, 1U, active_record(epoch2_key, 2U)));
    const ClientHello retired_hello = create_client_hello(epoch1_key, client_context(1U, 6U));
    CHECK(name, server.accept(2'200U, retired_hello.datagram, 1003U, nonce_with_seed(33U)).reason
        == HandshakeRejectReason::KeyRetired);

    const std::vector<std::uint8_t> still_valid_packet = encode_authenticated_packet(
        client.secrets.client_to_server_key, client.secrets.session_id, 2U, 2'201U, payload);
    CHECK(name, gateway.process(42U, 2'201U, still_valid_packet).accepted());
    CHECK(name, gateway.revoke_sessions_for_key(7U, 1U) == 1U);
    const std::vector<std::uint8_t> revoked_packet = encode_authenticated_packet(
        client.secrets.client_to_server_key, client.secrets.session_id, 3U, 2'202U, payload);
    CHECK(name, gateway.process(42U, 2'202U, revoked_packet).reason
        == PacketRejectReason::SessionRevoked);

    const ClientHello epoch2_hello = create_client_hello(epoch2_key, client_context(2U, 7U));
    const ServerHandshakeDecision epoch2 = server.accept(
        2'300U, epoch2_hello.datagram, 2001U, nonce_with_seed(34U));
    CHECK(name, epoch2.accepted());
    CHECK(name, epoch2.secrets.client_to_server_key != client.secrets.client_to_server_key);
    CHECK(name, gateway.install_session(42U, epoch2.secrets.gateway_binding(), 2'300U));

    const std::vector<std::uint8_t> old_session_packet = encode_authenticated_packet(
        client.secrets.client_to_server_key, client.secrets.session_id, 4U, 2'301U, payload);
    CHECK(name, gateway.process(42U, 2'301U, old_session_packet).reason
        == PacketRejectReason::SessionMismatch);

    const KeyRevocationResult revoked_key = revoke_key_and_sessions(ring, gateway, 7U, 2U);
    CHECK(name, revoked_key.root_key_revoked);
    CHECK(name, revoked_key.sessions_revoked == 1U);
    const ClientHello revoked_hello = create_client_hello(epoch2_key, client_context(2U, 8U));
    CHECK(name, server.accept(2'400U, revoked_hello.datagram, 2002U, nonce_with_seed(35U)).reason
        == HandshakeRejectReason::KeyRevoked);
}

void test_role_authorization_and_session_expiry() {
    constexpr const char* name = "role_authorization_and_session_expiry";
    const AuthenticationKey root = key_with_seed(55U);
    SessionKeyRing ring(2U);
    CHECK(name, ring.upsert(active_record(root, 1U, session_role_mask(SessionRole::Spectator))));
    SessionHandshakeServer server(ring, {
        .maximum_clock_skew_ms = 100U,
        .session_lifetime_ms = 50U,
        .maximum_root_keys = 2U,
        .maximum_handshake_replay_entries = 4U,
    });
    const ClientHello player = create_client_hello(root, client_context(1U, 41U));
    CHECK(name, server.accept(2'000U, player.datagram, 9U, nonce_with_seed(42U)).reason
        == HandshakeRejectReason::RoleNotAuthorized);

    RootKeyRecord spectator = active_record(root, 2U, session_role_mask(SessionRole::Spectator));
    CHECK(name, ring.upsert(spectator));
    ClientHandshakeContext spectator_context = client_context(2U, 43U);
    spectator_context.role = SessionRole::Spectator;
    const ClientHello hello = create_client_hello(root, spectator_context);
    const ServerHandshakeDecision accepted = server.accept(
        2'000U, hello.datagram, 10U, nonce_with_seed(44U));
    CHECK(name, accepted.accepted());

    NetworkSecurityGateway gateway(key_with_seed(100U), {
        .maximum_payload_bytes = 32U,
        .maximum_input_commands = 1U,
        .maximum_tracked_origins = 2U,
        .maximum_clock_skew_ms = 100U,
        .session_timeout_ms = 1'000U,
        .rate_limit_packets_per_second = 10U,
        .rate_limit_burst_packets = 10U,
        .require_established_session = true,
    });
    CHECK(name, gateway.install_session(42U, accepted.secrets.gateway_binding(), 2'000U));
    const std::array<std::uint8_t, 1> payload{0U};
    const auto expired = encode_authenticated_packet(
        accepted.secrets.client_to_server_key, 10U, 1U, 2'060U, payload);
    CHECK(name, gateway.process(42U, 2'060U, expired).reason == PacketRejectReason::SessionExpired);
}


void test_operational_runtime_checkpoint_restore_contract() {
    constexpr const char* name = "operational_runtime_checkpoint_restore_contract";
    const AuthenticationKey key = key_with_seed(11U);
    WorldState world{
        .frame = 0U,
        .bodies = {{
            .id = 1U,
            .position = {},
            .velocity = {},
        }},
    };
    OperationalRuntimeConfig config{};
    config.network.maximum_payload_bytes = 256U;
    config.network.maximum_input_commands = 4U;
    config.network.maximum_tracked_origins = 4U;
    config.network.maximum_clock_skew_ms = 1'000U;
    config.network.session_timeout_ms = 60'000U;
    config.network.rate_limit_packets_per_second = 100U;
    config.network.rate_limit_burst_packets = 100U;
    config.snapshot_capacity = 16U;
    config.trace_capacity = 128U;
    config.time_travel_frame_capacity = 16U;
    config.safe_checkpoint_interval_frames = 2U;
    OperationalRuntime runtime(std::move(world), key, config);

    const std::array<InputCommand, 1> commands{{
        {.entity = 1U, .acceleration = {Fixed::from_integer(1), Fixed{}}},
    }};
    const std::vector<std::uint8_t> payload = encode_input_payload(commands);
    for (std::uint64_t sequence = 1U; sequence <= 3U; ++sequence) {
        const std::uint64_t now = 1'000U + sequence;
        const auto packet = encode_authenticated_packet(key, 99U, sequence, now, payload);
        CHECK(name, runtime.ingest_authenticated_input(1U, now, sequence, packet).advanced);
    }
    CHECK(name, runtime.state().frame == 3U);
    CHECK(name, runtime.recovery().safe_checkpoint_frame() == 2U);

    const RecoveryContractEvent first_oom = runtime.report_external_fault_event(
        FaultKind::OutOfMemory, 200U, 10U);
    CHECK(name, first_oom.directive == HostDirective::DisableTelemetryAndPause);
    CHECK(name, runtime.acknowledge_recovery(first_oom.generation,
        RecoveryAcknowledgement::ResourcesRecovered, 201U).accepted);

    const RecoveryContractEvent second_oom = runtime.report_external_fault_event(
        FaultKind::OutOfMemory, 202U, 20U);
    CHECK(name, second_oom.directive == HostDirective::RestoreCheckpoint);
    CHECK(name, second_oom.signal.rollback_checkpoint_frame == 2U);
    const RecoveryAckResult restored = runtime.acknowledge_recovery(
        second_oom.generation, RecoveryAcknowledgement::CheckpointRestored, 203U, 2U, 30U);
    CHECK(name, restored.accepted);
    CHECK(name, runtime.state().frame == 2U);
    CHECK(name, runtime.time_travel().newest_frame().value_or(999U) == 2U);

    const auto packet4 = encode_authenticated_packet(key, 99U, 4U, 1'004U, payload);
    CHECK(name, runtime.ingest_authenticated_input(1U, 1'004U, 204U, packet4).advanced);
    CHECK(name, runtime.state().frame == 3U);
    CHECK(name, runtime.time_travel().newest_frame().value_or(999U) == 3U);
}

void test_recovery_contract_generation_and_acknowledgement() {
    constexpr const char* name = "recovery_contract_generation_and_acknowledgement";
    RecoveryController controller({
        .continue_headless_after_device_loss = true,
        .maximum_consecutive_io_stalls = 1U,
        .maximum_consecutive_network_gaps = 1U,
        .malformed_packets_before_quarantine = 2U,
    });
    controller.mark_safe_checkpoint(12U);
    RecoveryHostBridge bridge;

    const RecoveryContractEvent device = bridge.publish(
        controller.report_fault(FaultKind::DeviceLost, 20U, 101U));
    CHECK(name, device.directive == HostDirective::ContinueHeadless);
    CHECK(name, device.acknowledgement_required);
    CHECK(name, bridge.acknowledge(controller, device.generation + 1U,
        RecoveryAcknowledgement::DeviceRestored, 20U).reason
        == RecoveryAckRejectReason::StaleGeneration);
    CHECK(name, bridge.acknowledge(controller, device.generation,
        RecoveryAcknowledgement::DependencyRestored, 20U).reason
        == RecoveryAckRejectReason::InvalidAcknowledgement);
    const RecoveryAckResult restored = bridge.acknowledge(controller, device.generation,
        RecoveryAcknowledgement::DeviceRestored, 20U, 102U);
    CHECK(name, restored.accepted);
    CHECK(name, controller.mode() == RecoveryMode::Normal);

    const RecoveryContractEvent io = bridge.publish(
        controller.report_fault(FaultKind::IoStall, 21U, 103U));
    CHECK(name, io.directive == HostDirective::ReuseLastConfirmedInput);
    CHECK(name, bridge.acknowledge(controller, io.generation,
        RecoveryAcknowledgement::RetryLater, 21U).accepted);
    CHECK(name, bridge.pending().has_value());
    CHECK(name, bridge.acknowledge(controller, io.generation,
        RecoveryAcknowledgement::DependencyRestored, 21U).accepted);

    const RecoveryContractEvent oom1 = bridge.publish(
        controller.report_fault(FaultKind::OutOfMemory, 22U, 104U));
    CHECK(name, oom1.directive == HostDirective::DisableTelemetryAndPause);
    CHECK(name, bridge.acknowledge(controller, oom1.generation,
        RecoveryAcknowledgement::ResourcesRecovered, 22U).accepted);

    const RecoveryContractEvent oom2 = bridge.publish(
        controller.report_fault(FaultKind::OutOfMemory, 23U, 105U));
    CHECK(name, oom2.directive == HostDirective::RestoreCheckpoint);
    CHECK(name, bridge.acknowledge(controller, oom2.generation,
        RecoveryAcknowledgement::CheckpointRestored, 23U, 106U, 11U).reason
        == RecoveryAckRejectReason::CheckpointMismatch);
    CHECK(name, bridge.acknowledge(controller, oom2.generation,
        RecoveryAcknowledgement::CheckpointRestored, 23U, 106U, 12U).accepted);

    static_cast<void>(bridge.publish(controller.report_fault(
        FaultKind::MalformedPacket, 24U, 107U)));
    const RecoveryContractEvent quarantine = bridge.publish(controller.report_fault(
        FaultKind::MalformedPacket, 24U, 108U));
    CHECK(name, quarantine.directive == HostDirective::QuarantineOrigin);
    CHECK(name, bridge.acknowledge(controller, quarantine.generation,
        RecoveryAcknowledgement::OriginReset, 24U).accepted);

    const std::string json = recovery_contract_json(quarantine);
    CHECK(name, json.find("neoeng.dcore.recovery.v1") != std::string::npos);
    CHECK(name, json.find("quarantine_origin") != std::string::npos);

    RecoveryController no_checkpoint_controller;
    RecoveryHostBridge halted_bridge;
    const RecoveryContractEvent pressure = halted_bridge.publish(
        no_checkpoint_controller.report_fault(FaultKind::OutOfMemory, 30U, 109U));
    CHECK(name, pressure.directive == HostDirective::DisableTelemetryAndPause);
    const RecoveryContractEvent halted = halted_bridge.publish(
        no_checkpoint_controller.report_fault(FaultKind::OutOfMemory, 31U, 110U));
    CHECK(name, halted.directive == HostDirective::HaltSimulation);
    CHECK(name, halted.signal.mode == RecoveryMode::Halted);
    CHECK(name, !halted.acknowledgement_required);
    CHECK(name, halted_bridge.pending().has_value());
    CHECK(name, halted_bridge.pending()->generation == halted.generation);
    CHECK(name, halted_bridge.acknowledge(no_checkpoint_controller, halted.generation,
        RecoveryAcknowledgement::ResourcesRecovered, 31U).reason
        == RecoveryAckRejectReason::RuntimeHalted);
}

} // namespace

int main() {
    test_authenticated_handshake_key_rotation_and_session_gateway();
    test_role_authorization_and_session_expiry();
    test_operational_runtime_checkpoint_restore_contract();
    test_recovery_contract_generation_and_acknowledgement();
    if (failures != 0) {
        std::cerr << failures << " session/recovery contract test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "session_recovery_contract_tests=passed\n";
    return EXIT_SUCCESS;
}
