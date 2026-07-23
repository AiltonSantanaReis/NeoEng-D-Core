#include "neoeng/core/hash.hpp"
#include "neoeng/core/immutable_rollback.hpp"
#include "neoeng/core/offline_oracle.hpp"
#include "neoeng/core/persistent_checkpoint.hpp"
#include "neoeng/core/rollback.hpp"
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
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

namespace {

[[nodiscard]] WorldState make_world(std::size_t count) {
    WorldState state;
    state.bodies.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        state.bodies.push_back(Body{.id = static_cast<EntityId>(index + 1U)});
    }
    return state;
}

[[nodiscard]] double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(
        p * static_cast<double>(values.size() - 1U));
    return values[index];
}

[[nodiscard]] std::uint64_t elapsed_ns(Clock::time_point begin, Clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count());
}

struct RollbackRow final {
    std::string strategy;
    std::size_t chunk_size{};
    double frame_p50_ms{};
    double frame_p95_ms{};
    double rollback_ms{};
    std::size_t live_bytes{};
    std::uint64_t requested_bytes{};
    std::uint64_t bodies_copied{};
    std::uint64_t bodies_scanned{};
    std::uint64_t tree_nodes{};
    std::uint64_t chunks{};
    std::uint64_t canonical_hash{};
    std::uint64_t merkle_hash{};
};

[[nodiscard]] RollbackRow benchmark_legacy(
    SnapshotStrategy strategy,
    std::size_t page_size,
    std::size_t checkpoint_interval,
    std::span<const InputCommand> inputs,
    std::span<const InputCommand> corrected) {
    constexpr std::size_t body_count = 10'000U;
    constexpr std::size_t warmup = 30U;
    constexpr std::size_t measured = 200U;
    constexpr std::size_t rollback_depth = 8U;
    RollbackEngine engine(make_world(body_count), SnapshotStoreConfig{
        .strategy = strategy,
        .capacity = 300U,
        .page_bodies = page_size,
        .checkpoint_interval = checkpoint_interval,
        .persistent_leaf_bodies = page_size,
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
    if (resimulated != rollback_depth) throw std::runtime_error("Legacy rollback depth mismatch");
    const SnapshotStoreStats stats = engine.snapshots().stats();
    return RollbackRow{
        .strategy = std::string(to_string(strategy)),
        .chunk_size = page_size,
        .frame_p50_ms = percentile(frame_times, 0.50),
        .frame_p95_ms = percentile(frame_times, 0.95),
        .rollback_ms = std::chrono::duration<double, std::milli>(rollback_end - rollback_begin).count(),
        .live_bytes = stats.live_payload_bytes + stats.live_metadata_bytes,
        .requested_bytes = stats.payload_bytes_requested + stats.metadata_bytes_requested,
        .bodies_copied = 0U,
        .bodies_scanned = stats.comparison_entities_scanned,
        .tree_nodes = 0U,
        .chunks = 0U,
        .canonical_hash = stable_hash(engine.state()),
        .merkle_hash = 0U,
    };
}

[[nodiscard]] RollbackRow benchmark_immutable(
    std::size_t chunk_size,
    std::span<const InputCommand> inputs,
    std::span<const InputCommand> corrected) {
    constexpr std::size_t body_count = 10'000U;
    constexpr std::size_t warmup = 30U;
    constexpr std::size_t measured = 200U;
    constexpr std::size_t rollback_depth = 8U;
    ImmutableRollbackEngine engine(make_world(body_count), 300U, chunk_size);
    for (std::size_t frame = 0; frame < warmup; ++frame) engine.advance(inputs);
    std::vector<double> frame_times;
    frame_times.reserve(measured);
    for (std::size_t frame = 0; frame < measured; ++frame) {
        const auto begin = Clock::now();
        engine.advance(inputs);
        const auto end = Clock::now();
        frame_times.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    }
    const std::uint64_t correction_frame = engine.state().frame() - rollback_depth;
    const auto rollback_begin = Clock::now();
    const std::size_t resimulated = engine.correct_input_and_resimulate(correction_frame, corrected);
    const auto rollback_end = Clock::now();
    if (resimulated != rollback_depth) throw std::runtime_error("Immutable rollback depth mismatch");
    const ImmutableRollbackStats stats = engine.stats();
    const WorldState materialized = engine.materialized_state();
    return RollbackRow{
        .strategy = "immutable_chunked",
        .chunk_size = chunk_size,
        .frame_p50_ms = percentile(frame_times, 0.50),
        .frame_p95_ms = percentile(frame_times, 0.95),
        .rollback_ms = std::chrono::duration<double, std::milli>(rollback_end - rollback_begin).count(),
        .live_bytes = stats.retained_memory.payload_bytes + stats.retained_memory.metadata_bytes,
        .requested_bytes = stats.cumulative_allocation.chunk_payload_bytes_requested
            + stats.cumulative_allocation.tree_metadata_bytes_requested,
        .bodies_copied = stats.cumulative_allocation.bodies_copied,
        .bodies_scanned = stats.cumulative_allocation.bodies_scanned,
        .tree_nodes = stats.cumulative_allocation.tree_nodes_allocated,
        .chunks = stats.cumulative_allocation.chunks_allocated,
        .canonical_hash = stable_hash(materialized),
        .merkle_hash = engine.state().merkle_hash(),
    };
}

void write_rollback_matrix(const std::filesystem::path& output) {
    constexpr std::size_t body_count = 10'000U;
    const std::vector<InputCommand> inputs{
        {.entity = 1U, .acceleration = {Fixed::from_integer(1), Fixed::from_integer(-1)}},
        {.entity = static_cast<EntityId>(body_count),
         .acceleration = {Fixed::from_ratio(-1, 2), Fixed::from_ratio(1, 3)}},
    };
    std::vector<InputCommand> corrected = inputs;
    corrected.front().acceleration.x += Fixed::from_ratio(1, 7);

    std::vector<RollbackRow> rows;
    rows.push_back(benchmark_legacy(SnapshotStrategy::FullCopy, 64U, 16U, inputs, corrected));
    rows.push_back(benchmark_legacy(SnapshotStrategy::DeltaLog, 64U, 32U, inputs, corrected));
    rows.push_back(benchmark_legacy(SnapshotStrategy::PagedCopyOnWrite, 16U, 16U, inputs, corrected));
    rows.push_back(benchmark_legacy(SnapshotStrategy::PersistentChunkTree, 16U, 16U, inputs, corrected));
    rows.push_back(benchmark_legacy(SnapshotStrategy::HybridAdaptive, 16U, 32U, inputs, corrected));
    for (const std::size_t chunk_size : std::array<std::size_t, 5>{16U, 32U, 64U, 128U, 256U}) {
        rows.push_back(benchmark_immutable(chunk_size, inputs, corrected));
    }

    const std::uint64_t reference = rows.front().canonical_hash;
    for (const RollbackRow& row : rows) {
        if (row.canonical_hash != reference) {
            throw std::runtime_error("v0.4 rollback matrix detected state divergence");
        }
    }

    std::ofstream file(output);
    if (!file) throw std::runtime_error("Unable to create v0.4 rollback matrix");
    file << "strategy,chunk_size,body_count,frame_p50_ms,frame_p95_ms,rollback_depth,rollback_ms,"
            "live_bytes,requested_bytes,bodies_copied,bodies_scanned,tree_nodes_allocated,"
            "chunks_allocated,canonical_hash,merkle_hash\n";
    for (const RollbackRow& row : rows) {
        file << row.strategy << ',' << row.chunk_size << ',' << body_count << ','
             << std::fixed << std::setprecision(6) << row.frame_p50_ms << ',' << row.frame_p95_ms
             << ",8," << row.rollback_ms << ',' << row.live_bytes << ',' << row.requested_bytes
             << ',' << row.bodies_copied << ',' << row.bodies_scanned << ',' << row.tree_nodes
             << ',' << row.chunks << ',' << hash_hex(row.canonical_hash) << ','
             << (row.merkle_hash == 0U ? "" : hash_hex(row.merkle_hash)) << '\n';
    }
}

[[nodiscard]] std::string_view policy_name(PersistentCheckpointPolicy policy) noexcept {
    switch (policy) {
    case PersistentCheckpointPolicy::Fixed: return "fixed";
    case PersistentCheckpointPolicy::Geometric: return "geometric";
    case PersistentCheckpointPolicy::Adaptive: return "adaptive";
    }
    return "unknown";
}

void write_checkpoint_matrix(const std::filesystem::path& output) {
    constexpr std::size_t body_count = 10'000U;
    constexpr std::size_t frames = 160U;
    std::ofstream file(output);
    if (!file) throw std::runtime_error("Unable to create checkpoint matrix");
    file << "policy,max_delta_depth,capture_p50_us,capture_p95_us,restore_1_us,restore_8_us,"
            "restore_32_us,checkpoint_frames,delta_frames,delta_bodies,live_bytes,final_hash\n";

    for (const PersistentCheckpointPolicy policy : std::array{
             PersistentCheckpointPolicy::Fixed,
             PersistentCheckpointPolicy::Geometric,
             PersistentCheckpointPolicy::Adaptive}) {
        for (const std::size_t depth : std::array<std::size_t, 4>{4U, 8U, 16U, 32U}) {
            ImmutableWorldState state = make_immutable_world(make_world(body_count), 32U);
            PersistentCheckpointHistory history(PersistentCheckpointConfig{
                .capacity = 300U,
                .max_delta_depth = depth,
                .policy = policy,
                .adaptive_density_ppm = 50'000U,
            });
            history.capture(state, DirtySet::full(body_count));
            std::vector<double> captures;
            captures.reserve(frames);
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const std::vector<InputCommand> inputs{
                    {.entity = 1U,
                     .acceleration = {Fixed::from_ratio(1, 7), Fixed::from_ratio(-1, 11)}},
                    {.entity = static_cast<EntityId>(body_count),
                     .acceleration = {Fixed::from_ratio(-1, 13), Fixed::from_ratio(1, 17)}},
                };
                const ImmutableStepResult step_result = step_immutable(state, inputs);
                state = step_result.state;
                const auto begin = Clock::now();
                history.capture(state, step_result.dirty);
                const auto end = Clock::now();
                captures.push_back(static_cast<double>(elapsed_ns(begin, end)) / 1'000.0);
            }

            auto restore_us = [&](std::size_t rollback_depth) {
                const std::uint64_t frame = state.frame() - rollback_depth;
                const auto begin = Clock::now();
                const ImmutableWorldState restored = history.restore(frame);
                const volatile std::uint64_t sink = restored.merkle_hash();
                static_cast<void>(sink);
                const auto end = Clock::now();
                return static_cast<double>(elapsed_ns(begin, end)) / 1'000.0;
            };
            const PersistentCheckpointStats stats = history.stats();
            file << policy_name(policy) << ',' << depth << ',' << std::fixed << std::setprecision(3)
                 << percentile(captures, 0.50) << ',' << percentile(captures, 0.95) << ','
                 << restore_us(1U) << ',' << restore_us(8U) << ',' << restore_us(32U) << ','
                 << stats.checkpoint_frames << ',' << stats.delta_frames << ','
                 << stats.delta_bodies_stored << ','
                 << stats.live_delta_payload_bytes + stats.checkpoint_memory.payload_bytes
                    + stats.checkpoint_memory.metadata_bytes << ','
                 << hash_hex(stable_hash(state.materialize())) << '\n';
        }
    }
}

struct TraceMutation final {
    std::vector<std::size_t> indices;
    DirtySet dirty;
};

[[nodiscard]] TraceMutation mutate_trace(WorldState& state, std::size_t trace_frame) {
    const std::size_t count = state.bodies.size();
    TraceMutation mutation{.indices = {}, .dirty = DirtySet(count)};
    const std::size_t phase = trace_frame / 24U;
    std::size_t changed = 0U;
    switch (phase) {
    case 0U: changed = 2U; break;                 // sparse clustered
    case 1U: changed = count / 100U; break;      // 1% dispersed
    case 2U: changed = count / 10U; break;       // 10% clustered
    case 3U: changed = count / 10U; break;       // 10% dispersed
    default: changed = count; break;              // full mutation
    }
    mutation.indices.reserve(changed);
    for (std::size_t item = 0; item < changed; ++item) {
        std::size_t index = 0U;
        if (phase == 0U) {
            index = (trace_frame * 31U + item) % count;
        } else if (phase == 1U || phase == 3U) {
            index = (item * 97U + trace_frame * 13U) % count;
        } else {
            const std::size_t start = (trace_frame * 37U) % (count - changed + 1U);
            index = start + item;
        }
        mutation.indices.push_back(index);
    }
    std::sort(mutation.indices.begin(), mutation.indices.end());
    mutation.indices.erase(std::unique(mutation.indices.begin(), mutation.indices.end()),
                           mutation.indices.end());
    for (const std::size_t index : mutation.indices) {
        state.bodies[index].position.x += Fixed::from_ratio(1, 17);
        state.bodies[index].velocity.y -= Fixed::from_ratio(1, 31);
        mutation.dirty.mark(index, DirtyComponent::PositionX | DirtyComponent::VelocityY);
    }
    ++state.frame;
    return mutation;
}

[[nodiscard]] std::uint64_t timed_restore_ns(
    const ISnapshotStore& store,
    std::uint64_t frame) {
    const auto begin = Clock::now();
    const WorldState restored = store.restore(frame);
    const volatile std::uint64_t sink = stable_hash(restored);
    static_cast<void>(sink);
    return elapsed_ns(begin, Clock::now());
}

void write_oracle_outputs(
    const std::filesystem::path& csv_output,
    const std::filesystem::path& summary_output) {
    constexpr std::size_t body_count = 10'000U;
    constexpr std::size_t trace_frames = 120U;
    constexpr std::size_t chunk_size = 32U;
    constexpr std::size_t memory_gate = 32U * 1024U * 1024U;

    auto full_store = make_snapshot_store(SnapshotStoreConfig{
        .strategy = SnapshotStrategy::FullCopy,
        .capacity = 300U,
        .page_bodies = chunk_size,
        .checkpoint_interval = 16U,
        .persistent_leaf_bodies = chunk_size,
        .audit_dirty_contract = false,
    });
    auto delta_store = make_snapshot_store(SnapshotStoreConfig{
        .strategy = SnapshotStrategy::DeltaLog,
        .capacity = 300U,
        .page_bodies = chunk_size,
        .checkpoint_interval = 16U,
        .persistent_leaf_bodies = chunk_size,
        .audit_dirty_contract = false,
    });

    WorldState state = make_world(body_count);
    DirtySet initial = DirtySet::full(body_count);
    full_store->capture(state, &initial);
    delta_store->capture(state, &initial);
    ImmutableWorldState persistent = make_immutable_world(state, chunk_size);
    std::vector<ImmutableWorldState> persistent_history{persistent};
    persistent_history.reserve(trace_frames + 1U);

    std::vector<OracleFrameCost> frame_costs;
    std::vector<OracleEncoding> online;
    std::vector<std::array<std::uint64_t, 3>> captures;
    std::vector<std::array<std::uint64_t, 3>> restores;
    std::vector<std::array<std::size_t, 3>> live_memory;
    std::vector<std::size_t> changed_counts;
    std::vector<std::size_t> touched_chunks;
    frame_costs.reserve(trace_frames);

    std::size_t online_delta_run = 0U;
    for (std::size_t trace_frame = 0; trace_frame < trace_frames; ++trace_frame) {
        const TraceMutation mutation = mutate_trace(state, trace_frame);
        std::vector<Body> updated;
        updated.reserve(mutation.indices.size());
        for (const std::size_t index : mutation.indices) updated.push_back(state.bodies[index]);

        std::array<std::uint64_t, 3> capture{};
        auto begin = Clock::now();
        full_store->capture(state, &mutation.dirty);
        capture[0] = elapsed_ns(begin, Clock::now());
        begin = Clock::now();
        delta_store->capture(state, &mutation.dirty);
        capture[1] = elapsed_ns(begin, Clock::now());
        ImmutableAllocationStats persistent_allocation;
        begin = Clock::now();
        persistent = apply_immutable_updates(
            persistent, state.frame, mutation.indices, updated, &persistent_allocation);
        capture[2] = elapsed_ns(begin, Clock::now());
        persistent_history.push_back(persistent);

        std::array<std::uint64_t, 3> expected_restore{};
        constexpr std::array<std::size_t, 3> depths{1U, 8U, 32U};
        constexpr std::array<std::uint64_t, 3> weights{700U, 200U, 100U};
        for (std::size_t depth_index = 0; depth_index < depths.size(); ++depth_index) {
            if (state.frame < depths[depth_index]) continue;
            const std::uint64_t target = state.frame - depths[depth_index];
            expected_restore[0] += timed_restore_ns(*full_store, target) * weights[depth_index] / 1'000U;
            expected_restore[1] += timed_restore_ns(*delta_store, target) * weights[depth_index] / 1'000U;
            begin = Clock::now();
            const ImmutableWorldState root = persistent_history[static_cast<std::size_t>(target)];
            const volatile std::uint64_t sink = root.merkle_hash();
            static_cast<void>(sink);
            expected_restore[2] += elapsed_ns(begin, Clock::now()) * weights[depth_index] / 1'000U;
        }

        const SnapshotStoreStats full_stats = full_store->stats();
        const SnapshotStoreStats delta_stats = delta_store->stats();
        const ImmutableMemoryFootprint persistent_memory =
            estimate_retained_immutable_memory(persistent_history);
        const std::array<std::size_t, 3> memory{
            full_stats.live_payload_bytes + full_stats.live_metadata_bytes,
            delta_stats.live_payload_bytes + delta_stats.live_metadata_bytes,
            persistent_memory.payload_bytes + persistent_memory.metadata_bytes,
        };

        OracleFrameCost cost;
        for (std::size_t encoding = 0; encoding < 3U; ++encoding) {
            cost.operation_cost[encoding] = capture[encoding] + expected_restore[encoding];
            cost.allowed[encoding] = memory[encoding] <= memory_gate;
        }
        // The initial full representation is always permitted as a recovery base.
        if (trace_frame == 0U) cost.allowed[0] = true;
        frame_costs.push_back(cost);
        captures.push_back(capture);
        restores.push_back(expected_restore);
        live_memory.push_back(memory);
        changed_counts.push_back(mutation.indices.size());
        const std::size_t pages = mutation.indices.empty() ? 0U : 1U + std::inner_product(
            mutation.indices.begin() + 1, mutation.indices.end(), mutation.indices.begin(),
            std::size_t{0}, std::plus<>{}, [=](std::size_t rhs, std::size_t lhs) {
                return static_cast<std::size_t>(rhs / chunk_size != lhs / chunk_size);
            });
        touched_chunks.push_back(pages);

        OracleEncoding selected = OracleEncoding::Persistent;
        const std::uint64_t density_ppm = mutation.indices.size() * 1'000'000ULL / body_count;
        if (density_ppm >= 500'000U && cost.allowed[0]) {
            selected = OracleEncoding::Full;
            online_delta_run = 0U;
        } else if (pages * chunk_size > mutation.indices.size() * 8U && cost.allowed[1]
                   && online_delta_run < 16U) {
            selected = OracleEncoding::Delta;
            ++online_delta_run;
        } else {
            selected = OracleEncoding::Persistent;
            online_delta_run = 0U;
        }
        if (!cost.allowed[static_cast<std::size_t>(selected)]) {
            selected = cost.allowed[2] ? OracleEncoding::Persistent : OracleEncoding::Delta;
        }
        online.push_back(selected);
    }

    // Transition costs are measured once on the final representative state.
    OracleConfig config;
    config.max_delta_run = 16U;
    const auto conversion_begin = Clock::now();
    const ImmutableWorldState converted = make_immutable_world(state, chunk_size);
    const std::uint64_t full_to_persistent = elapsed_ns(conversion_begin, Clock::now());
    const auto materialize_begin = Clock::now();
    const WorldState materialized = converted.materialize();
    const volatile std::uint64_t conversion_sink = stable_hash(materialized);
    static_cast<void>(conversion_sink);
    const std::uint64_t persistent_to_full = elapsed_ns(materialize_begin, Clock::now());
    const std::uint64_t checkpoint_cost = captures.back()[0];
    config.transition_cost[0][1] = checkpoint_cost;
    config.transition_cost[1][0] = checkpoint_cost;
    config.transition_cost[0][2] = full_to_persistent;
    config.transition_cost[2][0] = persistent_to_full;
    config.transition_cost[1][2] = full_to_persistent;
    config.transition_cost[2][1] = checkpoint_cost;

    const OracleResult oracle = solve_offline_oracle(frame_costs, config);
    const std::uint64_t online_cost = evaluate_encoding_sequence(frame_costs, online, config);
    const std::uint64_t regret = online_cost - oracle.total_cost;

    std::ofstream csv(csv_output);
    if (!csv) throw std::runtime_error("Unable to create oracle frame output");
    csv << "frame,changed_bodies,touched_chunks,full_capture_ns,delta_capture_ns,persistent_capture_ns,"
           "full_expected_restore_ns,delta_expected_restore_ns,persistent_expected_restore_ns,"
           "full_live_bytes,delta_live_bytes,persistent_live_bytes,full_allowed,delta_allowed,"
           "persistent_allowed,online_encoding,oracle_encoding\n";
    const auto encoding_name = [](OracleEncoding encoding) -> std::string_view {
        switch (encoding) {
        case OracleEncoding::Full: return "full";
        case OracleEncoding::Delta: return "delta";
        case OracleEncoding::Persistent: return "persistent";
        }
        return "unknown";
    };
    for (std::size_t frame = 0; frame < frame_costs.size(); ++frame) {
        csv << frame + 1U << ',' << changed_counts[frame] << ',' << touched_chunks[frame] << ','
            << captures[frame][0] << ',' << captures[frame][1] << ',' << captures[frame][2] << ','
            << restores[frame][0] << ',' << restores[frame][1] << ',' << restores[frame][2] << ','
            << live_memory[frame][0] << ',' << live_memory[frame][1] << ',' << live_memory[frame][2]
            << ',' << frame_costs[frame].allowed[0] << ',' << frame_costs[frame].allowed[1] << ','
            << frame_costs[frame].allowed[2] << ',' << encoding_name(online[frame]) << ','
            << encoding_name(oracle.sequence[frame]) << '\n';
    }

    std::ofstream summary(summary_output);
    if (!summary) throw std::runtime_error("Unable to create oracle summary");
    summary << "{\n"
            << "  \"frames\": " << trace_frames << ",\n"
            << "  \"memory_gate_bytes\": " << memory_gate << ",\n"
            << "  \"max_delta_run\": " << config.max_delta_run << ",\n"
            << "  \"online_cost_ns\": " << online_cost << ",\n"
            << "  \"oracle_cost_ns\": " << oracle.total_cost << ",\n"
            << "  \"regret_ns\": " << regret << ",\n"
            << "  \"regret_percent\": " << std::fixed << std::setprecision(3)
            << (oracle.total_cost == 0U ? 0.0
                : static_cast<double>(regret) * 100.0 / static_cast<double>(oracle.total_cost)) << ",\n"
            << "  \"transition_full_to_persistent_ns\": " << full_to_persistent << ",\n"
            << "  \"transition_persistent_to_full_ns\": " << persistent_to_full << ",\n"
            << "  \"transition_checkpoint_ns\": " << checkpoint_cost << "\n"
            << "}\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("artifacts/v0.4-benchmark");
        std::filesystem::create_directories(output);
        write_rollback_matrix(output / "rollback_matrix.csv");
        write_checkpoint_matrix(output / "checkpoint_policies.csv");
        write_oracle_outputs(output / "oracle_frames.csv", output / "oracle_summary.json");
        std::cout << "NeoEng Core Lab v0.4 benchmark completed: " << output.string() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "NeoEng v0.4 benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
