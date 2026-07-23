#include "neoeng/core/hash.hpp"
#include "neoeng/core/interactive_rollback.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

[[nodiscard]] std::uint64_t elapsed_ns(Clock::time_point begin, Clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
}

[[nodiscard]] double percentile_ms(std::vector<std::uint64_t> samples, double percentile) {
    std::sort(samples.begin(), samples.end());
    const std::size_t index = static_cast<std::size_t>(
        percentile * static_cast<double>(samples.size() - 1U));
    return static_cast<double>(samples[index]) / 1'000'000.0;
}


[[nodiscard]] WorldState make_noncontact_world(std::size_t body_count) {
    WorldState world;
    world.bodies.reserve(body_count);
    const std::size_t width = 100U;
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
    const std::size_t width = 50U;
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
    std::size_t bodies{};
    std::string broadphase{};
    double p50_ms{};
    double p95_ms{};
    std::uint64_t contacts_per_trial{};
    std::uint64_t candidates_per_trial{};
    std::uint64_t projections_per_trial{};
    std::uint64_t final_hash{};
};

[[nodiscard]] Record benchmark_rollback(
    std::string scenario,
    const WorldState& initial,
    std::size_t trials,
    ContactBroadphaseMode mode) {
    InteractiveRollbackConfig config;
    config.capacity = 32U;
    config.page_size = 256U;
    config.arena_bytes_per_epoch = 64U * 1024U;
    config.contacts = ContactSolverConfig{
        .half_extent = Fixed::from_ratio(1, 2),
        .cell_size = Fixed::from_integer(2),
        .position_iterations = 4U,
        .max_cells_per_body = 64U,
        .broadphase_mode = mode,
    };
    std::vector<std::uint64_t> samples;
    samples.reserve(trials);
    std::uint64_t contacts = 0U;
    std::uint64_t candidates = 0U;
    std::uint64_t projections = 0U;
    std::uint64_t final_hash = 0U;
    for (std::size_t trial = 0U; trial < trials; ++trial) {
        InteractiveRollbackEngine engine(initial, config);
        try {
            for (std::size_t frame = 0U; frame < 8U; ++frame) engine.advance({});
        } catch (const std::exception& error) {
            throw std::runtime_error(scenario + " preparation trial "
                + std::to_string(trial) + ": " + error.what());
        }
        const InteractiveRollbackStats before = engine.stats();
        const auto begin = Clock::now();
        std::size_t frames = 0U;
        try {
            frames = engine.correct_input_and_resimulate(0U, {});
        } catch (const std::exception& error) {
            throw std::runtime_error(scenario + " rollback trial "
                + std::to_string(trial) + ": " + error.what());
        }
        const auto end = Clock::now();
        if (frames != 8U) throw std::logic_error("Unexpected rollback depth");
        samples.push_back(elapsed_ns(begin, end));
        const InteractiveRollbackStats after = engine.stats();
        contacts += after.contacts_solved - before.contacts_solved;
        candidates += after.cumulative_solver.candidate_pairs
            - before.cumulative_solver.candidate_pairs;
        projections += after.cumulative_solver.position_projections
            - before.cumulative_solver.position_projections;
        final_hash = stable_hash(engine.materialized_state());
    }
    return Record{
        .scenario = std::move(scenario),
        .bodies = initial.bodies.size(),
        .broadphase = mode == ContactBroadphaseMode::SweptCellRaster ? "swept_raster" : "center_expansion",
        .p50_ms = percentile_ms(samples, 0.50),
        .p95_ms = percentile_ms(samples, 0.95),
        .contacts_per_trial = contacts / trials,
        .candidates_per_trial = candidates / trials,
        .projections_per_trial = projections / trials,
        .final_hash = final_hash,
    };
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output_directory = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("artifacts/v0.7-benchmark");
    std::filesystem::create_directories(output_directory);

    std::vector<Record> records;
    const WorldState noncontact = make_noncontact_world(10'000U);
    const WorldState paired_world = make_paired_world(10'000U);
    const WorldState chain = make_chain_world(1'000U);
    for (const ContactBroadphaseMode mode : {
            ContactBroadphaseMode::SweptCellRaster,
            ContactBroadphaseMode::CenterGridExpansion}) {
        const char* mode_name = mode == ContactBroadphaseMode::SweptCellRaster
            ? "swept_raster" : "center_expansion";
        std::cerr << "benchmark noncontact_dense_10000 " << mode_name << '\n';
        records.push_back(benchmark_rollback(
            "noncontact_dense_10000", noncontact, 16U, mode));
        std::cerr << "benchmark paired_impacts_10000 " << mode_name << '\n';
        records.push_back(benchmark_rollback(
            "paired_impacts_10000", paired_world, 20U, mode));
        std::cerr << "benchmark contact_chain_1000 " << mode_name << '\n';
        records.push_back(benchmark_rollback(
            "contact_chain_1000", chain, 24U, mode));
    }

    std::ofstream csv(output_directory / "interactive_rollback.csv");
    csv << "scenario,broadphase,bodies,p50_ms,p95_ms,contacts_per_trial,candidates_per_trial,"
           "projections_per_trial,final_hash\n";
    csv << std::fixed << std::setprecision(6);
    for (const Record& record : records) {
        csv << record.scenario << ',' << record.broadphase << ',' << record.bodies << ',' << record.p50_ms << ','
            << record.p95_ms << ',' << record.contacts_per_trial << ','
            << record.candidates_per_trial << ',' << record.projections_per_trial << ','
            << hash_hex(record.final_hash) << '\n';
    }

    const auto paired = std::find_if(records.begin(), records.end(), [](const Record& record) {
        return record.scenario == "paired_impacts_10000"
            && record.broadphase == "swept_raster";
    });
    std::ofstream summary(output_directory / "summary.json");
    summary << std::fixed << std::setprecision(6)
        << "{\n"
        << "  \"interactive_scenario\": \"" << paired->scenario << "\",\n"
        << "  \"broadphase\": \"" << paired->broadphase << "\",\n"
        << "  \"bodies\": " << paired->bodies << ",\n"
        << "  \"p50_ms\": " << paired->p50_ms << ",\n"
        << "  \"p95_ms\": " << paired->p95_ms << ",\n"
        << "  \"contacts_per_trial\": " << paired->contacts_per_trial << ",\n"
        << "  \"gate_p95_le_2ms\": " << (paired->p95_ms <= 2.0 ? "true" : "false") << ",\n"
        << "  \"final_hash\": \"" << hash_hex(paired->final_hash) << "\"\n"
        << "}\n";

    std::cout << "v0.7 benchmark complete; paired impacts p95="
              << paired->p95_ms << " ms\n";
    return 0;
}
