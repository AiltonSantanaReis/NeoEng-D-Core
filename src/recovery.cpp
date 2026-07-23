#include "neoeng/core/recovery.hpp"

#include <limits>

namespace neoeng::core {
namespace {

void increment_saturating(std::uint32_t& value) noexcept {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

} // namespace

RecoveryController::RecoveryController(RecoveryPolicy policy) noexcept : policy_(policy) {}

void RecoveryController::mark_safe_checkpoint(std::uint64_t frame) noexcept {
    safe_checkpoint_frame_ = frame;
    has_safe_checkpoint_ = true;
}

void RecoveryController::reset_transient_counters_except(FaultKind fault) noexcept {
    if (fault != FaultKind::IoStall) {
        consecutive_io_stalls_ = 0U;
    }
    if (fault != FaultKind::NetworkUnavailable) {
        consecutive_network_gaps_ = 0U;
    }
    if (fault != FaultKind::MalformedPacket) {
        malformed_packet_count_ = 0U;
    }
}

RecoverySignal RecoveryController::report_fault(
    FaultKind fault,
    std::uint64_t frame,
    CorrelationId correlation_id) noexcept {
    reset_transient_counters_except(fault);
    RecoverySignal signal{
        .fault = fault,
        .mode = mode_,
        .frame = frame,
        .correlation_id = correlation_id,
        .rollback_checkpoint_frame = safe_checkpoint_frame_,
    };

    switch (fault) {
    case FaultKind::None:
        signal.action = RecoveryAction::None;
        break;
    case FaultKind::DeviceLost:
        if (policy_.continue_headless_after_device_loss) {
            mode_ = RecoveryMode::Headless;
            signal.action = RecoveryAction::ContinueHeadless;
        } else {
            mode_ = RecoveryMode::SafeWait;
            signal.action = RecoveryAction::EnterSafeWait;
        }
        break;
    case FaultKind::IoStall:
        increment_saturating(consecutive_io_stalls_);
        signal.consecutive_fault_count = consecutive_io_stalls_;
        if (consecutive_io_stalls_ <= policy_.maximum_consecutive_io_stalls) {
            mode_ = RecoveryMode::ReusingLastConfirmedInput;
            signal.action = RecoveryAction::ReuseLastConfirmedInput;
        } else {
            mode_ = RecoveryMode::SafeWait;
            signal.action = RecoveryAction::EnterSafeWait;
        }
        break;
    case FaultKind::NetworkUnavailable:
        increment_saturating(consecutive_network_gaps_);
        signal.consecutive_fault_count = consecutive_network_gaps_;
        if (consecutive_network_gaps_ <= policy_.maximum_consecutive_network_gaps) {
            mode_ = RecoveryMode::ReusingLastConfirmedInput;
            signal.action = RecoveryAction::ReuseLastConfirmedInput;
        } else {
            mode_ = RecoveryMode::SafeWait;
            signal.action = RecoveryAction::EnterSafeWait;
        }
        break;
    case FaultKind::MalformedPacket:
        increment_saturating(malformed_packet_count_);
        signal.consecutive_fault_count = malformed_packet_count_;
        if (malformed_packet_count_ < policy_.malformed_packets_before_quarantine) {
            signal.action = RecoveryAction::DropInput;
            if (mode_ == RecoveryMode::Normal) {
                mode_ = RecoveryMode::Normal;
            }
        } else {
            mode_ = RecoveryMode::QuarantinedInput;
            signal.action = RecoveryAction::QuarantineOrigin;
        }
        break;
    case FaultKind::OutOfMemory:
        if (!telemetry_disabled_) {
            telemetry_disabled_ = true;
            signal.action = RecoveryAction::DisableNonessentialTelemetry;
            mode_ = RecoveryMode::SafeWait;
        } else if (has_safe_checkpoint_) {
            mode_ = RecoveryMode::RollingBackToCheckpoint;
            signal.action = RecoveryAction::RollbackToCheckpoint;
        } else {
            mode_ = RecoveryMode::Halted;
            signal.action = RecoveryAction::HaltSafely;
        }
        break;
    }
    signal.mode = mode_;
    signal.rollback_checkpoint_frame = safe_checkpoint_frame_;
    return signal;
}

RecoverySignal RecoveryController::acknowledge_recovery(
    std::uint64_t frame,
    CorrelationId correlation_id) noexcept {
    if (mode_ != RecoveryMode::Halted) {
        mode_ = RecoveryMode::Normal;
    }
    consecutive_io_stalls_ = 0U;
    consecutive_network_gaps_ = 0U;
    return {
        .fault = FaultKind::None,
        .action = RecoveryAction::None,
        .mode = mode_,
        .frame = frame,
        .correlation_id = correlation_id,
        .rollback_checkpoint_frame = safe_checkpoint_frame_,
    };
}

void RecoveryController::reset_input_quarantine() noexcept {
    malformed_packet_count_ = 0U;
    if (mode_ == RecoveryMode::QuarantinedInput) {
        mode_ = RecoveryMode::Normal;
    }
}

TraceEvent recovery_trace_event(
    const RecoverySignal& signal,
    std::uint64_t monotonic_time_ns) noexcept {
    TraceCode code = TraceCode::None;
    switch (signal.fault) {
    case FaultKind::None: break;
    case FaultKind::DeviceLost: code = TraceCode::DeviceLost; break;
    case FaultKind::IoStall:
    case FaultKind::NetworkUnavailable: code = TraceCode::IoStall; break;
    case FaultKind::MalformedPacket: code = TraceCode::InputMalformed; break;
    case FaultKind::OutOfMemory: code = TraceCode::OutOfMemory; break;
    }
    if (signal.action == RecoveryAction::EnterSafeWait) {
        code = TraceCode::SafeWaitEntered;
    } else if (signal.action == RecoveryAction::RollbackToCheckpoint) {
        code = TraceCode::SafeRollbackEntered;
    } else if (signal.action == RecoveryAction::ContinueHeadless) {
        code = TraceCode::HeadlessModeEntered;
    }
    return {
        .correlation_id = signal.correlation_id,
        .frame = signal.frame,
        .monotonic_time_ns = monotonic_time_ns,
        .category = TraceCategory::Recovery,
        .outcome = signal.mode == RecoveryMode::Normal
            ? TraceOutcome::Recovered
            : (signal.mode == RecoveryMode::Halted ? TraceOutcome::Failed : TraceOutcome::Degraded),
        .code = code,
        .measured_value = signal.consecutive_fault_count,
        .budget_limit = static_cast<std::int64_t>(signal.rollback_checkpoint_frame),
    };
}

const char* to_string(FaultKind fault) noexcept {
    switch (fault) {
    case FaultKind::None: return "none";
    case FaultKind::DeviceLost: return "device_lost";
    case FaultKind::IoStall: return "io_stall";
    case FaultKind::NetworkUnavailable: return "network_unavailable";
    case FaultKind::MalformedPacket: return "malformed_packet";
    case FaultKind::OutOfMemory: return "out_of_memory";
    }
    return "unknown";
}

const char* to_string(RecoveryMode mode) noexcept {
    switch (mode) {
    case RecoveryMode::Normal: return "normal";
    case RecoveryMode::Headless: return "headless";
    case RecoveryMode::ReusingLastConfirmedInput: return "reusing_last_confirmed_input";
    case RecoveryMode::QuarantinedInput: return "quarantined_input";
    case RecoveryMode::SafeWait: return "safe_wait";
    case RecoveryMode::RollingBackToCheckpoint: return "rolling_back_to_checkpoint";
    case RecoveryMode::Halted: return "halted";
    }
    return "unknown";
}

const char* to_string(RecoveryAction action) noexcept {
    switch (action) {
    case RecoveryAction::None: return "none";
    case RecoveryAction::DropInput: return "drop_input";
    case RecoveryAction::ContinueHeadless: return "continue_headless";
    case RecoveryAction::ReuseLastConfirmedInput: return "reuse_last_confirmed_input";
    case RecoveryAction::QuarantineOrigin: return "quarantine_origin";
    case RecoveryAction::EnterSafeWait: return "enter_safe_wait";
    case RecoveryAction::RollbackToCheckpoint: return "rollback_to_checkpoint";
    case RecoveryAction::DisableNonessentialTelemetry: return "disable_nonessential_telemetry";
    case RecoveryAction::HaltSafely: return "halt_safely";
    }
    return "unknown";
}

} // namespace neoeng::core
