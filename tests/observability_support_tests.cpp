#include "neoeng/core/diagnostics.hpp"
#include "neoeng/core/support_bundle.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
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

void test_uint64_json_round_trip() {
    constexpr const char* name = "uint64_json_round_trip";
    constexpr std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    constexpr std::int64_t maximum_signed = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t minimum_signed = std::numeric_limits<std::int64_t>::min();
    const std::vector<TraceEvent> traces{{
        .correlation_id = maximum,
        .sequence = maximum,
        .frame = maximum,
        .monotonic_time_ns = maximum,
        .category = TraceCategory::Tooling,
        .outcome = TraceOutcome::Applied,
        .code = TraceCode::BudgetSampled,
        .measured_value = maximum_signed,
        .budget_limit = minimum_signed,
        .related_hash = maximum,
    }};
    const SupportBundlePolicy policy{
        .maximum_trace_events = 4U,
        .maximum_entry_bytes = 1024U * 1024U,
        .maximum_total_bytes = 4U * 1024U * 1024U,
        .include_time_travel = false,
        .time_travel_payload_authorized = false,
        .include_visual_correlation = false,
        .include_monotonic_timestamps = true,
        .pseudonymization_salt = "uint64-round-trip-salt",
    };
    const SupportBundleContext context{
        .project_version = "uint64-test",
        .environment_id = "test-environment",
        .hardware_profile = "test-host",
        .seed = maximum,
        .traces = traces,
        .time_travel_json = {},
        .evidence_records = {},
        .visual_records = {},
        .deferred_gates = {},
    };
    const SupportBundleArtifact bundle = build_support_bundle(context, policy);
    std::string traces_json;
    std::string metadata_json;
    for (const SupportBundleEntry& entry : bundle.entries) {
        if (entry.path == "traces.json") traces_json = entry.content;
        if (entry.path == "metadata.json") metadata_json = entry.content;
    }
    const std::string maximum_text = "\"" + std::to_string(maximum) + "\"";
    CHECK(name, traces_json.find("\"correlation_id\":" + maximum_text) != std::string::npos);
    CHECK(name, traces_json.find("\"sequence\":" + maximum_text) != std::string::npos);
    CHECK(name, traces_json.find("\"frame\":" + maximum_text) != std::string::npos);
    CHECK(name, traces_json.find("\"monotonic_time_ns\":" + maximum_text) != std::string::npos);
    CHECK(name, traces_json.find("\"related_hash\":" + maximum_text) != std::string::npos);
    CHECK(name, traces_json.find("\"measured_value\":\"9223372036854775807\"")
        != std::string::npos);
    CHECK(name, traces_json.find("\"budget_limit\":\"-9223372036854775808\"")
        != std::string::npos);
    CHECK(name, metadata_json.find("\"seed\": \"" + std::to_string(maximum) + "\"")
        != std::string::npos);
}
void test_deferred_gate_schema() {
    constexpr const char* name = "deferred_gate_schema";
    const std::string json = export_deferred_validation_gates_json(gates());
    CHECK(name, json.find(std::string(kDeferredValidationSchema)) != std::string::npos);
    CHECK(name, json.find("native_validation_pending") != std::string::npos);
    CHECK(name, json.find("blocking_for_current_changeset\":false") != std::string::npos);
}

bool valid_utf8(std::string_view value) {
    for (std::size_t index = 0U; index < value.size();) {
        const auto lead = static_cast<unsigned char>(value[index]);
        std::size_t length{};
        std::uint32_t code_point{};
        if (lead <= 0x7FU) {
            length = 1U;
            code_point = lead;
        } else if (lead >= 0xC2U && lead <= 0xDFU) {
            length = 2U;
            code_point = lead & 0x1FU;
        } else if (lead >= 0xE0U && lead <= 0xEFU) {
            length = 3U;
            code_point = lead & 0x0FU;
        } else if (lead >= 0xF0U && lead <= 0xF4U) {
            length = 4U;
            code_point = lead & 0x07U;
        } else {
            return false;
        }
        if (index + length > value.size()) return false;
        for (std::size_t offset = 1U; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xC0U) != 0x80U) return false;
            code_point = (code_point << 6U) | (continuation & 0x3FU);
        }
        if ((length == 2U && code_point < 0x80U)
            || (length == 3U && code_point < 0x800U)
            || (length == 4U && code_point < 0x10000U)
            || code_point > 0x10FFFFU
            || (code_point >= 0xD800U && code_point <= 0xDFFFU)) {
            return false;
        }
        index += length;
    }
    return true;
}

void test_support_bundle_adversarial_contracts() {
    constexpr const char* name = "support_bundle_adversarial_contracts";
    const SupportBundlePolicy policy{
        .maximum_trace_events = 16U,
        .maximum_entry_bytes = 1024U * 1024U,
        .maximum_total_bytes = 4U * 1024U * 1024U,
        .include_time_travel = false,
        .time_travel_payload_authorized = false,
        .include_visual_correlation = false,
        .include_monotonic_timestamps = false,
        .pseudonymization_salt = "adversarial-test-salt",
    };
    const std::vector<TraceEvent> traces{};
    const std::vector<SignedStateEvidence> evidence{};
    const std::vector<DeferredValidationGate> deferred = gates();
    SupportBundleContext context{
        .project_version = "1.14.0-test",
        .environment_id = "test-environment",
        .hardware_profile = "test-host",
        .seed = 7U,
        .traces = traces,
        .time_travel_json = {},
        .evidence_records = evidence,
        .visual_records = {},
        .deferred_gates = deferred,
    };
    SupportBundleArtifact bundle = build_support_bundle(context, policy);

    SupportBundleArtifact invalid_manifest = bundle;
    invalid_manifest.manifest_json = "THIS IS NOT JSON\n";
    invalid_manifest.manifest_sha256 = sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(invalid_manifest.manifest_json.data()),
        invalid_manifest.manifest_json.size()));
    CHECK(name, !verify_support_bundle(invalid_manifest, policy).accepted());

    SupportBundlePolicy tiny_total = policy;
    tiny_total.maximum_total_bytes = 1U;
    bool builder_rejected{};
    try {
        (void)build_support_bundle(context, tiny_total);
    } catch (const std::length_error&) {
        builder_rejected = true;
    }
    CHECK(name, builder_rejected);
    SupportBundlePolicy tiny_entry = policy;
    tiny_entry.maximum_entry_bytes = 1U;
    CHECK(name, verify_support_bundle(bundle, tiny_entry).reason
        == SupportBundleVerifyReason::EntryTooLarge);

    SupportBundleContext invalid_utf8 = context;
    invalid_utf8.project_version.push_back(static_cast<char>(0xFFU));
    const SupportBundleArtifact escaped = build_support_bundle(invalid_utf8, policy);
    bool all_json_utf8 = valid_utf8(escaped.manifest_json);
    for (const SupportBundleEntry& entry : escaped.entries) {
        all_json_utf8 = all_json_utf8 && valid_utf8(entry.content);
    }
    CHECK(name, all_json_utf8);

    std::vector<DeferredValidationGate> unknown = gates();
    unknown.front().category = static_cast<ValidationGateCategory>(255U);
    bool enum_rejected{};
    try {
        (void)export_deferred_validation_gates_json(unknown);
    } catch (const std::invalid_argument&) {
        enum_rejected = true;
    }
    CHECK(name, enum_rejected);
}

} // namespace

int main() {
    test_budget_monitor();
    test_divergence_diagnostics();
    test_support_bundle_and_tamper_detection();
    test_uint64_json_round_trip();
    test_deferred_gate_schema();
    test_support_bundle_adversarial_contracts();
    if (failures != 0) {
        std::cerr << failures << " observability/support checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "observability/support checks passed\n";
    return EXIT_SUCCESS;
}
