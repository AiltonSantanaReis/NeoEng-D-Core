#include "neoeng/core/hardware_profile.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using neoeng::core::ExecutionEnvironmentKind;
using neoeng::core::HardwareProfileId;

[[noreturn]] void usage(std::string_view message = {}) {
    if (!message.empty()) std::cerr << "error: " << message << '\n';
    std::cerr
        << "usage: neoeng_hardware_profile_probe"
        << " --profile P0|P1|P2|P3|P4"
        << " --environment-id ID --cpu-sku VALUE --gpu-sku VALUE"
        << " --driver-version VALUE --os-build VALUE --power-profile VALUE"
        << " --execution-kind virtualized|native_physical|containerized"
        << " --architecture VALUE --memory VALUE --storage VALUE"
        << " --firmware VALUE --thermal-policy VALUE"
        << " --profile-compatible 0|1 --environment-locked 0|1"
        << " --rollback-p99-ns N --rollback-samples N"
        << " --ecs-p99-ns N --ecs-samples N"
        << " --determinism-passed 0|1 --serialization-passed 0|1"
        << " --full-test-report-present 0|1 --full-tests-passed 0|1"
        << " --ecs-scope-complete 0|1 --benchmark-report-present 0|1"
        << " --raw-samples-present 0|1 --binary-hashes-present 0|1"
        << " --source-manifest-present 0|1 --hardware-inventory-present 0|1"
        << " --thermal-record-present 0|1 --campaign-verified 0|1"
        << " --clock-policy-recorded 0|1 --cpu-migration-detected 0|1"
        << " --allocation-gate-passed 0|1\n";
    std::exit(2);
}

[[nodiscard]] std::map<std::string, std::string> parse_options(int argc, char** argv) {
    if (argc < 2 || ((argc - 1) % 2) != 0) usage("options must be key/value pairs");
    std::map<std::string, std::string> result;
    for (int index = 1; index < argc; index += 2) {
        std::string key = argv[index];
        if (!key.starts_with("--")) usage("option name must start with --");
        if (!result.emplace(std::move(key), argv[index + 1]).second) {
            usage("duplicate option");
        }
    }
    return result;
}


void reject_unknown_options(const std::map<std::string, std::string>& options) {
    static const std::set<std::string> allowed{
        "--profile", "--environment-id", "--cpu-sku", "--gpu-sku",
        "--driver-version", "--os-build", "--power-profile", "--execution-kind",
        "--architecture", "--memory", "--storage", "--firmware", "--thermal-policy",
        "--profile-compatible", "--environment-locked", "--rollback-p99-ns",
        "--rollback-samples", "--ecs-p99-ns", "--ecs-samples",
        "--determinism-passed", "--serialization-passed", "--full-test-report-present",
        "--full-tests-passed", "--ecs-scope-complete", "--benchmark-report-present",
        "--raw-samples-present", "--binary-hashes-present", "--source-manifest-present",
        "--hardware-inventory-present", "--thermal-record-present", "--campaign-verified",
        "--clock-policy-recorded", "--cpu-migration-detected", "--allocation-gate-passed",
    };
    for (const auto& [key, value] : options) {
        static_cast<void>(value);
        if (!allowed.contains(key)) usage(std::string("unknown option: ") + key);
    }
}

[[nodiscard]] const std::string& required(
    const std::map<std::string, std::string>& options,
    std::string_view key) {
    const auto found = options.find(std::string(key));
    if (found == options.end() || found->second.empty()) usage(std::string("missing ") + std::string(key));
    return found->second;
}

[[nodiscard]] bool boolean_value(
    const std::map<std::string, std::string>& options,
    std::string_view key) {
    const std::string& value = required(options, key);
    if (value == "1" || value == "true") return true;
    if (value == "0" || value == "false") return false;
    usage(std::string(key) + " must be 0, 1, true or false");
}

[[nodiscard]] std::uint64_t unsigned_value(
    const std::map<std::string, std::string>& options,
    std::string_view key) {
    const std::string& value = required(options, key);
    std::uint64_t parsed{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error != std::errc{} || end != value.data() + value.size()) {
        usage(std::string(key) + " must be an unsigned integer");
    }
    return parsed;
}

[[nodiscard]] HardwareProfileId parse_profile(std::string_view value) {
    if (value == "P0") return HardwareProfileId::P0Reference;
    if (value == "P1") return HardwareProfileId::P1NvidiaTarget;
    if (value == "P2") return HardwareProfileId::P2AmdTarget;
    if (value == "P3") return HardwareProfileId::P3Arm64Compatibility;
    if (value == "P4") return HardwareProfileId::P4EightGbCompatibility;
    usage("profile must be P0, P1, P2, P3 or P4");
}

[[nodiscard]] ExecutionEnvironmentKind parse_execution_kind(std::string_view value) {
    if (value == "virtualized") return ExecutionEnvironmentKind::Virtualized;
    if (value == "native_physical") return ExecutionEnvironmentKind::NativePhysical;
    if (value == "containerized") return ExecutionEnvironmentKind::Containerized;
    if (value == "unknown") return ExecutionEnvironmentKind::Unknown;
    usage("execution-kind is invalid");
}

[[nodiscard]] std::string json_escape(std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                throw std::invalid_argument("control character in text option");
            }
            output += character;
            break;
        }
    }
    return output;
}

} // namespace

int main(int argc, char** argv) {
    using namespace neoeng::core;
    try {
        const auto options = parse_options(argc, argv);
        reject_unknown_options(options);
        const HardwareProfileId profile = parse_profile(required(options, "--profile"));
        const ExecutionEnvironmentKind execution_kind = parse_execution_kind(
            required(options, "--execution-kind"));
        const std::uint64_t rollback = unsigned_value(options, "--rollback-p99-ns");
        const std::uint64_t ecs = unsigned_value(options, "--ecs-p99-ns");
        const std::size_t rollback_samples = static_cast<std::size_t>(
            unsigned_value(options, "--rollback-samples"));
        const std::size_t ecs_samples = static_cast<std::size_t>(
            unsigned_value(options, "--ecs-samples"));
        const bool ecs_scope_claim = boolean_value(options, "--ecs-scope-complete");

        const HardwareEnvironmentBaseline baseline{
            .environment_id = required(options, "--environment-id"),
            .cpu_sku = required(options, "--cpu-sku"),
            .gpu_sku = required(options, "--gpu-sku"),
            .driver_version = required(options, "--driver-version"),
            .os_build = required(options, "--os-build"),
            .power_profile = required(options, "--power-profile"),
            .execution_environment = execution_kind,
            .architecture = required(options, "--architecture"),
            .memory_configuration = required(options, "--memory"),
            .storage_configuration = required(options, "--storage"),
            .firmware_version = required(options, "--firmware"),
            .thermal_policy = required(options, "--thermal-policy"),
            .profile_compatibility_confirmed = boolean_value(options, "--profile-compatible"),
            .environment_lock_recorded = boolean_value(options, "--environment-locked"),
        };
        const HardwareMeasurement measurement{
            .profile = profile,
            .environment_id = baseline.environment_id,
            .rollback_p99_ns = rollback,
            .ecs_maintenance_p99_ns = ecs,
            .rollback_sample_count = rollback_samples,
            .ecs_sample_count = ecs_samples,
            .rollback_measurement_present = rollback_samples != 0U,
            .ecs_measurement_present = ecs_samples != 0U,
            .determinism_passed = boolean_value(options, "--determinism-passed"),
            .serialization_compatibility_passed = boolean_value(options, "--serialization-passed"),
            .full_test_report_present = boolean_value(options, "--full-test-report-present"),
            .full_test_suite_passed = boolean_value(options, "--full-tests-passed"),
            .ecs_scope_evidence_complete = ecs_scope_claim,
            .benchmark_report_present = boolean_value(options, "--benchmark-report-present"),
            .raw_samples_present = boolean_value(options, "--raw-samples-present"),
            .binary_hashes_present = boolean_value(options, "--binary-hashes-present"),
            .source_manifest_present = boolean_value(options, "--source-manifest-present"),
            .hardware_inventory_present = boolean_value(options, "--hardware-inventory-present"),
            .thermal_record_present = boolean_value(options, "--thermal-record-present"),
            .campaign_manifest_verified = boolean_value(options, "--campaign-verified"),
            .clock_policy_recorded = boolean_value(options, "--clock-policy-recorded"),
            .cpu_migration_detected = boolean_value(options, "--cpu-migration-detected"),
            .allocation_gate_passed = boolean_value(options, "--allocation-gate-passed"),
        };
        const HardwareQualificationResult result = evaluate_hardware_qualification(baseline, measurement);
        std::cout << "{\n"
                  << "  \"schema\": \"neoeng.dcore.hardware-qualification.v2\",\n"
                  << "  \"project_version\": \"1.14.1\",\n"
                  << "  \"independent_verification_required\": true,\n"
                  << "  \"profile\": \"" << to_string(profile) << "\",\n"
                  << "  \"environment_id\": \"" << json_escape(baseline.environment_id) << "\",\n"
                  << "  \"execution_kind\": \"" << to_string(execution_kind) << "\",\n"
                  << "  \"evidence_disposition\": \""
                  << to_string(result.evidence_disposition) << "\",\n"
                  << "  \"rollback_p99_ns\": " << rollback << ",\n"
                  << "  \"rollback_samples\": " << rollback_samples << ",\n"
                  << "  \"ecs_maintenance_p99_ns\": " << ecs << ",\n"
                  << "  \"ecs_samples\": " << ecs_samples << ",\n"
                  << "  \"failure_mask\": " << static_cast<std::uint64_t>(result.failures) << ",\n"
                  << "  \"status\": \"" << to_string(result.status) << "\"\n"
                  << "}\n";
        return result.status == QualificationStatus::Passed ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        std::cerr << "hardware profile probe failed: " << exception.what() << '\n';
        return 2;
    }
}
