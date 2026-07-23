#include "neoeng/core/hash.hpp"
#include "neoeng/core/rollback.hpp"
#include "neoeng/core/snapshot_store.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

namespace {

constexpr std::array<SnapshotStrategy, 4> kStrategies{
    SnapshotStrategy::FullCopy,
    SnapshotStrategy::DeltaLog,
    SnapshotStrategy::PagedCopyOnWrite,
    SnapshotStrategy::PersistentChunkTree,
};
constexpr std::array<int, 4> kMutationPercentages{1, 10, 50, 100};
constexpr std::array<std::uint64_t, 6> kRollbackDepths{1, 2, 4, 8, 16, 32};
constexpr std::array<std::string_view, 2> kPatterns{"clustered", "scattered"};

[[nodiscard]] WorldState make_world(std::uint32_t body_count) {
    std::vector<Body> bodies;
    bodies.reserve(body_count);
    for (std::uint32_t id = 1; id <= body_count; ++id) {
        bodies.push_back(Body{.id = id});
    }
    return WorldState{.frame = 0, .bodies = std::move(bodies)};
}

[[nodiscard]] std::vector<std::size_t> selected_indices(
    std::size_t body_count, int percentage, std::string_view pattern) {
    const std::size_t changed_count = std::max<std::size_t>(
        1U, body_count * static_cast<std::size_t>(percentage) / 100U);
    std::vector<std::size_t> indices;
    indices.reserve(changed_count);
    if (pattern == "clustered") {
        for (std::size_t index = 0; index < changed_count; ++index) {
            indices.push_back(index);
        }
    } else {
        for (std::size_t item = 0; item < changed_count; ++item) {
            indices.push_back(item * body_count / changed_count);
        }
    }
    return indices;
}

void mutate_for_snapshot(
    WorldState& state, std::uint64_t frame, std::span<const std::size_t> indices) {
    state.frame = frame;
    const Fixed dx = Fixed::from_raw(static_cast<Fixed::rep>(frame * 1'009U + 17U));
    const Fixed dv = Fixed::from_raw(static_cast<Fixed::rep>(frame * 313U + 5U));
    for (const std::size_t index : indices) {
        Body& body = state.bodies[index];
        body.position.x += dx;
        body.position.y -= Fixed::from_raw(dx.raw() / 3);
        body.velocity.x += dv;
    }
}

[[nodiscard]] std::vector<InputCommand> make_inputs(
    std::size_t body_count, int percentage, std::string_view pattern, Fixed acceleration) {
    const std::vector<std::size_t> indices = selected_indices(body_count, percentage, pattern);
    std::vector<InputCommand> inputs;
    inputs.reserve(indices.size());
    for (const std::size_t index : indices) {
        inputs.push_back(InputCommand{
            .entity = static_cast<EntityId>(index + 1U),
            .acceleration = {acceleration, Fixed::from_raw(-acceleration.raw() / 2)},
        });
    }
    return inputs;
}

[[nodiscard]] double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        p * static_cast<double>(values.size() - 1U));
    return values[index];
}

[[nodiscard]] double elapsed_us(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::micro>(end - start).count();
}

[[nodiscard]] double elapsed_ms(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void mix_hash(std::uint64_t& accumulator, std::uint64_t value) noexcept {
    constexpr std::uint64_t prime = 1'099'511'628'211ULL;
    accumulator ^= value;
    accumulator *= prime;
}

struct CaptureResult final {
    std::uint64_t hash{};
    std::uint64_t sink{};
};

CaptureResult run_snapshot_matrix(
    const std::filesystem::path& output_directory,
    std::uint32_t body_count,
    std::size_t frame_count,
    std::size_t capacity) {
    std::ofstream capture_file(output_directory / "snapshot_capture.csv");
    std::ofstream restore_file(output_directory / "snapshot_restore.csv");
    if (!capture_file || !restore_file) {
        throw std::runtime_error("Unable to create snapshot benchmark output files");
    }

    capture_file
        << "strategy,mutation_percent,pattern,body_count,frames,capture_p50_us,capture_p95_us,"
           "capture_p99_us,lookup_ns_per_op,scan_ms,requested_bytes_per_frame,live_payload_bytes,"
           "live_metadata_bytes,peak_live_payload_bytes,peak_live_metadata_bytes,allocation_count,"
           "final_hash\n";
    restore_file
        << "strategy,mutation_percent,pattern,body_count,depth,restore_p50_us,restore_p95_us,"
           "restored_hash\n";

    std::uint64_t combined_hash = 14'695'981'039'346'656'037ULL;
    std::uint64_t sink = 14'695'981'039'346'656'037ULL;

    for (const SnapshotStrategy strategy : kStrategies) {
        for (const int percentage : kMutationPercentages) {
            for (const std::string_view pattern : kPatterns) {
                auto store = make_snapshot_store(strategy, capacity);
                WorldState world = make_world(body_count);
                const std::vector<std::size_t> indices =
                    selected_indices(body_count, percentage, pattern);
                store->capture(world);
                const SnapshotStoreStats initial_stats = store->stats();

                std::vector<double> capture_us;
                capture_us.reserve(frame_count);
                for (std::size_t frame = 1; frame <= frame_count; ++frame) {
                    mutate_for_snapshot(world, static_cast<std::uint64_t>(frame), indices);
                    const auto start = Clock::now();
                    store->capture(world);
                    const auto end = Clock::now();
                    capture_us.push_back(elapsed_us(start, end));
                }

                constexpr std::size_t lookup_repetitions = 20;
                const auto lookup_start = Clock::now();
                for (std::size_t repetition = 0; repetition < lookup_repetitions; ++repetition) {
                    for (std::size_t index = 0; index < body_count; index += 17U) {
                        const auto body = store->lookup(
                            static_cast<std::uint64_t>(frame_count),
                            static_cast<EntityId>(index + 1U));
                        if (body.has_value()) {
                            mix_hash(sink, static_cast<std::uint64_t>(body->position.x.raw()));
                        }
                    }
                }
                const auto lookup_end = Clock::now();
                const std::size_t lookup_operations = lookup_repetitions
                    * ((static_cast<std::size_t>(body_count) + 16U) / 17U);
                const double lookup_ns = elapsed_us(lookup_start, lookup_end) * 1'000.0
                    / static_cast<double>(lookup_operations);

                constexpr std::size_t scan_repetitions = 10;
                const auto scan_start = Clock::now();
                for (std::size_t repetition = 0; repetition < scan_repetitions; ++repetition) {
                    mix_hash(sink, store->scan_hash(static_cast<std::uint64_t>(frame_count)));
                }
                const auto scan_end = Clock::now();
                const double scan_ms = elapsed_ms(scan_start, scan_end)
                    / static_cast<double>(scan_repetitions);

                const SnapshotStoreStats stats = store->stats();
                const std::uint64_t requested_delta =
                    (stats.payload_bytes_requested - initial_stats.payload_bytes_requested)
                    + (stats.metadata_bytes_requested - initial_stats.metadata_bytes_requested);
                const double requested_per_frame = static_cast<double>(requested_delta)
                    / static_cast<double>(frame_count);
                const std::uint64_t final_hash = stable_hash(world);
                if (store->scan_hash(static_cast<std::uint64_t>(frame_count)) != final_hash) {
                    throw std::runtime_error("Snapshot scan hash diverged from canonical world hash");
                }
                mix_hash(combined_hash, final_hash);

                capture_file << to_string(strategy) << ',' << percentage << ',' << pattern << ','
                    << body_count << ',' << frame_count << ',' << std::fixed << std::setprecision(3)
                    << percentile(capture_us, 0.50) << ',' << percentile(capture_us, 0.95) << ','
                    << percentile(capture_us, 0.99) << ',' << lookup_ns << ',' << scan_ms << ','
                    << requested_per_frame << ',' << stats.live_payload_bytes << ','
                    << stats.live_metadata_bytes << ',' << stats.peak_live_payload_bytes << ','
                    << stats.peak_live_metadata_bytes << ',' << stats.allocation_count << ','
                    << hash_hex(final_hash) << '\n';

                for (const std::uint64_t depth : kRollbackDepths) {
                    std::vector<double> restore_us;
                    constexpr std::size_t restore_repetitions = 15;
                    restore_us.reserve(restore_repetitions);
                    const std::uint64_t target_frame =
                        static_cast<std::uint64_t>(frame_count) - depth;
                    std::uint64_t restored_hash = 0;
                    for (std::size_t repetition = 0; repetition < restore_repetitions; ++repetition) {
                        const auto start = Clock::now();
                        const WorldState restored = store->restore(target_frame);
                        const auto end = Clock::now();
                        restore_us.push_back(elapsed_us(start, end));
                        restored_hash ^= stable_hash(restored);
                    }
                    mix_hash(sink, restored_hash);
                    restore_file << to_string(strategy) << ',' << percentage << ',' << pattern << ','
                        << body_count << ',' << depth << ',' << std::fixed << std::setprecision(3)
                        << percentile(restore_us, 0.50) << ',' << percentile(restore_us, 0.95) << ','
                        << hash_hex(store->scan_hash(target_frame)) << '\n';
                }
            }
        }
    }
    return CaptureResult{.hash = combined_hash, .sink = sink};
}

std::uint64_t run_rollback_matrix(
    const std::filesystem::path& output_directory, std::uint32_t body_count) {
    std::ofstream rollback_file(output_directory / "rollback_resimulation.csv");
    if (!rollback_file) {
        throw std::runtime_error("Unable to create rollback benchmark output file");
    }
    rollback_file
        << "strategy,mutation_percent,pattern,body_count,rollback_depth,rollback_ms,"
           "resimulated_frames,final_hash\n";

    std::uint64_t combined_hash = 14'695'981'039'346'656'037ULL;
    constexpr std::uint64_t total_frames = 64;
    for (const SnapshotStrategy strategy : kStrategies) {
        for (const int percentage : kMutationPercentages) {
            for (const std::string_view pattern : kPatterns) {
                const std::vector<InputCommand> normal = make_inputs(
                    body_count, percentage, pattern, Fixed::from_ratio(3, 2));
                std::vector<InputCommand> corrected = normal;
                if (!corrected.empty()) {
                    corrected.front().acceleration.x += Fixed::from_ratio(1, 7);
                }

                for (const std::uint64_t depth : kRollbackDepths) {
                    RollbackEngine engine(make_world(body_count), 300, strategy);
                    for (std::uint64_t frame = 0; frame < total_frames; ++frame) {
                        engine.advance(normal);
                    }
                    const std::uint64_t correction_frame = total_frames - depth;
                    const auto start = Clock::now();
                    const std::size_t resimulated = engine.correct_input_and_resimulate(
                        correction_frame, corrected);
                    const auto end = Clock::now();
                    const std::uint64_t final_hash = stable_hash(engine.state());
                    mix_hash(combined_hash, final_hash);
                    rollback_file << to_string(strategy) << ',' << percentage << ',' << pattern
                        << ',' << body_count << ',' << depth << ',' << std::fixed
                        << std::setprecision(6) << elapsed_ms(start, end) << ',' << resimulated
                        << ',' << hash_hex(final_hash) << '\n';
                }
            }
        }
    }
    return combined_hash;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output_directory = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("artifacts/v0.2-benchmark");
        std::filesystem::create_directories(output_directory);

        constexpr std::uint32_t snapshot_body_count = 4'096;
        constexpr std::size_t snapshot_frames = 300;
        constexpr std::size_t snapshot_capacity = 301;
        constexpr std::uint32_t rollback_body_count = 2'048;

        const CaptureResult capture = run_snapshot_matrix(
            output_directory, snapshot_body_count, snapshot_frames, snapshot_capacity);
        const std::uint64_t rollback_hash = run_rollback_matrix(
            output_directory, rollback_body_count);

        std::ofstream manifest(output_directory / "benchmark_manifest.json");
        manifest << "{\n"
                 << "  \"schema_version\": 1,\n"
                 << "  \"snapshot_body_count\": " << snapshot_body_count << ",\n"
                 << "  \"snapshot_frames\": " << snapshot_frames << ",\n"
                 << "  \"snapshot_capacity\": " << snapshot_capacity << ",\n"
                 << "  \"rollback_body_count\": " << rollback_body_count << ",\n"
                 << "  \"capture_combined_hash\": \"" << hash_hex(capture.hash) << "\",\n"
                 << "  \"rollback_combined_hash\": \"" << hash_hex(rollback_hash) << "\",\n"
                 << "  \"optimization_sink\": \"" << hash_hex(capture.sink) << "\"\n"
                 << "}\n";

        std::cout << "NeoEng v0.2 snapshot benchmark completed.\n"
                  << "Output: " << output_directory.string() << '\n'
                  << "Capture matrix hash: " << hash_hex(capture.hash) << '\n'
                  << "Rollback matrix hash: " << hash_hex(rollback_hash) << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
