#pragma once

#include "neoeng/core/crypto_hash.hpp"
#include "neoeng/core/diagnostics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neoeng::core {

inline constexpr std::string_view kTemporalClosureSchema{
    "neoeng.dcore.temporal-closure.v1"};
inline constexpr std::uint16_t kDurableTimelineFormatVersion = 1U;
inline constexpr std::size_t kDefaultMaximumTemporalRecordBytes =
    16U * 1024U * 1024U;
inline constexpr std::size_t kMaximumExternalEffectKeyBytes = 128U;
inline constexpr std::size_t kMaximumExternalEffectKindBytes = 128U;

struct CanonicalFieldDescriptor final {
    std::string_view path{};
    bool covered_by_diff{};
    bool covered_by_canonical_sha256{};
    bool covered_by_merkle{};
};

[[nodiscard]] std::span<const CanonicalFieldDescriptor>
canonical_world_v1_fields() noexcept;

enum class MandatoryOperationalPath : std::uint8_t {
    InputIngest,
    StateAdvance,
    Rollback,
    EcsMaintenance,
    EvidenceCheckpoint,
    SupportBundleExport,
    DivergenceLocalization,
    DurableRecorder,
    ExternalEffectCommit,
};

struct MandatoryPathDescriptor final {
    MandatoryOperationalPath path{MandatoryOperationalPath::InputIngest};
    BudgetId budget{BudgetId::InputIngest};
    TraceSubsystem subsystem{TraceSubsystem::Unknown};
    bool automatic_when_tracing_enabled{true};
};

[[nodiscard]] std::span<const MandatoryPathDescriptor>
mandatory_operational_paths_v1() noexcept;

struct DurableTimelineInput final {
    EvidenceBranchId branch_id{};
    std::uint64_t first_frame{};
    std::uint64_t last_frame{};
    std::string timeline_json{};
    std::string evidence_json{};
};

struct DurableTimelineRecord final {
    std::uint64_t sequence{};
    EvidenceBranchId branch_id{};
    std::uint64_t first_frame{};
    std::uint64_t last_frame{};
    Sha256Digest previous_record_hash{};
    Sha256Digest timeline_sha256{};
    Sha256Digest evidence_sha256{};
    Sha256Digest record_sha256{};
    std::string timeline_json{};
    std::string evidence_json{};
};

enum class DurableRecorderReason : std::uint8_t {
    None,
    InvalidInput,
    NotRecovered,
    IoFailure,
    RecordTooLarge,
    CorruptRecord,
    SequenceMismatch,
    ChainMismatch,
};

struct DurableRecorderResult final {
    DurableRecorderReason reason{DurableRecorderReason::None};
    std::uint64_t sequence{};
    Sha256Digest record_sha256{};

    [[nodiscard]] bool accepted() const noexcept {
        return reason == DurableRecorderReason::None;
    }
};

class DurableTimelineRecorder final {
public:
    explicit DurableTimelineRecorder(
        std::filesystem::path directory,
        std::size_t maximum_record_bytes =
            kDefaultMaximumTemporalRecordBytes);

    [[nodiscard]] DurableRecorderResult recover();
    [[nodiscard]] DurableRecorderResult append(
        const DurableTimelineInput& input,
        CorrelationId correlation_id = 0U,
        TraceBuffer* traces = nullptr);

    [[nodiscard]] const std::vector<DurableTimelineRecord>& records() const noexcept {
        return records_;
    }
    [[nodiscard]] const std::filesystem::path& directory() const noexcept {
        return directory_;
    }
    [[nodiscard]] bool recovered() const noexcept { return recovered_; }

private:
    std::filesystem::path directory_{};
    std::size_t maximum_record_bytes_{};
    bool recovered_{};
    std::vector<DurableTimelineRecord> records_{};
};

[[nodiscard]] DurableRecorderResult verify_durable_timeline_directory(
    const std::filesystem::path& directory,
    std::size_t maximum_record_bytes =
        kDefaultMaximumTemporalRecordBytes);

enum class ExternalEffectState : std::uint8_t {
    Prepared,
    Committed,
    Compensated,
};

struct ExternalEffectIntent final {
    std::string idempotency_key{};
    std::string kind{};
    std::uint64_t frame{};
    Sha256Digest payload_sha256{};
    bool compensation_supported{};
};

struct ExternalEffectRecord final {
    ExternalEffectIntent intent{};
    ExternalEffectState state{ExternalEffectState::Prepared};
};

enum class ExternalEffectApplyResult : std::uint8_t {
    Applied,
    AlreadyApplied,
    RetryableFailure,
    PermanentFailure,
};

class ExternalEffectExecutor {
public:
    virtual ~ExternalEffectExecutor() = default;
    [[nodiscard]] virtual ExternalEffectApplyResult commit(
        const ExternalEffectIntent& intent) = 0;
    [[nodiscard]] virtual ExternalEffectApplyResult compensate(
        const ExternalEffectIntent& intent) = 0;
};

enum class ExternalEffectReason : std::uint8_t {
    None,
    InvalidIntent,
    IdempotencyConflict,
    UnknownEffect,
    NotConfirmed,
    InvalidState,
    ExecutionFailed,
    CompensationUnsupported,
    CommittedEffectCrossedRollback,
};

struct ExternalEffectDecision final {
    ExternalEffectReason reason{ExternalEffectReason::None};
    ExternalEffectState state{ExternalEffectState::Prepared};
    bool executor_invoked{};

    [[nodiscard]] bool accepted() const noexcept {
        return reason == ExternalEffectReason::None;
    }
};

struct ExternalEffectRollbackDecision final {
    ExternalEffectReason reason{ExternalEffectReason::None};
    std::size_t prepared_effects_discarded{};
    std::size_t committed_effects_after_frame{};

    [[nodiscard]] bool safe() const noexcept {
        return reason == ExternalEffectReason::None;
    }
};

class ExternalEffectLedger final {
public:
    [[nodiscard]] ExternalEffectDecision prepare(
        ExternalEffectIntent intent,
        CorrelationId correlation_id = 0U,
        TraceBuffer* traces = nullptr);
    [[nodiscard]] ExternalEffectDecision commit(
        std::string_view idempotency_key,
        std::uint64_t confirmed_frame,
        ExternalEffectExecutor& executor,
        CorrelationId correlation_id = 0U,
        TraceBuffer* traces = nullptr);
    [[nodiscard]] ExternalEffectDecision compensate(
        std::string_view idempotency_key,
        ExternalEffectExecutor& executor,
        CorrelationId correlation_id = 0U,
        TraceBuffer* traces = nullptr);
    [[nodiscard]] ExternalEffectRollbackDecision reconcile_rollback(
        std::uint64_t restored_frame,
        CorrelationId correlation_id = 0U,
        TraceBuffer* traces = nullptr);

    [[nodiscard]] const std::vector<ExternalEffectRecord>& records() const noexcept {
        return records_;
    }

private:
    [[nodiscard]] std::size_t find(std::string_view idempotency_key) const noexcept;

    std::vector<ExternalEffectRecord> records_{};
};

[[nodiscard]] const char* to_string(
    MandatoryOperationalPath path) noexcept;
[[nodiscard]] const char* to_string(
    DurableRecorderReason reason) noexcept;
[[nodiscard]] const char* to_string(
    ExternalEffectState state) noexcept;
[[nodiscard]] const char* to_string(
    ExternalEffectReason reason) noexcept;

} // namespace neoeng::core
