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
#include <limits>
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
    return static_cast<double>(samples.at(index)) / 1'000'000.0;
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

[[nodiscard]] WorldState make_paired_world(std::size_t body_count) {
    WorldState world;
    world.bodies.reserve(body_count);
    constexpr std::size_t width = 50U;
    for (std::size_t pair = 0U; pair < body_count / 2U; ++pair) {
        const Fixed x = Fixed::from_integer(static_cast<Fixed::rep>((pair % width) * 4U));
        const Fixed y = Fixed::from_integer(static_cast<Fixed::rep>((pair / width) * 4U));
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(pair * 2U + 1U),
            .position = {x, y},
            .velocity = {Fixed::from_integer(8), {}},
        });
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(pair * 2U + 2U),
            .position = {x + Fixed::from_ratio(5, 4), y},
            .velocity = {Fixed::from_integer(-8), {}},
        });
    }
    return world;
}

struct Record final {
    std::string scenario;
    std::string solver;
    std::size_t bodies{};
    std::size_t trials{};
    double p50_ms{};
    double p95_ms{};
    std::uint64_t contacts{};
    std::uint64_t chain_accepts{};
    std::uint64_t chain_bodies{};
    std::uint64_t warm_attempts{};
    std::uint64_t warm_accepts{};
    std::int64_t momentum_error_raw{};
    std::uint64_t hash{};
};

struct Quality final {
    std::string solver;
    std::int64_t max_velocity_violation_raw{};
    std::int64_t max_penetration_raw{};
    std::int64_t minimum_velocity_prefix_residual_raw{};
    std::int64_t minimum_position_prefix_residual_raw{};
    std::int64_t velocity_total_residual_raw{};
    std::int64_t position_total_residual_raw{};
    std::uint64_t hash{};
};

[[nodiscard]] std::int64_t narrow_quality(WideInteger value) {
    if (value < static_cast<WideInteger>(std::numeric_limits<std::int64_t>::min())
        || value > static_cast<WideInteger>(std::numeric_limits<std::int64_t>::max())) {
        throw std::overflow_error("Quality metric exceeded int64 range");
    }
    return static_cast<std::int64_t>(value);
}

[[nodiscard]] Quality evaluate_one_step(
    std::string solver,
    const WorldState& initial,
    InteractiveRollbackConfig config) {
    InteractiveRollbackEngine engine(initial, config);
    engine.advance({});
    const WorldState solved = engine.materialized_state();
    WideInteger max_velocity_violation = 0;
    WideInteger max_penetration = 0;
    WideInteger velocity_prefix = 0;
    WideInteger position_prefix = 0;
    WideInteger minimum_velocity_prefix = 0;
    WideInteger minimum_position_prefix = 0;
    const WideInteger diameter = Fixed::from_integer(1).raw();
    for (std::size_t index = 0U; index < solved.bodies.size(); ++index) {
        velocity_prefix += static_cast<WideInteger>(initial.bodies[index].velocity.x.raw())
            - solved.bodies[index].velocity.x.raw();
        const Fixed predicted = initial.bodies[index].position.x
            + initial.bodies[index].velocity.x * kSimulationDelta;
        const WideInteger offset = static_cast<WideInteger>(index) * diameter;
        position_prefix += (static_cast<WideInteger>(predicted.raw()) - offset)
            - (static_cast<WideInteger>(solved.bodies[index].position.x.raw()) - offset);
        minimum_velocity_prefix = std::min(minimum_velocity_prefix, velocity_prefix);
        minimum_position_prefix = std::min(minimum_position_prefix, position_prefix);
        if (index == 0U) continue;
        const WideInteger velocity_violation = static_cast<WideInteger>(
            solved.bodies[index - 1U].velocity.x.raw())
            - solved.bodies[index].velocity.x.raw();
        max_velocity_violation = std::max(max_velocity_violation, velocity_violation);
        const WideInteger separation = static_cast<WideInteger>(
            solved.bodies[index].position.x.raw())
            - solved.bodies[index - 1U].position.x.raw();
        max_penetration = std::max(max_penetration, diameter - separation);
    }
    return Quality{
        .solver = std::move(solver),
        .max_velocity_violation_raw = narrow_quality(std::max<WideInteger>(0, max_velocity_violation)),
        .max_penetration_raw = narrow_quality(std::max<WideInteger>(0, max_penetration)),
        .minimum_velocity_prefix_residual_raw = narrow_quality(minimum_velocity_prefix),
        .minimum_position_prefix_residual_raw = narrow_quality(minimum_position_prefix),
        .velocity_total_residual_raw = narrow_quality(velocity_prefix),
        .position_total_residual_raw = narrow_quality(position_prefix),
        .hash = stable_hash(solved),
    };
}

[[nodiscard]] InteractiveRollbackConfig config_for(
    ConnectedContactSolverMode mode, bool warm_start) {
    InteractiveRollbackConfig config;
    config.capacity = 32U;
    config.page_size = 256U;
    config.contacts = ContactSolverConfig{
        .half_extent = Fixed::from_ratio(1, 2),
        .cell_size = Fixed::from_integer(2),
        .position_iterations = 4U,
        .max_cells_per_body = 64U,
        .broadphase_mode = ContactBroadphaseMode::SweptCellRaster,
        .connected_solver_mode = mode,
        .enable_chain_warm_start = warm_start,
    };
    config.temporal = TemporalBroadphaseConfig{
        .horizon_frames = 8U,
        .incremental_body_limit = 512U,
    };
    config.use_temporal_cache = true;
    config.step_options = ComponentStepOptions{.kernel_mode = FixedKernelMode::Scalar};
    return config;
}

[[nodiscard]] Record benchmark(
    std::string scenario,
    std::string solver,
    const WorldState& initial,
    InteractiveRollbackConfig config,
    std::size_t trials) {
    {
        InteractiveRollbackEngine warm(initial, config);
        for (std::size_t frame = 0U; frame < 8U; ++frame) warm.advance({});
        static_cast<void>(warm.correct_input_and_resimulate(0U, {}));
    }
    std::vector<std::uint64_t> samples;
    samples.reserve(trials);
    Record record{.scenario = std::move(scenario), .solver = std::move(solver),
        .bodies = initial.bodies.size(), .trials = trials};
    for (std::size_t trial = 0U; trial < trials; ++trial) {
        InteractiveRollbackEngine engine(initial, config);
        for (std::size_t frame = 0U; frame < 8U; ++frame) engine.advance({});
        const InteractiveRollbackStats before = engine.stats();
        const auto begin = Clock::now();
        const std::size_t depth = engine.correct_input_and_resimulate(0U, {});
        const auto end = Clock::now();
        if (depth != 8U) throw std::logic_error("Unexpected rollback depth");
        samples.push_back(elapsed_ns(begin, end));
        const InteractiveRollbackStats after = engine.stats();
        record.contacts += after.contacts_solved - before.contacts_solved;
        record.chain_accepts += after.cumulative_solver.chain_solver_accepts
            - before.cumulative_solver.chain_solver_accepts;
        record.chain_bodies += after.cumulative_solver.chain_bodies_solved
            - before.cumulative_solver.chain_bodies_solved;
        record.warm_attempts += after.cumulative_solver.warm_start_attempts
            - before.cumulative_solver.warm_start_attempts;
        record.warm_accepts += after.cumulative_solver.warm_start_accepts
            - before.cumulative_solver.warm_start_accepts;
        record.momentum_error_raw += after.cumulative_solver.momentum_rounding_error_raw
            - before.cumulative_solver.momentum_rounding_error_raw;
        const std::uint64_t hash = stable_hash(engine.materialized_state());
        if (trial != 0U && hash != record.hash) {
            throw std::logic_error("Nondeterministic v0.9 benchmark hash");
        }
        record.hash = hash;
    }
    const auto divide = [trials](auto value) { return value / static_cast<decltype(value)>(trials); };
    record.contacts = divide(record.contacts);
    record.chain_accepts = divide(record.chain_accepts);
    record.chain_bodies = divide(record.chain_bodies);
    record.warm_attempts = divide(record.warm_attempts);
    record.warm_accepts = divide(record.warm_accepts);
    record.momentum_error_raw = divide(record.momentum_error_raw);
    record.p50_ms = percentile_ms(samples, 0.50);
    record.p95_ms = percentile_ms(samples, 0.95);
    return record;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1
        ? std::filesystem::path(argv[1]) : std::filesystem::path("artifacts/v0.9-benchmark");
    std::filesystem::create_directories(output);
    const WorldState chain = make_chain_world(1'000U);
    const WorldState paired = make_paired_world(10'000U);
    std::vector<Record> records;
    records.push_back(benchmark("contact_chain_1000", "general_colored", chain,
        config_for(ConnectedContactSolverMode::GeneralColored, false), 32U));
    records.push_back(benchmark("contact_chain_1000", "chain_isotonic_cold", chain,
        config_for(ConnectedContactSolverMode::ChainIsotonic, false), 64U));
    records.push_back(benchmark("contact_chain_1000", "chain_isotonic_warm", chain,
        config_for(ConnectedContactSolverMode::ChainIsotonic, true), 64U));
    records.push_back(benchmark("paired_impacts_10000", "matching_fused", paired,
        config_for(ConnectedContactSolverMode::Auto, true), 32U));

    const std::vector<Quality> quality{
        evaluate_one_step("general_colored", chain,
            config_for(ConnectedContactSolverMode::GeneralColored, false)),
        evaluate_one_step("chain_isotonic", chain,
            config_for(ConnectedContactSolverMode::ChainIsotonic, true)),
    };
    std::ofstream quality_csv(output / "solver_quality.csv");
    quality_csv << "solver,max_velocity_violation_raw,max_penetration_raw,"
        "minimum_velocity_prefix_residual_raw,minimum_position_prefix_residual_raw,"
        "velocity_total_residual_raw,position_total_residual_raw,final_hash\n";
    for (const Quality& q : quality) {
        quality_csv << q.solver << ',' << q.max_velocity_violation_raw << ','
            << q.max_penetration_raw << ',' << q.minimum_velocity_prefix_residual_raw << ','
            << q.minimum_position_prefix_residual_raw << ',' << q.velocity_total_residual_raw << ','
            << q.position_total_residual_raw << ',' << hash_hex(q.hash) << '\n';
    }

    std::ofstream csv(output / "connected_solver.csv");
    csv << "scenario,solver,bodies,trials,p50_ms,p95_ms,contacts_per_trial,chain_accepts_per_trial,"
           "chain_bodies_per_trial,warm_attempts_per_trial,warm_accepts_per_trial,"
           "momentum_rounding_error_raw_per_trial,final_hash\n";
    csv << std::fixed << std::setprecision(6);
    for (const Record& r : records) {
        csv << r.scenario << ',' << r.solver << ',' << r.bodies << ',' << r.trials << ','
            << r.p50_ms << ',' << r.p95_ms << ',' << r.contacts << ',' << r.chain_accepts << ','
            << r.chain_bodies << ',' << r.warm_attempts << ',' << r.warm_accepts << ','
            << r.momentum_error_raw << ',' << hash_hex(r.hash) << '\n';
    }
    const Record& cold = records[1];
    const Record& warm = records[2];
    if (cold.hash != warm.hash) throw std::logic_error("Cold and warm chain states differ");
    std::ofstream summary(output / "summary.json");
    summary << std::fixed << std::setprecision(6)
        << "{\n"
        << "  \"chain_cold_p95_ms\": " << cold.p95_ms << ",\n"
        << "  \"chain_warm_p95_ms\": " << warm.p95_ms << ",\n"
        << "  \"chain_gate_p95_le_2ms\": " << (warm.p95_ms <= 2.0 ? "true" : "false") << ",\n"
        << "  \"paired_p95_ms\": " << records[3].p95_ms << ",\n"
        << "  \"paired_gate_p95_le_2ms\": " << (records[3].p95_ms <= 2.0 ? "true" : "false") << ",\n"
        << "  \"warm_acceptance_ratio\": "
        << (warm.warm_attempts == 0U ? 0.0
            : static_cast<double>(warm.warm_accepts) / static_cast<double>(warm.warm_attempts)) << ",\n"
        << "  \"chain_hash\": \"" << hash_hex(warm.hash) << "\"\n"
        << "}\n";
    std::cout << "v0.9 chain warm p95=" << warm.p95_ms
              << " ms; paired p95=" << records[3].p95_ms << " ms\n";
}
