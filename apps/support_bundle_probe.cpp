#include "neoeng/core/diagnostics.hpp"
#include "neoeng/core/support_bundle.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace neoeng::core;

namespace {

WorldState make_world(std::uint64_t frame, std::int64_t offset) {
    return {
        .frame = frame,
        .bodies = {
            {.id = 1U, .position = {Fixed::from_raw(100 + offset), Fixed::from_raw(200)},
             .velocity = {Fixed::from_raw(300), Fixed::from_raw(400)}},
            {.id = 2U, .position = {Fixed::from_raw(500), Fixed::from_raw(600)},
             .velocity = {Fixed::from_raw(700), Fixed::from_raw(800)}},
        },
    };
}

std::vector<DeferredValidationGate> make_gates() {
    return {
        {.gate_id = "NATIVE-WIN-X64-001",
         .category = ValidationGateCategory::NativeValidationPending,
         .target = "Windows x86_64 physical",
         .reason = "Native execution deferred by project owner",
         .implementation_status = "ready_for_campaign",
         .execution_status = ValidationExecutionStatus::NotExecuted,
         .required_profile = "P1 or P4",
         .required_artifacts = {"build-log", "ctest-report", "determinism-probe", "hardware-inventory"},
         .blocking_for_current_changeset = false,
         .blocking_for_profile_qualification = true},
        {.gate_id = "NATIVE-ARM64-001",
         .category = ValidationGateCategory::NativeValidationPending,
         .target = "ARM64 physical",
         .reason = "Native ARM64 host unavailable in current virtualized campaign",
         .implementation_status = "ready_for_campaign",
         .execution_status = ValidationExecutionStatus::NotExecuted,
         .required_profile = "P3",
         .required_artifacts = {"build-log", "ctest-report", "evidence-chain", "hardware-inventory"},
         .blocking_for_current_changeset = false,
         .blocking_for_profile_qualification = true},
    };
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "support-bundle-probe";
    TraceBuffer traces(64U);
    BudgetMonitor budgets;
    (void)budgets.record({
        .definition = {.id = BudgetId::Rollback, .subsystem = TraceSubsystem::Rollback,
            .limit_ns = 2'000'000U, .exceed_severity = TraceSeverity::Warning},
        .correlation_id = 0x5001U,
        .frame = 2U,
        .measured_ns = 1'750'000U,
    }, traces);
    (void)budgets.record({
        .definition = {.id = BudgetId::EcsMaintenance, .subsystem = TraceSubsystem::Simulation,
            .limit_ns = 100'000U, .exceed_severity = TraceSeverity::Warning},
        .correlation_id = 0x5002U,
        .frame = 2U,
        .measured_ns = 125'000U,
    }, traces);
    const WorldState frame1 = make_world(1U, 0);
    const WorldState frame2 = make_world(2U, 1);
    (void)diagnose_state_divergence(frame1, frame2, 0x5003U, &traces, 0U, 1U);

    TimeTravelDebugger debugger(8U);
    debugger.record_frame(frame1, {}, traces.snapshot());
    debugger.record_frame(frame2, {}, traces.snapshot());
    EvidenceChain chain(0x500U, "neoeng-dcore-support-probe", 1U);
    (void)chain.append(frame1, 0x5001U, 0U, nullptr, &traces);
    (void)chain.append(frame2, 0x5002U, 0U, nullptr, &traces);
    const std::vector<DeferredValidationGate> gates = make_gates();
    const std::vector<TraceEvent> snapshot = traces.snapshot();
    const SupportBundlePolicy policy{
        .maximum_trace_events = 64U,
        .maximum_entry_bytes = 4U * 1024U * 1024U,
        .maximum_total_bytes = 16U * 1024U * 1024U,
        .include_time_travel = true,
        .time_travel_payload_authorized = true,
        .include_visual_correlation = false,
        .include_monotonic_timestamps = false,
        .pseudonymization_salt = "neoeng-dcore-probe-public-salt",
    };
    const SupportBundleContext context{
        .project_version = "1.5.0",
        .environment_id = "linux-x86_64-virtualized",
        .hardware_profile = "P0-unqualified",
        .seed = 0x5005U,
        .traces = snapshot,
        .time_travel_json = debugger.export_reproducible_json(0x5005U, "linux-x86_64-virtualized"),
        .evidence_records = chain.records(),
        .deferred_gates = gates,
    };
    const SupportBundleArtifact bundle = build_support_bundle(context, policy, &traces, 0x5005U);
    const SupportBundleVerifyResult verification = verify_support_bundle(bundle, policy);
    if (!verification.accepted()) {
        std::cerr << "support bundle verification failed: " << to_string(verification.reason) << '\n';
        return EXIT_FAILURE;
    }
    std::filesystem::remove_all(output);
    write_support_bundle_directory(bundle, output);
    std::cout << "schema=" << kSupportBundleSchema << '\n'
              << "entries=" << bundle.entries.size() << '\n'
              << "manifest_sha256=" << sha256_hex(bundle.manifest_sha256) << '\n'
              << "deferred_gates=" << gates.size() << '\n'
              << "verification=" << to_string(verification.reason) << '\n';
    return EXIT_SUCCESS;
}
