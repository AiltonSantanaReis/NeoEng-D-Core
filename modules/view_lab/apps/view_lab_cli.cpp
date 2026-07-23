#include "neoeng/core/observability.hpp"
#include "neoeng/core/simulation.hpp"
#include "neoeng/view_lab/view_lab.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("neoeng-dcore-view-lab-output");
        const std::string environment = argc > 2 ? argv[2] : "VIRTUALIZED-UNQUALIFIED";

        using namespace neoeng::core;
        WorldState state{
            .frame = 0U,
            .bodies = {
                {.id = 1U, .position = {Fixed::from_integer(-4), Fixed{}}, .velocity = {}},
                {.id = 2U, .position = {Fixed::from_integer(4), Fixed{}}, .velocity = {}},
            },
        };
        TimeTravelDebugger debugger(64U);
        debugger.record_frame(state, {});
        for (std::uint64_t frame = 1U; frame <= 30U; ++frame) {
            const std::array<InputCommand, 2U> inputs{{
                {.entity = 1U, .acceleration = {Fixed::from_ratio(1, 4), Fixed::from_ratio(1, 16)}},
                {.entity = 2U, .acceleration = {Fixed::from_ratio(-1, 4), Fixed::from_ratio(-1, 16)}},
            }};
            state = step(state, inputs);
            const std::array<TraceEvent, 2U> events{{
                {
                    .correlation_id = 0xD003000000000000ULL | frame,
                    .frame = frame,
                    .category = TraceCategory::Simulation,
                    .outcome = TraceOutcome::Applied,
                    .code = TraceCode::StateAdvanced,
                    .entity = 1U,
                },
                {
                    .correlation_id = 0xD003100000000000ULL | frame,
                    .frame = frame,
                    .category = TraceCategory::Simulation,
                    .outcome = TraceOutcome::Applied,
                    .code = TraceCode::StateAdvanced,
                    .entity = 2U,
                },
            }};
            debugger.record_frame(state, inputs, events);
        }

        const neoeng::view_lab::ViewerExportResult result =
            neoeng::view_lab::export_static_viewer(debugger, output, environment, {
                .width = 640U,
                .height = 360U,
                .pixels_per_world_unit = 20,
                .body_half_extent_pixels = 7,
                .write_ppm_reference = false,
            });
        std::cout << "viewer=" << result.viewer_path.string() << '\n'
                  << "correlation=" << result.correlation_path.string() << '\n'
                  << "frames=" << result.frames_written << '\n'
                  << "range=" << result.first_frame << '-' << result.last_frame << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "view-lab error: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
