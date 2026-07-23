#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace neoeng::core {

enum class HardwareProfileId : std::uint8_t {
    P0Reference,
    P1NvidiaTarget,
    P2AmdTarget,
    P3Arm64Compatibility,
    P4EightGbCompatibility,
};

enum class QualificationStatus : std::uint8_t {
    Unqualified,
    Passed,
    Failed,
};

enum class ExecutionEnvironmentKind : std::uint8_t {
    Unknown,
    Virtualized,
    NativePhysical,
    Containerized,
};

enum class QualificationEvidenceDisposition : std::uint8_t {
    Incomplete,
    EngineeringBaseline,
    QualificationCandidate,
};

enum class QualificationFailure : std::uint64_t {
    None = 0ULL,
    MissingBaseline = 1ULL << 0U,
    EnvironmentMismatch = 1ULL << 1U,
    MissingMeasurement = 1ULL << 2U,
    RollbackBudgetExceeded = 1ULL << 3U,
    EcsBudgetExceeded = 1ULL << 4U,
    DeterminismFailed = 1ULL << 5U,
    SerializationFailed = 1ULL << 6U,
    NativeExecutionRequired = 1ULL << 7U,
    MissingFullTestReport = 1ULL << 8U,
    MissingRawSamples = 1ULL << 9U,
    MissingBinaryHashes = 1ULL << 10U,
    MissingSourceManifest = 1ULL << 11U,
    MissingHardwareInventory = 1ULL << 12U,
    MissingThermalRecord = 1ULL << 13U,
    CampaignVerificationFailed = 1ULL << 14U,
    ProfileCompatibilityFailed = 1ULL << 15U,
    InsufficientSamples = 1ULL << 16U,
    MissingBenchmarkReport = 1ULL << 17U,
    ClockPolicyMissing = 1ULL << 18U,
    CpuMigrationDetected = 1ULL << 19U,
    AllocationGateFailed = 1ULL << 20U,
    FullTestSuiteFailed = 1ULL << 21U,
    EcsScopeIncomplete = 1ULL << 22U,
};

constexpr QualificationFailure operator|(
    QualificationFailure lhs,
    QualificationFailure rhs) noexcept {
    return static_cast<QualificationFailure>(
        static_cast<std::uint64_t>(lhs) | static_cast<std::uint64_t>(rhs));
}

constexpr QualificationFailure& operator|=(
    QualificationFailure& lhs,
    QualificationFailure rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool contains(
    QualificationFailure value,
    QualificationFailure flag) noexcept {
    return (static_cast<std::uint64_t>(value) & static_cast<std::uint64_t>(flag)) != 0ULL;
}

struct HardwareProfileContract final {
    HardwareProfileId id{HardwareProfileId::P0Reference};
    std::string_view name{};
    std::string_view purpose{};
    bool requires_locked_environment{true};
    bool requires_native_physical_execution{true};
    bool requires_determinism{true};
    bool requires_serialization_compatibility{true};
    bool requires_full_test_suite{true};
    bool requires_benchmark_report{true};
    bool requires_raw_samples{true};
    bool requires_binary_hashes{true};
    bool requires_source_manifest{true};
    bool requires_hardware_inventory{true};
    bool requires_thermal_record{true};
    bool requires_clock_policy{true};
    bool requires_campaign_verification{true};
    bool requires_allocation_gate{};
    bool requires_complete_ecs_scope{};
    bool enforces_rollback_budget{};
    bool enforces_ecs_budget{};
    std::size_t minimum_rollback_samples{};
    std::size_t minimum_ecs_samples{};
    std::uint64_t rollback_p99_limit_ns{};
    std::uint64_t ecs_maintenance_p99_limit_ns{};
};

struct HardwareEnvironmentBaseline final {
    std::string_view environment_id{};
    std::string_view cpu_sku{};
    std::string_view gpu_sku{};
    std::string_view driver_version{};
    std::string_view os_build{};
    std::string_view power_profile{};
    ExecutionEnvironmentKind execution_environment{ExecutionEnvironmentKind::Unknown};
    std::string_view architecture{};
    std::string_view memory_configuration{};
    std::string_view storage_configuration{};
    std::string_view firmware_version{};
    std::string_view thermal_policy{};
    bool profile_compatibility_confirmed{};
    bool environment_lock_recorded{};
};

struct HardwareMeasurement final {
    HardwareProfileId profile{HardwareProfileId::P0Reference};
    std::string_view environment_id{};
    std::uint64_t rollback_p99_ns{};
    std::uint64_t ecs_maintenance_p99_ns{};
    std::size_t rollback_sample_count{};
    std::size_t ecs_sample_count{};
    bool rollback_measurement_present{};
    bool ecs_measurement_present{};
    bool determinism_passed{};
    bool serialization_compatibility_passed{};
    bool full_test_report_present{};
    bool full_test_suite_passed{};
    bool ecs_scope_evidence_complete{};
    bool benchmark_report_present{};
    bool raw_samples_present{};
    bool binary_hashes_present{};
    bool source_manifest_present{};
    bool hardware_inventory_present{};
    bool thermal_record_present{};
    bool campaign_manifest_verified{};
    bool clock_policy_recorded{};
    bool cpu_migration_detected{};
    bool allocation_gate_passed{};
};

struct HardwareQualificationResult final {
    QualificationStatus status{QualificationStatus::Unqualified};
    QualificationEvidenceDisposition evidence_disposition{
        QualificationEvidenceDisposition::Incomplete};
    QualificationFailure failures{QualificationFailure::None};
    HardwareProfileContract contract{};
};

[[nodiscard]] HardwareProfileContract hardware_profile_contract(
    HardwareProfileId id) noexcept;

[[nodiscard]] HardwareQualificationResult evaluate_hardware_qualification(
    const HardwareEnvironmentBaseline& baseline,
    const HardwareMeasurement& measurement) noexcept;

[[nodiscard]] const char* to_string(HardwareProfileId id) noexcept;
[[nodiscard]] const char* to_string(QualificationStatus status) noexcept;
[[nodiscard]] const char* to_string(ExecutionEnvironmentKind kind) noexcept;
[[nodiscard]] const char* to_string(QualificationEvidenceDisposition disposition) noexcept;

} // namespace neoeng::core
