#pragma once

#include "neoeng/core/observability.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace neoeng::core {

inline constexpr std::uint16_t kVisualCorrelationSchemaVersion = 1U;
inline constexpr std::string_view kVisualCorrelationSchema =
    "neoeng.dcore.visual-correlation.v1";

enum class VisualProducerKind : std::uint8_t {
    CpuReference,
    GpuInstrumented,
};

struct GpuTimelineCorrelation final {
    std::uint64_t submission_id{};
    std::uint64_t gpu_begin_ns{};
    std::uint64_t gpu_end_ns{};
    std::uint64_t host_sample_begin_ns{};
    std::uint64_t host_sample_end_ns{};

    [[nodiscard]] bool valid() const noexcept {
        return submission_id != 0U && gpu_end_ns >= gpu_begin_ns
            && host_sample_end_ns >= host_sample_begin_ns;
    }
};

struct VisualCorrelationRecord final {
    std::uint16_t schema_version{kVisualCorrelationSchemaVersion};
    std::uint64_t core_frame{};
    std::uint64_t render_frame{};
    std::uint64_t state_hash{};
    std::uint64_t visibility_hash{};
    std::uint64_t color_hash{};
    CorrelationId correlation_id{};
    VisualProducerKind producer{VisualProducerKind::CpuReference};
    std::uint64_t cpu_begin_ns{};
    std::uint64_t cpu_end_ns{};
    std::optional<GpuTimelineCorrelation> gpu{};

    [[nodiscard]] bool valid() const noexcept {
        if (schema_version != kVisualCorrelationSchemaVersion
            || correlation_id == 0U
            || cpu_end_ns < cpu_begin_ns) {
            return false;
        }
        switch (producer) {
        case VisualProducerKind::CpuReference:
            return !gpu.has_value() || gpu->valid();
        case VisualProducerKind::GpuInstrumented:
            return gpu.has_value() && gpu->valid();
        default:
            return false;
        }
    }
};

[[nodiscard]] const char* to_string(VisualProducerKind producer) noexcept;
[[nodiscard]] std::string visual_correlation_json(
    std::span<const VisualCorrelationRecord> records,
    std::string_view environment_id);

} // namespace neoeng::core
