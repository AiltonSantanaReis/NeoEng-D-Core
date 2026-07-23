#include "neoeng/core/hash.hpp"
#include "neoeng/core/interactive_rollback.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
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

[[nodiscard]] double percentile_ms(std::vector<std::uint64_t> samples, double percentile) {
    if (samples.empty()) throw std::invalid_argument("Percentile requires samples");
    std::sort(samples.begin(), samples.end());
    const std::size_t index = static_cast<std::size_t>(
        percentile * static_cast<double>(samples.size() - 1U));
    return static_cast<double>(samples[index]) / 1'000'000.0;
}

[[nodiscard]] WorldState make_noncontact_world(std::size_t body_count) {
    WorldState world;
    world.bodies.reserve(body_count);
    constexpr std::size_t width = 100U;
    for (std::size_t index = 0U; index < body_count; ++index) {
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(index + 1U),
            .position = {
                Fixed::from_integer(static_cast<Fixed::rep>((index % width) * 4U)),
                Fixed::from_integer(static_cast<Fixed::rep>((index / width) * 4U)),
            },
            .velocity = {Fixed::from_integer(1), {}},
        });
    }
    return world;
}

[[nodiscard]] WorldState make_paired_world(std::size_t body_count) {
    WorldState world;
    world.bodies.reserve(body_count);
    const std::size_t pairs = body_count / 2U;
    constexpr std::size_t width = 50U;
    for (std::size_t pair = 0U; pair < pairs; ++pair) {
        const Fixed base_x = Fixed::from_integer(static_cast<Fixed::rep>((pair % width) * 4U));
        const Fixed base_y = Fixed::from_integer(static_cast<Fixed::rep>((pair / width) * 4U));
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(pair * 2U + 1U),
            .position = {base_x, base_y},
            .velocity = {Fixed::from_integer(8), {}},
        });
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(pair * 2U + 2U),
            .position = {base_x + Fixed::from_ratio(5, 4), base_y},
            .velocity = {Fixed::from_integer(-8), {}},
        });
    }
    return world;
}

[[nodiscard]] WorldState make_chain_world(std::size_t body_count) {
    WorldState world;
    world.bodies.reserve(body_count);
    for (std::size_t index = 0U; index < body_count; ++index) {
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(index + 1U),
            .position = {Fixed::from_integer(static_cast<Fixed::rep>(index)), {}},
            .velocity = index == 0U ? Vec2{Fixed::from_integer(6), {}} : Vec2{},
        });
    }
    return world;
}

struct Record final {
    std::string scenario{};
    std::string mode{};
    std::size_t bodies{};
    std::size_t trials{};
    double p50_ms{};
    double p95_ms{};
    std::uint64_t contacts_per_trial{};
    std::uint64_t candidates_per_trial{};
    std::uint64_t cache_builds_per_trial{};
    std::uint64_t cache_updates_per_trial{};
    std::uint64_t cache_reuses_per_trial{};
    std::uint64_t escaped_bodies_per_trial{};
    std::uint64_t manifold_reuses_per_trial{};
    std::uint64_t final_hash{};
};

[[nodiscard]] Record benchmark_rollback(
    std::string scenario,
    const WorldState& initial,
    std::size_t trials,
    bool temporal_cache) {
    InteractiveRollbackConfig config;
    config.capacity = 32U;
    config.page_size = 256U;
    config.arena_bytes_per_epoch = 64U * 1024U;
    config.contacts = ContactSolverConfig{
        .half_extent = Fixed::from_ratio(1, 2),
        .cell_size = Fixed::from_integer(2),
        .position_iterations = 4U,
        .max_cells_per_body = 64U,
        .broadphase_mode = ContactBroadphaseMode::SweptCellRaster,
    };
    config.temporal = TemporalBroadphaseConfig{
        .horizon_frames = 8U,
        .incremental_body_limit = 512U,
    };
    config.use_temporal_cache = temporal_cache;
    config.step_options = ComponentStepOptions{.kernel_mode = FixedKernelMode::Scalar};

    std::vector<std::uint64_t> samples;
    samples.reserve(trials);
    std::uint64_t contacts = 0U;
    std::uint64_t candidates = 0U;
    std::uint64_t builds = 0U;
    std::uint64_t updates = 0U;
    std::uint64_t reuses = 0U;
    std::uint64_t escaped = 0U;
    std::uint64_t manifold_reuses = 0U;
    std::uint64_t final_hash = 0U;

    // One untimed pass faults in code/data and avoids treating startup as rollback cost.
    {
        InteractiveRollbackEngine warm(initial, config);
        for (std::size_t frame = 0U; frame < 8U; ++frame) warm.advance({});
        static_cast<void>(warm.correct_input_and_resimulate(0U, {}));
    }

    for (std::size_t trial = 0U; trial < trials; ++trial) {
        InteractiveRollbackEngine engine(initial, config);
        for (std::size_t frame = 0U; frame < 8U; ++frame) engine.advance({});
        const InteractiveRollbackStats before = engine.stats();
        const auto begin = Clock::now();
        const std::size_t frames = engine.correct_input_and_resimulate(0U, {});
        const auto end = Clock::now();
        if (frames != 8U) throw std::logic_error("Unexpected rollback depth");
        samples.push_back(elapsed_ns(begin, end));
        const InteractiveRollbackStats after = engine.stats();
        contacts += after.contacts_solved - before.contacts_solved;
        candidates += after.cumulative_solver.candidate_pairs
            - before.cumulative_solver.candidate_pairs;
        builds += after.cumulative_temporal.pair_cache_builds
            - before.cumulative_temporal.pair_cache_builds;
        updates += after.cumulative_temporal.pair_cache_incremental_updates
            - before.cumulative_temporal.pair_cache_incremental_updates;
        reuses += after.cumulative_temporal.pair_cache_reuses
            - before.cumulative_temporal.pair_cache_reuses;
        escaped += after.cumulative_temporal.escaped_bodies
            - before.cumulative_temporal.escaped_bodies;
        manifold_reuses += after.cumulative_temporal.manifold_pairs_reused
            - before.cumulative_temporal.manifold_pairs_reused;
        const std::uint64_t hash = stable_hash(engine.materialized_state());
        if (trial != 0U && hash != final_hash) {
            throw std::logic_error("Benchmark produced a nondeterministic final hash");
        }
        final_hash = hash;
    }

    const auto divide = [trials](std::uint64_t value) {
        return value / static_cast<std::uint64_t>(trials);
    };
    return Record{
        .scenario = std::move(scenario),
        .mode = temporal_cache ? "temporal_fused" : "rebuild_reference",
        .bodies = initial.bodies.size(),
        .trials = trials,
        .p50_ms = percentile_ms(samples, 0.50),
        .p95_ms = percentile_ms(samples, 0.95),
        .contacts_per_trial = divide(contacts),
        .candidates_per_trial = divide(candidates),
        .cache_builds_per_trial = divide(builds),
        .cache_updates_per_trial = divide(updates),
        .cache_reuses_per_trial = divide(reuses),
        .escaped_bodies_per_trial = divide(escaped),
        .manifold_reuses_per_trial = divide(manifold_reuses),
        .final_hash = final_hash,
    };
}

[[nodiscard]] const Record& find_record(
    const std::vector<Record>& records,
    std::string_view scenario,
    std::string_view mode) {
    const auto found = std::find_if(records.begin(), records.end(), [&](const Record& record) {
        return record.scenario == scenario && record.mode == mode;
    });
    if (found == records.end()) throw std::logic_error("Required benchmark record missing");
    return *found;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output_directory = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("artifacts/v0.8-benchmark");
    std::filesystem::create_directories(output_directory);

    const WorldState paired = make_paired_world(10'000U);
    const WorldState noncontact = make_noncontact_world(10'000U);
    const WorldState chain = make_chain_world(1'000U);
    std::vector<Record> records;
    for (const bool temporal : {true, false}) {
        const char* mode = temporal ? "temporal_fused" : "rebuild_reference";
        std::cerr << "benchmark paired_impacts_10000 " << mode << '\n';
        records.push_back(benchmark_rollback("paired_impacts_10000", paired, 40U, temporal));
        std::cerr << "benchmark noncontact_dense_10000 " << mode << '\n';
        records.push_back(benchmark_rollback("noncontact_dense_10000", noncontact, 20U, temporal));
        std::cerr << "benchmark contact_chain_1000 " << mode << '\n';
        records.push_back(benchmark_rollback("contact_chain_1000", chain, 24U, temporal));
    }

    std::ofstream csv(output_directory / "interactive_rollback.csv");
    csv << "scenario,mode,bodies,trials,p50_ms,p95_ms,contacts_per_trial,candidates_per_trial,"
           "cache_builds_per_trial,cache_updates_per_trial,cache_reuses_per_trial,"
           "escaped_bodies_per_trial,manifold_reuses_per_trial,final_hash\n";
    csv << std::fixed << std::setprecision(6);
    for (const Record& record : records) {
        csv << record.scenario << ',' << record.mode << ',' << record.bodies << ','
            << record.trials << ',' << record.p50_ms << ',' << record.p95_ms << ','
            << record.contacts_per_trial << ',' << record.candidates_per_trial << ','
            << record.cache_builds_per_trial << ',' << record.cache_updates_per_trial << ','
            << record.cache_reuses_per_trial << ',' << record.escaped_bodies_per_trial << ','
            << record.manifold_reuses_per_trial << ',' << hash_hex(record.final_hash) << '\n';
    }

    const Record& temporal = find_record(records, "paired_impacts_10000", "temporal_fused");
    const Record& reference = find_record(records, "paired_impacts_10000", "rebuild_reference");
    if (temporal.final_hash != reference.final_hash) {
        throw std::logic_error("Temporal and reference modes diverged");
    }
    const double speedup = temporal.p50_ms == 0.0 ? 0.0 : reference.p50_ms / temporal.p50_ms;
    std::ofstream summary(output_directory / "summary.json");
    summary << std::fixed << std::setprecision(6)
        << "{\n"
        << "  \"interactive_scenario\": \"paired_impacts_10000\",\n"
        << "  \"bodies\": " << temporal.bodies << ",\n"
        << "  \"contacts\": " << temporal.contacts_per_trial << ",\n"
        << "  \"temporal_p50_ms\": " << temporal.p50_ms << ",\n"
        << "  \"temporal_p95_ms\": " << temporal.p95_ms << ",\n"
        << "  \"reference_p50_ms\": " << reference.p50_ms << ",\n"
        << "  \"reference_p95_ms\": " << reference.p95_ms << ",\n"
        << "  \"p50_speedup\": " << speedup << ",\n"
        << "  \"gate_p95_le_2ms\": " << (temporal.p95_ms <= 2.0 ? "true" : "false") << ",\n"
        << "  \"final_hash\": \"" << hash_hex(temporal.final_hash) << "\"\n"
        << "}\n";

    std::cout << "v0.8 benchmark complete; paired temporal p95="
              << temporal.p95_ms << " ms, reference p95=" << reference.p95_ms
              << " ms\n";
    return 0;
}
