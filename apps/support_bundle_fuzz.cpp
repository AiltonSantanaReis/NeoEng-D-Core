#include "neoeng/core/support_bundle.hpp"

#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace neoeng::core;

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1
        ? static_cast<std::size_t>(std::stoull(argv[1])) : 100'000U;
    const SupportBundlePolicy policy{
        .maximum_trace_events = 8U,
        .maximum_entry_bytes = 1024U * 1024U,
        .maximum_total_bytes = 4U * 1024U * 1024U,
        .include_time_travel = false,
        .include_visual_correlation = false,
        .include_monotonic_timestamps = false,
        .pseudonymization_salt = "fuzz-public-salt",
    };
    const std::vector<DeferredValidationGate> gates{{
        .gate_id = "FUZZ-GATE",
        .category = ValidationGateCategory::FutureInfrastructure,
        .target = "test",
        .reason = "test",
        .implementation_status = "test",
        .execution_status = ValidationExecutionStatus::NotApplicable,
        .required_profile = "P0",
        .required_artifacts = {},
        .blocking_for_current_changeset = false,
        .blocking_for_profile_qualification = false,
    }};
    std::vector<TraceEvent> traces{{
        .correlation_id = 1U,
        .frame = 1U,
        .category = TraceCategory::Tooling,
        .outcome = TraceOutcome::Applied,
        .code = TraceCode::SupportBundleCreated,
        .subsystem = TraceSubsystem::SupportBundle,
    }};
    const SupportBundleContext context{
        .project_version = "1.5.0",
        .environment_id = "fuzz",
        .hardware_profile = "P0-unqualified",
        .seed = 1U,
        .traces = traces,
        .deferred_gates = gates,
    };
    const SupportBundleArtifact canonical = build_support_bundle(context, policy);
    if (!verify_support_bundle(canonical, policy).accepted()) return EXIT_FAILURE;

    std::mt19937_64 random(0xD005B00D1EULL);
    std::uniform_int_distribution<std::size_t> entry_distribution(0U, canonical.entries.size() - 1U);
    std::size_t rejected{};
    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        SupportBundleArtifact tampered = canonical;
        SupportBundleEntry& entry = tampered.entries[entry_distribution(random)];
        if (entry.content.empty()) entry.content.push_back('x');
        else entry.content[iteration % entry.content.size()] ^= static_cast<char>(1U << (iteration % 7U));
        if (!verify_support_bundle(tampered, policy).accepted()) ++rejected;
    }
    std::cout << "iterations=" << iterations << '\n'
              << "tampered_rejected=" << rejected << '\n'
              << "false_accepts=" << (iterations - rejected) << '\n'
              << "manifest_sha256=" << sha256_hex(canonical.manifest_sha256) << '\n';
    return rejected == iterations ? EXIT_SUCCESS : EXIT_FAILURE;
}
