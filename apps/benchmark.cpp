#include "neoeng/core/hash.hpp"
#include "neoeng/core/rollback.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

int main() {
    constexpr std::uint32_t body_count = 10'000;
    constexpr int warmup_frames = 30;
    constexpr int measured_frames = 300;

    std::vector<Body> bodies;
    bodies.reserve(body_count);
    for (std::uint32_t id = 1; id <= body_count; ++id) {
        bodies.push_back(Body{.id = id});
    }

    RollbackEngine engine(WorldState{.frame = 0, .bodies = std::move(bodies)});
    const std::vector<InputCommand> inputs{
        InputCommand{.entity = 1, .acceleration = {Fixed::from_integer(1), {}}},
        InputCommand{.entity = body_count, .acceleration = {{}, Fixed::from_integer(-1)}}
    };

    for (int i = 0; i < warmup_frames; ++i) {
        engine.advance(inputs);
    }

    std::vector<double> frame_ms;
    frame_ms.reserve(measured_frames);
    for (int i = 0; i < measured_frames; ++i) {
        const auto start = Clock::now();
        engine.advance(inputs);
        const auto end = Clock::now();
        frame_ms.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    const std::uint64_t correction_frame = engine.state().frame - 8U;
    const std::vector<InputCommand> corrected{
        InputCommand{.entity = 1, .acceleration = {Fixed::from_integer(2), {}}}
    };
    const auto rollback_start = Clock::now();
    const std::size_t resimulated = engine.correct_input_and_resimulate(correction_frame, corrected);
    const auto rollback_end = Clock::now();
    const double rollback_ms = std::chrono::duration<double, std::milli>(
        rollback_end - rollback_start).count();

    std::sort(frame_ms.begin(), frame_ms.end());
    const auto percentile = [&frame_ms](double p) {
        const auto index = static_cast<std::size_t>(p * static_cast<double>(frame_ms.size() - 1U));
        return frame_ms[index];
    };

    std::cout << std::fixed << std::setprecision(6)
              << "{\n"
              << "  \"body_count\": " << body_count << ",\n"
              << "  \"measured_frames\": " << measured_frames << ",\n"
              << "  \"frame_p50_ms\": " << percentile(0.50) << ",\n"
              << "  \"frame_p95_ms\": " << percentile(0.95) << ",\n"
              << "  \"frame_p99_ms\": " << percentile(0.99) << ",\n"
              << "  \"rollback_frames\": " << resimulated << ",\n"
              << "  \"rollback_ms\": " << rollback_ms << ",\n"
              << "  \"final_hash\": \"" << hash_hex(stable_hash(engine.state())) << "\"\n"
              << "}\n";
    return 0;
}
