#pragma once

#include <cstdint>
#include <string_view>

namespace neoeng::core {

enum class HardwareProfileId : std::uint8_t {
    P0Reference,
    P1NvidiaTarget,
    P2AmdTarget,
    P3Arm64Compatibility,
};

enum class QualificationStatus : std::uint8_t {
    Unqualified,
    Passed,
    Failed,
};

enum class QualificationFailure : std::uint32_t {
    None = 0U,
    MissingBaseline = 1U << 0U,
    EnvironmentMismatch = 1U << 1U,
    MissingMeasurement = 1U << 2U,
    RollbackBudgetExceeded = 1U << 3U,
    EcsBudgetExceeded = 1U << 4U,
    DeterminismFailed = 1U << 5U,
    SerializationFailed = 1U << 6U,
};

constexpr QualificationFailure operator|(
    QualificationFailure lhs,
    QualificationFailure rhs) noexcept {
    return static_cast<QualificationFailure>(
        static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
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
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0U;
}

struct HardwareProfileContract final {
    HardwareProfileId id{HardwareProfileId::P0Reference};
    std::string_view name{};
    std::string_view purpose{};
    bool requires_locked_environment{true};
    bool requires_determinism{true};
    bool requires_serialization_compatibility{true};
    bool enforces_rollback_budget{};
    bool enforces_ecs_budget{};
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
};

struct HardwareMeasurement final {
    HardwareProfileId profile{HardwareProfileId::P0Reference};
    std::string_view environment_id{};
    std::uint64_t rollback_p99_ns{};
    std::uint64_t ecs_maintenance_p99_ns{};
    bool rollback_measurement_present{};
    bool ecs_measurement_present{};
    bool determinism_passed{};
    bool serialization_compatibility_passed{};
};

struct HardwareQualificationResult final {
    QualificationStatus status{QualificationStatus::Unqualified};
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

} // namespace neoeng::core
