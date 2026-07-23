#include "neoeng/core/hardware_profile.hpp"

namespace neoeng::core {
namespace {

[[nodiscard]] bool baseline_complete(const HardwareEnvironmentBaseline& baseline) noexcept {
    return !baseline.environment_id.empty()
        && !baseline.cpu_sku.empty()
        && !baseline.gpu_sku.empty()
        && !baseline.driver_version.empty()
        && !baseline.os_build.empty()
        && !baseline.power_profile.empty()
        && !baseline.architecture.empty()
        && !baseline.memory_configuration.empty()
        && !baseline.storage_configuration.empty()
        && !baseline.firmware_version.empty()
        && !baseline.thermal_policy.empty();
}

[[nodiscard]] bool is_incomplete_failure(QualificationFailure failures) noexcept {
    constexpr QualificationFailure incomplete_flags[] = {
        QualificationFailure::MissingBaseline,
        QualificationFailure::EnvironmentMismatch,
        QualificationFailure::MissingMeasurement,
        QualificationFailure::NativeExecutionRequired,
        QualificationFailure::MissingFullTestReport,
        QualificationFailure::MissingRawSamples,
        QualificationFailure::MissingBinaryHashes,
        QualificationFailure::MissingSourceManifest,
        QualificationFailure::MissingHardwareInventory,
        QualificationFailure::MissingThermalRecord,
        QualificationFailure::CampaignVerificationFailed,
        QualificationFailure::ProfileCompatibilityFailed,
        QualificationFailure::InsufficientSamples,
        QualificationFailure::MissingBenchmarkReport,
        QualificationFailure::ClockPolicyMissing,
        QualificationFailure::EcsScopeIncomplete,
    };
    for (const QualificationFailure flag : incomplete_flags) {
        if (contains(failures, flag)) return true;
    }
    return false;
}

[[nodiscard]] QualificationEvidenceDisposition classify_evidence(
    const HardwareProfileContract& contract,
    const HardwareEnvironmentBaseline& baseline,
    const HardwareMeasurement& measurement) noexcept {
    const bool common_evidence = baseline_complete(baseline)
        && baseline.environment_lock_recorded
        && baseline.profile_compatibility_confirmed
        && measurement.full_test_report_present
        && measurement.benchmark_report_present
        && measurement.raw_samples_present
        && measurement.binary_hashes_present
        && measurement.source_manifest_present
        && measurement.hardware_inventory_present
        && measurement.thermal_record_present
        && measurement.clock_policy_recorded
        && measurement.campaign_manifest_verified;
    const bool profile_evidence = (!contract.enforces_rollback_budget
            || measurement.rollback_measurement_present)
        && (!contract.enforces_ecs_budget || measurement.ecs_measurement_present)
        && (!contract.requires_complete_ecs_scope
            || measurement.ecs_scope_evidence_complete);
    if (!common_evidence || !profile_evidence) {
        return QualificationEvidenceDisposition::Incomplete;
    }
    if (baseline.execution_environment == ExecutionEnvironmentKind::NativePhysical) {
        return QualificationEvidenceDisposition::QualificationCandidate;
    }
    return QualificationEvidenceDisposition::EngineeringBaseline;
}

} // namespace

HardwareProfileContract hardware_profile_contract(HardwareProfileId id) noexcept {
    switch (id) {
    case HardwareProfileId::P0Reference:
        return {
            .id = id,
            .name = "P0 - Reference",
            .purpose = "Fixed laboratory reproducibility and daily regression",
        };
    case HardwareProfileId::P1NvidiaTarget:
        return {
            .id = id,
            .name = "P1 - NVIDIA target",
            .purpose = "Primary contractual performance target",
            .requires_allocation_gate = true,
            .requires_complete_ecs_scope = true,
            .enforces_rollback_budget = true,
            .enforces_ecs_budget = true,
            .minimum_rollback_samples = 1'000U,
            .minimum_ecs_samples = 1'000U,
            .rollback_p99_limit_ns = 2'000'000U,
            .ecs_maintenance_p99_limit_ns = 100'000U,
        };
    case HardwareProfileId::P2AmdTarget:
        return {
            .id = id,
            .name = "P2 - AMD target",
            .purpose = "Semantic portability and separately qualified performance",
        };
    case HardwareProfileId::P3Arm64Compatibility:
        return {
            .id = id,
            .name = "P3 - ARM64 compatibility",
            .purpose = "Determinism and serialization compatibility",
        };
    case HardwareProfileId::P4EightGbCompatibility:
        return {
            .id = id,
            .name = "P4 - 8 GB compatibility",
            .purpose = "Compatibility, safe degradation and evidence collection on 8 GB GPUs",
        };
    }
    return {};
}

HardwareQualificationResult evaluate_hardware_qualification(
    const HardwareEnvironmentBaseline& baseline,
    const HardwareMeasurement& measurement) noexcept {
    const HardwareProfileContract contract = hardware_profile_contract(measurement.profile);
    HardwareQualificationResult result{
        .status = QualificationStatus::Passed,
        .evidence_disposition = classify_evidence(contract, baseline, measurement),
        .contract = contract,
    };

    if (!baseline_complete(baseline)) {
        result.failures |= QualificationFailure::MissingBaseline;
    }
    if (baseline.environment_id.empty() || measurement.environment_id.empty()
        || baseline.environment_id != measurement.environment_id) {
        result.failures |= QualificationFailure::EnvironmentMismatch;
    }
    if (result.contract.requires_locked_environment && !baseline.environment_lock_recorded) {
        result.failures |= QualificationFailure::EnvironmentMismatch;
    }
    if (!baseline.profile_compatibility_confirmed) {
        result.failures |= QualificationFailure::ProfileCompatibilityFailed;
    }
    if (result.contract.requires_native_physical_execution
        && baseline.execution_environment != ExecutionEnvironmentKind::NativePhysical) {
        result.failures |= QualificationFailure::NativeExecutionRequired;
    }
    if (result.contract.requires_full_test_suite) {
        if (!measurement.full_test_report_present) {
            result.failures |= QualificationFailure::MissingFullTestReport;
        } else if (!measurement.full_test_suite_passed) {
            result.failures |= QualificationFailure::FullTestSuiteFailed;
        }
    }
    if (result.contract.requires_complete_ecs_scope
        && !measurement.ecs_scope_evidence_complete) {
        result.failures |= QualificationFailure::EcsScopeIncomplete;
    }
    if (result.contract.requires_benchmark_report && !measurement.benchmark_report_present) {
        result.failures |= QualificationFailure::MissingBenchmarkReport;
    }
    if (result.contract.requires_raw_samples && !measurement.raw_samples_present) {
        result.failures |= QualificationFailure::MissingRawSamples;
    }
    if (result.contract.requires_binary_hashes && !measurement.binary_hashes_present) {
        result.failures |= QualificationFailure::MissingBinaryHashes;
    }
    if (result.contract.requires_source_manifest && !measurement.source_manifest_present) {
        result.failures |= QualificationFailure::MissingSourceManifest;
    }
    if (result.contract.requires_hardware_inventory && !measurement.hardware_inventory_present) {
        result.failures |= QualificationFailure::MissingHardwareInventory;
    }
    if (result.contract.requires_thermal_record && !measurement.thermal_record_present) {
        result.failures |= QualificationFailure::MissingThermalRecord;
    }
    if (result.contract.requires_campaign_verification && !measurement.campaign_manifest_verified) {
        result.failures |= QualificationFailure::CampaignVerificationFailed;
    }
    if (result.contract.requires_clock_policy && !measurement.clock_policy_recorded) {
        result.failures |= QualificationFailure::ClockPolicyMissing;
    }
    if (measurement.cpu_migration_detected) {
        result.failures |= QualificationFailure::CpuMigrationDetected;
    }
    if (result.contract.requires_allocation_gate && !measurement.allocation_gate_passed) {
        result.failures |= QualificationFailure::AllocationGateFailed;
    }

    if (result.contract.enforces_rollback_budget) {
        if (!measurement.rollback_measurement_present) {
            result.failures |= QualificationFailure::MissingMeasurement;
        } else {
            if (measurement.rollback_sample_count < result.contract.minimum_rollback_samples) {
                result.failures |= QualificationFailure::InsufficientSamples;
            }
            if (measurement.rollback_p99_ns > result.contract.rollback_p99_limit_ns) {
                result.failures |= QualificationFailure::RollbackBudgetExceeded;
            }
        }
    }
    if (result.contract.enforces_ecs_budget) {
        if (!measurement.ecs_measurement_present) {
            result.failures |= QualificationFailure::MissingMeasurement;
        } else {
            if (measurement.ecs_sample_count < result.contract.minimum_ecs_samples) {
                result.failures |= QualificationFailure::InsufficientSamples;
            }
            if (measurement.ecs_maintenance_p99_ns
                > result.contract.ecs_maintenance_p99_limit_ns) {
                result.failures |= QualificationFailure::EcsBudgetExceeded;
            }
        }
    }
    if (result.contract.requires_determinism && !measurement.determinism_passed) {
        result.failures |= QualificationFailure::DeterminismFailed;
    }
    if (result.contract.requires_serialization_compatibility
        && !measurement.serialization_compatibility_passed) {
        result.failures |= QualificationFailure::SerializationFailed;
    }

    if (is_incomplete_failure(result.failures)) {
        result.status = QualificationStatus::Unqualified;
    } else if (result.failures != QualificationFailure::None) {
        result.status = QualificationStatus::Failed;
    }
    return result;
}

const char* to_string(HardwareProfileId id) noexcept {
    switch (id) {
    case HardwareProfileId::P0Reference: return "P0";
    case HardwareProfileId::P1NvidiaTarget: return "P1";
    case HardwareProfileId::P2AmdTarget: return "P2";
    case HardwareProfileId::P3Arm64Compatibility: return "P3";
    case HardwareProfileId::P4EightGbCompatibility: return "P4";
    }
    return "unknown";
}

const char* to_string(QualificationStatus status) noexcept {
    switch (status) {
    case QualificationStatus::Unqualified: return "unqualified";
    case QualificationStatus::Passed: return "passed";
    case QualificationStatus::Failed: return "failed";
    }
    return "unknown";
}

const char* to_string(ExecutionEnvironmentKind kind) noexcept {
    switch (kind) {
    case ExecutionEnvironmentKind::Unknown: return "unknown";
    case ExecutionEnvironmentKind::Virtualized: return "virtualized";
    case ExecutionEnvironmentKind::NativePhysical: return "native_physical";
    case ExecutionEnvironmentKind::Containerized: return "containerized";
    }
    return "unknown";
}

const char* to_string(QualificationEvidenceDisposition disposition) noexcept {
    switch (disposition) {
    case QualificationEvidenceDisposition::Incomplete: return "incomplete";
    case QualificationEvidenceDisposition::EngineeringBaseline: return "engineering_baseline";
    case QualificationEvidenceDisposition::QualificationCandidate: return "qualification_candidate";
    }
    return "unknown";
}

} // namespace neoeng::core
