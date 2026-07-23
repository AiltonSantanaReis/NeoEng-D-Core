#include "neoeng/core/hardware_profile.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

neoeng::core::HardwareProfileId parse_profile(std::string_view value) {
    using neoeng::core::HardwareProfileId;
    if (value == "P0") return HardwareProfileId::P0Reference;
    if (value == "P1") return HardwareProfileId::P1NvidiaTarget;
    if (value == "P2") return HardwareProfileId::P2AmdTarget;
    if (value == "P3") return HardwareProfileId::P3Arm64Compatibility;
    std::cerr << "profile must be P0, P1, P2 or P3\n";
    std::exit(2);
}

} // namespace

int main(int argc, char** argv) {
    using namespace neoeng::core;
    if (argc != 12) {
        std::cerr << "usage: neoeng_hardware_profile_probe PROFILE ENVIRONMENT_ID CPU_SKU GPU_SKU "
                     "DRIVER OS_BUILD POWER_PROFILE ROLLBACK_P99_NS ECS_P99_NS DETERMINISM_OK SERIALIZATION_OK\n";
        return 2;
    }
    const HardwareProfileId profile = parse_profile(argv[1]);
    const HardwareEnvironmentBaseline baseline{
        .environment_id = argv[2],
        .cpu_sku = argv[3],
        .gpu_sku = argv[4],
        .driver_version = argv[5],
        .os_build = argv[6],
        .power_profile = argv[7],
    };
    const std::uint64_t rollback = std::strtoull(argv[8], nullptr, 10);
    const std::uint64_t ecs = std::strtoull(argv[9], nullptr, 10);
    const HardwareMeasurement measurement{
        .profile = profile,
        .environment_id = argv[2],
        .rollback_p99_ns = rollback,
        .ecs_maintenance_p99_ns = ecs,
        .rollback_measurement_present = rollback != 0U,
        .ecs_measurement_present = ecs != 0U,
        .determinism_passed = std::string_view(argv[10]) == "1",
        .serialization_compatibility_passed = std::string_view(argv[11]) == "1",
    };
    const HardwareQualificationResult result = evaluate_hardware_qualification(baseline, measurement);
    std::cout << "{\n"
              << "  \"schema\": \"neoeng.dcore.hardware-qualification.v1\",\n"
              << "  \"profile\": \"" << to_string(profile) << "\",\n"
              << "  \"environment_id\": \"" << baseline.environment_id << "\",\n"
              << "  \"rollback_p99_ns\": " << rollback << ",\n"
              << "  \"ecs_maintenance_p99_ns\": " << ecs << ",\n"
              << "  \"failure_mask\": " << static_cast<std::uint32_t>(result.failures) << ",\n"
              << "  \"status\": \"" << to_string(result.status) << "\"\n"
              << "}\n";
    return result.status == QualificationStatus::Passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
