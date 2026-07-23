#include "neoeng/core/recovery_contract.hpp"

#include <limits>
#include <sstream>

namespace neoeng::core {
namespace {

[[nodiscard]] bool acknowledgement_matches(
    const RecoveryContractEvent& event,
    RecoveryAcknowledgement acknowledgement) noexcept {
    switch (event.signal.mode) {
    case RecoveryMode::Headless:
        return acknowledgement == RecoveryAcknowledgement::DeviceRestored;
    case RecoveryMode::ReusingLastConfirmedInput:
        return acknowledgement == RecoveryAcknowledgement::DependencyRestored;
    case RecoveryMode::QuarantinedInput:
        return acknowledgement == RecoveryAcknowledgement::OriginReset;
    case RecoveryMode::SafeWait:
        switch (event.signal.fault) {
        case FaultKind::DeviceLost:
            return acknowledgement == RecoveryAcknowledgement::DeviceRestored;
        case FaultKind::IoStall:
        case FaultKind::NetworkUnavailable:
            return acknowledgement == RecoveryAcknowledgement::DependencyRestored;
        case FaultKind::OutOfMemory:
            return acknowledgement == RecoveryAcknowledgement::ResourcesRecovered;
        case FaultKind::None:
        case FaultKind::MalformedPacket:
            return false;
        }
        return false;
    case RecoveryMode::RollingBackToCheckpoint:
        return acknowledgement == RecoveryAcknowledgement::CheckpointRestored;
    case RecoveryMode::Normal:
    case RecoveryMode::Halted:
        return false;
    }
    return false;
}

} // namespace

RecoveryStatusCode recovery_status_code(const RecoverySignal& signal) noexcept {
    switch (signal.action) {
    case RecoveryAction::None: return RecoveryStatusCode::Healthy;
    case RecoveryAction::DropInput: return RecoveryStatusCode::InputDropped;
    case RecoveryAction::QuarantineOrigin: return RecoveryStatusCode::InputQuarantined;
    case RecoveryAction::ContinueHeadless: return RecoveryStatusCode::DeviceLost;
    case RecoveryAction::ReuseLastConfirmedInput:
        return signal.fault == FaultKind::NetworkUnavailable
            ? RecoveryStatusCode::NetworkUnavailable
            : RecoveryStatusCode::IoStall;
    case RecoveryAction::EnterSafeWait:
        if (signal.fault == FaultKind::DeviceLost) {
            return RecoveryStatusCode::DeviceLost;
        }
        if (signal.fault == FaultKind::NetworkUnavailable) {
            return RecoveryStatusCode::NetworkUnavailable;
        }
        if (signal.fault == FaultKind::OutOfMemory) {
            return RecoveryStatusCode::ResourcePressure;
        }
        return RecoveryStatusCode::IoStall;
    case RecoveryAction::RollbackToCheckpoint:
        return RecoveryStatusCode::CheckpointRestoreRequired;
    case RecoveryAction::DisableNonessentialTelemetry:
        return RecoveryStatusCode::ResourcePressure;
    case RecoveryAction::HaltSafely:
        return RecoveryStatusCode::SafeHalt;
    }
    return RecoveryStatusCode::SafeHalt;
}

HostDirective recovery_host_directive(const RecoverySignal& signal) noexcept {
    switch (signal.action) {
    case RecoveryAction::None: return HostDirective::ContinueSimulation;
    case RecoveryAction::DropInput: return HostDirective::DropInput;
    case RecoveryAction::QuarantineOrigin: return HostDirective::QuarantineOrigin;
    case RecoveryAction::ContinueHeadless: return HostDirective::ContinueHeadless;
    case RecoveryAction::ReuseLastConfirmedInput: return HostDirective::ReuseLastConfirmedInput;
    case RecoveryAction::EnterSafeWait: return HostDirective::PauseSimulation;
    case RecoveryAction::RollbackToCheckpoint: return HostDirective::RestoreCheckpoint;
    case RecoveryAction::DisableNonessentialTelemetry:
        return HostDirective::DisableTelemetryAndPause;
    case RecoveryAction::HaltSafely: return HostDirective::HaltSimulation;
    }
    return HostDirective::HaltSimulation;
}

bool recovery_requires_acknowledgement(const RecoverySignal& signal) noexcept {
    switch (signal.action) {
    case RecoveryAction::ContinueHeadless:
    case RecoveryAction::ReuseLastConfirmedInput:
    case RecoveryAction::QuarantineOrigin:
    case RecoveryAction::EnterSafeWait:
    case RecoveryAction::RollbackToCheckpoint:
    case RecoveryAction::DisableNonessentialTelemetry:
        return true;
    case RecoveryAction::None:
    case RecoveryAction::DropInput:
    case RecoveryAction::HaltSafely:
        return false;
    }
    return false;
}

RecoveryContractEvent RecoveryHostBridge::publish(const RecoverySignal& signal) noexcept {
    if (generation_ != std::numeric_limits<std::uint64_t>::max()) {
        ++generation_;
    }
    RecoveryContractEvent event{
        .generation = generation_,
        .status = recovery_status_code(signal),
        .directive = recovery_host_directive(signal),
        .signal = signal,
        .acknowledgement_required = recovery_requires_acknowledgement(signal),
    };
    if (event.acknowledgement_required || signal.mode == RecoveryMode::Halted) {
        // A terminal halt supersedes any older recoverable event. Keeping it as the
        // latest blocking event allows the host to receive a stable RuntimeHalted
        // result instead of accidentally acknowledging a stale recovery generation.
        pending_ = event;
    } else if (signal.mode == RecoveryMode::Normal && signal.action == RecoveryAction::None) {
        pending_.reset();
    }
    return event;
}

RecoveryAckResult RecoveryHostBridge::acknowledge(
    RecoveryController& controller,
    std::uint64_t generation,
    RecoveryAcknowledgement acknowledgement,
    std::uint64_t frame,
    CorrelationId correlation_id,
    std::uint64_t restored_checkpoint_frame) noexcept {
    if (!pending_.has_value()) {
        return {.reason = RecoveryAckRejectReason::NoPendingRecovery};
    }
    const RecoveryContractEvent pending = *pending_;
    if (pending.signal.mode == RecoveryMode::Halted || controller.mode() == RecoveryMode::Halted) {
        return {.reason = RecoveryAckRejectReason::RuntimeHalted, .event = pending};
    }
    if (generation != pending.generation) {
        return {.reason = RecoveryAckRejectReason::StaleGeneration, .event = pending};
    }
    if (acknowledgement == RecoveryAcknowledgement::RetryLater) {
        return {
            .accepted = true,
            .reason = RecoveryAckRejectReason::None,
            .event = pending,
            .resulting_signal = pending.signal,
        };
    }
    if (!acknowledgement_matches(pending, acknowledgement)) {
        return {.reason = RecoveryAckRejectReason::InvalidAcknowledgement, .event = pending};
    }
    if (acknowledgement == RecoveryAcknowledgement::CheckpointRestored
        && restored_checkpoint_frame != pending.signal.rollback_checkpoint_frame) {
        return {.reason = RecoveryAckRejectReason::CheckpointMismatch, .event = pending};
    }
    if (acknowledgement == RecoveryAcknowledgement::OriginReset) {
        controller.reset_input_quarantine();
    }
    const RecoverySignal recovered = controller.acknowledge_recovery(frame, correlation_id);
    pending_.reset();
    RecoveryContractEvent recovered_event{
        .generation = generation,
        .status = RecoveryStatusCode::Healthy,
        .directive = HostDirective::ContinueSimulation,
        .signal = recovered,
        .acknowledgement_required = false,
    };
    return {
        .accepted = true,
        .reason = RecoveryAckRejectReason::None,
        .event = recovered_event,
        .resulting_signal = recovered,
    };
}

std::string recovery_contract_json(const RecoveryContractEvent& event) {
    std::ostringstream stream;
    stream << "{\"schema\":\"neoeng.dcore.recovery.v1\""
           << ",\"contract_version\":" << event.contract_version
           << ",\"generation\":" << event.generation
           << ",\"status_code\":" << static_cast<std::uint32_t>(event.status)
           << ",\"status\":\"" << to_string(event.status) << "\""
           << ",\"directive\":\"" << to_string(event.directive) << "\""
           << ",\"fault\":\"" << to_string(event.signal.fault) << "\""
           << ",\"mode\":\"" << to_string(event.signal.mode) << "\""
           << ",\"action\":\"" << to_string(event.signal.action) << "\""
           << ",\"frame\":" << event.signal.frame
           << ",\"correlation_id\":" << event.signal.correlation_id
           << ",\"checkpoint_frame\":" << event.signal.rollback_checkpoint_frame
           << ",\"acknowledgement_required\":"
           << (event.acknowledgement_required ? "true" : "false") << "}";
    return stream.str();
}

const char* to_string(RecoveryStatusCode status) noexcept {
    switch (status) {
    case RecoveryStatusCode::Healthy: return "healthy";
    case RecoveryStatusCode::InputDropped: return "input_dropped";
    case RecoveryStatusCode::InputQuarantined: return "input_quarantined";
    case RecoveryStatusCode::DeviceLost: return "device_lost";
    case RecoveryStatusCode::IoStall: return "io_stall";
    case RecoveryStatusCode::NetworkUnavailable: return "network_unavailable";
    case RecoveryStatusCode::ResourcePressure: return "resource_pressure";
    case RecoveryStatusCode::CheckpointRestoreRequired: return "checkpoint_restore_required";
    case RecoveryStatusCode::SafeHalt: return "safe_halt";
    }
    return "unknown";
}

const char* to_string(HostDirective directive) noexcept {
    switch (directive) {
    case HostDirective::ContinueSimulation: return "continue_simulation";
    case HostDirective::DropInput: return "drop_input";
    case HostDirective::QuarantineOrigin: return "quarantine_origin";
    case HostDirective::ContinueHeadless: return "continue_headless";
    case HostDirective::ReuseLastConfirmedInput: return "reuse_last_confirmed_input";
    case HostDirective::PauseSimulation: return "pause_simulation";
    case HostDirective::RestoreCheckpoint: return "restore_checkpoint";
    case HostDirective::DisableTelemetryAndPause: return "disable_telemetry_and_pause";
    case HostDirective::HaltSimulation: return "halt_simulation";
    }
    return "unknown";
}

const char* to_string(RecoveryAcknowledgement acknowledgement) noexcept {
    switch (acknowledgement) {
    case RecoveryAcknowledgement::RetryLater: return "retry_later";
    case RecoveryAcknowledgement::DeviceRestored: return "device_restored";
    case RecoveryAcknowledgement::DependencyRestored: return "dependency_restored";
    case RecoveryAcknowledgement::ResourcesRecovered: return "resources_recovered";
    case RecoveryAcknowledgement::OriginReset: return "origin_reset";
    case RecoveryAcknowledgement::CheckpointRestored: return "checkpoint_restored";
    }
    return "unknown";
}

const char* to_string(RecoveryAckRejectReason reason) noexcept {
    switch (reason) {
    case RecoveryAckRejectReason::None: return "none";
    case RecoveryAckRejectReason::NoPendingRecovery: return "no_pending_recovery";
    case RecoveryAckRejectReason::StaleGeneration: return "stale_generation";
    case RecoveryAckRejectReason::InvalidAcknowledgement: return "invalid_acknowledgement";
    case RecoveryAckRejectReason::CheckpointMismatch: return "checkpoint_mismatch";
    case RecoveryAckRejectReason::CheckpointUnavailable: return "checkpoint_unavailable";
    case RecoveryAckRejectReason::ResourceExhausted: return "resource_exhausted";
    case RecoveryAckRejectReason::RuntimeHalted: return "runtime_halted";
    }
    return "unknown";
}

} // namespace neoeng::core
