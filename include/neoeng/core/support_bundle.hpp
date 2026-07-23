#pragma once

#include "neoeng/core/crypto_hash.hpp"
#include "neoeng/core/observability.hpp"
#include "neoeng/core/state_evidence.hpp"
#include "neoeng/core/visual_correlation.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neoeng::core {

inline constexpr std::string_view kSupportBundleSchema = "neoeng.dcore.support-bundle.v1";
inline constexpr std::string_view kDeferredValidationSchema =
    "neoeng.dcore.deferred-validation-gates.v1";

enum class ValidationGateCategory : std::uint8_t {
    ImplementationGap,
    NativeValidationPending,
    ExternalAssurancePending,
    FutureInfrastructure,
};

enum class ValidationExecutionStatus : std::uint8_t {
    NotExecuted,
    Passed,
    Failed,
    NotApplicable,
};

struct DeferredValidationGate final {
    std::string gate_id{};
    ValidationGateCategory category{ValidationGateCategory::NativeValidationPending};
    std::string target{};
    std::string reason{};
    std::string implementation_status{};
    ValidationExecutionStatus execution_status{ValidationExecutionStatus::NotExecuted};
    std::string required_profile{};
    std::vector<std::string> required_artifacts{};
    bool blocking_for_current_changeset{};
    bool blocking_for_profile_qualification{true};
};

struct SupportBundlePolicy final {
    std::size_t maximum_trace_events{4'096U};
    std::size_t maximum_entry_bytes{16U * 1024U * 1024U};
    std::size_t maximum_total_bytes{64U * 1024U * 1024U};
    bool include_time_travel{true};
    bool time_travel_payload_authorized{false};
    bool include_visual_correlation{true};
    bool include_monotonic_timestamps{false};
    std::string pseudonymization_salt{};
};

struct SupportBundleContext final {
    std::string project_version{};
    std::string environment_id{};
    std::string hardware_profile{};
    std::uint64_t seed{};
    std::span<const TraceEvent> traces{};
    std::string time_travel_json{};
    std::span<const SignedStateEvidence> evidence_records{};
    std::span<const VisualCorrelationRecord> visual_records{};
    std::span<const DeferredValidationGate> deferred_gates{};
};

struct SupportBundleEntry final {
    std::string path{};
    std::string content{};
    Sha256Digest sha256{};
};

struct SupportBundleArtifact final {
    std::vector<SupportBundleEntry> entries{};
    std::string manifest_json{};
    Sha256Digest manifest_sha256{};
};

enum class SupportBundleVerifyReason : std::uint8_t {
    None,
    InvalidPath,
    DuplicatePath,
    EntryTooLarge,
    TotalTooLarge,
    HashMismatch,
    MissingRequiredEntry,
    ManifestMismatch,
};

struct SupportBundleVerifyResult final {
    SupportBundleVerifyReason reason{SupportBundleVerifyReason::None};
    std::size_t entry_index{};
    [[nodiscard]] bool accepted() const noexcept {
        return reason == SupportBundleVerifyReason::None;
    }
};

[[nodiscard]] std::string export_deferred_validation_gates_json(
    std::span<const DeferredValidationGate> gates);
[[nodiscard]] SupportBundleArtifact build_support_bundle(
    const SupportBundleContext& context,
    const SupportBundlePolicy& policy,
    TraceBuffer* audit_traces = nullptr,
    CorrelationId correlation_id = 0U);
[[nodiscard]] SupportBundleVerifyResult verify_support_bundle(
    const SupportBundleArtifact& bundle,
    const SupportBundlePolicy& policy) noexcept;
void write_support_bundle_directory(
    const SupportBundleArtifact& bundle,
    const std::filesystem::path& directory);

[[nodiscard]] const char* to_string(ValidationGateCategory category) noexcept;
[[nodiscard]] const char* to_string(ValidationExecutionStatus status) noexcept;
[[nodiscard]] const char* to_string(SupportBundleVerifyReason reason) noexcept;

} // namespace neoeng::core
