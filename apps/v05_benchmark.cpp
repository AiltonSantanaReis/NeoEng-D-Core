#include "neoeng/core/active_world.hpp"
#include "neoeng/core/component_world.hpp"
#include "neoeng/core/contextual_policy.hpp"
#include "neoeng/core/hash.hpp"
#include "neoeng/core/immutable_world.hpp"
#include "neoeng/core/offline_oracle.hpp"
#include "neoeng/core/radix_world.hpp"
#include "neoeng/core/simulation.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

struct RuntimeRecord final {
    std::string pattern;
    std::size_t active_count{};
    std::string representation;
    double p50_us{};
    double p95_us{};
    std::uint64_t scanned_per_frame{};
    std::uint64_t copied_units_per_frame{};
    std::size_t retained_bytes{};
    std::uint64_t final_hash{};
};

[[nodiscard]] std::uint64_t elapsed_ns(Clock::time_point begin, Clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
}

[[nodiscard]] double percentile(std::vector<std::uint64_t> values, double probability) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double raw = probability * static_cast<double>(values.size() - 1U);
    const std::size_t index = static_cast<std::size_t>(raw);
    return static_cast<double>(values[index]) / 1'000.0;
}

[[nodiscard]] std::string hash_hex(std::uint64_t value) {
    std::ostringstream stream;
    stream << "0x" << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << value;
    return stream.str();
}

[[nodiscard]] std::vector<std::size_t> active_indices(
    std::size_t body_count,
    std::size_t active_count,
    bool dispersed) {
    std::vector<std::size_t> indices;
    indices.reserve(active_count);
    if (active_count == 0U) return indices;
    for (std::size_t item = 0U; item < active_count; ++item) {
        const std::size_t index = dispersed
            ? item * body_count / active_count
            : item;
        indices.push_back(std::min(index, body_count - 1U));
    }
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

[[nodiscard]] WorldState make_world(
    std::size_t body_count,
    std::size_t active_count,
    bool dispersed) {
    WorldState state;
    state.frame = 0U;
    state.bodies.reserve(body_count);
    for (std::size_t index = 0U; index < body_count; ++index) {
        state.bodies.push_back(Body{
            .id = static_cast<EntityId>(index + 1U),
            .position = {
                Fixed::from_ratio(static_cast<Fixed::rep>(index % 257U), 17),
                Fixed::from_ratio(static_cast<Fixed::rep>(index % 131U), 19)},
            .velocity = {},
        });
    }
    for (const std::size_t index : active_indices(body_count, active_count, dispersed)) {
        state.bodies[index].velocity = {
            Fixed::from_ratio(static_cast<Fixed::rep>((index % 7U) + 1U), 31),
            Fixed::from_ratio(-static_cast<Fixed::rep>((index % 5U) + 1U), 37)};
    }
    return state;
}

[[nodiscard]] RuntimeRecord benchmark_full_scan(
    const WorldState& initial,
    std::string pattern,
    std::size_t active_count) {
    ImmutableWorldState state = make_immutable_world(initial, 64U);
    std::vector<ImmutableWorldState> history;
    history.reserve(81U);
    history.push_back(state);
    std::vector<std::uint64_t> samples;
    ImmutableAllocationStats cumulative;
    for (std::size_t frame = 0U; frame < 90U; ++frame) {
        const auto begin = Clock::now();
        ImmutableStepResult result = step_immutable(state, {});
        const auto end = Clock::now();
        state = std::move(result.state);
        if (frame >= 10U) {
            samples.push_back(elapsed_ns(begin, end));
            cumulative += result.allocation;
            history.push_back(state);
        }
    }
    const ImmutableMemoryFootprint memory = estimate_retained_immutable_memory(history);
    return RuntimeRecord{
        .pattern = std::move(pattern),
        .active_count = active_count,
        .representation = "full_scan_binary_aos",
        .p50_us = percentile(samples, 0.50),
        .p95_us = percentile(samples, 0.95),
        .scanned_per_frame = cumulative.bodies_scanned / samples.size(),
        .copied_units_per_frame = cumulative.bodies_copied / samples.size(),
        .retained_bytes = memory.payload_bytes + memory.metadata_bytes,
        .final_hash = stable_hash(state.materialize()),
    };
}

[[nodiscard]] RuntimeRecord benchmark_active_binary(
    const WorldState& initial,
    std::string pattern,
    std::size_t active_count) {
    ImmutableWorldState state = make_immutable_world(initial, 64U);
    DeterministicActiveSet active = DeterministicActiveSet::from_world(initial);
    std::vector<ImmutableWorldState> history;
    history.reserve(81U);
    history.push_back(state);
    std::vector<std::uint64_t> samples;
    ActiveStepStats cumulative;
    for (std::size_t frame = 0U; frame < 90U; ++frame) {
        const auto begin = Clock::now();
        ActiveImmutableStepResult result = step_immutable_active(state, active, {});
        const auto end = Clock::now();
        state = std::move(result.state);
        active = std::move(result.active);
        if (frame >= 10U) {
            samples.push_back(elapsed_ns(begin, end));
            cumulative.candidate_bodies_scanned += result.stats.candidate_bodies_scanned;
            cumulative.immutable_allocation += result.stats.immutable_allocation;
            history.push_back(state);
        }
    }
    const ImmutableMemoryFootprint memory = estimate_retained_immutable_memory(history);
    return RuntimeRecord{
        .pattern = std::move(pattern),
        .active_count = active_count,
        .representation = "active_binary_aos",
        .p50_us = percentile(samples, 0.50),
        .p95_us = percentile(samples, 0.95),
        .scanned_per_frame = cumulative.candidate_bodies_scanned / samples.size(),
        .copied_units_per_frame = cumulative.immutable_allocation.bodies_copied / samples.size(),
        .retained_bytes = memory.payload_bytes + memory.metadata_bytes,
        .final_hash = stable_hash(state.materialize()),
    };
}

[[nodiscard]] RuntimeRecord benchmark_component(
    const WorldState& initial,
    std::string pattern,
    std::size_t active_count) {
    ComponentWorldState state = make_component_world(initial, 64U);
    DeterministicActiveSet active = DeterministicActiveSet::from_world(initial);
    std::vector<ComponentWorldState> history;
    history.reserve(81U);
    history.push_back(state);
    std::vector<std::uint64_t> samples;
    ComponentAllocationStats cumulative;
    for (std::size_t frame = 0U; frame < 90U; ++frame) {
        const auto begin = Clock::now();
        ComponentStepResult result = step_component_active(state, active, {});
        const auto end = Clock::now();
        state = std::move(result.state);
        active = std::move(result.active);
        if (frame >= 10U) {
            samples.push_back(elapsed_ns(begin, end));
            cumulative += result.allocation;
            history.push_back(state);
        }
    }
    const ComponentMemoryFootprint memory = estimate_retained_component_memory(history);
    return RuntimeRecord{
        .pattern = std::move(pattern),
        .active_count = active_count,
        .representation = "active_component_soa",
        .p50_us = percentile(samples, 0.50),
        .p95_us = percentile(samples, 0.95),
        .scanned_per_frame = cumulative.candidate_bodies_scanned / samples.size(),
        .copied_units_per_frame = cumulative.component_values_copied / samples.size(),
        .retained_bytes = memory.payload_bytes + memory.metadata_bytes,
        .final_hash = stable_hash(state.materialize()),
    };
}

[[nodiscard]] RuntimeRecord benchmark_radix(
    const WorldState& initial,
    std::string pattern,
    std::size_t active_count,
    std::size_t fanout) {
    RadixWorldState state = make_radix_world(initial, 64U, fanout);
    DeterministicActiveSet active = DeterministicActiveSet::from_world(initial);
    std::vector<RadixWorldState> history;
    history.reserve(81U);
    history.push_back(state);
    std::vector<std::uint64_t> samples;
    RadixAllocationStats cumulative;
    for (std::size_t frame = 0U; frame < 90U; ++frame) {
        const auto begin = Clock::now();
        RadixStepResult result = step_radix_active(state, active, {});
        const auto end = Clock::now();
        state = std::move(result.state);
        active = std::move(result.active);
        if (frame >= 10U) {
            samples.push_back(elapsed_ns(begin, end));
            cumulative += result.allocation;
            history.push_back(state);
        }
    }
    const RadixMemoryFootprint memory = estimate_retained_radix_memory(history);
    return RuntimeRecord{
        .pattern = std::move(pattern),
        .active_count = active_count,
        .representation = "active_radix" + std::to_string(fanout) + "_aos",
        .p50_us = percentile(samples, 0.50),
        .p95_us = percentile(samples, 0.95),
        .scanned_per_frame = cumulative.candidate_bodies_scanned / samples.size(),
        .copied_units_per_frame = cumulative.bodies_copied / samples.size(),
        .retained_bytes = memory.payload_bytes + memory.metadata_bytes,
        .final_hash = stable_hash(state.materialize()),
    };
}

void write_runtime_csv(
    const std::filesystem::path& output,
    const std::vector<RuntimeRecord>& records) {
    std::ofstream file(output);
    file << "pattern,active_bodies,active_density,representation,p50_us,p95_us,"
            "scanned_per_frame,copied_units_per_frame,retained_bytes,final_hash\n";
    for (const RuntimeRecord& record : records) {
        file << record.pattern << ',' << record.active_count << ','
             << std::fixed << std::setprecision(6)
             << static_cast<double>(record.active_count) / 10'000.0 << ','
             << record.representation << ',' << std::setprecision(3)
             << record.p50_us << ',' << record.p95_us << ','
             << record.scanned_per_frame << ',' << record.copied_units_per_frame << ','
             << record.retained_bytes << ',' << hash_hex(record.final_hash) << '\n';
    }
}

struct RollbackRecord final {
    std::size_t active_count{};
    std::string representation;
    double p50_ms{};
    double p95_ms{};
    std::uint64_t final_hash{};
};

[[nodiscard]] RollbackRecord benchmark_rollback_full(
    const WorldState& initial, std::size_t active_count) {
    ImmutableWorldState base = make_immutable_world(initial, 64U);
    for (std::size_t frame = 0U; frame < 32U; ++frame) base = step_immutable(base, {}).state;
    std::vector<std::uint64_t> samples;
    for (std::size_t trial = 0U; trial < 80U; ++trial) {
        ImmutableWorldState state = base;
        const auto begin = Clock::now();
        for (std::size_t frame = 0U; frame < 8U; ++frame) state = step_immutable(state, {}).state;
        const auto end = Clock::now();
        const volatile std::uint64_t sink = state.merkle_hash();
        static_cast<void>(sink);
        samples.push_back(elapsed_ns(begin, end));
    }
    ImmutableWorldState final = base;
    for (std::size_t frame = 0U; frame < 8U; ++frame) final = step_immutable(final, {}).state;
    return RollbackRecord{
        .active_count = active_count,
        .representation = "full_scan_binary_aos",
        .p50_ms = percentile(samples, 0.50) / 1'000.0,
        .p95_ms = percentile(samples, 0.95) / 1'000.0,
        .final_hash = stable_hash(final.materialize()),
    };
}

[[nodiscard]] RollbackRecord benchmark_rollback_active(
    const WorldState& initial, std::size_t active_count) {
    ImmutableWorldState base = make_immutable_world(initial, 64U);
    DeterministicActiveSet active = DeterministicActiveSet::from_world(initial);
    for (std::size_t frame = 0U; frame < 32U; ++frame) {
        auto result = step_immutable_active(base, active, {});
        base = std::move(result.state);
        active = std::move(result.active);
    }
    std::vector<std::uint64_t> samples;
    for (std::size_t trial = 0U; trial < 80U; ++trial) {
        ImmutableWorldState state = base;
        DeterministicActiveSet active_trial = active;
        const auto begin = Clock::now();
        for (std::size_t frame = 0U; frame < 8U; ++frame) {
            auto result = step_immutable_active(state, active_trial, {});
            state = std::move(result.state);
            active_trial = std::move(result.active);
        }
        const auto end = Clock::now();
        const volatile std::uint64_t sink = state.merkle_hash();
        static_cast<void>(sink);
        samples.push_back(elapsed_ns(begin, end));
    }
    ImmutableWorldState final = base;
    DeterministicActiveSet final_active = active;
    for (std::size_t frame = 0U; frame < 8U; ++frame) {
        auto result = step_immutable_active(final, final_active, {});
        final = std::move(result.state);
        final_active = std::move(result.active);
    }
    return RollbackRecord{
        .active_count = active_count,
        .representation = "active_binary_aos",
        .p50_ms = percentile(samples, 0.50) / 1'000.0,
        .p95_ms = percentile(samples, 0.95) / 1'000.0,
        .final_hash = stable_hash(final.materialize()),
    };
}

[[nodiscard]] RollbackRecord benchmark_rollback_component(
    const WorldState& initial, std::size_t active_count) {
    ComponentWorldState base = make_component_world(initial, 64U);
    DeterministicActiveSet active = DeterministicActiveSet::from_world(initial);
    for (std::size_t frame = 0U; frame < 32U; ++frame) {
        auto result = step_component_active(base, active, {});
        base = std::move(result.state);
        active = std::move(result.active);
    }
    std::vector<std::uint64_t> samples;
    for (std::size_t trial = 0U; trial < 80U; ++trial) {
        ComponentWorldState state = base;
        DeterministicActiveSet active_trial = active;
        const auto begin = Clock::now();
        for (std::size_t frame = 0U; frame < 8U; ++frame) {
            auto result = step_component_active(state, active_trial, {});
            state = std::move(result.state);
            active_trial = std::move(result.active);
        }
        const auto end = Clock::now();
        const volatile auto sink = state.body_at(active_trial.indices().empty()
            ? 0U : active_trial.indices().front()).position.x.raw();
        static_cast<void>(sink);
        samples.push_back(elapsed_ns(begin, end));
    }
    ComponentWorldState final = base;
    DeterministicActiveSet final_active = active;
    for (std::size_t frame = 0U; frame < 8U; ++frame) {
        auto result = step_component_active(final, final_active, {});
        final = std::move(result.state);
        final_active = std::move(result.active);
    }
    return RollbackRecord{
        .active_count = active_count,
        .representation = "active_component_soa",
        .p50_ms = percentile(samples, 0.50) / 1'000.0,
        .p95_ms = percentile(samples, 0.95) / 1'000.0,
        .final_hash = stable_hash(final.materialize()),
    };
}

[[nodiscard]] RollbackRecord benchmark_rollback_radix(
    const WorldState& initial, std::size_t active_count, std::size_t fanout) {
    RadixWorldState base = make_radix_world(initial, 64U, fanout);
    DeterministicActiveSet active = DeterministicActiveSet::from_world(initial);
    for (std::size_t frame = 0U; frame < 32U; ++frame) {
        auto result = step_radix_active(base, active, {});
        base = std::move(result.state);
        active = std::move(result.active);
    }
    std::vector<std::uint64_t> samples;
    for (std::size_t trial = 0U; trial < 80U; ++trial) {
        RadixWorldState state = base;
        DeterministicActiveSet active_trial = active;
        const auto begin = Clock::now();
        for (std::size_t frame = 0U; frame < 8U; ++frame) {
            auto result = step_radix_active(state, active_trial, {});
            state = std::move(result.state);
            active_trial = std::move(result.active);
        }
        const auto end = Clock::now();
        const volatile auto sink = state.body_at(active_trial.indices().empty()
            ? 0U : active_trial.indices().front()).position.x.raw();
        static_cast<void>(sink);
        samples.push_back(elapsed_ns(begin, end));
    }
    RadixWorldState final = base;
    DeterministicActiveSet final_active = active;
    for (std::size_t frame = 0U; frame < 8U; ++frame) {
        auto result = step_radix_active(final, final_active, {});
        final = std::move(result.state);
        final_active = std::move(result.active);
    }
    return RollbackRecord{
        .active_count = active_count,
        .representation = "active_radix" + std::to_string(fanout) + "_aos",
        .p50_ms = percentile(samples, 0.50) / 1'000.0,
        .p95_ms = percentile(samples, 0.95) / 1'000.0,
        .final_hash = stable_hash(final.materialize()),
    };
}

void write_rollback_csv(
    const std::filesystem::path& output,
    const std::vector<RollbackRecord>& records) {
    std::ofstream file(output);
    file << "active_bodies,representation,rollback_8_p50_ms,rollback_8_p95_ms,final_hash\n";
    for (const RollbackRecord& record : records) {
        file << record.active_count << ',' << record.representation << ','
             << std::fixed << std::setprecision(6) << record.p50_ms << ','
             << record.p95_ms << ',' << hash_hex(record.final_hash) << '\n';
    }
}

[[nodiscard]] const RuntimeRecord& find_record(
    const std::vector<RuntimeRecord>& records,
    std::string_view pattern,
    std::size_t active_count,
    std::string_view representation) {
    const auto iterator = std::find_if(records.begin(), records.end(), [&](const RuntimeRecord& record) {
        return record.pattern == pattern && record.active_count == active_count
            && record.representation == representation;
    });
    if (iterator == records.end()) throw std::logic_error("Missing runtime record for policy trace");
    return *iterator;
}

[[nodiscard]] OracleEncoding to_oracle(RuntimeRepresentation representation) noexcept {
    return static_cast<OracleEncoding>(static_cast<std::uint8_t>(representation));
}

[[nodiscard]] std::string_view representation_name(RuntimeRepresentation representation) noexcept {
    switch (representation) {
    case RuntimeRepresentation::FullScanAoS: return "full_scan_binary_aos";
    case RuntimeRepresentation::ActiveChunkedAoS: return "active_radix16_aos";
    case RuntimeRepresentation::ActiveComponentSoA: return "active_component_soa";
    }
    return "unknown";
}

void write_policy_outputs(
    const std::filesystem::path& frames_output,
    const std::filesystem::path& summary_output,
    const std::vector<RuntimeRecord>& records) {
    struct Context final { std::string_view pattern; std::size_t active; std::size_t frames; };
    constexpr std::array<Context, 8> contexts{{
        {"clustered", 2U, 12U},
        {"dispersed", 100U, 12U},
        {"clustered", 1'000U, 12U},
        {"dispersed", 5'000U, 12U},
        {"clustered", 10'000U, 12U},
        {"dispersed", 10U, 12U},
        {"clustered", 100U, 12U},
        {"dispersed", 1'000U, 12U},
    }};

    ContextualPolicyConfig policy_config;
    policy_config.prior_weight = 0U;
    policy_config.planning_horizon_frames = 12U;
    // Conversion penalties are conservative fixed laboratory costs. They prevent
    // gratuitous representation switching while remaining small relative to a phase.
    for (std::size_t from = 0U; from < kRuntimeRepresentationCount; ++from) {
        for (std::size_t to = 0U; to < kRuntimeRepresentationCount; ++to) {
            policy_config.transition_cost[from][to] = from == to ? 0U : 150'000U;
        }
    }
    ContextualEncodingPolicy policy(policy_config);

    // Calibration is full-information and excluded from regret. This is a lab policy,
    // not yet a production policy that can observe every alternative for free.
    for (const Context& context : contexts) {
        const PolicyFeatures features{
            .body_count = 10'000U,
            .changed_bodies = context.active,
            .touched_chunks = context.pattern == "dispersed"
                ? std::min<std::size_t>(context.active, (10'000U + 63U) / 64U)
                : (context.active + 63U) / 64U,
            .chunk_size = 64U,
        };
        const std::array<std::uint64_t, kRuntimeRepresentationCount> costs{
            static_cast<std::uint64_t>(find_record(records, context.pattern, context.active,
                "full_scan_binary_aos").p50_us * 1'000.0),
            static_cast<std::uint64_t>(find_record(records, context.pattern, context.active,
                "active_radix16_aos").p50_us * 1'000.0),
            static_cast<std::uint64_t>(find_record(records, context.pattern, context.active,
                "active_component_soa").p50_us * 1'000.0),
        };
        policy.observe(features, costs);
    }
    policy.reset_sequence();

    std::vector<OracleFrameCost> oracle_frames;
    std::vector<OracleEncoding> online_sequence;
    std::vector<std::tuple<std::size_t, PolicyFeatures, ContextualPolicyDecision,
                           std::array<std::uint64_t, kRuntimeRepresentationCount>>> trace;
    std::size_t frame_index = 0U;
    for (std::size_t cycle = 0U; cycle < 4U; ++cycle) {
        for (const Context& context : contexts) {
            const PolicyFeatures features{
                .body_count = 10'000U,
                .changed_bodies = context.active,
                .touched_chunks = context.pattern == "dispersed"
                    ? std::min<std::size_t>(context.active, (10'000U + 63U) / 64U)
                    : (context.active + 63U) / 64U,
                .chunk_size = 64U,
            };
            const std::array<std::uint64_t, kRuntimeRepresentationCount> costs{
                static_cast<std::uint64_t>(find_record(records, context.pattern, context.active,
                    "full_scan_binary_aos").p50_us * 1'000.0),
                static_cast<std::uint64_t>(find_record(records, context.pattern, context.active,
                    "active_radix16_aos").p50_us * 1'000.0),
                static_cast<std::uint64_t>(find_record(records, context.pattern, context.active,
                    "active_component_soa").p50_us * 1'000.0),
            };
            for (std::size_t repeat = 0U; repeat < context.frames; ++repeat) {
                const auto decision = policy.choose(features, {true, true, true});
                online_sequence.push_back(to_oracle(decision.representation));
                oracle_frames.push_back(OracleFrameCost{
                    .operation_cost = costs,
                    .allowed = {true, true, true},
                });
                trace.emplace_back(frame_index++, features, decision, costs);
            }
        }
    }

    OracleConfig oracle_config;
    oracle_config.max_delta_run = oracle_frames.size() + 1U;
    oracle_config.transition_cost = policy_config.transition_cost;
    const OracleResult oracle = solve_offline_oracle(oracle_frames, oracle_config);
    const std::uint64_t online_cost = evaluate_encoding_sequence(
        oracle_frames, online_sequence, oracle_config);
    const std::uint64_t regret = online_cost - oracle.total_cost;
    const double regret_percent = oracle.total_cost == 0U ? 0.0
        : 100.0 * static_cast<double>(regret) / static_cast<double>(oracle.total_cost);

    std::ofstream frames(frames_output);
    frames << "frame,changed_bodies,touched_chunks,density_bucket,dispersion_bucket,selected,"
              "full_ns,active_radix16_ns,component_soa_ns,oracle_selected\n";
    for (std::size_t index = 0U; index < trace.size(); ++index) {
        const auto& [frame, features, decision, costs] = trace[index];
        frames << frame << ',' << features.changed_bodies << ',' << features.touched_chunks << ','
               << decision.density_bucket << ',' << decision.dispersion_bucket << ','
               << representation_name(decision.representation) << ','
               << costs[0] << ',' << costs[1] << ',' << costs[2] << ','
               << representation_name(static_cast<RuntimeRepresentation>(
                    static_cast<std::uint8_t>(oracle.sequence[index]))) << '\n';
    }

    std::array<std::size_t, kRuntimeRepresentationCount> selected_counts{};
    for (const OracleEncoding encoding : online_sequence) ++selected_counts[static_cast<std::size_t>(encoding)];
    std::ofstream summary(summary_output);
    summary << "{\n"
            << "  \"calibration_mode\": \"full_information_excluded_from_evaluation\",\n"
            << "  \"evaluation_frames\": " << oracle_frames.size() << ",\n"
            << "  \"online_cost_ns\": " << online_cost << ",\n"
            << "  \"oracle_cost_ns\": " << oracle.total_cost << ",\n"
            << "  \"regret_ns\": " << regret << ",\n"
            << "  \"regret_percent\": " << std::fixed << std::setprecision(6)
            << regret_percent << ",\n"
            << "  \"selected_full_scan\": " << selected_counts[0] << ",\n"
            << "  \"selected_active_radix16\": " << selected_counts[1] << ",\n"
            << "  \"selected_component_soa\": " << selected_counts[2] << "\n"
            << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("artifacts/v0.5-benchmark");
        std::filesystem::create_directories(output);

        constexpr std::array<std::size_t, 7> active_counts{
            2U, 10U, 100U, 1'000U, 5'000U, 10'000U, 1U};
        std::vector<RuntimeRecord> runtime_records;
        for (const bool dispersed : {false, true}) {
            const std::string pattern = dispersed ? "dispersed" : "clustered";
            for (const std::size_t active_count : active_counts) {
                const WorldState initial = make_world(10'000U, active_count, dispersed);
                const std::size_t begin = runtime_records.size();
                runtime_records.push_back(benchmark_full_scan(initial, pattern, active_count));
                runtime_records.push_back(benchmark_active_binary(initial, pattern, active_count));
                runtime_records.push_back(benchmark_component(initial, pattern, active_count));
                runtime_records.push_back(benchmark_radix(initial, pattern, active_count, 16U));
                runtime_records.push_back(benchmark_radix(initial, pattern, active_count, 32U));
                runtime_records.push_back(benchmark_radix(initial, pattern, active_count, 64U));
                const std::uint64_t expected = runtime_records[begin].final_hash;
                for (std::size_t index = begin; index < runtime_records.size(); ++index) {
                    if (runtime_records[index].final_hash != expected) {
                        throw std::runtime_error("Runtime representations diverged in benchmark");
                    }
                }
            }
        }
        write_runtime_csv(output / "runtime_crossover.csv", runtime_records);

        std::vector<RollbackRecord> rollback_records;
        for (const std::size_t active_count : {2U, 100U, 1'000U, 10'000U}) {
            const WorldState initial = make_world(10'000U, active_count, true);
            const std::size_t begin = rollback_records.size();
            rollback_records.push_back(benchmark_rollback_full(initial, active_count));
            rollback_records.push_back(benchmark_rollback_active(initial, active_count));
            rollback_records.push_back(benchmark_rollback_component(initial, active_count));
            rollback_records.push_back(benchmark_rollback_radix(initial, active_count, 16U));
            rollback_records.push_back(benchmark_rollback_radix(initial, active_count, 32U));
            rollback_records.push_back(benchmark_rollback_radix(initial, active_count, 64U));
            const std::uint64_t expected = rollback_records[begin].final_hash;
            for (std::size_t index = begin; index < rollback_records.size(); ++index) {
                if (rollback_records[index].final_hash != expected) {
                    throw std::runtime_error("Rollback representations diverged in benchmark");
                }
            }
        }
        write_rollback_csv(output / "rollback_matrix.csv", rollback_records);
        write_policy_outputs(output / "policy_frames.csv", output / "policy_summary.json",
                             runtime_records);

        std::cout << "NeoEng v0.5 benchmark completed at " << output << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "NeoEng v0.5 benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
