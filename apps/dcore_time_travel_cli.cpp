#include "neoeng/core/observability.hpp"
#include "neoeng/core/simulation.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

int main() {
    using namespace neoeng::core;
    WorldState state{
        .frame = 0U,
        .bodies = {{.id = 1U, .position = {}, .velocity = {}}},
    };
    TimeTravelDebugger debugger(16U);
    TraceBuffer trace(64U);
    debugger.record_frame(state, {});
    for (std::uint64_t frame = 0U; frame < 8U; ++frame) {
        const std::array<InputCommand, 1> inputs{{
            {.entity = 1U, .acceleration = {Fixed::from_integer(1), {}}},
        }};
        trace.record({
            .correlation_id = 0xD000U + frame,
            .frame = frame + 1U,
            .category = TraceCategory::Simulation,
            .outcome = TraceOutcome::Applied,
            .code = TraceCode::StateAdvanced,
            .entity = 1U,
        });
        state = step(state, inputs);
        const std::vector<TraceEvent> frame_events = trace.by_frame(state.frame);
        debugger.record_frame(state, inputs, frame_events);
    }
    std::cout << debugger.export_reproducible_json(0xDC0E0001U, "UNQUALIFIED-DEMO");
    return EXIT_SUCCESS;
}
