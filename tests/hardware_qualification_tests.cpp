#include "neoeng/core/hardware_profile.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using namespace neoeng::core;
int failures{};

#define CHECK(test_name, condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "FAIL " << (test_name) << ": " << #condition << '\n'; \
            ++failures; \
        } \
    } while (false)

HardwareEnvironmentBaseline baseline(ExecutionEnvironmentKind execution) {
    return {
        .environment_id = "P1-NVIDIA-LAB-001",
        .cpu_sku = "registered-cpu",
        .gpu_sku = "registered-nvidia-gpu",
        .driver_version = "registered-driver",
        .os_build = "registered-os-build",
        .power_profile = "registered-power-profile",
        .execution_environment = execution,
        .architecture = "x86_64",
        .memory_configuration = "registered-memory",
        .storage_configuration = "registered-storage",
        .firmware_version = "registered-firmware",
        .thermal_policy = "registered-thermal-policy",
        .profile_compatibility_confirmed = true,
        .environment_lock_recorded = true,
    };
}

HardwareMeasurement complete_p1() {
    return {
        .profile = HardwareProfileId::P1NvidiaTarget,
        .environment_id = "P1-NVIDIA-LAB-001",
        .rollback_p99_ns = 1'900'000U,
        .ecs_maintenance_p99_ns = 90'000U,
        .rollback_sample_count = 1'000U,
        .ecs_sample_count = 1'000U,
        .rollback_measurement_present = true,
        .ecs_measurement_present = true,
        .determinism_passed = true,
        .serialization_compatibility_passed = true,
        .full_test_report_present = true,
        .full_test_suite_passed = true,
        .ecs_scope_evidence_complete = true,
        .benchmark_report_present = true,
        .raw_samples_present = true,
        .binary_hashes_present = true,
        .source_manifest_present = true,
        .hardware_inventory_present = true,
        .thermal_record_present = true,
        .campaign_manifest_verified = true,
        .clock_policy_recorded = true,
        .allocation_gate_passed = true,
    };
}

void test_native_candidate_can_pass() {
    constexpr std::string_view name = "native_candidate_can_pass";
    const HardwareQualificationResult result = evaluate_hardware_qualification(
        baseline(ExecutionEnvironmentKind::NativePhysical), complete_p1());
    CHECK(name, result.status == QualificationStatus::Passed);
    CHECK(name, result.evidence_disposition
        == QualificationEvidenceDisposition::QualificationCandidate);
    CHECK(name, result.failures == QualificationFailure::None);
}

void test_virtualized_run_is_baseline_only() {
    constexpr std::string_view name = "virtualized_run_is_baseline_only";
    const HardwareQualificationResult result = evaluate_hardware_qualification(
        baseline(ExecutionEnvironmentKind::Virtualized), complete_p1());
    CHECK(name, result.status == QualificationStatus::Unqualified);
    CHECK(name, result.evidence_disposition
        == QualificationEvidenceDisposition::EngineeringBaseline);
    CHECK(name, contains(result.failures, QualificationFailure::NativeExecutionRequired));
}

void test_incomplete_evidence_cannot_pass() {
    constexpr std::string_view name = "incomplete_evidence_cannot_pass";
    HardwareMeasurement measurement = complete_p1();
    measurement.raw_samples_present = false;
    measurement.campaign_manifest_verified = false;
    const HardwareQualificationResult result = evaluate_hardware_qualification(
        baseline(ExecutionEnvironmentKind::NativePhysical), measurement);
    CHECK(name, result.status == QualificationStatus::Unqualified);
    CHECK(name, result.evidence_disposition == QualificationEvidenceDisposition::Incomplete);
    CHECK(name, contains(result.failures, QualificationFailure::MissingRawSamples));
    CHECK(name, contains(result.failures, QualificationFailure::CampaignVerificationFailed));
}

void test_insufficient_samples_are_unqualified() {
    constexpr std::string_view name = "insufficient_samples_are_unqualified";
    HardwareMeasurement measurement = complete_p1();
    measurement.rollback_sample_count = 999U;
    const HardwareQualificationResult result = evaluate_hardware_qualification(
        baseline(ExecutionEnvironmentKind::NativePhysical), measurement);
    CHECK(name, result.status == QualificationStatus::Unqualified);
    CHECK(name, contains(result.failures, QualificationFailure::InsufficientSamples));
}

void test_measured_failure_is_failed_not_unqualified() {
    constexpr std::string_view name = "measured_failure_is_failed_not_unqualified";
    HardwareMeasurement measurement = complete_p1();
    measurement.rollback_p99_ns = 2'000'001U;
    measurement.cpu_migration_detected = true;
    measurement.allocation_gate_passed = false;
    const HardwareQualificationResult result = evaluate_hardware_qualification(
        baseline(ExecutionEnvironmentKind::NativePhysical), measurement);
    CHECK(name, result.status == QualificationStatus::Failed);
    CHECK(name, contains(result.failures, QualificationFailure::RollbackBudgetExceeded));
    CHECK(name, contains(result.failures, QualificationFailure::CpuMigrationDetected));
    CHECK(name, contains(result.failures, QualificationFailure::AllocationGateFailed));
}

void test_non_p1_profiles_do_not_inherit_p1_budgets() {
    constexpr std::string_view name = "non_p1_profiles_do_not_inherit_p1_budgets";
    for (const HardwareProfileId profile : {
            HardwareProfileId::P0Reference,
            HardwareProfileId::P2AmdTarget,
            HardwareProfileId::P3Arm64Compatibility,
            HardwareProfileId::P4EightGbCompatibility}) {
        const HardwareProfileContract contract = hardware_profile_contract(profile);
        CHECK(name, !contract.enforces_rollback_budget);
        CHECK(name, !contract.enforces_ecs_budget);
        CHECK(name, contract.rollback_p99_limit_ns == 0U);
        CHECK(name, contract.ecs_maintenance_p99_limit_ns == 0U);
    }
}

} // namespace

int main() {
    test_native_candidate_can_pass();
    test_virtualized_run_is_baseline_only();
    test_incomplete_evidence_cannot_pass();
    test_insufficient_samples_are_unqualified();
    test_measured_failure_is_failed_not_unqualified();
    test_non_p1_profiles_do_not_inherit_p1_budgets();
    if (failures != 0) {
        std::cerr << failures << " hardware qualification test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "hardware_qualification_tests=passed\n";
    return EXIT_SUCCESS;
}
