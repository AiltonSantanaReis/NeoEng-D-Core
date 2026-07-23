#include "neoeng/core/hardware_profile.hpp"
#include "neoeng/core/network_security.hpp"
#include "neoeng/core/observability.hpp"
#include "neoeng/core/operational_runtime.hpp"
#include "neoeng/core/recovery.hpp"
#include "neoeng/core/simulation.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, std::string_view test, std::string_view expression) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << test << ": " << expression << '\n';
    }
}

#define CHECK(test, expression) check(static_cast<bool>(expression), test, #expression)

using namespace neoeng::core;

AuthenticationKey test_key() {
    AuthenticationKey key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(index + 1U);
    }
    return key;
}

WorldState world_at(std::uint64_t frame, Fixed position_x = {}) {
    return {
        .frame = frame,
        .bodies = {{
            .id = 1U,
            .position = {position_x, {}},
            .velocity = {},
        }},
    };
}

void test_hmac_sha256_rfc4231() {
    constexpr std::string_view name = "hmac_sha256_rfc4231";
    const std::array<std::uint8_t, 20> key{
        0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU,
        0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU, 0x0bU,
    };
    constexpr std::array<std::uint8_t, 8> message{
        'H', 'i', ' ', 'T', 'h', 'e', 'r', 'e',
    };
    constexpr AuthenticationTag expected{
        0xb0U, 0x34U, 0x4cU, 0x61U, 0xd8U, 0xdbU, 0x38U, 0x53U,
        0x5cU, 0xa8U, 0xafU, 0xceU, 0xafU, 0x0bU, 0xf1U, 0x2bU,
        0x88U, 0x1dU, 0xc2U, 0x00U, 0xc9U, 0x83U, 0x3dU, 0xa7U,
        0x26U, 0xe9U, 0x37U, 0x6cU, 0x2eU, 0x32U, 0xcfU, 0xf7U,
    };
    CHECK(name, hmac_sha256(key, message) == expected);
}

void test_secure_packet_authentication_replay_rate_limit_and_timeout() {
    constexpr std::string_view name = "secure_packet_authentication_replay_rate_limit_timeout";
    NetworkSecurityLimits limits{
        .maximum_payload_bytes = 256U,
        .maximum_input_commands = 4U,
        .maximum_tracked_origins = 2U,
        .maximum_clock_skew_ms = 50U,
        .session_timeout_ms = 100U,
        .rate_limit_packets_per_second = 2U,
        .rate_limit_burst_packets = 2U,
    };
    bool zero_key_rejected = false;
    try {
        [[maybe_unused]] NetworkSecurityGateway invalid_gateway(AuthenticationKey{}, limits);
    } catch (const std::invalid_argument&) {
        zero_key_rejected = true;
    }
    CHECK(name, zero_key_rejected);

    NetworkSecurityGateway gateway(test_key(), limits);
    const std::array<InputCommand, 1> commands{{
        {.entity = 1U, .acceleration = {Fixed::from_integer(1), Fixed::from_integer(-1)}},
    }};
    const std::vector<std::uint8_t> payload = encode_input_payload(commands);
    const auto packet1 = encode_authenticated_packet(test_key(), 7U, 1U, 1'000U, payload);
    const PacketDecision accepted = gateway.process(10U, 1'000U, packet1);
    CHECK(name, accepted.accepted());
    CHECK(name, accepted.packet.sequence == 1U);
    CHECK(name, accepted.packet.payload.size() == payload.size());
    CHECK(name, gateway.process(10U, 1'000U, packet1).reason == PacketRejectReason::ReplayDuplicate);

    const auto packet0 = encode_authenticated_packet(test_key(), 7U, 0U, 1'000U, payload);
    CHECK(name, gateway.process(10U, 1'000U, packet0).accepted());
    const auto packet2 = encode_authenticated_packet(test_key(), 7U, 2U, 1'000U, payload);
    CHECK(name, gateway.process(10U, 1'000U, packet2).reason == PacketRejectReason::RateLimited);

    std::vector<std::uint8_t> tampered = packet2;
    tampered[kSecurePacketHeaderBytes] ^= 0x80U;
    CHECK(name, gateway.process(11U, 1'000U, tampered).reason == PacketRejectReason::AuthenticationFailed);

    const auto stale = encode_authenticated_packet(test_key(), 8U, 0U, 800U, payload);
    CHECK(name, gateway.process(11U, 1'000U, stale).reason == PacketRejectReason::TimestampTooOld);

    const auto session_mismatch = encode_authenticated_packet(test_key(), 8U, 3U, 1'010U, payload);
    CHECK(name, gateway.process(10U, 1'010U, session_mismatch).reason == PacketRejectReason::SessionMismatch);

    const auto rotated = encode_authenticated_packet(test_key(), 8U, 0U, 1'200U, payload);
    CHECK(name, gateway.process(10U, 1'200U, rotated).accepted());
}

void test_input_payload_total_parser() {
    constexpr std::string_view name = "input_payload_total_parser";
    const std::array<InputCommand, 2> commands{{
        {.entity = 1U, .acceleration = {Fixed::from_integer(2), Fixed::from_integer(-3)}},
        {.entity = 7U, .acceleration = {Fixed::from_ratio(1, 2), Fixed::from_ratio(3, 4)}},
    }};
    const std::vector<std::uint8_t> payload = encode_input_payload(commands);
    std::array<InputCommand, 4> output{};
    const InputPayloadParseResult parsed = parse_input_payload(payload, output);
    CHECK(name, parsed.accepted());
    CHECK(name, parsed.command_count == commands.size());
    CHECK(name, output[0] == commands[0]);
    CHECK(name, output[1] == commands[1]);

    CHECK(name, parse_input_payload(std::span<const std::uint8_t>{}, output).reason
        == InputPayloadRejectReason::HeaderTruncated);
    std::vector<std::uint8_t> trailing = payload;
    trailing.push_back(0U);
    CHECK(name, parse_input_payload(trailing, output).reason == InputPayloadRejectReason::InvalidLength);
    std::vector<std::uint8_t> invalid_entity = payload;
    invalid_entity[2] = 0U;
    invalid_entity[3] = 0U;
    invalid_entity[4] = 0U;
    invalid_entity[5] = 0U;
    CHECK(name, parse_input_payload(invalid_entity, output).reason == InputPayloadRejectReason::InvalidEntity);
}

void test_hostile_parser_fuzz_smoke() {
    constexpr std::string_view name = "hostile_parser_fuzz_smoke";
    NetworkSecurityLimits limits{
        .maximum_payload_bytes = 512U,
        .maximum_input_commands = 8U,
        .maximum_tracked_origins = 4U,
        .maximum_clock_skew_ms = 5'000U,
        .session_timeout_ms = 1'000U,
        .rate_limit_packets_per_second = 1'000U,
        .rate_limit_burst_packets = 1'000U,
    };
    NetworkSecurityGateway gateway(test_key(), limits);
    std::mt19937_64 random(0x4E454F454E47ULL);
    std::uniform_int_distribution<std::size_t> size_distribution(0U, 1'600U);
    std::uniform_int_distribution<unsigned int> byte_distribution(0U, 255U);
    std::array<InputCommand, 8> output{};
    for (std::size_t iteration = 0; iteration < 20'000U; ++iteration) {
        std::vector<std::uint8_t> bytes(size_distribution(random));
        for (std::uint8_t& byte : bytes) {
            byte = static_cast<std::uint8_t>(byte_distribution(random));
        }
        const PacketDecision decision = gateway.process(iteration % 4U, 10'000U, bytes);
        if (decision.accepted()) {
            const InputPayloadParseResult parsed = parse_input_payload(decision.packet.payload, output, limits);
            CHECK(name, parsed.command_count <= output.size());
        }
    }
    CHECK(name, gateway.tracked_origins() <= limits.maximum_tracked_origins);
}

void test_trace_buffer_and_time_travel() {
    constexpr std::string_view name = "trace_buffer_and_time_travel";
    TraceBuffer trace(2U);
    trace.record({.correlation_id = 10U, .frame = 0U, .category = TraceCategory::Input,
        .outcome = TraceOutcome::Accepted, .code = TraceCode::InputAuthenticated});
    trace.record({.correlation_id = 10U, .frame = 1U, .category = TraceCategory::Simulation,
        .outcome = TraceOutcome::Applied, .code = TraceCode::StateAdvanced});
    trace.record({.correlation_id = 11U, .frame = 1U, .category = TraceCategory::Budget,
        .outcome = TraceOutcome::Rejected, .code = TraceCode::BudgetExceeded});
    const std::vector<TraceEvent> retained = trace.snapshot();
    CHECK(name, retained.size() == 2U);
    CHECK(name, retained.front().sequence == 1U);
    CHECK(name, trace.overwritten_events() == 1U);
    CHECK(name, trace.by_frame(1U).size() == 2U);
    CHECK(name, trace.by_correlation(10U).size() == 1U);

    TimeTravelDebugger debugger(2U);
    const std::array<InputCommand, 1> inputs{{
        {.entity = 1U, .acceleration = {Fixed::from_integer(1), {}}},
    }};
    const WorldState first = world_at(0U);
    const WorldState second = step(first, inputs);
    debugger.record_frame(first, {}, {});
    debugger.record_frame(second, inputs, retained);
    CHECK(name, debugger.frame(0U) != nullptr);
    CHECK(name, debugger.entity(1U, 1U) != nullptr);
    const FrameDiff diff = debugger.compare(0U, 1U);
    CHECK(name, !diff.identical());
    CHECK(name, !diff.changes.empty());
    CHECK(name, debugger.correlate(10U).size() == 1U);
    const std::string capture = debugger.export_reproducible_json(42U, "P0-lab-2026-07");
    CHECK(name, capture.find("neoeng.dcore.time-travel.v1") != std::string::npos);
    CHECK(name, capture.find("P0-lab-2026-07") != std::string::npos);

    bool duplicate_frame_rejected = false;
    try {
        debugger.record_frame(second, inputs, retained);
    } catch (const std::invalid_argument&) {
        duplicate_frame_rejected = true;
    }
    CHECK(name, duplicate_frame_rejected);

    debugger.record_frame(world_at(2U, Fixed::from_integer(9)), {}, {});
    CHECK(name, debugger.frame(0U) == nullptr);
    CHECK(name, debugger.oldest_frame() == 1U);
    CHECK(name, debugger.newest_frame() == 2U);
}

void test_operational_runtime_end_to_end() {
    constexpr std::string_view name = "operational_runtime_end_to_end";
    OperationalRuntimeConfig config{};
    config.network.maximum_payload_bytes = 256U;
    config.network.maximum_input_commands = 4U;
    config.network.maximum_tracked_origins = 8U;
    config.network.maximum_clock_skew_ms = 100U;
    config.network.session_timeout_ms = 1'000U;
    config.network.rate_limit_packets_per_second = 60U;
    config.network.rate_limit_burst_packets = 120U;
    config.trace_capacity = 32U;
    config.time_travel_frame_capacity = 8U;
    OperationalRuntime runtime(world_at(0U), test_key(), config);
    const std::array<InputCommand, 1> commands{{
        {.entity = 1U, .acceleration = {Fixed::from_integer(1), {}}},
    }};
    const std::vector<std::uint8_t> payload = encode_input_payload(commands);
    const std::vector<std::uint8_t> packet = encode_authenticated_packet(
        test_key(), 99U, 0U, 5'000U, payload);
    const OperationalStepResult step_result = runtime.ingest_authenticated_input(
        55U, 5'000U, 0xABCDEFU, packet);
    CHECK(name, step_result.advanced);
    CHECK(name, step_result.resulting_frame == 1U);
    CHECK(name, runtime.time_travel().frame(1U) != nullptr);
    CHECK(name, runtime.time_travel().correlate(0xABCDEFU).size() >= 2U);

    std::vector<std::uint8_t> tampered = packet;
    tampered.back() ^= 1U;
    const OperationalStepResult rejected = runtime.ingest_authenticated_input(
        56U, 5'000U, 0xBADU, tampered);
    CHECK(name, !rejected.advanced);
    CHECK(name, rejected.packet_reason == PacketRejectReason::AuthenticationFailed);
    CHECK(name, runtime.traces().by_correlation(0xBADU).size() >= 2U);
}

void test_recovery_state_machine() {
    constexpr std::string_view name = "recovery_state_machine";
    RecoveryController recovery({
        .continue_headless_after_device_loss = true,
        .maximum_consecutive_io_stalls = 2U,
        .maximum_consecutive_network_gaps = 2U,
        .malformed_packets_before_quarantine = 3U,
    });
    recovery.mark_safe_checkpoint(12U);
    const RecoverySignal device = recovery.report_fault(FaultKind::DeviceLost, 20U, 100U);
    CHECK(name, device.action == RecoveryAction::ContinueHeadless);
    CHECK(name, device.simulation_may_advance());
    CHECK(name, recovery.acknowledge_recovery(20U, 100U).mode == RecoveryMode::Normal);

    CHECK(name, recovery.report_fault(FaultKind::IoStall, 21U).action
        == RecoveryAction::ReuseLastConfirmedInput);
    CHECK(name, recovery.report_fault(FaultKind::IoStall, 22U).action
        == RecoveryAction::ReuseLastConfirmedInput);
    const RecoverySignal stalled = recovery.report_fault(FaultKind::IoStall, 23U);
    CHECK(name, stalled.action == RecoveryAction::EnterSafeWait);
    CHECK(name, !stalled.simulation_may_advance());
    CHECK(name, recovery.acknowledge_recovery(23U).mode == RecoveryMode::Normal);

    CHECK(name, recovery.report_fault(FaultKind::MalformedPacket, 24U).action
        == RecoveryAction::DropInput);
    CHECK(name, recovery.report_fault(FaultKind::MalformedPacket, 24U).action
        == RecoveryAction::DropInput);
    CHECK(name, recovery.report_fault(FaultKind::MalformedPacket, 24U).action
        == RecoveryAction::QuarantineOrigin);
    recovery.reset_input_quarantine();
    CHECK(name, recovery.mode() == RecoveryMode::Normal);

    const RecoverySignal oom1 = recovery.report_fault(FaultKind::OutOfMemory, 25U, 200U);
    CHECK(name, oom1.action == RecoveryAction::DisableNonessentialTelemetry);
    const RecoverySignal oom2 = recovery.report_fault(FaultKind::OutOfMemory, 25U, 200U);
    CHECK(name, oom2.action == RecoveryAction::RollbackToCheckpoint);
    CHECK(name, oom2.rollback_checkpoint_frame == 12U);
    const TraceEvent event = recovery_trace_event(oom2, 123U);
    CHECK(name, event.code == TraceCode::SafeRollbackEntered);
    CHECK(name, event.correlation_id == 200U);
}

HardwareEnvironmentBaseline complete_baseline() {
    return {
        .environment_id = "P1-NVIDIA-LAB-001",
        .cpu_sku = "registered-cpu-sku",
        .gpu_sku = "registered-nvidia-sku",
        .driver_version = "registered-driver",
        .os_build = "registered-windows-build",
        .power_profile = "registered-power-profile",
    };
}

void test_hardware_profile_qualification_gate() {
    constexpr std::string_view name = "hardware_profile_qualification_gate";
    const HardwareProfileContract p1 = hardware_profile_contract(HardwareProfileId::P1NvidiaTarget);
    CHECK(name, p1.rollback_p99_limit_ns == 2'000'000U);
    CHECK(name, p1.ecs_maintenance_p99_limit_ns == 100'000U);
    CHECK(name, p1.enforces_rollback_budget);
    CHECK(name, p1.enforces_ecs_budget);

    const HardwareMeasurement passing{
        .profile = HardwareProfileId::P1NvidiaTarget,
        .environment_id = "P1-NVIDIA-LAB-001",
        .rollback_p99_ns = 2'000'000U,
        .ecs_maintenance_p99_ns = 100'000U,
        .rollback_measurement_present = true,
        .ecs_measurement_present = true,
        .determinism_passed = true,
        .serialization_compatibility_passed = true,
    };
    CHECK(name, evaluate_hardware_qualification(complete_baseline(), passing).status
        == QualificationStatus::Passed);

    HardwareEnvironmentBaseline missing{};
    CHECK(name, evaluate_hardware_qualification(missing, passing).status
        == QualificationStatus::Unqualified);

    HardwareMeasurement slow = passing;
    slow.rollback_p99_ns = 2'000'001U;
    const HardwareQualificationResult failed = evaluate_hardware_qualification(
        complete_baseline(), slow);
    CHECK(name, failed.status == QualificationStatus::Failed);
    CHECK(name, contains(failed.failures, QualificationFailure::RollbackBudgetExceeded));

    HardwareMeasurement p3{
        .profile = HardwareProfileId::P3Arm64Compatibility,
        .environment_id = "P3-ARM64-LAB-001",
        .determinism_passed = true,
        .serialization_compatibility_passed = true,
    };
    HardwareEnvironmentBaseline p3_baseline{
        .environment_id = "P3-ARM64-LAB-001",
        .cpu_sku = "registered-arm64-sku",
        .gpu_sku = "not-applicable",
        .driver_version = "not-applicable",
        .os_build = "registered-os-build",
        .power_profile = "registered-power-profile",
    };
    CHECK(name, evaluate_hardware_qualification(p3_baseline, p3).status
        == QualificationStatus::Passed);
}

} // namespace

int main() {
    test_hmac_sha256_rfc4231();
    test_secure_packet_authentication_replay_rate_limit_and_timeout();
    test_input_payload_total_parser();
    test_hostile_parser_fuzz_smoke();
    test_trace_buffer_and_time_travel();
    test_operational_runtime_end_to_end();
    test_recovery_state_machine();
    test_hardware_profile_qualification_gate();
    if (failures != 0) {
        std::cerr << failures << " operational hardening test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "operational_hardening_tests=passed\n";
    return EXIT_SUCCESS;
}
