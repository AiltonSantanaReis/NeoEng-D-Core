#include "neoeng/core/broadphase.hpp"
#include "neoeng/core/component_world.hpp"
#include "neoeng/core/epoch_arena.hpp"
#include "neoeng/core/fixed_simd.hpp"
#include "neoeng/core/hash.hpp"
#include "neoeng/core/indexed_ring.hpp"

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
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t elapsed_ns(Clock::time_point begin, Clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
}

[[nodiscard]] double percentile_ms(std::vector<std::uint64_t> values, double probability) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        probability * static_cast<double>(values.size() - 1U));
    return static_cast<double>(values[index]) / 1'000'000.0;
}

[[nodiscard]] double percentile_us(std::vector<std::uint64_t> values, double probability) {
    return percentile_ms(std::move(values), probability) * 1'000.0;
}

[[nodiscard]] std::string local_hash_hex(std::uint64_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(16)
           << std::setfill('0') << value;
    return stream.str();
}

[[nodiscard]] WorldState make_dense_world(std::size_t count) {
    WorldState state;
    state.bodies.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        state.bodies.push_back(Body{
            .id = static_cast<EntityId>(index + 1U),
            .position = {
                Fixed::from_ratio(static_cast<Fixed::rep>(index % 257U), 17),
                Fixed::from_ratio(static_cast<Fixed::rep>(index % 131U), 19)},
            .velocity = {
                Fixed::from_ratio(static_cast<Fixed::rep>((index % 7U) + 1U), 31),
                Fixed::from_ratio(-static_cast<Fixed::rep>((index % 5U) + 1U), 37)},
        });
    }
    return state;
}

struct RollbackRecord final {
    std::size_t page_size{};
    std::string kernel;
    double p50_ms{};
    double p95_ms{};
    std::uint64_t body_reconstructions{};
    std::uint64_t scalar_lanes{};
    std::uint64_t simd_lanes{};
    std::uint64_t final_hash{};
};

[[nodiscard]] RollbackRecord benchmark_dense_rollback(
    const WorldState& initial,
    std::size_t page_size,
    std::string kernel,
    bool legacy,
    FixedKernelMode mode) {
    ComponentWorldState base = make_component_world(initial, page_size);
    DeterministicActiveSet active = DeterministicActiveSet::from_world(initial);
    for (std::size_t frame = 0U; frame < 16U; ++frame) {
        ComponentStepResult result = legacy
            ? step_component_active_legacy(base, active, {})
            : step_component_active(base, active, {}, ComponentStepOptions{.kernel_mode = mode});
        base = std::move(result.state);
        active = std::move(result.active);
    }

    std::vector<std::uint64_t> samples;
    samples.reserve(80U);
    ComponentAllocationStats cumulative;
    for (std::size_t trial = 0U; trial < 80U; ++trial) {
        ComponentWorldState state = base;
        DeterministicActiveSet trial_active = active;
        ComponentAllocationStats trial_stats;
        const auto begin = Clock::now();
        for (std::size_t frame = 0U; frame < 8U; ++frame) {
            ComponentStepResult result = legacy
                ? step_component_active_legacy(state, trial_active, {})
                : step_component_active(state, trial_active, {},
                    ComponentStepOptions{.kernel_mode = mode});
            trial_stats += result.allocation;
            state = std::move(result.state);
            trial_active = std::move(result.active);
        }
        const auto end = Clock::now();
        const volatile auto sink = state.position_x_at(0U).raw();
        static_cast<void>(sink);
        samples.push_back(elapsed_ns(begin, end));
        cumulative += trial_stats;
    }

    ComponentWorldState final = base;
    DeterministicActiveSet final_active = active;
    for (std::size_t frame = 0U; frame < 8U; ++frame) {
        ComponentStepResult result = legacy
            ? step_component_active_legacy(final, final_active, {})
            : step_component_active(final, final_active, {},
                ComponentStepOptions{.kernel_mode = mode});
        final = std::move(result.state);
        final_active = std::move(result.active);
    }
    return RollbackRecord{
        .page_size = page_size,
        .kernel = std::move(kernel),
        .p50_ms = percentile_ms(samples, 0.50),
        .p95_ms = percentile_ms(samples, 0.95),
        .body_reconstructions = cumulative.body_reconstructions / 80U,
        .scalar_lanes = cumulative.fixed_kernel.scalar_lanes / 80U,
        .simd_lanes = cumulative.fixed_kernel.simd_lanes / 80U,
        .final_hash = stable_hash(final.materialize()),
    };
}

struct SimdRecord final {
    std::string mode;
    double p50_us{};
    double p95_us{};
    std::uint64_t scalar_lanes{};
    std::uint64_t simd_lanes{};
    std::uint64_t output_hash{};
};

[[nodiscard]] SimdRecord benchmark_simd_kernel(
    std::span<const Fixed> input,
    FixedKernelMode mode,
    std::string label) {
    std::vector<Fixed> output(input.size());
    std::vector<std::uint64_t> samples;
    FixedKernelStats cumulative;
    for (std::size_t trial = 0U; trial < 200U; ++trial) {
        FixedKernelStats trial_stats;
        const auto begin = Clock::now();
        multiply_simulation_delta_exact(input, output, mode, &trial_stats);
        const auto end = Clock::now();
        samples.push_back(elapsed_ns(begin, end));
        cumulative.lanes += trial_stats.lanes;
        cumulative.scalar_lanes += trial_stats.scalar_lanes;
        cumulative.simd_lanes += trial_stats.simd_lanes;
    }
    WorldState hash_state;
    hash_state.bodies.reserve(output.size());
    for (std::size_t index = 0U; index < output.size(); ++index) {
        hash_state.bodies.push_back(Body{
            .id = static_cast<EntityId>(index + 1U),
            .position = {output[index], {}},
        });
    }
    return SimdRecord{
        .mode = std::move(label),
        .p50_us = percentile_us(samples, 0.50),
        .p95_us = percentile_us(samples, 0.95),
        .scalar_lanes = cumulative.scalar_lanes / 200U,
        .simd_lanes = cumulative.simd_lanes / 200U,
        .output_hash = stable_hash(hash_state),
    };
}

struct BroadphaseRecord final {
    std::string pattern;
    std::size_t body_count{};
    std::size_t seeds{};
    std::size_t closure{};
    std::size_t overlap_pairs{};
    double closure_p50_us{};
    double closure_p95_us{};
    std::uint64_t candidate_tests{};
};

[[nodiscard]] BroadphaseRecord benchmark_broadphase(
    std::string pattern,
    WorldState world,
    std::vector<std::size_t> seed_indices,
    Fixed cell_size,
    Fixed half_extent) {
    ComponentWorldState state = make_component_world(world, 64U);
    const GridBroadphaseState grid = make_grid_broadphase(state, cell_size, half_extent);
    const DeterministicActiveSet seeds(std::move(seed_indices));
    std::vector<std::uint64_t> samples;
    IslandClosure final;
    for (std::size_t trial = 0U; trial < 100U; ++trial) {
        const auto begin = Clock::now();
        IslandClosure closure = conservative_island_closure(state, grid, seeds);
        const auto end = Clock::now();
        samples.push_back(elapsed_ns(begin, end));
        final = std::move(closure);
    }
    return BroadphaseRecord{
        .pattern = std::move(pattern),
        .body_count = state.body_count(),
        .seeds = seeds.size(),
        .closure = final.bodies.size(),
        .overlap_pairs = final.overlaps.size(),
        .closure_p50_us = percentile_us(samples, 0.50),
        .closure_p95_us = percentile_us(samples, 0.95),
        .candidate_tests = final.stats.candidate_pairs_tested,
    };
}

void write_rollback_csv(
    const std::filesystem::path& path,
    std::span<const RollbackRecord> records) {
    std::ofstream output(path);
    output << "page_size,kernel,p50_ms,p95_ms,body_reconstructions_per_trial,"
              "scalar_lanes_per_trial,simd_lanes_per_trial,final_hash\n";
    output << std::fixed << std::setprecision(6);
    for (const RollbackRecord& record : records) {
        output << record.page_size << ',' << record.kernel << ',' << record.p50_ms << ','
               << record.p95_ms << ',' << record.body_reconstructions << ','
               << record.scalar_lanes << ',' << record.simd_lanes << ','
               << local_hash_hex(record.final_hash) << '\n';
    }
}

void write_simd_csv(
    const std::filesystem::path& path,
    std::span<const SimdRecord> records) {
    std::ofstream output(path);
    output << "mode,p50_us,p95_us,scalar_lanes,simd_lanes,output_hash\n";
    output << std::fixed << std::setprecision(6);
    for (const SimdRecord& record : records) {
        output << record.mode << ',' << record.p50_us << ',' << record.p95_us << ','
               << record.scalar_lanes << ',' << record.simd_lanes << ','
               << local_hash_hex(record.output_hash) << '\n';
    }
}

void write_broadphase_csv(
    const std::filesystem::path& path,
    std::span<const BroadphaseRecord> records) {
    std::ofstream output(path);
    output << "pattern,body_count,seeds,closure,overlap_pairs,p50_us,p95_us,candidate_tests\n";
    output << std::fixed << std::setprecision(6);
    for (const BroadphaseRecord& record : records) {
        output << record.pattern << ',' << record.body_count << ',' << record.seeds << ','
               << record.closure << ',' << record.overlap_pairs << ','
               << record.closure_p50_us << ',' << record.closure_p95_us << ','
               << record.candidate_tests << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output_directory = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("artifacts/v0.6-benchmark");
    std::filesystem::create_directories(output_directory);

    const WorldState dense = make_dense_world(10'000U);
    std::vector<RollbackRecord> rollback_records;
    for (const std::size_t page_size : {32U, 64U, 128U, 256U}) {
        rollback_records.push_back(benchmark_dense_rollback(
            dense, page_size, "legacy_scalar", true, FixedKernelMode::Scalar));
        rollback_records.push_back(benchmark_dense_rollback(
            dense, page_size, "direct_scalar", false, FixedKernelMode::Scalar));
        rollback_records.push_back(benchmark_dense_rollback(
            dense, page_size, "direct_auto", false, FixedKernelMode::Auto));
    }
    write_rollback_csv(output_directory / "dense_rollback.csv", rollback_records);

    std::vector<Fixed> kernel_input(100'000U);
    for (std::size_t index = 0U; index < kernel_input.size(); ++index) {
        const std::int64_t signed_value = static_cast<std::int64_t>(index % 20'001U) - 10'000LL;
        kernel_input[index] = Fixed::from_ratio(signed_value, 17);
    }
    const std::array<SimdRecord, 2U> simd_records{
        benchmark_simd_kernel(kernel_input, FixedKernelMode::Scalar, "scalar"),
        benchmark_simd_kernel(kernel_input, FixedKernelMode::Auto, "auto"),
    };
    write_simd_csv(output_directory / "simd_kernel.csv", simd_records);

    WorldState separated = make_dense_world(10'000U);
    for (std::size_t index = 0U; index < separated.bodies.size(); ++index) {
        separated.bodies[index].position.x = Fixed::from_integer(
            static_cast<Fixed::rep>((index % 100U) * 4U));
        separated.bodies[index].position.y = Fixed::from_integer(
            static_cast<Fixed::rep>((index / 100U) * 4U));
        separated.bodies[index].velocity = {};
    }
    WorldState chain = make_dense_world(1'000U);
    for (std::size_t index = 0U; index < chain.bodies.size(); ++index) {
        chain.bodies[index].position.x = Fixed::from_ratio(
            static_cast<Fixed::rep>(index * 3U), 2);
        chain.bodies[index].position.y = {};
        chain.bodies[index].velocity = {};
    }
    const std::array<BroadphaseRecord, 2U> broadphase_records{
        benchmark_broadphase("separated", std::move(separated), {0U, 9'999U},
            Fixed::from_integer(2), Fixed::from_ratio(1, 2)),
        benchmark_broadphase("contact_chain", std::move(chain), {0U},
            Fixed::from_integer(2), Fixed::from_integer(1)),
    };
    write_broadphase_csv(output_directory / "broadphase.csv", broadphase_records);

    IndexedFrameRing<std::uint64_t> ring(300U);
    for (std::uint64_t frame = 0U; frame < 300U; ++frame) ring.capture(frame, frame * 17U);
    std::uint64_t ring_sink = 0U;
    const auto ring_begin = Clock::now();
    for (std::size_t repeat = 0U; repeat < 1'000'000U; ++repeat) {
        ring_sink ^= ring.at(static_cast<std::uint64_t>(repeat % 300U));
    }
    const auto ring_end = Clock::now();

    PersistentEpochArena arena(300U, 64U * 1024U);
    const auto arena_begin = Clock::now();
    for (std::uint64_t frame = 0U; frame < 1'000U; ++frame) {
        arena.begin_epoch(frame);
        for (std::size_t block = 0U; block < 64U; ++block) {
            auto values = arena.allocate_array<std::uint64_t>(64U);
            values[0] = frame + block;
            ring_sink ^= values[0];
        }
    }
    const auto arena_end = Clock::now();
    const EpochArenaStats arena_stats = arena.stats();

    std::ofstream infrastructure(output_directory / "infrastructure.json");
    infrastructure << "{\n"
        << "  \"indexed_ring_lookup_ns_total\": " << elapsed_ns(ring_begin, ring_end) << ",\n"
        << "  \"indexed_ring_lookups\": 1000000,\n"
        << "  \"arena_ns_total\": " << elapsed_ns(arena_begin, arena_end) << ",\n"
        << "  \"arena_allocations\": " << arena_stats.allocations << ",\n"
        << "  \"arena_bytes_requested\": " << arena_stats.bytes_requested << ",\n"
        << "  \"arena_bytes_committed\": " << arena_stats.bytes_committed << ",\n"
        << "  \"arena_epochs_reclaimed\": " << arena_stats.epochs_reclaimed << ",\n"
        << "  \"arena_overflow_blocks\": " << arena_stats.overflow_blocks << ",\n"
        << "  \"simd_available\": " << (fixed_simd_available() ? "true" : "false") << ",\n"
        << "  \"sink\": " << ring_sink << "\n"
        << "}\n";

    const auto best = std::min_element(rollback_records.begin(), rollback_records.end(),
        [](const RollbackRecord& lhs, const RollbackRecord& rhs) {
            return lhs.p95_ms < rhs.p95_ms;
        });
    std::ofstream summary(output_directory / "summary.json");
    summary << std::fixed << std::setprecision(6)
        << "{\n"
        << "  \"best_kernel\": \"" << best->kernel << "\",\n"
        << "  \"best_page_size\": " << best->page_size << ",\n"
        << "  \"best_p50_ms\": " << best->p50_ms << ",\n"
        << "  \"best_p95_ms\": " << best->p95_ms << ",\n"
        << "  \"dense_gate_p95_le_2ms\": " << (best->p95_ms <= 2.0 ? "true" : "false") << ",\n"
        << "  \"canonical_hash\": \"" << local_hash_hex(best->final_hash) << "\"\n"
        << "}\n";

    std::cout << "v0.6 benchmark complete; best dense p95=" << best->p95_ms
              << " ms (" << best->kernel << ", page=" << best->page_size << ")\n";
    return 0;
}
