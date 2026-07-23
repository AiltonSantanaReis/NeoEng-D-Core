#include "neoeng/core/operational_runtime.hpp"

#include "neoeng/core/hash.hpp"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace neoeng::core {
namespace {

[[nodiscard]] TraceCode trace_code_for_packet_rejection(PacketRejectReason reason) noexcept {
    switch (reason) {
    case PacketRejectReason::ReplayDuplicate:
    case PacketRejectReason::ReplayTooOld:
        return TraceCode::InputReplayRejected;
    case PacketRejectReason::RateLimited:
        return TraceCode::InputRateLimited;
    default:
        return TraceCode::InputMalformed;
    }
}

[[nodiscard]] std::uint64_t milliseconds_to_nanoseconds(std::uint64_t milliseconds) noexcept {
    constexpr std::uint64_t multiplier = 1'000'000U;
    return milliseconds > std::numeric_limits<std::uint64_t>::max() / multiplier
        ? std::numeric_limits<std::uint64_t>::max()
        : milliseconds * multiplier;
}

[[nodiscard]] bool is_structural_or_authentication_rejection(PacketRejectReason reason) noexcept {
    switch (reason) {
    case PacketRejectReason::None:
    case PacketRejectReason::RateLimited:
    case PacketRejectReason::ReplayDuplicate:
    case PacketRejectReason::ReplayTooOld:
        return false;
    default:
        return true;
    }
}

} // namespace

OperationalRuntime::OperationalRuntime(
    WorldState initial,
    AuthenticationKey authentication_key,
    OperationalRuntimeConfig config)
    : engine_(std::move(initial), config.snapshot_capacity, config.snapshot_strategy),
      gateway_(authentication_key, config.network),
      recovery_(config.recovery),
      traces_(config.trace_capacity),
      time_travel_(config.time_travel_frame_capacity),
      input_buffer_(config.network.maximum_input_commands),
      safe_checkpoint_interval_frames_(config.safe_checkpoint_interval_frames) {
    if (safe_checkpoint_interval_frames_ == 0U) {
        throw std::invalid_argument("Safe checkpoint interval must be greater than zero");
    }
    recovery_.mark_safe_checkpoint(engine_.state().frame);
    time_travel_.record_frame(engine_.state(), {});
}

void OperationalRuntime::record_network_rejection(
    CorrelationId correlation_id,
    PacketRejectReason reason,
    std::uint64_t monotonic_time_ns) noexcept {
    traces_.record({
        .correlation_id = correlation_id,
        .frame = engine_.state().frame,
        .monotonic_time_ns = monotonic_time_ns,
        .category = TraceCategory::Network,
        .outcome = TraceOutcome::Rejected,
        .code = trace_code_for_packet_rejection(reason),
        .measured_value = static_cast<std::int64_t>(reason),
    });
}

void OperationalRuntime::record_payload_rejection(
    CorrelationId correlation_id,
    InputPayloadRejectReason reason,
    std::uint64_t monotonic_time_ns) noexcept {
    traces_.record({
        .correlation_id = correlation_id,
        .frame = engine_.state().frame,
        .monotonic_time_ns = monotonic_time_ns,
        .category = TraceCategory::Input,
        .outcome = TraceOutcome::Rejected,
        .code = TraceCode::InputMalformed,
        .measured_value = static_cast<std::int64_t>(reason),
    });
}

RecoveryContractEvent OperationalRuntime::publish_recovery(
    const RecoverySignal& signal,
    std::uint64_t monotonic_time_ns) noexcept {
    const RecoveryContractEvent event = recovery_host_bridge_.publish(signal);
    traces_.record(recovery_trace_event(signal, monotonic_time_ns));
    return event;
}

OperationalStepResult OperationalRuntime::ingest_authenticated_input(
    OriginId origin,
    std::uint64_t now_ms,
    CorrelationId correlation_id,
    std::span<const std::uint8_t> datagram) {
    OperationalStepResult result{
        .resulting_frame = engine_.state().frame,
        .state_hash = stable_hash(engine_.state()),
    };
    const PacketDecision packet = gateway_.process(origin, now_ms, datagram);
    result.packet_reason = packet.reason;
    if (!packet.accepted()) {
        record_network_rejection(correlation_id, packet.reason, milliseconds_to_nanoseconds(now_ms));
        if (is_structural_or_authentication_rejection(packet.reason)) {
            result.recovery = recovery_.report_fault(
                packet.reason == PacketRejectReason::ResourceExhausted
                    ? FaultKind::OutOfMemory
                    : FaultKind::MalformedPacket,
                engine_.state().frame,
                correlation_id);
            result.recovery_event = publish_recovery(
                result.recovery, milliseconds_to_nanoseconds(now_ms));
        }
        return result;
    }

    const InputPayloadParseResult parsed = parse_input_payload(
        packet.packet.payload, input_buffer_, gateway_.limits());
    result.payload_reason = parsed.reason;
    if (!parsed.accepted()) {
        record_payload_rejection(correlation_id, parsed.reason, milliseconds_to_nanoseconds(now_ms));
        result.recovery = recovery_.report_fault(
            FaultKind::MalformedPacket, engine_.state().frame, correlation_id);
        result.recovery_event = publish_recovery(
            result.recovery, milliseconds_to_nanoseconds(now_ms));
        return result;
    }

    try {
        traces_.record({
            .correlation_id = correlation_id,
            .frame = engine_.state().frame,
            .monotonic_time_ns = milliseconds_to_nanoseconds(now_ms),
            .category = TraceCategory::Input,
            .outcome = TraceOutcome::Accepted,
            .code = TraceCode::InputAuthenticated,
            .measured_value = static_cast<std::int64_t>(parsed.command_count),
        });
        engine_.advance(std::span<const InputCommand>(input_buffer_.data(), parsed.command_count));
        if (engine_.state().frame % safe_checkpoint_interval_frames_ == 0U) {
            recovery_.mark_safe_checkpoint(engine_.state().frame);
        }
        traces_.record({
            .correlation_id = correlation_id,
            .frame = engine_.state().frame,
            .monotonic_time_ns = milliseconds_to_nanoseconds(now_ms),
            .category = TraceCategory::Simulation,
            .outcome = TraceOutcome::Applied,
            .code = TraceCode::StateAdvanced,
            .measured_value = static_cast<std::int64_t>(stable_hash(engine_.state())),
        });
        result.advanced = true;
        result.resulting_frame = engine_.state().frame;
        result.state_hash = stable_hash(engine_.state());
        try {
            const std::vector<TraceEvent> correlated = traces_.by_correlation(correlation_id);
            time_travel_.record_frame(
                engine_.state(),
                std::span<const InputCommand>(input_buffer_.data(), parsed.command_count),
                correlated);
        } catch (const std::bad_alloc&) {
            result.recovery = recovery_.report_fault(
                FaultKind::OutOfMemory, engine_.state().frame, correlation_id);
            result.recovery_event = publish_recovery(
                result.recovery, milliseconds_to_nanoseconds(now_ms));
        }
        return result;
    } catch (const std::bad_alloc&) {
        result.recovery = recovery_.report_fault(
            FaultKind::OutOfMemory, engine_.state().frame, correlation_id);
        result.recovery_event = publish_recovery(
            result.recovery, milliseconds_to_nanoseconds(now_ms));
        result.resulting_frame = engine_.state().frame;
        result.state_hash = stable_hash(engine_.state());
        return result;
    }
}

RecoveryContractEvent OperationalRuntime::report_external_fault_event(
    FaultKind fault,
    CorrelationId correlation_id,
    std::uint64_t monotonic_time_ns) noexcept {
    const RecoverySignal signal = recovery_.report_fault(
        fault, engine_.state().frame, correlation_id);
    return publish_recovery(signal, monotonic_time_ns);
}

RecoverySignal OperationalRuntime::report_external_fault(
    FaultKind fault,
    CorrelationId correlation_id,
    std::uint64_t monotonic_time_ns) noexcept {
    return report_external_fault_event(fault, correlation_id, monotonic_time_ns).signal;
}

bool OperationalRuntime::install_authenticated_session(
    OriginId origin,
    const SecureSessionBinding& binding,
    std::uint64_t now_ms) noexcept {
    const bool installed = gateway_.install_session(origin, binding, now_ms);
    traces_.record({
        .correlation_id = binding.session_id,
        .frame = engine_.state().frame,
        .monotonic_time_ns = milliseconds_to_nanoseconds(now_ms),
        .category = TraceCategory::Network,
        .outcome = installed ? TraceOutcome::Accepted : TraceOutcome::Rejected,
        .code = installed ? TraceCode::SessionEstablished : TraceCode::SessionRejected,
        .measured_value = static_cast<std::int64_t>(binding.key_epoch),
        .budget_limit = static_cast<std::int64_t>(binding.expires_at_ms),
    });
    return installed;
}

bool OperationalRuntime::revoke_authenticated_session(
    OriginId origin,
    std::uint64_t session_id) noexcept {
    return gateway_.revoke_session(origin, session_id);
}

std::size_t OperationalRuntime::revoke_authenticated_sessions_for_key(
    std::uint32_t key_id,
    std::uint32_t key_epoch) noexcept {
    return gateway_.revoke_sessions_for_key(key_id, key_epoch);
}

RecoveryAckResult OperationalRuntime::acknowledge_recovery(
    std::uint64_t generation,
    RecoveryAcknowledgement acknowledgement,
    CorrelationId correlation_id,
    std::uint64_t restored_checkpoint_frame,
    std::uint64_t monotonic_time_ns) noexcept {
    const auto& pending = recovery_host_bridge_.pending();
    if (pending.has_value()
        && pending->generation == generation
        && acknowledgement == RecoveryAcknowledgement::CheckpointRestored
        && restored_checkpoint_frame == pending->signal.rollback_checkpoint_frame) {
        if (!engine_.snapshots().contains(restored_checkpoint_frame)) {
            return {
                .accepted = false,
                .reason = RecoveryAckRejectReason::CheckpointUnavailable,
                .event = *pending,
            };
        }
        try {
            engine_.restore_checkpoint(restored_checkpoint_frame);
            time_travel_.truncate_after(restored_checkpoint_frame);
        } catch (const std::bad_alloc&) {
            return {
                .accepted = false,
                .reason = RecoveryAckRejectReason::ResourceExhausted,
                .event = *pending,
            };
        } catch (const std::out_of_range&) {
            return {
                .accepted = false,
                .reason = RecoveryAckRejectReason::CheckpointUnavailable,
                .event = *pending,
            };
        } catch (...) {
            return {
                .accepted = false,
                .reason = RecoveryAckRejectReason::CheckpointUnavailable,
                .event = *pending,
            };
        }
    }

    RecoveryAckResult result = recovery_host_bridge_.acknowledge(
        recovery_, generation, acknowledgement, engine_.state().frame, correlation_id,
        restored_checkpoint_frame);
    const TraceOutcome outcome = !result.accepted
        ? TraceOutcome::Rejected
        : (result.resulting_signal.mode == RecoveryMode::Normal
            ? TraceOutcome::Recovered
            : TraceOutcome::Applied);
    traces_.record({
        .correlation_id = correlation_id,
        .frame = engine_.state().frame,
        .monotonic_time_ns = monotonic_time_ns,
        .category = TraceCategory::Recovery,
        .outcome = outcome,
        .code = result.accepted
            ? TraceCode::RecoveryAcknowledged
            : TraceCode::RecoveryAcknowledgementRejected,
        .measured_value = static_cast<std::int64_t>(result.reason),
        .budget_limit = static_cast<std::int64_t>(generation),
    });
    return result;
}

} // namespace neoeng::core
