#pragma once

#include "neoeng/core/observability.hpp"

#include <cstdint>

namespace neoeng::core {

enum class FaultKind : std::uint8_t {
    None,
    DeviceLost,
    IoStall,
    NetworkUnavailable,
    MalformedPacket,
    OutOfMemory,
};

enum class RecoveryMode : std::uint8_t {
    Normal,
    Headless,
    ReusingLastConfirmedInput,
    QuarantinedInput,
    SafeWait,
    RollingBackToCheckpoint,
    Halted,
};

enum class RecoveryAction : std::uint8_t {
    None,
    DropInput,
    ContinueHeadless,
    ReuseLastConfirmedInput,
    QuarantineOrigin,
    EnterSafeWait,
    RollbackToCheckpoint,
    DisableNonessentialTelemetry,
    HaltSafely,
};

struct RecoveryPolicy final {
    bool continue_headless_after_device_loss{true};
    std::uint32_t maximum_consecutive_io_stalls{3U};
    std::uint32_t maximum_consecutive_network_gaps{8U};
    std::uint32_t malformed_packets_before_quarantine{8U};
};

struct RecoverySignal final {
    FaultKind fault{FaultKind::None};
    RecoveryAction action{RecoveryAction::None};
    RecoveryMode mode{RecoveryMode::Normal};
    std::uint64_t frame{};
    CorrelationId correlation_id{};
    std::uint32_t consecutive_fault_count{};
    std::uint64_t rollback_checkpoint_frame{};

    [[nodiscard]] bool simulation_may_advance() const noexcept {
        return mode == RecoveryMode::Normal
            || mode == RecoveryMode::Headless
            || mode == RecoveryMode::ReusingLastConfirmedInput
            || mode == RecoveryMode::QuarantinedInput;
    }
};

class RecoveryController final {
public:
    explicit RecoveryController(RecoveryPolicy policy = {}) noexcept;

    void mark_safe_checkpoint(std::uint64_t frame) noexcept;
    [[nodiscard]] RecoverySignal report_fault(
        FaultKind fault,
        std::uint64_t frame,
        CorrelationId correlation_id = 0U) noexcept;
    [[nodiscard]] RecoverySignal acknowledge_recovery(
        std::uint64_t frame,
        CorrelationId correlation_id = 0U) noexcept;
    void reset_input_quarantine() noexcept;

    [[nodiscard]] RecoveryMode mode() const noexcept { return mode_; }
    [[nodiscard]] std::uint64_t safe_checkpoint_frame() const noexcept {
        return safe_checkpoint_frame_;
    }
    [[nodiscard]] bool has_safe_checkpoint() const noexcept { return has_safe_checkpoint_; }

private:
    void reset_transient_counters_except(FaultKind fault) noexcept;

    RecoveryPolicy policy_{};
    RecoveryMode mode_{RecoveryMode::Normal};
    std::uint64_t safe_checkpoint_frame_{};
    bool has_safe_checkpoint_{};
    std::uint32_t consecutive_io_stalls_{};
    std::uint32_t consecutive_network_gaps_{};
    std::uint32_t malformed_packet_count_{};
    bool telemetry_disabled_{};
};

[[nodiscard]] TraceEvent recovery_trace_event(
    const RecoverySignal& signal,
    std::uint64_t monotonic_time_ns = 0U) noexcept;

[[nodiscard]] const char* to_string(FaultKind fault) noexcept;
[[nodiscard]] const char* to_string(RecoveryMode mode) noexcept;
[[nodiscard]] const char* to_string(RecoveryAction action) noexcept;

} // namespace neoeng::core
