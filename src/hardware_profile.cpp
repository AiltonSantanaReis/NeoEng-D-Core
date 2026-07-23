#include "neoeng/core/hardware_profile.hpp"

namespace neoeng::core {
namespace {

[[nodiscard]] bool baseline_complete(const HardwareEnvironmentBaseline& baseline) noexcept {
    return !baseline.environment_id.empty()
        && !baseline.cpu_sku.empty()
        && !baseline.gpu_sku.empty()
        && !baseline.driver_version.empty()
        && !baseline.os_build.empty()
        && !baseline.power_profile.empty();
}

} // namespace

HardwareProfileContract hardware_profile_contract(HardwareProfileId id) noexcept {
    switch (id) {
    case HardwareProfileId::P0Reference:
        return {
            .id = id,
            .name = "P0 - Reference",
            .purpose = "Fixed laboratory reproducibility and daily regression",
            .requires_locked_environment = true,
            .requires_determinism = true,
            .requires_serialization_compatibility = true,
        };
    case HardwareProfileId::P1NvidiaTarget:
        return {
            .id = id,
            .name = "P1 - NVIDIA target",
            .purpose = "Primary contractual performance target",
            .requires_locked_environment = true,
            .requires_determinism = true,
            .requires_serialization_compatibility = true,
            .enforces_rollback_budget = true,
            .enforces_ecs_budget = true,
            .rollback_p99_limit_ns = 2'000'000U,
            .ecs_maintenance_p99_limit_ns = 100'000U,
        };
    case HardwareProfileId::P2AmdTarget:
        return {
            .id = id,
            .name = "P2 - AMD target",
            .purpose = "Semantic portability and separately qualified performance",
            .requires_locked_environment = true,
            .requires_determinism = true,
            .requires_serialization_compatibility = true,
        };
    case HardwareProfileId::P3Arm64Compatibility:
        return {
            .id = id,
            .name = "P3 - ARM64 compatibility",
            .purpose = "Determinism and serialization compatibility",
            .requires_locked_environment = true,
            .requires_determinism = true,
            .requires_serialization_compatibility = true,
        };
    case HardwareProfileId::P4EightGbCompatibility:
        return {
            .id = id,
            .name = "P4 - 8 GB compatibility",
            .purpose = "Compatibility, safe degradation and evidence collection on 8 GB GPUs",
            .requires_locked_environment = true,
            .requires_determinism = true,
            .requires_serialization_compatibility = true,
        };
    }
    return {};
}

HardwareQualificationResult evaluate_hardware_qualification(
    const HardwareEnvironmentBaseline& baseline,
    const HardwareMeasurement& measurement) noexcept {
    HardwareQualificationResult result{
        .status = QualificationStatus::Passed,
        .contract = hardware_profile_contract(measurement.profile),
    };
    if (!baseline_complete(baseline)) {
        result.failures |= QualificationFailure::MissingBaseline;
    }
    if (baseline.environment_id.empty() || measurement.environment_id.empty()
        || baseline.environment_id != measurement.environment_id) {
        result.failures |= QualificationFailure::EnvironmentMismatch;
    }
    if (result.contract.enforces_rollback_budget) {
        if (!measurement.rollback_measurement_present) {
            result.failures |= QualificationFailure::MissingMeasurement;
        } else if (measurement.rollback_p99_ns > result.contract.rollback_p99_limit_ns) {
            result.failures |= QualificationFailure::RollbackBudgetExceeded;
        }
    }
    if (result.contract.enforces_ecs_budget) {
        if (!measurement.ecs_measurement_present) {
            result.failures |= QualificationFailure::MissingMeasurement;
        } else if (measurement.ecs_maintenance_p99_ns
                   > result.contract.ecs_maintenance_p99_limit_ns) {
            result.failures |= QualificationFailure::EcsBudgetExceeded;
        }
    }
    if (result.contract.requires_determinism && !measurement.determinism_passed) {
        result.failures |= QualificationFailure::DeterminismFailed;
    }
    if (result.contract.requires_serialization_compatibility
        && !measurement.serialization_compatibility_passed) {
        result.failures |= QualificationFailure::SerializationFailed;
    }

    if (contains(result.failures, QualificationFailure::MissingBaseline)
        || contains(result.failures, QualificationFailure::EnvironmentMismatch)
        || contains(result.failures, QualificationFailure::MissingMeasurement)) {
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

} // namespace neoeng::core
