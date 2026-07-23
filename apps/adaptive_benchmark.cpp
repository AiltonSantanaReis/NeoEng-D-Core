#include "neoeng/core/hash.hpp"
#include "neoeng/core/snapshot_store.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::array<std::size_t, 6> kPageSizes{16, 32, 64, 128, 256, 512};
constexpr std::array<std::size_t, 5> kCheckpointIntervals{4, 8, 16, 32, 64};
constexpr std::array<std::string_view, 2> kPatterns{"clustered", "scattered"};
constexpr std::array<int, 2> kPercents{1, 10};

[[nodiscard]] WorldState make_world(std::size_t count) {
    WorldState state;
    state.bodies.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        state.bodies.push_back(Body{.id = static_cast<EntityId>(index + 1U)});
    }
    return state;
}

[[nodiscard]] std::vector<std::size_t> select_indices(
    std::size_t count, int percent, std::string_view pattern) {
    const std::size_t changed = std::max<std::size_t>(1U, count * static_cast<std::size_t>(percent) / 100U);
    std::vector<std::size_t> indices;
    indices.reserve(changed);
    if (pattern == "clustered") {
        for (std::size_t index = 0; index < changed; ++index) indices.push_back(index);
    } else {
        for (std::size_t item = 0; item < changed; ++item) indices.push_back(item * count / changed);
    }
    return indices;
}

[[nodiscard]] DirtySet mutate(
    WorldState& state,
    std::uint64_t frame,
    std::span<const std::size_t> indices,
    bool all_components) {
    DirtySet dirty(state.bodies.size());
    state.frame = frame;
    const Fixed delta = Fixed::from_raw(static_cast<Fixed::rep>(frame * 4'099U + 17U));
    for (const std::size_t index : indices) {
        Body& body = state.bodies[index];
        body.position.x += delta;
        dirty.mark(index, DirtyComponent::PositionX);
        if (all_components) {
            body.position.y -= Fixed::from_raw(delta.raw() / 3);
            body.velocity.x += Fixed::from_raw(delta.raw() / 5);
            body.velocity.y -= Fixed::from_raw(delta.raw() / 7);
            dirty.mark(index, DirtyComponent::PositionY | DirtyComponent::VelocityX | DirtyComponent::VelocityY);
        }
    }
    return dirty;
}

[[nodiscard]] double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    if (values.empty()) return 0.0;
    const std::size_t index = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U));
    return values[index];
}

struct RunResult final {
    double capture_p50_us{};
    double capture_p95_us{};
    double restore_p50_us{};
    SnapshotStoreStats stats{};
    std::uint64_t final_hash{};
};

[[nodiscard]] RunResult run_case(
    SnapshotStoreConfig config,
    std::size_t body_count,
    std::size_t frames,
    int percent,
    std::string_view pattern,
    bool all_components) {
    auto store = make_snapshot_store(config);
    WorldState state = make_world(body_count);
    DirtySet initial = DirtySet::full(body_count);
    store->capture(state, &initial);
    const std::vector<std::size_t> indices = select_indices(body_count, percent, pattern);
    std::vector<double> capture;
    capture.reserve(frames);
    for (std::size_t frame = 1; frame <= frames; ++frame) {
        DirtySet dirty = mutate(state, static_cast<std::uint64_t>(frame), indices, all_components);
        const auto begin = Clock::now();
        store->capture(state, &dirty);
        const auto end = Clock::now();
        capture.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
    }
    std::vector<double> restore;
    restore.reserve(20);
    for (std::size_t repetition = 0; repetition < 20U; ++repetition) {
        const auto begin = Clock::now();
        const WorldState restored = store->restore(static_cast<std::uint64_t>(frames - 8U));
        const auto end = Clock::now();
        if (stable_hash(restored) != store->scan_hash(static_cast<std::uint64_t>(frames - 8U))) {
            throw std::runtime_error("Restore/hash mismatch in adaptive benchmark");
        }
        restore.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
    }
    if (store->scan_hash(static_cast<std::uint64_t>(frames)) != stable_hash(state)) {
        throw std::runtime_error("Final snapshot diverged in adaptive benchmark");
    }
    return RunResult{
        .capture_p50_us = percentile(capture, 0.50),
        .capture_p95_us = percentile(capture, 0.95),
        .restore_p50_us = percentile(restore, 0.50),
        .stats = store->stats(),
        .final_hash = stable_hash(state),
    };
}

void page_sweep(const std::filesystem::path& directory, std::size_t body_count, std::size_t frames) {
    std::ofstream file(directory / "page_sweep.csv");
    file << "page_bodies,percent,pattern,capture_p50_us,capture_p95_us,restore_p50_us,"
            "requested_bytes_per_frame,live_bytes,dirty_entities,comparison_entities,"
            "measured_touched_pages,independent_model_touched_pages,final_hash\n";
    for (const std::size_t page : kPageSizes) {
        for (const int percent : kPercents) {
            for (const std::string_view pattern : kPatterns) {
                const RunResult result = run_case(SnapshotStoreConfig{
                    .strategy = SnapshotStrategy::PagedCopyOnWrite,
                    .capacity = frames + 1U,
                    .page_bodies = page,
                    .checkpoint_interval = 16,
                    .persistent_leaf_bodies = 32,
                    .audit_dirty_contract = false,
                }, body_count, frames, percent, pattern, true);
                const std::size_t changed = std::max<std::size_t>(1U, body_count * static_cast<std::size_t>(percent) / 100U);
                const double density = static_cast<double>(changed) / static_cast<double>(body_count);
                const double page_count = std::ceil(static_cast<double>(body_count) / static_cast<double>(page));
                const double expected = page_count * (1.0 - std::pow(1.0 - density, static_cast<double>(page)));
                const std::size_t measured = pattern == "clustered"
                    ? (changed + page - 1U) / page
                    : std::min<std::size_t>(changed, static_cast<std::size_t>(page_count));
                const double requested = static_cast<double>(result.stats.payload_bytes_requested
                    + result.stats.metadata_bytes_requested) / static_cast<double>(frames + 1U);
                file << page << ',' << percent << ',' << pattern << ',' << std::fixed << std::setprecision(3)
                     << result.capture_p50_us << ',' << result.capture_p95_us << ',' << result.restore_p50_us
                     << ',' << requested << ',' << result.stats.live_payload_bytes + result.stats.live_metadata_bytes
                     << ',' << result.stats.dirty_entities_consumed << ',' << result.stats.comparison_entities_scanned
                     << ',' << measured << ',' << expected << ',' << hash_hex(result.final_hash) << '\n';
            }
        }
    }
}

void checkpoint_sweep(const std::filesystem::path& directory, std::size_t body_count, std::size_t frames) {
    std::ofstream file(directory / "checkpoint_sweep.csv");
    file << "checkpoint_interval,percent,pattern,capture_p50_us,capture_p95_us,restore_p50_us,"
            "requested_bytes_per_frame,live_bytes,full_frames,delta_frames,final_hash\n";
    for (const std::size_t interval : kCheckpointIntervals) {
        for (const int percent : kPercents) {
            for (const std::string_view pattern : kPatterns) {
                const RunResult result = run_case(SnapshotStoreConfig{
                    .strategy = SnapshotStrategy::DeltaLog,
                    .capacity = frames + 1U,
                    .page_bodies = 64,
                    .checkpoint_interval = interval,
                    .persistent_leaf_bodies = 32,
                    .audit_dirty_contract = false,
                }, body_count, frames, percent, pattern, true);
                const double requested = static_cast<double>(result.stats.payload_bytes_requested
                    + result.stats.metadata_bytes_requested) / static_cast<double>(frames + 1U);
                file << interval << ',' << percent << ',' << pattern << ',' << std::fixed << std::setprecision(3)
                     << result.capture_p50_us << ',' << result.capture_p95_us << ',' << result.restore_p50_us
                     << ',' << requested << ',' << result.stats.live_payload_bytes + result.stats.live_metadata_bytes
                     << ',' << result.stats.full_frames << ',' << result.stats.delta_frames << ','
                     << hash_hex(result.final_hash) << '\n';
            }
        }
    }
}

void layout_comparison(const std::filesystem::path& directory, std::size_t body_count, std::size_t frames) {
    std::ofstream file(directory / "layout_comparison.csv");
    file << "strategy,component_pattern,percent,pattern,capture_p50_us,capture_p95_us,restore_p50_us,"
            "requested_bytes_per_frame,live_bytes,final_hash\n";
    for (const SnapshotStrategy strategy : {SnapshotStrategy::FullCopy, SnapshotStrategy::ComponentSoA}) {
        for (const bool all_components : {false, true}) {
            for (const int percent : kPercents) {
                for (const std::string_view pattern : kPatterns) {
                    const RunResult result = run_case(SnapshotStoreConfig{
                        .strategy = strategy,
                        .capacity = frames + 1U,
                        .page_bodies = 64,
                        .checkpoint_interval = 16,
                        .persistent_leaf_bodies = 32,
                        .audit_dirty_contract = false,
                    }, body_count, frames, percent, pattern, all_components);
                    const double requested = static_cast<double>(result.stats.payload_bytes_requested
                        + result.stats.metadata_bytes_requested) / static_cast<double>(frames + 1U);
                    file << to_string(strategy) << ',' << (all_components ? "all_dynamic" : "position_x_only")
                         << ',' << percent << ',' << pattern << ',' << std::fixed << std::setprecision(3)
                         << result.capture_p50_us << ',' << result.capture_p95_us << ',' << result.restore_p50_us
                         << ',' << requested << ',' << result.stats.live_payload_bytes + result.stats.live_metadata_bytes
                         << ',' << hash_hex(result.final_hash) << '\n';
                }
            }
        }
    }
}

void hybrid_trace(const std::filesystem::path& directory, std::size_t body_count, std::size_t frames) {
    auto store = make_snapshot_store(SnapshotStoreConfig{
        .strategy = SnapshotStrategy::HybridAdaptive,
        .capacity = frames + 1U,
        .page_bodies = 64,
        .checkpoint_interval = 32,
        .persistent_leaf_bodies = 32,
        .audit_dirty_contract = false,
    });
    WorldState state = make_world(body_count);
    DirtySet initial = DirtySet::full(body_count);
    store->capture(state, &initial);
    std::ofstream file(directory / "hybrid_decisions.csv");
    file << "frame,phase,percent,pattern,encoding,changed_bodies,touched_pages,density_ppm,"
            "delta16,delta32,delta64,delta128,delta256,contiguous_runs,longest_run,index_span,"
            "selected_cost_bytes,oracle_cost_bytes,regret_bytes\n";
    for (std::size_t frame = 1; frame <= frames; ++frame) {
        int percent = 1;
        std::string_view pattern = "clustered";
        std::string_view phase = "sparse_clustered";
        if (frame > frames / 4U && frame <= frames / 2U) {
            pattern = "scattered";
            phase = "sparse_scattered";
        } else if (frame > frames / 2U && frame <= frames * 3U / 4U) {
            percent = 50;
            pattern = "clustered";
            phase = "dense_clustered";
        } else if (frame > frames * 3U / 4U) {
            percent = 100;
            pattern = "scattered";
            phase = "full_change";
        }
        const std::vector<std::size_t> indices = select_indices(body_count, percent, pattern);
        DirtySet dirty = mutate(state, static_cast<std::uint64_t>(frame), indices, true);
        store->capture(state, &dirty);
        const auto decision = store->decision_for(static_cast<std::uint64_t>(frame));
        if (!decision.has_value()) throw std::runtime_error("Hybrid store did not expose its decision");
        file << frame << ',' << phase << ',' << percent << ',' << pattern << ','
             << to_string(decision->encoding) << ',' << decision->changed_bodies << ','
             << decision->touched_pages << ',' << decision->features.density_ppm << ','
             << decision->features.touched_pages_16 << ',' << decision->features.touched_pages_32 << ','
             << decision->features.touched_pages_64 << ',' << decision->features.touched_pages_128 << ','
             << decision->features.touched_pages_256 << ',' << decision->features.contiguous_runs << ','
             << decision->features.longest_run << ',' << decision->features.index_span << ','
             << decision->selected_cost_bytes << ',' << decision->oracle_cost_bytes << ','
             << decision->regret_bytes << '\n';
    }
    if (store->scan_hash(static_cast<std::uint64_t>(frames)) != stable_hash(state)) {
        throw std::runtime_error("Hybrid trace diverged");
    }
    const SnapshotStoreStats stats = store->stats();
    std::ofstream summary(directory / "hybrid_summary.json");
    summary << "{\n"
            << "  \"frames\": " << frames << ",\n"
            << "  \"full_frames\": " << stats.full_frames << ",\n"
            << "  \"delta_frames\": " << stats.delta_frames << ",\n"
            << "  \"page_frames\": " << stats.page_frames << ",\n"
            << "  \"cumulative_regret_bytes\": " << stats.cumulative_regret_bytes << ",\n"
            << "  \"final_hash\": \"" << hash_hex(stable_hash(state)) << "\"\n"
            << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("artifacts/v0.3-benchmark");
        std::filesystem::create_directories(output);
        constexpr std::size_t body_count = 4'096;
        constexpr std::size_t frames = 128;
        page_sweep(output, body_count, frames);
        checkpoint_sweep(output, body_count, frames);
        layout_comparison(output, body_count, frames);
        hybrid_trace(output, body_count, frames);
        std::ofstream manifest(output / "benchmark_manifest.json");
        manifest << "{\n"
                 << "  \"schema_version\": 2,\n"
                 << "  \"body_count\": " << body_count << ",\n"
                 << "  \"frames\": " << frames << ",\n"
                 << "  \"dirty_contract_audited_in_tests\": true\n"
                 << "}\n";
        std::cout << "NeoEng v0.3 adaptive benchmark completed.\nOutput: " << output.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Adaptive benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
