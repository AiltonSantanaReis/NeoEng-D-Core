#include "neoeng/core/diagnostics.hpp"
#include "neoeng/core/support_bundle.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace neoeng::core;

namespace {

int failures{};
#define CHECK(name, expression) do { \
    if (!(expression)) { \
        std::cerr << "FAIL " << (name) << ": " << #expression << '\n'; \
        ++failures; \
    } \
} while (false)

WorldState make_world(std::uint64_t frame, std::int64_t offset = 0) {
    return {
        .frame = frame,
        .bodies = {
            {.id = 1U,
             .position = {Fixed::from_raw(100 + offset), Fixed::from_raw(200)},
             .velocity = {Fixed::from_raw(300), Fixed::from_raw(400)}},
            {.id = 2U,
             .position = {Fixed::from_raw(500), Fixed::from_raw(600)},
             .velocity = {Fixed::from_raw(700), Fixed::from_raw(800)}},
        },
    };
}

std::vector<DeferredValidationGate> gates() {
    return {
        {
            .gate_id = "NATIVE-WIN-X64-001",
            .category = ValidationGateCategory::NativeValidationPending,
            .target = "Windows x86_64 physical",
            .reason = "Physical Windows host unavailable in this delivery environment",
            .implementation_status = "complete_for_current_scope",
            .execution_status = ValidationExecutionStatus::NotExecuted,
            .required_profile = "P1 or P4",
            .required_artifacts = {"build-log", "ctest-report", "determinism-probe"},
            .blocking_for_current_changeset = false,
            .blocking_for_profile_qualification = true,
        },
        {
            .gate_id = "NATIVE-ARM64-001",
            .category = ValidationGateCategory::NativeValidationPending,
            .target = "ARM64 physical",
            .reason = "ARM64 execution deferred to the native qualification campaign",
            .implementation_status = "portable_sources_ready",
            .execution_status = ValidationExecutionStatus::NotExecuted,
            .required_profile = "P3",
            .required_artifacts = {"build-log", "ctest-report", "evidence-chain"},
            .blocking_for_current_changeset = false,
            .blocking_for_profile_qualification = true,
        },
    };
}

void test_budget_monitor() {
    constexpr const char* name = "budget_monitor";
    TraceBuffer traces(8U);
    BudgetMonitor monitor;
    const BudgetDefinition definition{
        .id = BudgetId::Rollback,
        .subsystem = TraceSubsystem::Rollback,
        .limit_ns = 2'000'000U,
        .exceed_severity = TraceSeverity::Error,
    };
    const BudgetEvaluation accepted = monitor.record({
        .definition = definition,
        .correlation_id = 10U,
        .frame = 5U,
        .measured_ns = 1'500'000U,
    }, traces);
    const BudgetEvaluation exceeded = monitor.record({
        .definition = definition,
        .correlation_id = 11U,
        .frame = 6U,
        .measured_ns = 2'000'001U,
    }, traces);
    CHECK(name, !accepted.exceeded);
    CHECK(name, exceeded.exceeded);
    CHECK(name, traces.by_correlation(10U).front().code == TraceCode::BudgetSampled);
    CHECK(name, traces.by_correlation(11U).front().code == TraceCode::BudgetExceeded);
    CHECK(name, traces.by_correlation(11U).front().severity == TraceSeverity::Error);
}

void test_divergence_diagnostics() {
    constexpr const char* name = "divergence_diagnostics";
    TraceBuffer traces(8U);
    const WorldState expected = make_world(9U);
    WorldState actual = expected;
    actual.bodies.front().position.x = Fixed::from_raw(101);
    const StateDivergenceReport report = diagnose_state_divergence(
        expected, actual, 99U, &traces, 123U, 1U);
    CHECK(name, report.divergent);
    CHECK(name, report.first_divergent_entity == 1U);
    CHECK(name, report.first_divergent_component == "position.x");
    CHECK(name, report.first_divergent_chunk == 0U);
    const std::vector<TraceEvent> correlated = traces.by_correlation(99U);
    CHECK(name, correlated.size() == 3U);
    CHECK(name, correlated.front().code == TraceCode::StateDivergence);
    CHECK(name, correlated.back().code == TraceCode::BudgetSampled);
    CHECK(name, correlated.back().detail_code
        == static_cast<std::uint32_t>(BudgetId::DivergenceLocalization));
    CHECK(name, !diagnose_state_divergence(expected, expected, 100U).divergent);
}

void test_support_bundle_and_tamper_detection() {
    constexpr const char* name = "support_bundle_and_tamper_detection";
    TraceBuffer traces(16U);
    traces.record({
        .correlation_id = 7U,
        .frame = 1U,
        .category = TraceCategory::Network,
        .outcome = TraceOutcome::Accepted,
        .code = TraceCode::SessionEstablished,
        .subsystem = TraceSubsystem::Session,
        .severity = TraceSeverity::Info,
        .subject_token = 0x12345678U,
    });
    TimeTravelDebugger debugger(4U);
    const WorldState world = make_world(1U);
    debugger.record_frame(world, {}, traces.snapshot());
    EvidenceChain chain(1U, "support-test", 1U);
    (void)chain.append(world, 7U, 0U, nullptr, &traces);
    const std::vector<DeferredValidationGate> deferred = gates();
    const SupportBundlePolicy policy{
        .maximum_trace_events = 16U,
        .maximum_entry_bytes = 1024U * 1024U,
        .maximum_total_bytes = 4U * 1024U * 1024U,
        .include_time_travel = true,
        .time_travel_payload_authorized = true,
        .include_visual_correlation = false,
        .include_monotonic_timestamps = false,
        .pseudonymization_salt = "unit-test-salt",
    };
    const std::vector<TraceEvent> snapshot = traces.snapshot();
    SupportBundleContext context{
        .project_version = "1.5.0-test",
        .environment_id = "virtual-linux-x86_64",
        .hardware_profile = "P0-unqualified",
        .seed = 42U,
        .traces = snapshot,
        .time_travel_json = debugger.export_reproducible_json(42U, "virtual-linux-x86_64"),
        .evidence_records = chain.records(),
        .deferred_gates = deferred,
    };
    SupportBundleArtifact bundle = build_support_bundle(context, policy, &traces, 77U);
    CHECK(name, verify_support_bundle(bundle, policy).accepted());
    CHECK(name, bundle.manifest_json.find(std::string(kSupportBundleSchema)) != std::string::npos);
    bool raw_subject_found = false;
    bool pseudonym_found = false;
    for (const SupportBundleEntry& entry : bundle.entries) {
        if (entry.path == "traces.json") {
            raw_subject_found = entry.content.find("305419896") != std::string::npos;
            pseudonym_found = entry.content.find("subject_pseudonym\":\"") != std::string::npos;
        }
    }
    CHECK(name, !raw_subject_found);
    CHECK(name, pseudonym_found);

    SupportBundleArtifact tampered = bundle;
    tampered.entries.front().content.push_back('x');
    CHECK(name, verify_support_bundle(tampered, policy).reason
        == SupportBundleVerifyReason::HashMismatch);

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "neoeng-dcore-support-tests";
    std::filesystem::remove_all(directory);
    write_support_bundle_directory(bundle, directory);
    CHECK(name, std::filesystem::is_regular_file(directory / "manifest.json"));
    CHECK(name, std::filesystem::is_regular_file(directory / "manifest.sha256"));
    std::filesystem::remove_all(directory);
}

void test_deferred_gate_schema() {
    constexpr const char* name = "deferred_gate_schema";
    const std::string json = export_deferred_validation_gates_json(gates());
    CHECK(name, json.find(std::string(kDeferredValidationSchema)) != std::string::npos);
    CHECK(name, json.find("native_validation_pending") != std::string::npos);
    CHECK(name, json.find("blocking_for_current_changeset\":false") != std::string::npos);
}

} // namespace

int main() {
    test_budget_monitor();
    test_divergence_diagnostics();
    test_support_bundle_and_tamper_detection();
    test_deferred_gate_schema();
    if (failures != 0) {
        std::cerr << failures << " observability/support checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "observability/support checks passed\n";
    return EXIT_SUCCESS;
}
