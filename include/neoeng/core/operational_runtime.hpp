#pragma once

#include "neoeng/core/network_security.hpp"
#include "neoeng/core/observability.hpp"
#include "neoeng/core/recovery.hpp"
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
};

struct OperationalStepResult final {
    bool advanced{};
    PacketRejectReason packet_reason{PacketRejectReason::None};
    InputPayloadRejectReason payload_reason{InputPayloadRejectReason::None};
    RecoverySignal recovery{};
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

    [[nodiscard]] RecoverySignal report_external_fault(
        FaultKind fault,
        CorrelationId correlation_id,
        std::uint64_t monotonic_time_ns = 0U) noexcept;

    [[nodiscard]] const WorldState& state() const noexcept { return engine_.state(); }
    [[nodiscard]] const TraceBuffer& traces() const noexcept { return traces_; }
    [[nodiscard]] const TimeTravelDebugger& time_travel() const noexcept { return time_travel_; }
    [[nodiscard]] const RecoveryController& recovery() const noexcept { return recovery_; }

private:
    void record_network_rejection(
        CorrelationId correlation_id,
        PacketRejectReason reason,
        std::uint64_t monotonic_time_ns) noexcept;
    void record_payload_rejection(
        CorrelationId correlation_id,
        InputPayloadRejectReason reason,
        std::uint64_t monotonic_time_ns) noexcept;

    RollbackEngine engine_;
    NetworkSecurityGateway gateway_;
    RecoveryController recovery_;
    TraceBuffer traces_;
    TimeTravelDebugger time_travel_;
    std::vector<InputCommand> input_buffer_{};
};

} // namespace neoeng::core
