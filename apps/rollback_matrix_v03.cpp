#include "neoeng/core/hash.hpp"
#include "neoeng/core/rollback.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

namespace {

struct StrategyCase final {
    SnapshotStrategy strategy;
    std::size_t page_bodies;
    std::size_t checkpoint_interval;
    std::size_t leaf_bodies;
};

constexpr std::array<StrategyCase, 6> kCases{{
    {SnapshotStrategy::FullCopy, 16, 64, 16},
    {SnapshotStrategy::DeltaLog, 16, 64, 16},
    {SnapshotStrategy::PagedCopyOnWrite, 16, 64, 16},
    {SnapshotStrategy::PersistentChunkTree, 16, 64, 16},
    {SnapshotStrategy::ComponentSoA, 16, 64, 16},
    {SnapshotStrategy::HybridAdaptive, 16, 32, 16},
}};

[[nodiscard]] WorldState make_world(std::size_t count) {
    WorldState state;
    state.bodies.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        state.bodies.push_back(Body{.id = static_cast<EntityId>(index + 1U)});
    }
    return state;
}

[[nodiscard]] double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U));
    return values[index];
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("artifacts/v0.3-rollback-matrix.csv");
        std::filesystem::create_directories(output.parent_path());
        std::ofstream file(output);
        if (!file) throw std::runtime_error("Unable to create rollback matrix output");
        file << "strategy,body_count,measured_frames,frame_p50_ms,frame_p95_ms,rollback_depth,"
                "rollback_ms,live_snapshot_bytes,requested_snapshot_bytes,dirty_entities,"
                "comparison_entities,full_frames,delta_frames,page_frames,soa_frames,regret_bytes,final_hash\n";

        constexpr std::size_t body_count = 10'000;
        constexpr std::size_t warmup = 30;
        constexpr std::size_t measured = 200;
        constexpr std::size_t rollback_depth = 8;
        const std::vector<InputCommand> inputs{
            {.entity = 1, .acceleration = {Fixed::from_integer(1), Fixed::from_integer(-1)}},
            {.entity = static_cast<EntityId>(body_count),
             .acceleration = {Fixed::from_ratio(-1, 2), Fixed::from_ratio(1, 3)}},
        };
        std::vector<InputCommand> corrected = inputs;
        corrected.front().acceleration.x += Fixed::from_ratio(1, 7);

        for (const StrategyCase& item : kCases) {
            RollbackEngine engine(make_world(body_count), SnapshotStoreConfig{
                .strategy = item.strategy,
                .capacity = 300,
                .page_bodies = item.page_bodies,
                .checkpoint_interval = item.checkpoint_interval,
                .persistent_leaf_bodies = item.leaf_bodies,
                .audit_dirty_contract = false,
            });
            for (std::size_t frame = 0; frame < warmup; ++frame) engine.advance(inputs);
            std::vector<double> frame_times;
            frame_times.reserve(measured);
            for (std::size_t frame = 0; frame < measured; ++frame) {
                const auto begin = Clock::now();
                engine.advance(inputs);
                const auto end = Clock::now();
                frame_times.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
            }
            const std::uint64_t correction_frame = engine.state().frame - rollback_depth;
            const auto rollback_begin = Clock::now();
            const std::size_t resimulated = engine.correct_input_and_resimulate(correction_frame, corrected);
            const auto rollback_end = Clock::now();
            if (resimulated != rollback_depth) throw std::runtime_error("Unexpected rollback depth");
            const double rollback_ms = std::chrono::duration<double, std::milli>(
                rollback_end - rollback_begin).count();
            const SnapshotStoreStats stats = engine.snapshots().stats();
            file << to_string(item.strategy) << ',' << body_count << ',' << measured << ','
                 << std::fixed << std::setprecision(6) << percentile(frame_times, 0.50) << ','
                 << percentile(frame_times, 0.95) << ',' << rollback_depth << ',' << rollback_ms << ','
                 << stats.live_payload_bytes + stats.live_metadata_bytes << ','
                 << stats.payload_bytes_requested + stats.metadata_bytes_requested << ','
                 << stats.dirty_entities_consumed << ',' << stats.comparison_entities_scanned << ','
                 << stats.full_frames << ',' << stats.delta_frames << ',' << stats.page_frames << ','
                 << stats.soa_frames << ',' << stats.cumulative_regret_bytes << ','
                 << hash_hex(stable_hash(engine.state())) << '\n';
        }
        std::cout << "NeoEng v0.3 rollback matrix completed.\nOutput: " << output.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Rollback matrix failed: " << error.what() << '\n';
        return 1;
    }
}
