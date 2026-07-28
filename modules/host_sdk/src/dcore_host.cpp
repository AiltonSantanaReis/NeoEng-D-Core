#include "neoeng/dcore_host.h"

#include "neoeng/core/hash.hpp"
#include "neoeng/core/observability.hpp"
#include "neoeng/core/recovery.hpp"
#include "neoeng/core/recovery_contract.hpp"
#include "neoeng/core/rollback.hpp"
#include "neoeng/core/simulation.hpp"
#include "neoeng/core/state_evidence.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

using neoeng::core::Body;
using neoeng::core::CorrelationId;
using neoeng::core::FaultKind;
using neoeng::core::Fixed;
using neoeng::core::HostDirective;
using neoeng::core::InputCommand;
using neoeng::core::RecoveryAckRejectReason;
using neoeng::core::RecoveryAckResult;
using neoeng::core::RecoveryAcknowledgement;
using neoeng::core::RecoveryAction;
using neoeng::core::RecoveryContractEvent;
using neoeng::core::RecoveryController;
using neoeng::core::RecoveryHostBridge;
using neoeng::core::RecoveryMode;
using neoeng::core::RecoveryPolicy;
using neoeng::core::RollbackEngine;
using neoeng::core::Sha256Digest;
using neoeng::core::SnapshotStrategy;
using neoeng::core::TraceBuffer;
using neoeng::core::TraceCategory;
using neoeng::core::TraceCode;
using neoeng::core::TraceEvent;
using neoeng::core::TraceOutcome;
using neoeng::core::TraceSeverity;
using neoeng::core::TraceSubsystem;
using neoeng::core::WorldState;

constexpr std::uint64_t kHandleMagic = UINT64_C(0x4E454F4443485357); // "NEODCHSW"
constexpr std::uint64_t kDefaultSnapshotCapacity = 300U;
constexpr std::uint64_t kDefaultTraceCapacity = 4'096U;
constexpr std::uint64_t kDefaultCheckpointInterval = 1U;
constexpr std::uint64_t kDefaultMaximumInputsPerStep = 64U;

static_assert(sizeof(neoeng_dcore_version_info) == 24U);
static_assert(sizeof(neoeng_dcore_host_config) == 48U);
static_assert(sizeof(neoeng_dcore_body) == 40U);
static_assert(sizeof(neoeng_dcore_input) == 24U);
static_assert(sizeof(neoeng_dcore_state_summary) == 96U);
static_assert(sizeof(neoeng_dcore_recovery_event) == 64U);
static_assert(sizeof(neoeng_dcore_recovery_ack_result) == 80U);
static_assert(sizeof(neoeng_dcore_trace_event) == 104U);

#define NEOENG_ASSERT_C_VALUE(cpp_value, c_value) \
    static_assert(static_cast<std::uint32_t>(cpp_value) == (c_value))

NEOENG_ASSERT_C_VALUE(RecoveryMode::Normal, NEOENG_DCORE_RECOVERY_MODE_NORMAL);
NEOENG_ASSERT_C_VALUE(RecoveryMode::Headless, NEOENG_DCORE_RECOVERY_MODE_HEADLESS);
NEOENG_ASSERT_C_VALUE(RecoveryMode::ReusingLastConfirmedInput, NEOENG_DCORE_RECOVERY_MODE_REUSING_LAST_CONFIRMED_INPUT);
NEOENG_ASSERT_C_VALUE(RecoveryMode::QuarantinedInput, NEOENG_DCORE_RECOVERY_MODE_QUARANTINED_INPUT);
NEOENG_ASSERT_C_VALUE(RecoveryMode::SafeWait, NEOENG_DCORE_RECOVERY_MODE_SAFE_WAIT);
NEOENG_ASSERT_C_VALUE(RecoveryMode::RollingBackToCheckpoint, NEOENG_DCORE_RECOVERY_MODE_ROLLING_BACK_TO_CHECKPOINT);
NEOENG_ASSERT_C_VALUE(RecoveryMode::Halted, NEOENG_DCORE_RECOVERY_MODE_HALTED);
NEOENG_ASSERT_C_VALUE(RecoveryAction::None, NEOENG_DCORE_RECOVERY_ACTION_NONE);
NEOENG_ASSERT_C_VALUE(RecoveryAction::DropInput, NEOENG_DCORE_RECOVERY_ACTION_DROP_INPUT);
NEOENG_ASSERT_C_VALUE(RecoveryAction::ContinueHeadless, NEOENG_DCORE_RECOVERY_ACTION_CONTINUE_HEADLESS);
NEOENG_ASSERT_C_VALUE(RecoveryAction::ReuseLastConfirmedInput, NEOENG_DCORE_RECOVERY_ACTION_REUSE_LAST_CONFIRMED_INPUT);
NEOENG_ASSERT_C_VALUE(RecoveryAction::QuarantineOrigin, NEOENG_DCORE_RECOVERY_ACTION_QUARANTINE_ORIGIN);
NEOENG_ASSERT_C_VALUE(RecoveryAction::EnterSafeWait, NEOENG_DCORE_RECOVERY_ACTION_ENTER_SAFE_WAIT);
NEOENG_ASSERT_C_VALUE(RecoveryAction::RollbackToCheckpoint, NEOENG_DCORE_RECOVERY_ACTION_ROLLBACK_TO_CHECKPOINT);
NEOENG_ASSERT_C_VALUE(RecoveryAction::DisableNonessentialTelemetry, NEOENG_DCORE_RECOVERY_ACTION_DISABLE_NONESSENTIAL_TELEMETRY);
NEOENG_ASSERT_C_VALUE(RecoveryAction::HaltSafely, NEOENG_DCORE_RECOVERY_ACTION_HALT_SAFELY);
NEOENG_ASSERT_C_VALUE(HostDirective::ContinueSimulation, NEOENG_DCORE_HOST_DIRECTIVE_CONTINUE_SIMULATION);
NEOENG_ASSERT_C_VALUE(HostDirective::DropInput, NEOENG_DCORE_HOST_DIRECTIVE_DROP_INPUT);
NEOENG_ASSERT_C_VALUE(HostDirective::QuarantineOrigin, NEOENG_DCORE_HOST_DIRECTIVE_QUARANTINE_ORIGIN);
NEOENG_ASSERT_C_VALUE(HostDirective::ContinueHeadless, NEOENG_DCORE_HOST_DIRECTIVE_CONTINUE_HEADLESS);
NEOENG_ASSERT_C_VALUE(HostDirective::ReuseLastConfirmedInput, NEOENG_DCORE_HOST_DIRECTIVE_REUSE_LAST_CONFIRMED_INPUT);
NEOENG_ASSERT_C_VALUE(HostDirective::PauseSimulation, NEOENG_DCORE_HOST_DIRECTIVE_PAUSE_SIMULATION);
NEOENG_ASSERT_C_VALUE(HostDirective::RestoreCheckpoint, NEOENG_DCORE_HOST_DIRECTIVE_RESTORE_CHECKPOINT);
NEOENG_ASSERT_C_VALUE(HostDirective::DisableTelemetryAndPause, NEOENG_DCORE_HOST_DIRECTIVE_DISABLE_TELEMETRY_AND_PAUSE);
NEOENG_ASSERT_C_VALUE(HostDirective::HaltSimulation, NEOENG_DCORE_HOST_DIRECTIVE_HALT_SIMULATION);
NEOENG_ASSERT_C_VALUE(RecoveryAckRejectReason::None, NEOENG_DCORE_RECOVERY_ACK_REJECT_NONE);
NEOENG_ASSERT_C_VALUE(RecoveryAckRejectReason::NoPendingRecovery, NEOENG_DCORE_RECOVERY_ACK_REJECT_NO_PENDING_RECOVERY);
NEOENG_ASSERT_C_VALUE(RecoveryAckRejectReason::StaleGeneration, NEOENG_DCORE_RECOVERY_ACK_REJECT_STALE_GENERATION);
NEOENG_ASSERT_C_VALUE(RecoveryAckRejectReason::InvalidAcknowledgement, NEOENG_DCORE_RECOVERY_ACK_REJECT_INVALID_ACKNOWLEDGEMENT);
NEOENG_ASSERT_C_VALUE(RecoveryAckRejectReason::CheckpointMismatch, NEOENG_DCORE_RECOVERY_ACK_REJECT_CHECKPOINT_MISMATCH);
NEOENG_ASSERT_C_VALUE(RecoveryAckRejectReason::CheckpointUnavailable, NEOENG_DCORE_RECOVERY_ACK_REJECT_CHECKPOINT_UNAVAILABLE);
NEOENG_ASSERT_C_VALUE(RecoveryAckRejectReason::ResourceExhausted, NEOENG_DCORE_RECOVERY_ACK_REJECT_RESOURCE_EXHAUSTED);
NEOENG_ASSERT_C_VALUE(RecoveryAckRejectReason::RuntimeHalted, NEOENG_DCORE_RECOVERY_ACK_REJECT_RUNTIME_HALTED);
NEOENG_ASSERT_C_VALUE(TraceCategory::Input, NEOENG_DCORE_TRACE_CATEGORY_INPUT);
NEOENG_ASSERT_C_VALUE(TraceCategory::Network, NEOENG_DCORE_TRACE_CATEGORY_NETWORK);
NEOENG_ASSERT_C_VALUE(TraceCategory::Simulation, NEOENG_DCORE_TRACE_CATEGORY_SIMULATION);
NEOENG_ASSERT_C_VALUE(TraceCategory::Rollback, NEOENG_DCORE_TRACE_CATEGORY_ROLLBACK);
NEOENG_ASSERT_C_VALUE(TraceCategory::Budget, NEOENG_DCORE_TRACE_CATEGORY_BUDGET);
NEOENG_ASSERT_C_VALUE(TraceCategory::Recovery, NEOENG_DCORE_TRACE_CATEGORY_RECOVERY);
NEOENG_ASSERT_C_VALUE(TraceCategory::Tooling, NEOENG_DCORE_TRACE_CATEGORY_TOOLING);
NEOENG_ASSERT_C_VALUE(TraceCategory::Evidence, NEOENG_DCORE_TRACE_CATEGORY_EVIDENCE);
NEOENG_ASSERT_C_VALUE(TraceOutcome::Accepted, NEOENG_DCORE_TRACE_OUTCOME_ACCEPTED);
NEOENG_ASSERT_C_VALUE(TraceOutcome::Rejected, NEOENG_DCORE_TRACE_OUTCOME_REJECTED);
NEOENG_ASSERT_C_VALUE(TraceOutcome::Applied, NEOENG_DCORE_TRACE_OUTCOME_APPLIED);
NEOENG_ASSERT_C_VALUE(TraceOutcome::Skipped, NEOENG_DCORE_TRACE_OUTCOME_SKIPPED);
NEOENG_ASSERT_C_VALUE(TraceOutcome::Degraded, NEOENG_DCORE_TRACE_OUTCOME_DEGRADED);
NEOENG_ASSERT_C_VALUE(TraceOutcome::Recovered, NEOENG_DCORE_TRACE_OUTCOME_RECOVERED);
NEOENG_ASSERT_C_VALUE(TraceOutcome::Failed, NEOENG_DCORE_TRACE_OUTCOME_FAILED);
NEOENG_ASSERT_C_VALUE(TraceSeverity::Debug, NEOENG_DCORE_TRACE_SEVERITY_DEBUG);
NEOENG_ASSERT_C_VALUE(TraceSeverity::Info, NEOENG_DCORE_TRACE_SEVERITY_INFO);
NEOENG_ASSERT_C_VALUE(TraceSeverity::Warning, NEOENG_DCORE_TRACE_SEVERITY_WARNING);
NEOENG_ASSERT_C_VALUE(TraceSeverity::Error, NEOENG_DCORE_TRACE_SEVERITY_ERROR);
NEOENG_ASSERT_C_VALUE(TraceSeverity::Critical, NEOENG_DCORE_TRACE_SEVERITY_CRITICAL);
NEOENG_ASSERT_C_VALUE(TraceSubsystem::Unknown, NEOENG_DCORE_TRACE_SUBSYSTEM_UNKNOWN);
NEOENG_ASSERT_C_VALUE(TraceSubsystem::InputParser, NEOENG_DCORE_TRACE_SUBSYSTEM_INPUT_PARSER);
NEOENG_ASSERT_C_VALUE(TraceSubsystem::NetworkGateway, NEOENG_DCORE_TRACE_SUBSYSTEM_NETWORK_GATEWAY);
NEOENG_ASSERT_C_VALUE(TraceSubsystem::Session, NEOENG_DCORE_TRACE_SUBSYSTEM_SESSION);
NEOENG_ASSERT_C_VALUE(TraceSubsystem::Simulation, NEOENG_DCORE_TRACE_SUBSYSTEM_SIMULATION);
NEOENG_ASSERT_C_VALUE(TraceSubsystem::Rollback, NEOENG_DCORE_TRACE_SUBSYSTEM_ROLLBACK);
NEOENG_ASSERT_C_VALUE(TraceSubsystem::Recovery, NEOENG_DCORE_TRACE_SUBSYSTEM_RECOVERY);
NEOENG_ASSERT_C_VALUE(TraceSubsystem::Evidence, NEOENG_DCORE_TRACE_SUBSYSTEM_EVIDENCE);
NEOENG_ASSERT_C_VALUE(TraceSubsystem::ViewLab, NEOENG_DCORE_TRACE_SUBSYSTEM_VIEW_LAB);
NEOENG_ASSERT_C_VALUE(TraceSubsystem::SupportBundle, NEOENG_DCORE_TRACE_SUBSYSTEM_SUPPORT_BUNDLE);
NEOENG_ASSERT_C_VALUE(TraceSubsystem::Qualification, NEOENG_DCORE_TRACE_SUBSYSTEM_QUALIFICATION);
NEOENG_ASSERT_C_VALUE(TraceCode::None, NEOENG_DCORE_TRACE_CODE_NONE);
NEOENG_ASSERT_C_VALUE(TraceCode::InputAuthenticated, NEOENG_DCORE_TRACE_CODE_INPUT_AUTHENTICATED);
NEOENG_ASSERT_C_VALUE(TraceCode::InputMalformed, NEOENG_DCORE_TRACE_CODE_INPUT_MALFORMED);
NEOENG_ASSERT_C_VALUE(TraceCode::InputReplayRejected, NEOENG_DCORE_TRACE_CODE_INPUT_REPLAY_REJECTED);
NEOENG_ASSERT_C_VALUE(TraceCode::InputRateLimited, NEOENG_DCORE_TRACE_CODE_INPUT_RATE_LIMITED);
NEOENG_ASSERT_C_VALUE(TraceCode::StateAdvanced, NEOENG_DCORE_TRACE_CODE_STATE_ADVANCED);
NEOENG_ASSERT_C_VALUE(TraceCode::StateDivergence, NEOENG_DCORE_TRACE_CODE_STATE_DIVERGENCE);
NEOENG_ASSERT_C_VALUE(TraceCode::RollbackStarted, NEOENG_DCORE_TRACE_CODE_ROLLBACK_STARTED);
NEOENG_ASSERT_C_VALUE(TraceCode::RollbackCompleted, NEOENG_DCORE_TRACE_CODE_ROLLBACK_COMPLETED);
NEOENG_ASSERT_C_VALUE(TraceCode::BudgetSampled, NEOENG_DCORE_TRACE_CODE_BUDGET_SAMPLED);
NEOENG_ASSERT_C_VALUE(TraceCode::BudgetExceeded, NEOENG_DCORE_TRACE_CODE_BUDGET_EXCEEDED);
NEOENG_ASSERT_C_VALUE(TraceCode::DeviceLost, NEOENG_DCORE_TRACE_CODE_DEVICE_LOST);
NEOENG_ASSERT_C_VALUE(TraceCode::IoStall, NEOENG_DCORE_TRACE_CODE_IO_STALL);
NEOENG_ASSERT_C_VALUE(TraceCode::OutOfMemory, NEOENG_DCORE_TRACE_CODE_OUT_OF_MEMORY);
NEOENG_ASSERT_C_VALUE(TraceCode::SafeWaitEntered, NEOENG_DCORE_TRACE_CODE_SAFE_WAIT_ENTERED);
NEOENG_ASSERT_C_VALUE(TraceCode::SafeRollbackEntered, NEOENG_DCORE_TRACE_CODE_SAFE_ROLLBACK_ENTERED);
NEOENG_ASSERT_C_VALUE(TraceCode::HeadlessModeEntered, NEOENG_DCORE_TRACE_CODE_HEADLESS_MODE_ENTERED);
NEOENG_ASSERT_C_VALUE(TraceCode::RecoveryAcknowledged, NEOENG_DCORE_TRACE_CODE_RECOVERY_ACKNOWLEDGED);
NEOENG_ASSERT_C_VALUE(TraceCode::RecoveryAcknowledgementRejected, NEOENG_DCORE_TRACE_CODE_RECOVERY_ACKNOWLEDGEMENT_REJECTED);
NEOENG_ASSERT_C_VALUE(TraceCode::SessionEstablished, NEOENG_DCORE_TRACE_CODE_SESSION_ESTABLISHED);
NEOENG_ASSERT_C_VALUE(TraceCode::SessionRejected, NEOENG_DCORE_TRACE_CODE_SESSION_REJECTED);
NEOENG_ASSERT_C_VALUE(TraceCode::EvidenceCreated, NEOENG_DCORE_TRACE_CODE_EVIDENCE_CREATED);
NEOENG_ASSERT_C_VALUE(TraceCode::EvidenceVerificationFailed, NEOENG_DCORE_TRACE_CODE_EVIDENCE_VERIFICATION_FAILED);
NEOENG_ASSERT_C_VALUE(TraceCode::EvidenceChainBroken, NEOENG_DCORE_TRACE_CODE_EVIDENCE_CHAIN_BROKEN);
NEOENG_ASSERT_C_VALUE(TraceCode::EvidenceSignatureRejected, NEOENG_DCORE_TRACE_CODE_EVIDENCE_SIGNATURE_REJECTED);
NEOENG_ASSERT_C_VALUE(TraceCode::MerkleProofRejected, NEOENG_DCORE_TRACE_CODE_MERKLE_PROOF_REJECTED);
NEOENG_ASSERT_C_VALUE(TraceCode::DivergenceLocalized, NEOENG_DCORE_TRACE_CODE_DIVERGENCE_LOCALIZED);
NEOENG_ASSERT_C_VALUE(TraceCode::SupportBundleCreated, NEOENG_DCORE_TRACE_CODE_SUPPORT_BUNDLE_CREATED);
NEOENG_ASSERT_C_VALUE(TraceCode::SupportBundleVerificationFailed, NEOENG_DCORE_TRACE_CODE_SUPPORT_BUNDLE_VERIFICATION_FAILED);
NEOENG_ASSERT_C_VALUE(TraceCode::ValidationGateDeferred, NEOENG_DCORE_TRACE_CODE_VALIDATION_GATE_DEFERRED);
NEOENG_ASSERT_C_VALUE(TraceCode::TemporalRecordCommitted, NEOENG_DCORE_TRACE_CODE_TEMPORAL_RECORD_COMMITTED);
NEOENG_ASSERT_C_VALUE(TraceCode::TemporalRecordRejected, NEOENG_DCORE_TRACE_CODE_TEMPORAL_RECORD_REJECTED);
NEOENG_ASSERT_C_VALUE(TraceCode::ExternalEffectPrepared, NEOENG_DCORE_TRACE_CODE_EXTERNAL_EFFECT_PREPARED);
NEOENG_ASSERT_C_VALUE(TraceCode::ExternalEffectCommitted, NEOENG_DCORE_TRACE_CODE_EXTERNAL_EFFECT_COMMITTED);
NEOENG_ASSERT_C_VALUE(TraceCode::ExternalEffectCompensated, NEOENG_DCORE_TRACE_CODE_EXTERNAL_EFFECT_COMPENSATED);
NEOENG_ASSERT_C_VALUE(TraceCode::ExternalEffectRejected, NEOENG_DCORE_TRACE_CODE_EXTERNAL_EFFECT_REJECTED);

#undef NEOENG_ASSERT_C_VALUE
constexpr Fixed::rep kMaximumAccelerationRaw =
    static_cast<Fixed::rep>(2'147'483'647LL) << Fixed::fractional_bits;

[[nodiscard]] bool fits_size_t(std::uint64_t value) noexcept {
    return value <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
}

[[nodiscard]] bool acceleration_is_valid(std::int64_t value) noexcept {
    if (value == std::numeric_limits<std::int64_t>::min()) {
        return false;
    }
    const std::int64_t magnitude = value < 0 ? -value : value;
    return magnitude <= kMaximumAccelerationRaw;
}

[[nodiscard]] bool mode_allows_advance(RecoveryMode mode) noexcept {
    return mode == RecoveryMode::Normal
        || mode == RecoveryMode::Headless
        || mode == RecoveryMode::ReusingLastConfirmedInput
        || mode == RecoveryMode::QuarantinedInput;
}

[[nodiscard]] bool map_snapshot_strategy(
    neoeng_dcore_snapshot_strategy input,
    SnapshotStrategy& output) noexcept {
    switch (input) {
    case NEOENG_DCORE_SNAPSHOT_FULL_COPY:
        output = SnapshotStrategy::FullCopy;
        return true;
    case NEOENG_DCORE_SNAPSHOT_DELTA_LOG:
        output = SnapshotStrategy::DeltaLog;
        return true;
    case NEOENG_DCORE_SNAPSHOT_PAGED_COPY_ON_WRITE:
        output = SnapshotStrategy::PagedCopyOnWrite;
        return true;
    case NEOENG_DCORE_SNAPSHOT_PERSISTENT_CHUNK_TREE:
        output = SnapshotStrategy::PersistentChunkTree;
        return true;
    case NEOENG_DCORE_SNAPSHOT_COMPONENT_SOA:
        output = SnapshotStrategy::ComponentSoA;
        return true;
    case NEOENG_DCORE_SNAPSHOT_HYBRID_ADAPTIVE:
        output = SnapshotStrategy::HybridAdaptive;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool map_fault(neoeng_dcore_fault_kind input, FaultKind& output) noexcept {
    switch (input) {
    case NEOENG_DCORE_FAULT_NONE: output = FaultKind::None; return true;
    case NEOENG_DCORE_FAULT_DEVICE_LOST: output = FaultKind::DeviceLost; return true;
    case NEOENG_DCORE_FAULT_IO_STALL: output = FaultKind::IoStall; return true;
    case NEOENG_DCORE_FAULT_NETWORK_UNAVAILABLE:
        output = FaultKind::NetworkUnavailable;
        return true;
    case NEOENG_DCORE_FAULT_MALFORMED_PACKET:
        output = FaultKind::MalformedPacket;
        return true;
    case NEOENG_DCORE_FAULT_OUT_OF_MEMORY: output = FaultKind::OutOfMemory; return true;
    default: return false;
    }
}

[[nodiscard]] bool map_acknowledgement(
    neoeng_dcore_recovery_acknowledgement input,
    RecoveryAcknowledgement& output) noexcept {
    switch (input) {
    case NEOENG_DCORE_RECOVERY_ACK_RETRY_LATER:
        output = RecoveryAcknowledgement::RetryLater;
        return true;
    case NEOENG_DCORE_RECOVERY_ACK_DEVICE_RESTORED:
        output = RecoveryAcknowledgement::DeviceRestored;
        return true;
    case NEOENG_DCORE_RECOVERY_ACK_DEPENDENCY_RESTORED:
        output = RecoveryAcknowledgement::DependencyRestored;
        return true;
    case NEOENG_DCORE_RECOVERY_ACK_RESOURCES_RECOVERED:
        output = RecoveryAcknowledgement::ResourcesRecovered;
        return true;
    case NEOENG_DCORE_RECOVERY_ACK_ORIGIN_RESET:
        output = RecoveryAcknowledgement::OriginReset;
        return true;
    case NEOENG_DCORE_RECOVERY_ACK_CHECKPOINT_RESTORED:
        output = RecoveryAcknowledgement::CheckpointRestored;
        return true;
    default:
        return false;
    }
}

[[nodiscard]] std::uint32_t fault_value(FaultKind value) noexcept {
    switch (value) {
    case FaultKind::None: return NEOENG_DCORE_FAULT_NONE;
    case FaultKind::DeviceLost: return NEOENG_DCORE_FAULT_DEVICE_LOST;
    case FaultKind::IoStall: return NEOENG_DCORE_FAULT_IO_STALL;
    case FaultKind::NetworkUnavailable: return NEOENG_DCORE_FAULT_NETWORK_UNAVAILABLE;
    case FaultKind::MalformedPacket: return NEOENG_DCORE_FAULT_MALFORMED_PACKET;
    case FaultKind::OutOfMemory: return NEOENG_DCORE_FAULT_OUT_OF_MEMORY;
    }
    return NEOENG_DCORE_FAULT_NONE;
}

[[nodiscard]] std::uint32_t mode_value(RecoveryMode value) noexcept {
    switch (value) {
    case RecoveryMode::Normal: return 0U;
    case RecoveryMode::Headless: return 1U;
    case RecoveryMode::ReusingLastConfirmedInput: return 2U;
    case RecoveryMode::QuarantinedInput: return 3U;
    case RecoveryMode::SafeWait: return 4U;
    case RecoveryMode::RollingBackToCheckpoint: return 5U;
    case RecoveryMode::Halted: return 6U;
    }
    return 6U;
}

[[nodiscard]] std::uint32_t action_value(RecoveryAction value) noexcept {
    switch (value) {
    case RecoveryAction::None: return 0U;
    case RecoveryAction::DropInput: return 1U;
    case RecoveryAction::ContinueHeadless: return 2U;
    case RecoveryAction::ReuseLastConfirmedInput: return 3U;
    case RecoveryAction::QuarantineOrigin: return 4U;
    case RecoveryAction::EnterSafeWait: return 5U;
    case RecoveryAction::RollbackToCheckpoint: return 6U;
    case RecoveryAction::DisableNonessentialTelemetry: return 7U;
    case RecoveryAction::HaltSafely: return 8U;
    }
    return 8U;
}

void fill_recovery_event(
    const RecoveryContractEvent& input,
    neoeng_dcore_recovery_event& output) noexcept {
    output = {};
    output.struct_size = sizeof(output);
    output.contract_version = input.contract_version;
    output.acknowledgement_required = input.acknowledgement_required ? 1U : 0U;
    output.generation = input.generation;
    output.status_code = static_cast<std::uint32_t>(input.status);
    output.directive = static_cast<std::uint32_t>(input.directive);
    output.fault = fault_value(input.signal.fault);
    output.action = action_value(input.signal.action);
    output.mode = mode_value(input.signal.mode);
    output.consecutive_fault_count = input.signal.consecutive_fault_count;
    output.frame = input.signal.frame;
    output.correlation_id = input.signal.correlation_id;
    output.rollback_checkpoint_frame = input.signal.rollback_checkpoint_frame;
}

void fill_trace_event(const TraceEvent& input, neoeng_dcore_trace_event& output) noexcept {
    output = {};
    output.struct_size = sizeof(output);
    output.correlation_id = input.correlation_id;
    output.sequence = input.sequence;
    output.frame = input.frame;
    output.monotonic_time_ns = input.monotonic_time_ns;
    output.category = static_cast<std::uint32_t>(input.category);
    output.outcome = static_cast<std::uint32_t>(input.outcome);
    output.code = static_cast<std::uint32_t>(input.code);
    output.entity = input.entity;
    output.component = input.component;
    output.subsystem = static_cast<std::uint32_t>(input.subsystem);
    output.severity = static_cast<std::uint32_t>(input.severity);
    output.detail_code = input.detail_code;
    output.measured_value = input.measured_value;
    output.budget_limit = input.budget_limit;
    output.subject_token = input.subject_token;
    output.related_hash = input.related_hash;
}

void copy_digest(const Sha256Digest& digest, std::uint8_t* destination) noexcept {
    std::memcpy(destination, digest.data(), digest.size());
}

[[nodiscard]] neoeng_dcore_body export_body(const Body& body) noexcept {
    return {
        .entity_id = body.id,
        .reserved = 0U,
        .position_x_raw = body.position.x.raw(),
        .position_y_raw = body.position.y.raw(),
        .velocity_x_raw = body.velocity.x.raw(),
        .velocity_y_raw = body.velocity.y.raw(),
    };
}

[[nodiscard]] Body import_body(const neoeng_dcore_body& body) noexcept {
    return {
        .id = body.entity_id,
        .position = {
            Fixed::from_raw(body.position_x_raw),
            Fixed::from_raw(body.position_y_raw),
        },
        .velocity = {
            Fixed::from_raw(body.velocity_x_raw),
            Fixed::from_raw(body.velocity_y_raw),
        },
    };
}

[[nodiscard]] InputCommand import_input(const neoeng_dcore_input& input) noexcept {
    return {
        .entity = input.entity_id,
        .acceleration = {
            Fixed::from_raw(input.acceleration_x_raw),
            Fixed::from_raw(input.acceleration_y_raw),
        },
    };
}

[[nodiscard]] neoeng_dcore_status translate_current_exception() noexcept {
    try {
        throw;
    } catch (const std::bad_alloc&) {
        return NEOENG_DCORE_STATUS_OUT_OF_MEMORY;
    } catch (const std::overflow_error&) {
        return NEOENG_DCORE_STATUS_NUMERIC_OVERFLOW;
    } catch (const std::out_of_range&) {
        return NEOENG_DCORE_STATUS_NOT_FOUND;
    } catch (const std::invalid_argument&) {
        return NEOENG_DCORE_STATUS_INVALID_STATE;
    } catch (...) {
        return NEOENG_DCORE_STATUS_INTERNAL_ERROR;
    }
}

} // namespace

struct neoeng_dcore_host final {
    neoeng_dcore_host(
        WorldState initial,
        std::size_t snapshot_capacity,
        SnapshotStrategy snapshot_strategy,
        std::size_t trace_capacity,
        std::uint64_t safe_checkpoint_interval_frames,
        std::size_t maximum_inputs_per_step)
        : owner_thread(std::this_thread::get_id()),
          engine(std::move(initial), snapshot_capacity, snapshot_strategy),
          recovery(RecoveryPolicy{}),
          traces(trace_capacity),
          safe_checkpoint_interval(safe_checkpoint_interval_frames),
          maximum_inputs(maximum_inputs_per_step) {
        recovery.mark_safe_checkpoint(engine.state().frame);
    }

    std::uint64_t magic{kHandleMagic};
    std::thread::id owner_thread{};
    RollbackEngine engine;
    RecoveryController recovery;
    RecoveryHostBridge recovery_bridge;
    TraceBuffer traces;
    std::uint64_t safe_checkpoint_interval{};
    std::size_t maximum_inputs{};
};

namespace {

[[nodiscard]] neoeng_dcore_status check_host(const neoeng_dcore_host* host) noexcept {
    if (host == nullptr || host->magic != kHandleMagic) {
        return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    if (host->owner_thread != std::this_thread::get_id()) {
        return NEOENG_DCORE_STATUS_WRONG_THREAD;
    }
    return NEOENG_DCORE_STATUS_OK;
}

[[nodiscard]] neoeng_dcore_status fill_state_summary(
    const neoeng_dcore_host& host,
    neoeng_dcore_state_summary* output) {
    if (output == nullptr) {
        return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    const WorldState& state = host.engine.state();
    const Sha256Digest canonical = neoeng::core::canonical_state_sha256(state);
    const Sha256Digest merkle = neoeng::core::state_merkle_sha256(state).root;
    *output = {};
    output->struct_size = sizeof(*output);
    output->frame = state.frame;
    output->body_count = state.bodies.size();
    output->stable_hash = neoeng::core::stable_hash(state);
    copy_digest(canonical, output->canonical_sha256);
    copy_digest(merkle, output->merkle_root_sha256);
    return NEOENG_DCORE_STATUS_OK;
}

[[nodiscard]] neoeng_dcore_status import_inputs(
    const neoeng_dcore_input* inputs,
    std::uint64_t count,
    std::size_t maximum,
    std::vector<InputCommand>& output) {
    if (count > static_cast<std::uint64_t>(maximum) || !fits_size_t(count)) {
        return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    if (count != 0U && inputs == nullptr) {
        return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    output.clear();
    output.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0U; index < count; ++index) {
        const neoeng_dcore_input& input = inputs[index];
        if (input.entity_id == 0U
            || input.reserved != 0U
            || !acceleration_is_valid(input.acceleration_x_raw)
            || !acceleration_is_valid(input.acceleration_y_raw)) {
            return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
        }
        output.push_back(import_input(input));
    }
    return NEOENG_DCORE_STATUS_OK;
}

void record_state_advanced(
    neoeng_dcore_host& host,
    CorrelationId correlation_id,
    std::uint64_t monotonic_time_ns) noexcept {
    const std::uint64_t hash = neoeng::core::stable_hash(host.engine.state());
    host.traces.record({
        .correlation_id = correlation_id,
        .frame = host.engine.state().frame,
        .monotonic_time_ns = monotonic_time_ns,
        .category = TraceCategory::Simulation,
        .outcome = TraceOutcome::Applied,
        .code = TraceCode::StateAdvanced,
        .measured_value = static_cast<std::int64_t>(hash),
        .subsystem = TraceSubsystem::Simulation,
        .severity = TraceSeverity::Info,
        .related_hash = hash,
    });
}

void record_rollback(
    neoeng_dcore_host& host,
    TraceCode code,
    TraceOutcome outcome,
    CorrelationId correlation_id,
    std::uint64_t monotonic_time_ns,
    std::uint64_t source_frame) noexcept {
    host.traces.record({
        .correlation_id = correlation_id,
        .frame = host.engine.state().frame,
        .monotonic_time_ns = monotonic_time_ns,
        .category = TraceCategory::Rollback,
        .outcome = outcome,
        .code = code,
        .measured_value = static_cast<std::int64_t>(source_frame),
        .subsystem = TraceSubsystem::Rollback,
        .severity = outcome == TraceOutcome::Failed
            ? TraceSeverity::Error
            : TraceSeverity::Info,
        .related_hash = neoeng::core::stable_hash(host.engine.state()),
    });
}

} // namespace

extern "C" {

neoeng_dcore_status neoeng_dcore_host_get_version(neoeng_dcore_version_info* out_version) {
    if (out_version == nullptr) {
        return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    *out_version = {};
    out_version->struct_size = sizeof(*out_version);
    out_version->abi_major = NEOENG_DCORE_HOST_ABI_MAJOR;
    out_version->abi_minor = NEOENG_DCORE_HOST_ABI_MINOR;
    out_version->runtime_major = NEOENG_DCORE_RUNTIME_VERSION_MAJOR;
    out_version->runtime_minor = NEOENG_DCORE_RUNTIME_VERSION_MINOR;
    out_version->runtime_patch = NEOENG_DCORE_RUNTIME_VERSION_PATCH;
    out_version->capabilities = NEOENG_DCORE_CAPABILITY_STRICT_TRANSITION
        | NEOENG_DCORE_CAPABILITY_ROLLBACK
        | NEOENG_DCORE_CAPABILITY_STATE_EVIDENCE
        | NEOENG_DCORE_CAPABILITY_RECOVERY_CONTRACT
        | NEOENG_DCORE_CAPABILITY_TRACE_EXPORT;
    return NEOENG_DCORE_STATUS_OK;
}

neoeng_dcore_status neoeng_dcore_host_default_config(neoeng_dcore_host_config* out_config) {
    if (out_config == nullptr) {
        return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    *out_config = {};
    out_config->struct_size = sizeof(*out_config);
    out_config->abi_major = NEOENG_DCORE_HOST_ABI_MAJOR;
    out_config->abi_minor = NEOENG_DCORE_HOST_ABI_MINOR;
    out_config->snapshot_capacity = kDefaultSnapshotCapacity;
    out_config->snapshot_strategy = NEOENG_DCORE_SNAPSHOT_FULL_COPY;
    out_config->trace_capacity = kDefaultTraceCapacity;
    out_config->safe_checkpoint_interval_frames = kDefaultCheckpointInterval;
    out_config->maximum_inputs_per_step = kDefaultMaximumInputsPerStep;
    return NEOENG_DCORE_STATUS_OK;
}

const char* neoeng_dcore_status_string(neoeng_dcore_status status) {
    switch (status) {
    case NEOENG_DCORE_STATUS_OK: return "ok";
    case NEOENG_DCORE_STATUS_INVALID_ARGUMENT: return "invalid_argument";
    case NEOENG_DCORE_STATUS_ABI_MISMATCH: return "abi_mismatch";
    case NEOENG_DCORE_STATUS_WRONG_THREAD: return "wrong_thread";
    case NEOENG_DCORE_STATUS_INVALID_STATE: return "invalid_state";
    case NEOENG_DCORE_STATUS_NOT_FOUND: return "not_found";
    case NEOENG_DCORE_STATUS_BUFFER_TOO_SMALL: return "buffer_too_small";
    case NEOENG_DCORE_STATUS_RECOVERY_REQUIRED: return "recovery_required";
    case NEOENG_DCORE_STATUS_OUT_OF_MEMORY: return "out_of_memory";
    case NEOENG_DCORE_STATUS_NUMERIC_OVERFLOW: return "numeric_overflow";
    case NEOENG_DCORE_STATUS_INTERNAL_ERROR: return "internal_error";
    default: return "unknown";
    }
}

neoeng_dcore_status neoeng_dcore_host_create(
    std::uint64_t initial_frame,
    const neoeng_dcore_body* initial_bodies,
    std::uint64_t body_count,
    const neoeng_dcore_host_config* config,
    neoeng_dcore_host** out_host) {
    if (out_host == nullptr || (body_count != 0U && initial_bodies == nullptr)
        || !fits_size_t(body_count)) {
        return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    *out_host = nullptr;

    neoeng_dcore_host_config effective{};
    if (config == nullptr) {
        (void)neoeng_dcore_host_default_config(&effective);
    } else {
        if (config->struct_size != sizeof(neoeng_dcore_host_config)
            || config->abi_major != NEOENG_DCORE_HOST_ABI_MAJOR
            || config->abi_minor > NEOENG_DCORE_HOST_ABI_MINOR) {
            return NEOENG_DCORE_STATUS_ABI_MISMATCH;
        }
        effective = *config;
    }
    if (effective.reserved0 != 0U
        || effective.snapshot_capacity == 0U
        || effective.trace_capacity == 0U
        || effective.safe_checkpoint_interval_frames == 0U
        || effective.maximum_inputs_per_step == 0U
        || !fits_size_t(effective.snapshot_capacity)
        || !fits_size_t(effective.trace_capacity)
        || !fits_size_t(effective.maximum_inputs_per_step)) {
        return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    SnapshotStrategy strategy{};
    if (!map_snapshot_strategy(effective.snapshot_strategy, strategy)) {
        return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }

    try {
        WorldState initial{.frame = initial_frame};
        initial.bodies.reserve(static_cast<std::size_t>(body_count));
        for (std::uint64_t index = 0U; index < body_count; ++index) {
            if (initial_bodies[index].reserved != 0U) {
                return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
            }
            initial.bodies.push_back(import_body(initial_bodies[index]));
        }
        neoeng::core::validate_world(initial);
        *out_host = new neoeng_dcore_host(
            std::move(initial),
            static_cast<std::size_t>(effective.snapshot_capacity),
            strategy,
            static_cast<std::size_t>(effective.trace_capacity),
            effective.safe_checkpoint_interval_frames,
            static_cast<std::size_t>(effective.maximum_inputs_per_step));
        return NEOENG_DCORE_STATUS_OK;
    } catch (...) {
        return translate_current_exception();
    }
}

neoeng_dcore_status neoeng_dcore_host_destroy(neoeng_dcore_host* host) {
    const neoeng_dcore_status checked = check_host(host);
    if (checked != NEOENG_DCORE_STATUS_OK) {
        return checked;
    }
    host->magic = 0U;
    delete host;
    return NEOENG_DCORE_STATUS_OK;
}

neoeng_dcore_status neoeng_dcore_host_advance(
    neoeng_dcore_host* host,
    const neoeng_dcore_input* inputs,
    std::uint64_t input_count,
    std::uint64_t correlation_id,
    std::uint64_t monotonic_time_ns,
    neoeng_dcore_state_summary* out_state) {
    const neoeng_dcore_status checked = check_host(host);
    if (checked != NEOENG_DCORE_STATUS_OK || out_state == nullptr) {
        return checked != NEOENG_DCORE_STATUS_OK
            ? checked : NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    if (!mode_allows_advance(host->recovery.mode())) {
        return NEOENG_DCORE_STATUS_RECOVERY_REQUIRED;
    }
    try {
        std::vector<InputCommand> converted;
        const neoeng_dcore_status input_status = import_inputs(
            inputs, input_count, host->maximum_inputs, converted);
        if (input_status != NEOENG_DCORE_STATUS_OK) {
            return input_status;
        }
        host->engine.advance(converted);
        if (host->engine.state().frame % host->safe_checkpoint_interval == 0U) {
            host->recovery.mark_safe_checkpoint(host->engine.state().frame);
        }
        record_state_advanced(*host, correlation_id, monotonic_time_ns);
        return fill_state_summary(*host, out_state);
    } catch (...) {
        return translate_current_exception();
    }
}

neoeng_dcore_status neoeng_dcore_host_correct_input_and_resimulate(
    neoeng_dcore_host* host,
    std::uint64_t input_frame,
    const neoeng_dcore_input* corrected_inputs,
    std::uint64_t input_count,
    std::uint64_t correlation_id,
    std::uint64_t monotonic_time_ns,
    std::uint64_t* out_resimulated_frames,
    neoeng_dcore_state_summary* out_state) {
    const neoeng_dcore_status checked = check_host(host);
    if (checked != NEOENG_DCORE_STATUS_OK || out_resimulated_frames == nullptr
        || out_state == nullptr) {
        return checked != NEOENG_DCORE_STATUS_OK
            ? checked : NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    if (!mode_allows_advance(host->recovery.mode())) {
        return NEOENG_DCORE_STATUS_RECOVERY_REQUIRED;
    }
    try {
        std::vector<InputCommand> converted;
        const neoeng_dcore_status input_status = import_inputs(
            corrected_inputs, input_count, host->maximum_inputs, converted);
        if (input_status != NEOENG_DCORE_STATUS_OK) {
            return input_status;
        }
        record_rollback(
            *host, TraceCode::RollbackStarted, TraceOutcome::Applied,
            correlation_id, monotonic_time_ns, input_frame);
        const std::size_t resimulated = host->engine.correct_input_and_resimulate(
            input_frame, converted);
        *out_resimulated_frames = resimulated;
        record_rollback(
            *host, TraceCode::RollbackCompleted, TraceOutcome::Recovered,
            correlation_id, monotonic_time_ns, input_frame);
        return fill_state_summary(*host, out_state);
    } catch (...) {
        record_rollback(
            *host, TraceCode::RollbackCompleted, TraceOutcome::Failed,
            correlation_id, monotonic_time_ns, input_frame);
        return translate_current_exception();
    }
}

neoeng_dcore_status neoeng_dcore_host_restore_checkpoint(
    neoeng_dcore_host* host,
    std::uint64_t frame,
    std::uint64_t correlation_id,
    std::uint64_t monotonic_time_ns,
    neoeng_dcore_state_summary* out_state) {
    const neoeng_dcore_status checked = check_host(host);
    if (checked != NEOENG_DCORE_STATUS_OK || out_state == nullptr) {
        return checked != NEOENG_DCORE_STATUS_OK
            ? checked : NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    try {
        record_rollback(
            *host, TraceCode::RollbackStarted, TraceOutcome::Applied,
            correlation_id, monotonic_time_ns, frame);
        host->engine.restore_checkpoint(frame);
        host->recovery.mark_safe_checkpoint(frame);
        record_rollback(
            *host, TraceCode::RollbackCompleted, TraceOutcome::Recovered,
            correlation_id, monotonic_time_ns, frame);
        return fill_state_summary(*host, out_state);
    } catch (...) {
        record_rollback(
            *host, TraceCode::RollbackCompleted, TraceOutcome::Failed,
            correlation_id, monotonic_time_ns, frame);
        return translate_current_exception();
    }
}

neoeng_dcore_status neoeng_dcore_host_get_state_summary(
    const neoeng_dcore_host* host,
    neoeng_dcore_state_summary* out_state) {
    const neoeng_dcore_status checked = check_host(host);
    if (checked != NEOENG_DCORE_STATUS_OK) {
        return checked;
    }
    try {
        return fill_state_summary(*host, out_state);
    } catch (...) {
        return translate_current_exception();
    }
}

neoeng_dcore_status neoeng_dcore_host_copy_bodies(
    const neoeng_dcore_host* host,
    neoeng_dcore_body* out_bodies,
    std::uint64_t capacity,
    std::uint64_t* out_required_count) {
    const neoeng_dcore_status checked = check_host(host);
    if (checked != NEOENG_DCORE_STATUS_OK || out_required_count == nullptr) {
        return checked != NEOENG_DCORE_STATUS_OK
            ? checked : NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    const auto& bodies = host->engine.state().bodies;
    *out_required_count = bodies.size();
    if (capacity < bodies.size() || (out_bodies == nullptr && !bodies.empty())) {
        return NEOENG_DCORE_STATUS_BUFFER_TOO_SMALL;
    }
    for (std::size_t index = 0U; index < bodies.size(); ++index) {
        out_bodies[index] = export_body(bodies[index]);
    }
    return NEOENG_DCORE_STATUS_OK;
}

neoeng_dcore_status neoeng_dcore_host_copy_traces(
    const neoeng_dcore_host* host,
    neoeng_dcore_trace_event* out_events,
    std::uint64_t capacity,
    std::uint64_t* out_required_count) {
    const neoeng_dcore_status checked = check_host(host);
    if (checked != NEOENG_DCORE_STATUS_OK || out_required_count == nullptr) {
        return checked != NEOENG_DCORE_STATUS_OK
            ? checked : NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    try {
        const std::vector<TraceEvent> events = host->traces.snapshot();
        *out_required_count = events.size();
        if (capacity < events.size() || (out_events == nullptr && !events.empty())) {
            return NEOENG_DCORE_STATUS_BUFFER_TOO_SMALL;
        }
        for (std::size_t index = 0U; index < events.size(); ++index) {
            fill_trace_event(events[index], out_events[index]);
        }
        return NEOENG_DCORE_STATUS_OK;
    } catch (...) {
        return translate_current_exception();
    }
}

neoeng_dcore_status neoeng_dcore_host_report_fault(
    neoeng_dcore_host* host,
    neoeng_dcore_fault_kind fault,
    std::uint64_t correlation_id,
    std::uint64_t monotonic_time_ns,
    neoeng_dcore_recovery_event* out_event) {
    const neoeng_dcore_status checked = check_host(host);
    if (checked != NEOENG_DCORE_STATUS_OK || out_event == nullptr) {
        return checked != NEOENG_DCORE_STATUS_OK
            ? checked : NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    FaultKind mapped{};
    if (!map_fault(fault, mapped)) {
        return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    const auto signal = host->recovery.report_fault(
        mapped, host->engine.state().frame, correlation_id);
    const RecoveryContractEvent event = host->recovery_bridge.publish(signal);
    host->traces.record(neoeng::core::recovery_trace_event(signal, monotonic_time_ns));
    fill_recovery_event(event, *out_event);
    return NEOENG_DCORE_STATUS_OK;
}

neoeng_dcore_status neoeng_dcore_host_get_pending_recovery(
    const neoeng_dcore_host* host,
    neoeng_dcore_recovery_event* out_event) {
    const neoeng_dcore_status checked = check_host(host);
    if (checked != NEOENG_DCORE_STATUS_OK || out_event == nullptr) {
        return checked != NEOENG_DCORE_STATUS_OK
            ? checked : NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    const auto& pending = host->recovery_bridge.pending();
    if (!pending.has_value()) {
        return NEOENG_DCORE_STATUS_NOT_FOUND;
    }
    fill_recovery_event(*pending, *out_event);
    return NEOENG_DCORE_STATUS_OK;
}

neoeng_dcore_status neoeng_dcore_host_acknowledge_recovery(
    neoeng_dcore_host* host,
    std::uint64_t generation,
    neoeng_dcore_recovery_acknowledgement acknowledgement,
    std::uint64_t correlation_id,
    std::uint64_t restored_checkpoint_frame,
    std::uint64_t monotonic_time_ns,
    neoeng_dcore_recovery_ack_result* out_result) {
    const neoeng_dcore_status checked = check_host(host);
    if (checked != NEOENG_DCORE_STATUS_OK || out_result == nullptr) {
        return checked != NEOENG_DCORE_STATUS_OK
            ? checked : NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    RecoveryAcknowledgement mapped{};
    if (!map_acknowledgement(acknowledgement, mapped)) {
        return NEOENG_DCORE_STATUS_INVALID_ARGUMENT;
    }
    *out_result = {};
    out_result->struct_size = sizeof(*out_result);

    const auto& pending = host->recovery_bridge.pending();
    if (pending.has_value()
        && pending->generation == generation
        && mapped == RecoveryAcknowledgement::CheckpointRestored
        && restored_checkpoint_frame == pending->signal.rollback_checkpoint_frame) {
        if (!host->engine.snapshots().contains(restored_checkpoint_frame)) {
            out_result->accepted = 0U;
            out_result->reject_reason = static_cast<std::uint32_t>(
                RecoveryAckRejectReason::CheckpointUnavailable);
            fill_recovery_event(*pending, out_result->event);
            out_result->resulting_mode = mode_value(host->recovery.mode());
            return NEOENG_DCORE_STATUS_OK;
        }
        try {
            host->engine.restore_checkpoint(restored_checkpoint_frame);
        } catch (const std::bad_alloc&) {
            out_result->accepted = 0U;
            out_result->reject_reason = static_cast<std::uint32_t>(
                RecoveryAckRejectReason::ResourceExhausted);
            fill_recovery_event(*pending, out_result->event);
            out_result->resulting_mode = mode_value(host->recovery.mode());
            return NEOENG_DCORE_STATUS_OK;
        } catch (...) {
            out_result->accepted = 0U;
            out_result->reject_reason = static_cast<std::uint32_t>(
                RecoveryAckRejectReason::CheckpointUnavailable);
            fill_recovery_event(*pending, out_result->event);
            out_result->resulting_mode = mode_value(host->recovery.mode());
            return NEOENG_DCORE_STATUS_OK;
        }
    }

    const RecoveryAckResult result = host->recovery_bridge.acknowledge(
        host->recovery,
        generation,
        mapped,
        host->engine.state().frame,
        correlation_id,
        restored_checkpoint_frame);
    host->traces.record({
        .correlation_id = correlation_id,
        .frame = host->engine.state().frame,
        .monotonic_time_ns = monotonic_time_ns,
        .category = TraceCategory::Recovery,
        .outcome = !result.accepted
            ? TraceOutcome::Rejected
            : (result.resulting_signal.mode == RecoveryMode::Normal
                ? TraceOutcome::Recovered
                : TraceOutcome::Applied),
        .code = result.accepted
            ? TraceCode::RecoveryAcknowledged
            : TraceCode::RecoveryAcknowledgementRejected,
        .measured_value = static_cast<std::int64_t>(result.reason),
        .budget_limit = static_cast<std::int64_t>(generation),
        .subsystem = TraceSubsystem::Recovery,
        .severity = result.accepted ? TraceSeverity::Info : TraceSeverity::Warning,
    });
    out_result->accepted = result.accepted ? 1U : 0U;
    out_result->reject_reason = static_cast<std::uint32_t>(result.reason);
    out_result->resulting_mode = mode_value(result.resulting_signal.mode);
    fill_recovery_event(result.event, out_result->event);
    return NEOENG_DCORE_STATUS_OK;
}

} // extern "C"
