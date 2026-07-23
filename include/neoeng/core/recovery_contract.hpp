#pragma once

#include "neoeng/core/recovery.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace neoeng::core {

inline constexpr std::uint16_t kRecoveryContractVersion = 1U;

enum class RecoveryStatusCode : std::uint32_t {
    Healthy = 0x00000000U,
    InputDropped = 0xDC010001U,
    InputQuarantined = 0xDC010002U,
    DeviceLost = 0xDC020001U,
    IoStall = 0xDC020002U,
    NetworkUnavailable = 0xDC020003U,
    ResourcePressure = 0xDC030001U,
    CheckpointRestoreRequired = 0xDC030002U,
    SafeHalt = 0xDC03FFFFU,
};

enum class HostDirective : std::uint8_t {
    ContinueSimulation,
    DropInput,
    QuarantineOrigin,
    ContinueHeadless,
    ReuseLastConfirmedInput,
    PauseSimulation,
    RestoreCheckpoint,
    DisableTelemetryAndPause,
    HaltSimulation,
};

enum class RecoveryAcknowledgement : std::uint8_t {
    RetryLater,
    DeviceRestored,
    DependencyRestored,
    ResourcesRecovered,
    OriginReset,
    CheckpointRestored,
};

enum class RecoveryAckRejectReason : std::uint8_t {
    None,
    NoPendingRecovery,
    StaleGeneration,
    InvalidAcknowledgement,
    CheckpointMismatch,
    CheckpointUnavailable,
    ResourceExhausted,
    RuntimeHalted,
};

struct RecoveryContractEvent final {
    std::uint16_t contract_version{kRecoveryContractVersion};
    std::uint64_t generation{};
    RecoveryStatusCode status{RecoveryStatusCode::Healthy};
    HostDirective directive{HostDirective::ContinueSimulation};
    RecoverySignal signal{};
    bool acknowledgement_required{};

    [[nodiscard]] bool valid() const noexcept { return generation != 0U; }
};

struct RecoveryAckResult final {
    bool accepted{};
    RecoveryAckRejectReason reason{RecoveryAckRejectReason::None};
    RecoveryContractEvent event{};
    RecoverySignal resulting_signal{};
};

class RecoveryHostBridge final {
public:
    [[nodiscard]] RecoveryContractEvent publish(const RecoverySignal& signal) noexcept;
    [[nodiscard]] RecoveryAckResult acknowledge(
        RecoveryController& controller,
        std::uint64_t generation,
        RecoveryAcknowledgement acknowledgement,
        std::uint64_t frame,
        CorrelationId correlation_id = 0U,
        std::uint64_t restored_checkpoint_frame = 0U) noexcept;

    [[nodiscard]] const std::optional<RecoveryContractEvent>& pending() const noexcept {
        return pending_;
    }
    [[nodiscard]] std::uint64_t latest_generation() const noexcept { return generation_; }

private:
    std::uint64_t generation_{};
    std::optional<RecoveryContractEvent> pending_{};
};

[[nodiscard]] RecoveryStatusCode recovery_status_code(const RecoverySignal& signal) noexcept;
[[nodiscard]] HostDirective recovery_host_directive(const RecoverySignal& signal) noexcept;
[[nodiscard]] bool recovery_requires_acknowledgement(const RecoverySignal& signal) noexcept;
[[nodiscard]] std::string recovery_contract_json(const RecoveryContractEvent& event);

[[nodiscard]] const char* to_string(RecoveryStatusCode status) noexcept;
[[nodiscard]] const char* to_string(HostDirective directive) noexcept;
[[nodiscard]] const char* to_string(RecoveryAcknowledgement acknowledgement) noexcept;
[[nodiscard]] const char* to_string(RecoveryAckRejectReason reason) noexcept;

} // namespace neoeng::core
