#pragma once

#include "neoeng/core/observability.hpp"
#include "neoeng/core/visual_correlation.hpp"
#include "neoeng/render/visibility_buffer.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace neoeng::view_lab {

struct ViewLabConfig final {
    std::size_t width{640U};
    std::size_t height{360U};
    std::int32_t pixels_per_world_unit{16};
    std::int32_t body_half_extent_pixels{6};
    bool write_ppm_reference{};
};

struct ViewLabFrame final {
    std::uint64_t core_frame{};
    std::uint64_t render_frame{};
    neoeng::core::VisualCorrelationRecord correlation{};
    neoeng::render::VisibilityReferenceResult reference{};
};

struct ViewerExportResult final {
    std::filesystem::path viewer_path{};
    std::filesystem::path correlation_path{};
    std::size_t frames_written{};
    std::uint64_t first_frame{};
    std::uint64_t last_frame{};
};

[[nodiscard]] ViewLabFrame render_frame(
    const neoeng::core::FrameRecord& frame,
    std::uint64_t render_frame_number,
    const ViewLabConfig& config = {},
    neoeng::core::CorrelationId correlation_id = 0U);

void write_bmp(
    const neoeng::render::ColorBuffer& buffer,
    const std::filesystem::path& path);

void write_ppm(
    const neoeng::render::ColorBuffer& buffer,
    const std::filesystem::path& path);

[[nodiscard]] ViewerExportResult export_static_viewer(
    const neoeng::core::TimeTravelDebugger& debugger,
    const std::filesystem::path& output_directory,
    std::string_view environment_id,
    const ViewLabConfig& config = {});

} // namespace neoeng::view_lab
