#include "neoeng/core/hardware_profile.hpp"
#include "neoeng/core/hash.hpp"
#include "neoeng/core/observability.hpp"
#include "neoeng/core/simulation.hpp"
#include "neoeng/core/visual_correlation.hpp"
#include "neoeng/view_lab/view_lab.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view expression, std::string_view test) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL [" << test << "]: " << expression << '\n';
    }
}

#define CHECK(test_name, expression) check((expression), #expression, (test_name))

[[nodiscard]] std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("cannot read test output");
    }
    const std::uintmax_t size = std::filesystem::file_size(path);
    std::string output(static_cast<std::size_t>(size), '\0');
    stream.read(output.data(), static_cast<std::streamsize>(output.size()));
    if (!stream && !stream.eof()) {
        throw std::runtime_error("failed while reading test output");
    }
    return output;
}

void test_deterministic_frame_rendering() {
    constexpr std::string_view name = "deterministic_frame_rendering";
    using namespace neoeng::core;
    const WorldState state{
        .frame = 7U,
        .bodies = {
            {.id = 1U, .position = {Fixed::from_integer(-2), Fixed::from_integer(1)}, .velocity = {}},
            {.id = 2U, .position = {Fixed::from_integer(2), Fixed::from_integer(-1)}, .velocity = {}},
        },
    };
    const FrameRecord record{
        .state = state,
        .inputs = {},
        .events = {},
        .state_hash = stable_hash(state),
    };
    const auto first = neoeng::view_lab::render_frame(record, 3U, {
        .width = 160U,
        .height = 90U,
        .pixels_per_world_unit = 12,
        .body_half_extent_pixels = 4,
    });
    const auto second = neoeng::view_lab::render_frame(record, 3U, {
        .width = 160U,
        .height = 90U,
        .pixels_per_world_unit = 12,
        .body_half_extent_pixels = 4,
    });
    CHECK(name, first.reference.visibility_hash == second.reference.visibility_hash);
    CHECK(name, first.reference.deferred_hash == second.reference.deferred_hash);
    CHECK(name, first.reference.deferred_hash == first.reference.conventional_hash);
    CHECK(name, first.reference.color_mismatches == 0U);
    CHECK(name, first.correlation.valid());
    CHECK(name, first.correlation.core_frame == 7U);
    CHECK(name, first.correlation.render_frame == 3U);
    CHECK(name, first.correlation.state_hash == stable_hash(state));
}

void test_static_viewer_and_p4_contract() {
    constexpr std::string_view name = "static_viewer_and_p4_contract";
    using namespace neoeng::core;
    WorldState state{
        .frame = 0U,
        .bodies = {{.id = 9U, .position = {}, .velocity = {}}},
    };
    TimeTravelDebugger debugger(8U);
    debugger.record_frame(state, {});
    for (std::uint64_t frame = 1U; frame <= 3U; ++frame) {
        const std::array<InputCommand, 1U> input{{
            {.entity = 9U, .acceleration = {Fixed::from_integer(1), Fixed{}}},
        }};
        state = step(state, input);
        const std::array<TraceEvent, 1U> event{{{
            .correlation_id = 0xD003000000000000ULL + frame,
            .frame = frame,
            .category = TraceCategory::Simulation,
            .outcome = TraceOutcome::Applied,
            .code = TraceCode::StateAdvanced,
            .entity = 9U,
        }}};
        debugger.record_frame(state, input, event);
    }

    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "neoeng-dcore-view-lab-test";
    std::filesystem::remove_all(output);
    std::filesystem::create_directories(output);
    {
        std::ofstream user_named_frame(output / "frame-user.bmp", std::ios::binary);
        user_named_frame << "preserve-me";
    }
    {
        std::ofstream stale_generated_frame(output / "frame-99999999.bmp", std::ios::binary);
        stale_generated_frame << "remove-me";
    }
    const auto result = neoeng::view_lab::export_static_viewer(
        debugger, output, "VIRTUALIZED-TEST", {
            .width = 96U,
            .height = 64U,
            .pixels_per_world_unit = 8,
            .body_half_extent_pixels = 3,
        });
    CHECK(name, result.frames_written == 4U);
    CHECK(name, result.first_frame == 0U);
    CHECK(name, result.last_frame == 3U);
    CHECK(name, std::filesystem::is_regular_file(result.viewer_path));
    CHECK(name, std::filesystem::is_regular_file(result.correlation_path));
    CHECK(name, std::filesystem::is_regular_file(output / "frame-00000000.bmp"));
    CHECK(name, std::filesystem::is_regular_file(output / "frame-user.bmp"));
    CHECK(name, !std::filesystem::exists(output / "frame-99999999.bmp"));
    CHECK(name, read_all(result.viewer_path).find("NeoEng D-Core View Lab") != std::string::npos);
    CHECK(name, read_all(result.viewer_path).find(
        "\"correlation_id\":\"14988823984819142657\"") != std::string::npos);
    CHECK(name, read_all(result.correlation_path).find(kVisualCorrelationSchema)
        != std::string::npos);
    const std::string bmp = read_all(output / "frame-00000000.bmp");
    CHECK(name, bmp.size() > 54U);
    CHECK(name, bmp[0] == 'B' && bmp[1] == 'M');

    const HardwareProfileContract p4 = hardware_profile_contract(
        HardwareProfileId::P4EightGbCompatibility);
    CHECK(name, p4.id == HardwareProfileId::P4EightGbCompatibility);
    CHECK(name, !p4.enforces_rollback_budget);
    CHECK(name, !p4.enforces_ecs_budget);
    CHECK(name, std::string_view(to_string(p4.id)) == "P4");
    std::filesystem::remove_all(output);
}

void test_sparse_frame_export() {
    constexpr std::string_view name = "sparse_frame_export";
    using namespace neoeng::core;
    constexpr std::uint64_t maximum_frame = std::numeric_limits<std::uint64_t>::max();
    TimeTravelDebugger debugger(4U);
    const auto world_for = [](std::uint64_t frame) {
        return WorldState{
            .frame = frame,
            .bodies = {{.id = 1U, .position = {}, .velocity = {}}},
        };
    };
    debugger.record_frame(world_for(1U), {});
    debugger.record_frame(world_for(maximum_frame), {});

    const std::filesystem::path output =
        std::filesystem::temp_directory_path() / "neoeng-dcore-view-lab-sparse-test";
    std::filesystem::remove_all(output);
    const auto result = neoeng::view_lab::export_static_viewer(
        debugger, output, "VIRTUALIZED-SPARSE-TEST", {
            .width = 32U,
            .height = 32U,
            .pixels_per_world_unit = 4,
            .body_half_extent_pixels = 2,
        });
    CHECK(name, result.frames_written == 2U);
    CHECK(name, result.first_frame == 1U);
    CHECK(name, result.last_frame == maximum_frame);
    CHECK(name, std::filesystem::is_regular_file(
        output / "frame-00000001.bmp"));
    CHECK(name, std::filesystem::is_regular_file(
        output / "frame-18446744073709551615.bmp"));
    std::filesystem::remove_all(output);
}

void test_gpu_correlation_schema_requires_timeline() {
    constexpr std::string_view name = "gpu_correlation_schema_requires_timeline";
    using namespace neoeng::core;
    VisualCorrelationRecord invalid{
        .core_frame = 1U,
        .render_frame = 1U,
        .state_hash = 10U,
        .visibility_hash = 20U,
        .color_hash = 30U,
        .correlation_id = 40U,
        .producer = VisualProducerKind::GpuInstrumented,
    };
    CHECK(name, !invalid.valid());
    invalid.gpu = GpuTimelineCorrelation{
        .submission_id = 1U,
        .gpu_begin_ns = 100U,
        .gpu_end_ns = 200U,
        .host_sample_begin_ns = 90U,
        .host_sample_end_ns = 210U,
    };
    CHECK(name, invalid.valid());
    const std::array<VisualCorrelationRecord, 1U> records{invalid};
    const std::string json = visual_correlation_json(records, "P4-HISTORICAL-CORRELATION");
    CHECK(name, json.find("gpu_instrumented") != std::string::npos);
    CHECK(name, json.find("submission_id") != std::string::npos);
}

} // namespace

int main() {
    test_deterministic_frame_rendering();
    test_static_viewer_and_p4_contract();
    test_sparse_frame_export();
    test_gpu_correlation_schema_requires_timeline();
    if (failures != 0) {
        std::cerr << failures << " view-lab integration assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "view_lab_integration_tests=passed\n";
    return EXIT_SUCCESS;
}
