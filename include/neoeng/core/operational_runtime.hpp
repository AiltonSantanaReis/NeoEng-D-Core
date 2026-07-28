#pragma once

#include "neoeng/core/diagnostics.hpp"
#include "neoeng/core/network_security.hpp"
#include "neoeng/core/observability.hpp"
#include "neoeng/core/production_security.hpp"
#include "neoeng/core/recovery.hpp"
#include "neoeng/core/recovery_contract.hpp"
#include "neoeng/core/rollback.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct OperationalRuntimeConfig final {
    NetworkSecurityLimits network{};
    RecoveryPolicy recovery{};
    std::size_t snapshot_capacity{300U};
    SnapshotStrategy snapshot_strategy{SnapshotStrategy::FullCopy};
    std::size_t trace_capacity{4'096U};
    std::size_t time_travel_frame_capacity{300U};
    std::uint64_t safe_checkpoint_interval_frames{1U};
    bool enable_wall_clock_budget_tracing{true};
    std::uint64_t input_ingest_budget_ns{2'000'000U};
    std::uint64_t state_advance_budget_ns{2'000'000U};
    std::uint64_t rollback_budget_ns{2'000'000U};
};

struct OperationalStepResult final {
    bool advanced{};
    PacketRejectReason packet_reason{PacketRejectReason::None};
    InputPayloadRejectReason payload_reason{InputPayloadRejectReason::None};
    AuthorizationReason authorization_reason{AuthorizationReason::None};
    RecoverySignal recovery{};
    RecoveryContractEvent recovery_event{};
    std::uint64_t resulting_frame{};
    std::uint64_t state_hash{};
};

class OperationalRuntime final {
public:
    OperationalRuntime(
        WorldState initial,
        AuthenticationKey authentication_key,
        OperationalRuntimeConfig config = {});

    [[nodiscard]] OperationalStepResult ingest_authenticated_input(
        OriginId origin,
        std::uint64_t now_ms,
        CorrelationId correlation_id,
        std::span<const std::uint8_t> datagram);
    [[nodiscard]] OperationalStepResult ingest_authorized_input(
        OriginId origin,
        std::uint64_t now_ms,
        CorrelationId correlation_id,
        std::span<const std::uint8_t> datagram,
        const TransportSecurityContext& transport,
        const CommandAuthorizationPolicy& authorization);

    [[nodiscard]] RecoverySignal report_external_fault(
        FaultKind fault,
        CorrelationId correlation_id,
        std::uint64_t monotonic_time_ns = 0U) noexcept;
    [[nodiscard]] RecoveryContractEvent report_external_fault_event(
        FaultKind fault,
        CorrelationId correlation_id,
        std::uint64_t monotonic_time_ns = 0U) noexcept;

    [[nodiscard]] bool install_authenticated_session(
        OriginId origin,
        const SecureSessionBinding& binding,
        std::uint64_t now_ms) noexcept;
    [[nodiscard]] bool revoke_authenticated_session(
        OriginId origin,
        std::uint64_t session_id) noexcept;
    [[nodiscard]] std::size_t revoke_authenticated_sessions_for_key(
        std::uint32_t key_id,
        std::uint32_t key_epoch) noexcept;

    [[nodiscard]] BudgetEvaluation record_budget_sample(
        const BudgetSample& sample) noexcept;
    [[nodiscard]] StateDivergenceReport verify_state_against(
        const WorldState& expected,
        CorrelationId correlation_id,
        std::uint64_t monotonic_time_ns = 0U);

    [[nodiscard]] RecoveryAckResult acknowledge_recovery(
        std::uint64_t generation,
        RecoveryAcknowledgement acknowledgement,
        CorrelationId correlation_id,
        std::uint64_t restored_checkpoint_frame = 0U,
        std::uint64_t monotonic_time_ns = 0U) noexcept;

    [[nodiscard]] const WorldState& state() const noexcept { return engine_.state(); }
    [[nodiscard]] const TraceBuffer& traces() const noexcept { return traces_; }
    [[nodiscard]] const TimeTravelDebugger& time_travel() const noexcept { return time_travel_; }
    [[nodiscard]] const RecoveryController& recovery() const noexcept { return recovery_; }
    [[nodiscard]] const RecoveryHostBridge& recovery_host_bridge() const noexcept {
        return recovery_host_bridge_;
    }

private:
    [[nodiscard]] OperationalStepResult ingest_input(
        OriginId origin,
        std::uint64_t now_ms,
        CorrelationId correlation_id,
        std::span<const std::uint8_t> datagram,
        const TransportSecurityContext* transport,
        const CommandAuthorizationPolicy* authorization);
    void record_network_rejection(
        CorrelationId correlation_id,
        PacketRejectReason reason,
        std::uint64_t monotonic_time_ns) noexcept;
    void record_payload_rejection(
        CorrelationId correlation_id,
        InputPayloadRejectReason reason,
        std::uint64_t monotonic_time_ns) noexcept;
    [[nodiscard]] RecoveryContractEvent publish_recovery(
        const RecoverySignal& signal,
        std::uint64_t monotonic_time_ns) noexcept;

    RollbackEngine engine_;
    NetworkSecurityGateway gateway_;
    RecoveryController recovery_;
    RecoveryHostBridge recovery_host_bridge_;
    TraceBuffer traces_;
    TimeTravelDebugger time_travel_;
    std::vector<InputCommand> input_buffer_{};
    std::uint64_t safe_checkpoint_interval_frames_{1U};
    bool wall_clock_budget_tracing_{true};
    BudgetDefinition input_ingest_budget_{};
    BudgetDefinition state_advance_budget_{};
    BudgetDefinition rollback_budget_{};
    BudgetMonitor budget_monitor_{};
};

} // namespace neoeng::core
