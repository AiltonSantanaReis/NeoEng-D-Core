#include "neoeng/core/hash.hpp"
#include "neoeng/core/atomic_temporal_physics.hpp"
#include "neoeng/core/axis_forest_projection.hpp"
#include "neoeng/core/island_pair_cache.hpp"
#include "neoeng/core/versioned_island_pair_history.hpp"
#include "neoeng/core/oblique_tree_grid_dp.hpp"
#include "neoeng/core/small_oblique_grid_oracle.hpp"
#include "neoeng/core/paged_atomic_temporal_physics.hpp"
#include "neoeng/core/paged_atomic_history.hpp"
#include "neoeng/core/weighted_tree_projection.hpp"
#include "neoeng/core/general_lcp_solver.hpp"
#include "neoeng/core/active_world.hpp"
#include "neoeng/core/component_world.hpp"
#include "neoeng/core/contact_solver.hpp"
#include "neoeng/core/interactive_rollback.hpp"
#include "neoeng/core/broadphase.hpp"
#include "neoeng/core/epoch_arena.hpp"
#include "neoeng/core/fixed_simd.hpp"
#include "neoeng/core/indexed_ring.hpp"
#include "neoeng/core/contextual_policy.hpp"
#include "neoeng/core/radix_world.hpp"
#include "neoeng/core/immutable_rollback.hpp"
#include "neoeng/core/offline_oracle.hpp"
#include "neoeng/core/persistent_checkpoint.hpp"
#include "neoeng/core/rollback.hpp"
#include "neoeng/core/snapshot_store.hpp"

#include <array>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

using namespace neoeng::core;

namespace {

constexpr std::array<SnapshotStrategy, 6> kStrategies{
    SnapshotStrategy::FullCopy,
    SnapshotStrategy::DeltaLog,
    SnapshotStrategy::PagedCopyOnWrite,
    SnapshotStrategy::PersistentChunkTree,
    SnapshotStrategy::ComponentSoA,
    SnapshotStrategy::HybridAdaptive,
};

int failures = 0;

void check(bool condition, std::string_view expression, std::string_view test) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL [" << test << "]: " << expression << '\n';
    }
}

#define CHECK(test_name, expression) check((expression), #expression, (test_name))

WorldState make_world(std::uint32_t count = 4) {
    std::vector<Body> bodies;
    bodies.reserve(count);
    for (std::uint32_t id = 1; id <= count; ++id) {
        bodies.push_back(Body{.id = id});
    }
    return WorldState{.frame = 0, .bodies = std::move(bodies)};
}

WorldState mutate_world(WorldState state, std::uint64_t frame) {
    state.frame = frame;
    for (std::size_t index = 0; index < state.bodies.size(); ++index) {
        if ((index + static_cast<std::size_t>(frame)) % 7U == 0U) {
            Body& body = state.bodies[index];
            body.position.x += Fixed::from_integer(static_cast<Fixed::rep>(frame));
            body.velocity.y -= Fixed::from_ratio(1, 8);
        }
    }
    return state;
}

void test_fixed_arithmetic() {
    constexpr std::string_view name = "fixed_arithmetic";
    const Fixed one = Fixed::from_integer(1);
    const Fixed half = Fixed::from_ratio(1, 2);
    CHECK(name, (one + half).raw() == Fixed::from_ratio(3, 2).raw());
    CHECK(name, (one * half).raw() == half.raw());
    CHECK(name, (half / half).raw() == one.raw());
    CHECK(name, (Fixed::from_integer(-1) * half).raw() == Fixed::from_ratio(-1, 2).raw());
    CHECK(name, (Fixed::from_integer(-1) / Fixed::from_integer(2)).raw()
                    == Fixed::from_ratio(-1, 2).raw());
}

void test_repeated_execution_is_identical() {
    constexpr std::string_view name = "repeated_execution_is_identical";
    RollbackEngine first(make_world());
    RollbackEngine second(make_world());

    for (std::uint64_t frame = 0; frame < 1'000; ++frame) {
        const std::vector<InputCommand> inputs{
            InputCommand{
                .entity = static_cast<EntityId>((frame % 4U) + 1U),
                .acceleration = {Fixed::from_integer(1), Fixed::from_integer(-1)}
            }
        };
        first.advance(inputs);
        second.advance(inputs);
        CHECK(name, stable_hash(first.state()) == stable_hash(second.state()));
    }
}

void test_input_order_is_canonicalized() {
    constexpr std::string_view name = "input_order_is_canonicalized";
    const WorldState world = make_world(2);
    const std::vector<InputCommand> ordered{
        {.entity = 1, .acceleration = {Fixed::from_integer(1), {}}},
        {.entity = 2, .acceleration = {{}, Fixed::from_integer(2)}}
    };
    const std::vector<InputCommand> reversed{ordered.rbegin(), ordered.rend()};
    CHECK(name, step(world, ordered) == step(world, reversed));
}

void test_snapshot_strategies_restore_identically() {
    constexpr std::string_view name = "snapshot_strategies_restore_identically";
    std::vector<WorldState> history;
    history.push_back(make_world(1'025));
    for (std::uint64_t frame = 1; frame <= 40; ++frame) {
        history.push_back(mutate_world(history.back(), frame));
    }

    for (const SnapshotStrategy strategy : kStrategies) {
        auto store = make_snapshot_store(strategy, 64);
        for (const WorldState& state : history) {
            store->capture(state);
        }

        for (const std::uint64_t frame : {0ULL, 1ULL, 17ULL, 40ULL}) {
            const WorldState restored = store->restore(frame);
            CHECK(name, restored == history[static_cast<std::size_t>(frame)]);
            CHECK(name, store->scan_hash(frame) == stable_hash(restored));
        }

        const auto body = store->lookup(40, 513);
        CHECK(name, body.has_value());
        CHECK(name, body.has_value() && *body == history[40].bodies[512]);
        CHECK(name, !store->lookup(40, 99'999).has_value());

        const SnapshotStoreStats stats = store->stats();
        CHECK(name, stats.retained_frames == history.size());
        CHECK(name, stats.live_payload_bytes > 0U);
        CHECK(name, stats.peak_live_payload_bytes >= stats.live_payload_bytes);
        CHECK(name, stats.allocation_count > 0U);
    }
}

void test_snapshot_capacity_and_eviction() {
    constexpr std::string_view name = "snapshot_capacity_and_eviction";
    for (const SnapshotStrategy strategy : kStrategies) {
        auto store = make_snapshot_store(strategy, 5);
        WorldState state = make_world(300);
        store->capture(state);
        for (std::uint64_t frame = 1; frame <= 10; ++frame) {
            state = mutate_world(std::move(state), frame);
            store->capture(state);
        }
        CHECK(name, store->size() == 5U);
        CHECK(name, store->contains(10));
        CHECK(name, store->contains(6));
        CHECK(name, !store->contains(5));
        CHECK(name, store->restore(10) == state);
    }
}

void test_snapshot_frame_gaps_are_rejected() {
    constexpr std::string_view name = "snapshot_frame_gaps_are_rejected";
    for (const SnapshotStrategy strategy : kStrategies) {
        auto store = make_snapshot_store(strategy, 10);
        store->capture(make_world(16));
        WorldState skipped = make_world(16);
        skipped.frame = 2;
        bool threw = false;
        try {
            store->capture(skipped);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(name, threw);
    }
}

void test_truncate_and_recapture() {
    constexpr std::string_view name = "truncate_and_recapture";
    for (const SnapshotStrategy strategy : kStrategies) {
        auto store = make_snapshot_store(strategy, 20);
        WorldState state = make_world(257);
        std::vector<WorldState> history{state};
        store->capture(state);
        for (std::uint64_t frame = 1; frame <= 10; ++frame) {
            state = mutate_world(std::move(state), frame);
            history.push_back(state);
            store->capture(state);
        }

        store->truncate_after(6);
        CHECK(name, store->size() == 7U);
        CHECK(name, !store->contains(7));
        CHECK(name, store->restore(6) == history[6]);

        WorldState replacement = history[6];
        for (std::uint64_t frame = 7; frame <= 10; ++frame) {
            replacement.frame = frame;
            replacement.bodies[0].position.y += Fixed::from_integer(3);
            store->capture(replacement);
        }
        CHECK(name, store->restore(10) == replacement);
    }
}

void test_rollback_recovers_authoritative_state_for_all_strategies() {
    constexpr std::string_view name = "rollback_recovers_authoritative_state_all_strategies";
    const std::vector<InputCommand> normal{
        {.entity = 1, .acceleration = {Fixed::from_integer(1), {}}}
    };
    const std::vector<InputCommand> wrong{
        {.entity = 1, .acceleration = {Fixed::from_integer(9), {}}}
    };

    for (const SnapshotStrategy strategy : kStrategies) {
        RollbackEngine authoritative(make_world(600), 300, strategy);
        RollbackEngine predicted(make_world(600), 300, strategy);

        for (std::uint64_t frame = 0; frame < 30; ++frame) {
            authoritative.advance(normal);
            predicted.advance(frame == 22U ? wrong : normal);
        }
        CHECK(name, stable_hash(authoritative.state()) != stable_hash(predicted.state()));

        const std::size_t frames = predicted.correct_input_and_resimulate(22, normal);
        CHECK(name, frames == 8U);
        CHECK(name, stable_hash(authoritative.state()) == stable_hash(predicted.state()));
        CHECK(name, predicted.snapshots().scan_hash(30) == stable_hash(predicted.state()));
    }
}

void test_cross_strategy_hash_equivalence() {
    constexpr std::string_view name = "cross_strategy_hash_equivalence";
    std::array<std::uint64_t, kStrategies.size()> hashes{};
    std::size_t strategy_index = 0;
    for (const SnapshotStrategy strategy : kStrategies) {
        RollbackEngine engine(make_world(128), 80, strategy);
        for (std::uint64_t frame = 0; frame < 64; ++frame) {
            const std::vector<InputCommand> inputs{
                {.entity = static_cast<EntityId>((frame % 128U) + 1U),
                 .acceleration = {Fixed::from_ratio(3, 2), Fixed::from_ratio(-1, 3)}}
            };
            engine.advance(inputs);
        }
        hashes[strategy_index++] = stable_hash(engine.state());
    }
    for (const std::uint64_t hash : hashes) {
        CHECK(name, hash == hashes.front());
    }
}



void test_canonical_serialization_is_versioned() {
    constexpr std::string_view name = "canonical_serialization_is_versioned";
    const WorldState world = make_world(2);
    const std::vector<std::uint8_t> bytes = canonical_serialize(world);
    CHECK(name, bytes.size() == 24U + 2U * 36U);
    CHECK(name, bytes[0] == static_cast<std::uint8_t>('N'));
    CHECK(name, bytes[1] == static_cast<std::uint8_t>('E'));
    CHECK(name, bytes[2] == static_cast<std::uint8_t>('O'));
    CHECK(name, bytes[3] == static_cast<std::uint8_t>('W'));
    CHECK(name, bytes[4] == static_cast<std::uint8_t>(kCanonicalWorldFormatVersion));
}

void test_dirty_tracking_is_exact() {
    constexpr std::string_view name = "dirty_tracking_is_exact";
    WorldState world = make_world(8);
    world.bodies[1].velocity.x = Fixed::from_integer(2);
    const std::vector<InputCommand> inputs{
        {.entity = 3, .acceleration = {Fixed::from_integer(1), Fixed::from_integer(-1)}}
    };
    const StepResult result = step_with_dirty(world, inputs);
    CHECK(name, dirty_set_describes_transition(world, result.state, result.dirty));
    CHECK(name, result.dirty.is_dirty(1));
    CHECK(name, result.dirty.component_dirty(1, DirtyComponent::PositionX));
    CHECK(name, result.dirty.is_dirty(2));
    CHECK(name, result.dirty.component_dirty(2, DirtyComponent::VelocityX));
    CHECK(name, result.dirty.component_dirty(2, DirtyComponent::VelocityY));
    CHECK(name, !result.dirty.is_dirty(7));
}

void test_dirty_audit_rejects_under_report() {
    constexpr std::string_view name = "dirty_audit_rejects_under_report";
    SnapshotStoreConfig config{
        .strategy = SnapshotStrategy::DeltaLog,
        .capacity = 16,
        .page_bodies = 32,
        .checkpoint_interval = 4,
        .persistent_leaf_bodies = 16,
        .audit_dirty_contract = true,
    };
    auto store = make_snapshot_store(config);
    WorldState world = make_world(32);
    DirtySet initial = DirtySet::full(world.bodies.size());
    store->capture(world, &initial);
    WorldState next = world;
    next.frame = 1;
    next.bodies[5].position.x += Fixed::from_integer(1);
    DirtySet invalid(next.bodies.size());
    bool threw = false;
    try {
        store->capture(next, &invalid);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(name, threw);
}

void test_dirty_path_avoids_comparison_scan() {
    constexpr std::string_view name = "dirty_path_avoids_comparison_scan";
    SnapshotStoreConfig config{
        .strategy = SnapshotStrategy::PagedCopyOnWrite,
        .capacity = 32,
        .page_bodies = 16,
        .checkpoint_interval = 8,
        .persistent_leaf_bodies = 16,
        .audit_dirty_contract = false,
    };
    auto store = make_snapshot_store(config);
    WorldState world = make_world(128);
    DirtySet initial = DirtySet::full(world.bodies.size());
    store->capture(world, &initial);
    for (std::uint64_t frame = 1; frame <= 12; ++frame) {
        world.frame = frame;
        world.bodies[frame % world.bodies.size()].position.x += Fixed::from_integer(1);
        DirtySet dirty(world.bodies.size());
        dirty.mark(frame % world.bodies.size(), DirtyComponent::PositionX);
        store->capture(world, &dirty);
    }
    const SnapshotStoreStats stats = store->stats();
    CHECK(name, stats.comparison_entities_scanned == 0U);
    CHECK(name, stats.dirty_entities_consumed == 12U);
}

void test_configurable_page_and_checkpoint_families() {
    constexpr std::string_view name = "configurable_page_and_checkpoint_families";
    const std::array<std::size_t, 6> pages{16, 32, 64, 128, 256, 512};
    const std::array<std::size_t, 5> checkpoints{4, 8, 16, 32, 64};
    std::uint64_t reference = 0;
    for (const std::size_t page : pages) {
        RollbackEngine engine(make_world(1'025), SnapshotStoreConfig{
            .strategy = SnapshotStrategy::PagedCopyOnWrite,
            .capacity = 80,
            .page_bodies = page,
            .checkpoint_interval = 16,
            .persistent_leaf_bodies = 32,
            .audit_dirty_contract = true,
        });
        for (std::uint64_t frame = 0; frame < 64; ++frame) {
            const std::vector<InputCommand> inputs{{
                .entity = static_cast<EntityId>((frame % 1'025U) + 1U),
                .acceleration = {Fixed::from_ratio(1, 2), Fixed::from_ratio(-1, 3)}}};
            engine.advance(inputs);
        }
        const std::uint64_t hash = stable_hash(engine.state());
        if (reference == 0U) reference = hash;
        CHECK(name, hash == reference);
    }
    for (const std::size_t checkpoint : checkpoints) {
        RollbackEngine engine(make_world(1'025), SnapshotStoreConfig{
            .strategy = SnapshotStrategy::DeltaLog,
            .capacity = 80,
            .page_bodies = 64,
            .checkpoint_interval = checkpoint,
            .persistent_leaf_bodies = 32,
            .audit_dirty_contract = true,
        });
        for (std::uint64_t frame = 0; frame < 64; ++frame) {
            const std::vector<InputCommand> inputs{{
                .entity = static_cast<EntityId>((frame % 1'025U) + 1U),
                .acceleration = {Fixed::from_ratio(1, 2), Fixed::from_ratio(-1, 3)}}};
            engine.advance(inputs);
        }
        CHECK(name, stable_hash(engine.state()) == reference);
    }
}

void test_hybrid_records_decisions_and_nonnegative_regret() {
    constexpr std::string_view name = "hybrid_records_decisions_and_nonnegative_regret";
    SnapshotStoreConfig config{
        .strategy = SnapshotStrategy::HybridAdaptive,
        .capacity = 80,
        .page_bodies = 32,
        .checkpoint_interval = 16,
        .persistent_leaf_bodies = 16,
        .audit_dirty_contract = true,
    };
    RollbackEngine engine(make_world(256), config);
    for (std::uint64_t frame = 0; frame < 48; ++frame) {
        std::vector<InputCommand> inputs;
        const std::size_t count = frame < 16 ? 1U : frame < 32 ? 32U : 256U;
        inputs.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            inputs.push_back(InputCommand{
                .entity = static_cast<EntityId>(index + 1U),
                .acceleration = {Fixed::from_integer(1), Fixed::from_integer(-1)}});
        }
        engine.advance(inputs);
        const auto decision = engine.snapshots().decision_for(frame + 1U);
        CHECK(name, decision.has_value());
        CHECK(name, decision.has_value() && decision->selected_cost_bytes >= decision->oracle_cost_bytes);
        CHECK(name, decision.has_value() && decision->regret_bytes
            == decision->selected_cost_bytes - decision->oracle_cost_bytes);
        CHECK(name, decision.has_value() && decision->features.density_ppm <= 1'000'000U);
    }
    const SnapshotStoreStats stats = engine.snapshots().stats();
    CHECK(name, stats.full_frames > 0U);
    CHECK(name, stats.delta_frames + stats.page_frames > 0U);
}


void test_immutable_transition_matches_aos() {
    constexpr std::string_view name = "immutable_transition_matches_aos";
    for (const std::size_t chunk_size : std::array<std::size_t, 5>{16U, 32U, 64U, 128U, 256U}) {
        WorldState aos = make_world(1'025);
        ImmutableWorldState immutable = make_immutable_world(aos, chunk_size);
        for (std::uint64_t frame = 0; frame < 96U; ++frame) {
            const std::vector<InputCommand> inputs{
                {.entity = static_cast<EntityId>((frame * 17U) % 1'025U + 1U),
                 .acceleration = {Fixed::from_ratio(3, 5), Fixed::from_ratio(-2, 7)}},
                {.entity = static_cast<EntityId>((frame * 29U) % 1'025U + 1U),
                 .acceleration = {Fixed::from_ratio(-1, 9), Fixed::from_ratio(4, 11)}},
            };
            const StepResult aos_result = step_with_dirty(aos, inputs);
            const ImmutableStepResult immutable_result = step_immutable(immutable, inputs);
            CHECK(name, immutable_result.state.materialize() == aos_result.state);
            CHECK(name, immutable_result.dirty.changed_count() == aos_result.dirty.changed_count());
            CHECK(name, stable_hash(immutable_result.state.materialize()) == stable_hash(aos_result.state));
            aos = aos_result.state;
            immutable = immutable_result.state;
        }
    }
}

void test_immutable_shares_untouched_chunks() {
    constexpr std::string_view name = "immutable_shares_untouched_chunks";
    ImmutableAllocationStats initial_allocation;
    ImmutableWorldState state = make_immutable_world(make_world(1'024), 64U, &initial_allocation);
    ImmutableAllocationStats cumulative;
    const std::vector<InputCommand> first{{
        .entity = 1U,
        .acceleration = {Fixed::from_integer(1), Fixed::from_integer(0)}}};
    for (std::size_t frame = 0; frame < 20U; ++frame) {
        const ImmutableStepResult result = step_immutable(
            state, frame == 0U ? std::span<const InputCommand>{first}
                               : std::span<const InputCommand>{});
        cumulative += result.allocation;
        state = result.state;
    }
    CHECK(name, cumulative.chunks_allocated == 20U);
    CHECK(name, cumulative.bodies_copied == 20U * 64U);
    CHECK(name, cumulative.bodies_copied < 20U * 1'024U);
    CHECK(name, cumulative.changed_bodies == 20U);
    CHECK(name, initial_allocation.chunks_allocated == 16U);
}

void test_immutable_rollback_matches_authoritative() {
    constexpr std::string_view name = "immutable_rollback_matches_authoritative";
    const std::vector<InputCommand> normal{{
        .entity = 7U,
        .acceleration = {Fixed::from_ratio(1, 3), Fixed::from_ratio(-1, 5)}}};
    const std::vector<InputCommand> wrong{{
        .entity = 7U,
        .acceleration = {Fixed::from_integer(9), Fixed::from_ratio(-1, 5)}}};
    ImmutableRollbackEngine authoritative(make_world(2'048), 300U, 64U);
    ImmutableRollbackEngine predicted(make_world(2'048), 300U, 64U);
    for (std::uint64_t frame = 0; frame < 40U; ++frame) {
        authoritative.advance(normal);
        predicted.advance(frame == 32U ? wrong : normal);
    }
    CHECK(name, stable_hash(authoritative.materialized_state())
        != stable_hash(predicted.materialized_state()));
    CHECK(name, predicted.correct_input_and_resimulate(32U, normal) == 8U);
    CHECK(name, predicted.materialized_state() == authoritative.materialized_state());
    CHECK(name, predicted.state().merkle_hash() == authoritative.state().merkle_hash());
}

void test_persistent_checkpoints_restore_and_promote() {
    constexpr std::string_view name = "persistent_checkpoints_restore_and_promote";
    for (const PersistentCheckpointPolicy policy : std::array{
             PersistentCheckpointPolicy::Fixed,
             PersistentCheckpointPolicy::Geometric,
             PersistentCheckpointPolicy::Adaptive}) {
        PersistentCheckpointHistory history(PersistentCheckpointConfig{
            .capacity = 12U,
            .max_delta_depth = 4U,
            .policy = policy,
            .adaptive_density_ppm = 250'000U,
        });
        WorldState aos = make_world(257);
        ImmutableWorldState immutable = make_immutable_world(aos, 32U);
        history.capture(immutable, DirtySet::full(aos.bodies.size()));
        std::vector<WorldState> retained{aos};
        for (std::uint64_t frame = 0; frame < 24U; ++frame) {
            const std::vector<InputCommand> inputs{{
                .entity = static_cast<EntityId>((frame % 7U) + 1U),
                .acceleration = {Fixed::from_ratio(1, 4), Fixed::from_ratio(-1, 6)}}};
            const ImmutableStepResult result = step_immutable(immutable, inputs);
            immutable = result.state;
            history.capture(immutable, result.dirty);
            retained.push_back(immutable.materialize());
        }
        CHECK(name, history.size() == 12U);
        for (std::uint64_t frame = 13U; frame <= 24U; ++frame) {
            CHECK(name, history.contains(frame));
            CHECK(name, history.restore(frame).materialize() == retained[frame]);
        }
        const PersistentCheckpointStats stats = history.stats();
        CHECK(name, stats.checkpoint_frames > 0U);
        CHECK(name, stats.delta_frames > 0U);
        CHECK(name, stats.checkpoint_memory.unique_chunks > 0U);
    }
}

void test_offline_oracle_includes_transition_and_checkpoint_constraints() {
    constexpr std::string_view name = "offline_oracle_constraints";
    const std::vector<OracleFrameCost> frames{
        {{{100U, 10U, 30U}}, {{true, true, true}}},
        {{{100U, 10U, 30U}}, {{true, true, true}}},
        {{{100U, 10U, 30U}}, {{true, true, true}}},
        {{{20U, 100U, 25U}}, {{true, true, true}}},
        {{{20U, 100U, 25U}}, {{true, true, true}}},
    };
    OracleConfig config;
    config.max_delta_run = 2U;
    config.transition_cost[1][0] = 7U;
    config.transition_cost[1][2] = 4U;
    const OracleResult result = solve_offline_oracle(frames, config);
    CHECK(name, result.sequence.size() == frames.size());
    CHECK(name, result.sequence[0] == OracleEncoding::Delta);
    CHECK(name, result.sequence[1] == OracleEncoding::Delta);
    CHECK(name, result.sequence[2] != OracleEncoding::Delta);
    CHECK(name, result.total_cost == evaluate_encoding_sequence(frames, result.sequence, config));
    CHECK(name, result.total_cost < evaluate_encoding_sequence(
        frames,
        std::array{OracleEncoding::Persistent, OracleEncoding::Persistent,
                   OracleEncoding::Persistent, OracleEncoding::Persistent,
                   OracleEncoding::Persistent},
        config));
}

void test_merkle_hash_is_deterministic_and_domain_separated() {
    constexpr std::string_view name = "merkle_hash_deterministic";
    const WorldState world = make_world(130);
    const ImmutableWorldState first = make_immutable_world(world, 32U);
    const ImmutableWorldState second = make_immutable_world(world, 32U);
    const ImmutableWorldState different_chunking = make_immutable_world(world, 64U);
    CHECK(name, first.merkle_hash() == second.merkle_hash());
    CHECK(name, first.merkle_hash() != different_chunking.merkle_hash());
    const ImmutableStepResult changed = step_immutable(first, std::array{InputCommand{
        .entity = 1U,
        .acceleration = {Fixed::from_integer(1), Fixed::from_integer(0)}}});
    CHECK(name, changed.state.merkle_hash() != first.merkle_hash());
    CHECK(name, stable_hash(changed.state.materialize())
        == stable_hash(step(world, std::array{InputCommand{
            .entity = 1U,
            .acceleration = {Fixed::from_integer(1), Fixed::from_integer(0)}}})));
}


void test_active_step_matches_full_scan_and_skips_inactive_bodies() {
    constexpr std::string_view name = "active_step_matches_full_scan";
    WorldState aos = make_world(10'000);
    ImmutableWorldState full = make_immutable_world(aos, 64U);
    ImmutableWorldState active_state = full;
    DeterministicActiveSet active = DeterministicActiveSet::from_world(aos);
    std::uint64_t scanned = 0U;
    for (std::uint64_t frame = 0; frame < 120U; ++frame) {
        std::vector<InputCommand> inputs;
        if (frame == 0U) {
            inputs = {
                {.entity = 7U, .acceleration = {Fixed::from_ratio(1, 3), Fixed::from_ratio(-1, 5)}},
                {.entity = 9'997U, .acceleration = {Fixed::from_ratio(-2, 7), Fixed::from_ratio(1, 11)}},
            };
        }
        const StepResult aos_result = step_with_dirty(aos, inputs);
        const ImmutableStepResult full_result = step_immutable(full, inputs);
        const ActiveImmutableStepResult active_result = step_immutable_active(active_state, active, inputs);
        CHECK(name, full_result.state.materialize() == aos_result.state);
        CHECK(name, active_result.state.materialize() == aos_result.state);
        CHECK(name, active_result.active.size() == 2U);
        scanned += active_result.stats.candidate_bodies_scanned;
        aos = aos_result.state;
        full = full_result.state;
        active_state = active_result.state;
        active = active_result.active;
    }
    CHECK(name, scanned == 240U);
    CHECK(name, scanned < 120U * 10'000U);
}

void test_active_rollback_matches_authoritative() {
    constexpr std::string_view name = "active_rollback_matches_authoritative";
    const std::vector<InputCommand> normal{{
        .entity = 12U,
        .acceleration = {Fixed::from_ratio(1, 4), Fixed::from_ratio(-1, 9)}}};
    const std::vector<InputCommand> wrong{{
        .entity = 12U,
        .acceleration = {Fixed::from_integer(8), Fixed::from_ratio(-1, 9)}}};
    ActiveRollbackEngine authoritative(make_world(10'000), 300U, 64U);
    ActiveRollbackEngine predicted(make_world(10'000), 300U, 64U);
    for (std::uint64_t frame = 0; frame < 40U; ++frame) {
        authoritative.advance(frame == 0U ? normal : std::span<const InputCommand>{});
        predicted.advance(frame == 32U ? wrong : (frame == 0U ? std::span<const InputCommand>{normal}
                                                              : std::span<const InputCommand>{}));
    }
    CHECK(name, authoritative.materialized_state() != predicted.materialized_state());
    CHECK(name, predicted.correct_input_and_resimulate(32U, std::span<const InputCommand>{}) == 8U);
    // The wrong command was removed, so both histories contain only the frame-zero activation.
    CHECK(name, authoritative.materialized_state() == predicted.materialized_state());
    CHECK(name, predicted.stats().cumulative_step.candidate_bodies_scanned < 10'000U);
}

void test_component_world_matches_active_immutable() {
    constexpr std::string_view name = "component_world_matches_active_immutable";
    WorldState aos = make_world(2'049);
    ImmutableWorldState immutable = make_immutable_world(aos, 64U);
    ComponentWorldState component = make_component_world(aos, 64U);
    DeterministicActiveSet immutable_active;
    DeterministicActiveSet component_active;
    ComponentAllocationStats cumulative;
    for (std::uint64_t frame = 0; frame < 96U; ++frame) {
        std::vector<InputCommand> inputs;
        if (frame % 24U == 0U) {
            inputs.push_back(InputCommand{
                .entity = static_cast<EntityId>((frame * 31U) % 2'049U + 1U),
                .acceleration = {Fixed::from_ratio(2, 5), Fixed::from_ratio(-3, 11)}});
        }
        const ActiveImmutableStepResult immutable_result =
            step_immutable_active(immutable, immutable_active, inputs);
        const ComponentStepResult component_result =
            step_component_active(component, component_active, inputs);
        CHECK(name, component_result.state.materialize() == immutable_result.state.materialize());
        CHECK(name, component_result.active == immutable_result.active);
        cumulative += component_result.allocation;
        immutable = immutable_result.state;
        immutable_active = immutable_result.active;
        component = component_result.state;
        component_active = component_result.active;
    }
    CHECK(name, cumulative.component_values_copied > 0U);
    CHECK(name, cumulative.candidate_bodies_scanned < 96U * 2'049U);
}

void test_radix_fanouts_match_active_immutable() {
    constexpr std::string_view name = "radix_fanouts_match_active_immutable";
    for (const std::size_t fanout : std::array<std::size_t, 3>{16U, 32U, 64U}) {
        WorldState aos = make_world(4'097);
        RadixWorldState radix = make_radix_world(aos, 64U, fanout);
        DeterministicActiveSet active;
        for (std::uint64_t frame = 0; frame < 80U; ++frame) {
            std::vector<InputCommand> inputs;
            if (frame == 0U || frame == 20U) {
                inputs.push_back(InputCommand{
                    .entity = static_cast<EntityId>((frame * 197U) % 4'097U + 1U),
                    .acceleration = {Fixed::from_ratio(1, 7), Fixed::from_ratio(-2, 13)}});
            }
            const StepResult expected = step_with_dirty(aos, inputs);
            const RadixStepResult result = step_radix_active(radix, active, inputs);
            CHECK(name, result.state.materialize() == expected.state);
            aos = expected.state;
            radix = result.state;
            active = result.active;
        }
        CHECK(name, radix.fanout() == fanout);
    }
}



void test_active_set_deactivates_on_exact_zero_velocity() {
    constexpr std::string_view name = "active_set_exact_deactivation";
    WorldState world = make_world(4);
    world.bodies[1].velocity.x = kSimulationDelta;
    ImmutableWorldState state = make_immutable_world(world, 2U);
    DeterministicActiveSet active = DeterministicActiveSet::from_world(world);
    CHECK(name, active.size() == 1U);
    const std::array<InputCommand, 1> inputs{{
        {.entity = world.bodies[1].id,
         .acceleration = {Fixed::from_integer(-1), Fixed{}}}
    }};
    const ActiveImmutableStepResult result = step_immutable_active(state, active, inputs);
    const WorldState expected = step(world, inputs);
    CHECK(name, result.state.materialize() == expected);
    CHECK(name, result.active.empty());
    CHECK(name, result.state.body_at(1).velocity.x.raw() == 0);
}

void test_batch_update_rejects_duplicate_indices() {
    constexpr std::string_view name = "batch_update_rejects_duplicates";
    const WorldState world = make_world(8);
    const ImmutableWorldState state = make_immutable_world(world, 4U);
    const std::array<std::size_t, 2> indices{1U, 1U};
    const std::array<Body, 2> bodies{world.bodies[1], world.bodies[1]};
    bool threw = false;
    try {
        static_cast<void>(apply_immutable_updates(state, 1U, indices, bodies));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(name, threw);
}

void test_contextual_policy_uses_calibrated_contexts() {
    constexpr std::string_view name = "contextual_policy_calibrated_contexts";
    ContextualPolicyConfig config;
    config.prior_weight = 0U;
    config.planning_horizon_frames = 8U;
    ContextualEncodingPolicy policy(config);
    const PolicyFeatures sparse{
        .body_count = 10'000U,
        .changed_bodies = 2U,
        .touched_chunks = 2U,
        .chunk_size = 64U,
    };
    const PolicyFeatures dense{
        .body_count = 10'000U,
        .changed_bodies = 5'000U,
        .touched_chunks = 157U,
        .chunk_size = 64U,
    };
    policy.observe(sparse, {100U, 10U, 20U});
    policy.observe(dense, {70U, 90U, 15U});
    CHECK(name, policy.choose(sparse, {true, true, true}).representation
        == RuntimeRepresentation::ActiveChunkedAoS);
    CHECK(name, policy.choose(dense, {true, true, true}).representation
        == RuntimeRepresentation::ActiveComponentSoA);
}


void test_fixed_simd_matches_scalar_bit_exactly() {
    constexpr std::string_view name = "fixed_simd_bit_exact";
    std::vector<Fixed> values;
    for (std::int64_t raw : {
            -9'000'000'000LL, -4'294'967'297LL, -4'294'967'296LL, -1LL,
            0LL, 1LL, 4'294'967'295LL, 4'294'967'296LL, 9'000'000'000LL}) {
        values.push_back(Fixed::from_raw(raw));
    }
    std::vector<Fixed> scalar(values.size());
    std::vector<Fixed> automatic(values.size());
    FixedKernelStats scalar_stats;
    FixedKernelStats auto_stats;
    multiply_simulation_delta_exact(values, scalar, FixedKernelMode::Scalar, &scalar_stats);
    multiply_simulation_delta_exact(values, automatic, FixedKernelMode::Auto, &auto_stats);
    CHECK(name, scalar == automatic);
    CHECK(name, scalar_stats.scalar_lanes == values.size());
    CHECK(name, auto_stats.scalar_lanes + auto_stats.simd_lanes == values.size());
    if (fixed_simd_available()) CHECK(name, auto_stats.simd_lanes >= 4U);
}

void test_direct_component_kernel_matches_legacy_without_body_reconstruction() {
    constexpr std::string_view name = "direct_component_kernel";
    WorldState world = make_world(257);
    for (std::size_t index = 0U; index < world.bodies.size(); ++index) {
        if (index % 3U == 0U) {
            world.bodies[index].velocity.x = Fixed::from_ratio(1, 3);
            world.bodies[index].velocity.y = Fixed::from_ratio(-1, 7);
        }
    }
    ComponentWorldState direct = make_component_world(world, 32U);
    ComponentWorldState legacy = direct;
    DeterministicActiveSet direct_active = DeterministicActiveSet::from_world(world);
    DeterministicActiveSet legacy_active = direct_active;
    for (std::uint64_t frame = 0U; frame < 40U; ++frame) {
        const std::array<InputCommand, 1U> inputs{InputCommand{
            .entity = static_cast<EntityId>((frame % world.bodies.size()) + 1U),
            .acceleration = {Fixed::from_ratio(1, 11), Fixed::from_ratio(-1, 13)},
        }};
        const ComponentStepResult direct_result = step_component_active(
            direct, direct_active, inputs, ComponentStepOptions{.kernel_mode = FixedKernelMode::Auto});
        const ComponentStepResult legacy_result = step_component_active_legacy(
            legacy, legacy_active, inputs);
        CHECK(name, direct_result.state.materialize() == legacy_result.state.materialize());
        CHECK(name, direct_result.active == legacy_result.active);
        CHECK(name, direct_result.allocation.body_reconstructions == 0U);
        CHECK(name, legacy_result.allocation.body_reconstructions
            == legacy_result.allocation.candidate_bodies_scanned);
        direct = direct_result.state;
        legacy = legacy_result.state;
        direct_active = direct_result.active;
        legacy_active = legacy_result.active;
    }
}

void test_broadphase_is_conservative_and_wakes_contact_chain() {
    constexpr std::string_view name = "broadphase_conservative_chain";
    WorldState world = make_world(6);
    const Fixed spacing = Fixed::from_ratio(3, 2);
    for (std::size_t index = 0U; index < world.bodies.size(); ++index) {
        world.bodies[index].position.x = Fixed::from_integer(static_cast<Fixed::rep>(index)) * spacing;
    }
    world.bodies.front().velocity.x = Fixed::from_ratio(1, 10);
    ComponentWorldState component = make_component_world(world, 8U);
    const Fixed half_extent = Fixed::from_integer(1);
    const GridBroadphaseState grid = make_grid_broadphase(
        component, Fixed::from_integer(2), half_extent);
    const std::vector<BroadphasePair> brute = brute_force_overlap_pairs(component, half_extent);
    const IslandClosure closure = conservative_island_closure(
        component, grid, DeterministicActiveSet(std::vector<std::size_t>{0U}));
    CHECK(name, closure.overlaps == brute);
    CHECK(name, closure.bodies.size() == world.bodies.size());
    CHECK(name, closure.stats.bodies_woken == world.bodies.size() - 1U);
}

void test_broadphase_incremental_membership_update() {
    constexpr std::string_view name = "broadphase_incremental_update";
    WorldState before_world = make_world(3);
    before_world.bodies[0].position.x = Fixed::from_integer(0);
    before_world.bodies[1].position.x = Fixed::from_integer(4);
    before_world.bodies[2].position.x = Fixed::from_integer(8);
    ComponentWorldState before = make_component_world(before_world, 4U);
    GridBroadphaseState grid = make_grid_broadphase(
        before, Fixed::from_integer(2), Fixed::from_ratio(1, 2));

    WorldState after_world = before_world;
    after_world.frame = 1U;
    after_world.bodies[1].position.x = Fixed::from_integer(7);
    ComponentWorldState after = make_component_world(after_world, 4U);
    DirtySet dirty(after.body_count());
    dirty.mark(1U, DirtyComponent::PositionX);
    BroadphaseStats stats;
    const GridBroadphaseState updated = update_grid_broadphase(grid, after, dirty, &stats);
    CHECK(name, updated.frame() == 1U);
    CHECK(name, updated.cell_of(1U) != grid.cell_of(1U));
    CHECK(name, stats.cell_memberships_moved == 1U);
    CHECK(name, stats.cell_maps_cloned == 1U);
}

void test_indexed_ring_detects_aliases_and_supports_recapture() {
    constexpr std::string_view name = "indexed_ring";
    IndexedFrameRing<int> ring(3U);
    ring.capture(10U, 10);
    ring.capture(11U, 11);
    ring.capture(12U, 12);
    ring.capture(13U, 13);
    CHECK(name, !ring.contains(10U));
    CHECK(name, ring.contains(11U));
    CHECK(name, ring.at(13U) == 13);
    ring.truncate_after(11U);
    CHECK(name, !ring.contains(12U));
    ring.capture(12U, 120);
    CHECK(name, ring.at(12U) == 120);
}

void test_epoch_arena_reclaims_by_frame_slot() {
    constexpr std::string_view name = "epoch_arena";
    PersistentEpochArena arena(3U, 256U);
    arena.begin_epoch(0U);
    auto first = arena.allocate_array<std::uint64_t>(8U);
    first[0] = 42U;
    arena.begin_epoch(1U);
    auto second = arena.allocate_array<std::uint32_t>(16U);
    second[0] = 7U;
    arena.begin_epoch(3U);
    auto third = arena.allocate_array<std::uint64_t>(8U);
    third[0] = 99U;
    const EpochArenaStats stats = arena.stats();
    CHECK(name, stats.allocations == 3U);
    CHECK(name, stats.epochs_reclaimed == 1U);
    CHECK(name, third[0] == 99U);
}


void test_swept_aabb_prevents_tunneling() {
    constexpr std::string_view name = "swept_aabb_tunneling";
    WorldState world = make_world(2);
    world.bodies[0].position.x = Fixed::from_integer(-1);
    world.bodies[1].position.x = Fixed::from_integer(1);
    world.bodies[0].velocity.x = Fixed::from_integer(60);
    world.bodies[1].velocity.x = Fixed::from_integer(-60);
    const ComponentWorldState component = make_component_world(world, 8U);
    const DeterministicActiveSet active = DeterministicActiveSet::from_world(world);
    const ContactStepResult result = step_component_contacts(
        component, active, {}, ContactSolverConfig{
            .half_extent = Fixed::from_ratio(1, 2),
            .cell_size = Fixed::from_integer(2),
            .position_iterations = 4U,
            .max_cells_per_body = 64U,
        });
    CHECK(name, result.contacts.size() == 1U);
    CHECK(name, !result.contacts.front().initial_overlap);
    CHECK(name, result.contacts.front().final_overlap);
    const WorldState solved = result.state.materialize();
    CHECK(name, solved.bodies[0].position.x <= solved.bodies[1].position.x);
    CHECK(name, solved.bodies[0].velocity.x.raw() == 0);
    CHECK(name, solved.bodies[1].velocity.x.raw() == 0);
}

void test_swept_grid_matches_bruteforce() {
    constexpr std::string_view name = "swept_grid_matches_bruteforce";
    WorldState world = make_world(8);
    for (std::size_t index = 0U; index < world.bodies.size(); ++index) {
        world.bodies[index].position.x = Fixed::from_ratio(
            static_cast<Fixed::rep>(index * 3U), 2);
        world.bodies[index].velocity.x = index % 2U == 0U
            ? Fixed::from_integer(3) : Fixed::from_integer(-3);
    }
    const ComponentWorldState current = make_component_world(world, 8U);
    const ComponentStepResult predicted = step_component_active(
        current, DeterministicActiveSet::from_world(world), {});
    const ContactSolverConfig config{
        .half_extent = Fixed::from_ratio(1, 2),
        .cell_size = Fixed::from_integer(2),
        .position_iterations = 4U,
        .max_cells_per_body = 64U,
    };
    const auto grid = swept_aabb_contacts(current, predicted.state, config);
    const auto brute = brute_force_swept_aabb_contacts(
        current, predicted.state, config.half_extent);
    CHECK(name, grid == brute);
}

void test_interactive_rollback_matches_fresh_resimulation() {
    constexpr std::string_view name = "interactive_rollback";
    WorldState world = make_world(4);
    world.bodies[0].position.x = Fixed::from_integer(-2);
    world.bodies[1].position.x = Fixed::from_integer(2);
    world.bodies[0].velocity.x = Fixed::from_integer(10);
    world.bodies[1].velocity.x = Fixed::from_integer(-10);
    world.bodies[2].position.x = Fixed::from_integer(100);
    world.bodies[3].position.x = Fixed::from_integer(200);
    InteractiveRollbackConfig config;
    config.capacity = 32U;
    config.page_size = 8U;
    config.arena_bytes_per_epoch = 4096U;
    config.contacts.half_extent = Fixed::from_ratio(1, 2);
    config.contacts.cell_size = Fixed::from_integer(2);

    InteractiveRollbackEngine rollback(world, config);
    InteractiveRollbackEngine fresh(world, config);
    const InputCommand original{
        .entity = 3U,
        .acceleration = {Fixed::from_integer(1), {}},
    };
    const InputCommand corrected{
        .entity = 3U,
        .acceleration = {Fixed::from_integer(2), {}},
    };
    for (std::uint64_t frame = 0U; frame < 12U; ++frame) {
        if (frame == 4U) rollback.advance(std::span<const InputCommand>(&original, 1U));
        else rollback.advance({});
        if (frame == 4U) fresh.advance(std::span<const InputCommand>(&corrected, 1U));
        else fresh.advance({});
    }
    const std::size_t resimulated = rollback.correct_input_and_resimulate(
        4U, std::span<const InputCommand>(&corrected, 1U));
    CHECK(name, resimulated == 8U);
    CHECK(name, rollback.materialized_state() == fresh.materialized_state());
    CHECK(name, rollback.stats().arena.allocations > 0U);
    CHECK(name, rollback.stats().rollback_frames_resimulated == 8U);
}


void test_temporal_matching_fused_matches_reference() {
    constexpr std::string_view name = "temporal_matching_fused";
    WorldState world;
    for (std::size_t pair = 0U; pair < 16U; ++pair) {
        const Fixed base = Fixed::from_integer(static_cast<Fixed::rep>(pair * 4U));
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(pair * 2U + 1U),
            .position = {base, {}},
            .velocity = {Fixed::from_integer(8), {}},
        });
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(pair * 2U + 2U),
            .position = {base + Fixed::from_ratio(5, 4), {}},
            .velocity = {Fixed::from_integer(-8), {}},
        });
    }
    const ComponentWorldState current = make_component_world(world, 16U);
    const DeterministicActiveSet active = DeterministicActiveSet::from_world(world);
    ContactSolverConfig contacts;
    TemporalBroadphaseConfig temporal;
    const TemporalBroadphaseState cache = make_temporal_broadphase(
        current, contacts, temporal);
    const TemporalContactStepResult fused = step_component_contacts_temporal(
        current, active, cache, PersistentManifoldState{}, {}, contacts, temporal,
        ComponentStepOptions{.kernel_mode = FixedKernelMode::Scalar});
    const ContactStepResult reference = step_component_contacts(
        current, active, {}, contacts,
        ComponentStepOptions{.kernel_mode = FixedKernelMode::Scalar});
    CHECK(name, fused.contact.state.materialize() == reference.state.materialize());
    CHECK(name, fused.contact.active == reference.active);
    CHECK(name, fused.contact.contacts == reference.contacts);
    CHECK(name, fused.temporal_stats.pair_cache_reuses > 0U);
    CHECK(name, temporal_cache_is_conservative(
        fused.contact.state, fused.broadphase, contacts));
}

void test_temporal_cache_escapes_on_acceleration() {
    constexpr std::string_view name = "temporal_cache_escape";
    WorldState world = make_world(4);
    for (std::size_t index = 0U; index < world.bodies.size(); ++index) {
        world.bodies[index].position.x = Fixed::from_integer(
            static_cast<Fixed::rep>(index * 10U));
        world.bodies[index].velocity = {};
    }
    const ComponentWorldState current = make_component_world(world, 4U);
    const DeterministicActiveSet active = DeterministicActiveSet::from_world(world);
    ContactSolverConfig contacts;
    TemporalBroadphaseConfig temporal{.horizon_frames = 8U, .incremental_body_limit = 8U};
    const TemporalBroadphaseState cache = make_temporal_broadphase(
        current, contacts, temporal);
    const InputCommand acceleration{
        .entity = 1U,
        .acceleration = {Fixed::from_integer(6'000), {}},
    };
    const TemporalContactStepResult temporal_result = step_component_contacts_temporal(
        current, active, cache, PersistentManifoldState{},
        std::span<const InputCommand>(&acceleration, 1U), contacts, temporal,
        ComponentStepOptions{.kernel_mode = FixedKernelMode::Scalar});
    const ContactStepResult reference = step_component_contacts(
        current, active, std::span<const InputCommand>(&acceleration, 1U), contacts,
        ComponentStepOptions{.kernel_mode = FixedKernelMode::Scalar});
    CHECK(name, temporal_result.contact.state.materialize() == reference.state.materialize());
    CHECK(name, temporal_result.contact.active == reference.active);
    CHECK(name, temporal_result.temporal_stats.escaped_bodies > 0U);
    CHECK(name, temporal_result.temporal_stats.pair_cache_incremental_updates > 0U);
}

void test_temporal_escape_preserves_initial_contact() {
    constexpr std::string_view name = "temporal_escape_preserves_initial_contact";
    WorldState world;
    world.bodies = {
        Body{.id = 1U, .position = {{}, {}}, .velocity = {}},
        Body{.id = 2U, .position = {Fixed::from_ratio(3, 4), {}}, .velocity = {}},
    };
    const ComponentWorldState current = make_component_world(world, 4U);
    const DeterministicActiveSet active = DeterministicActiveSet::from_world(world);
    ContactSolverConfig contacts;
    TemporalBroadphaseConfig temporal{.horizon_frames = 8U, .incremental_body_limit = 8U};
    const TemporalBroadphaseState cache = make_temporal_broadphase(
        current, contacts, temporal);
    const InputCommand acceleration{
        .entity = 2U,
        .acceleration = {Fixed::from_integer(6'000), {}},
    };
    const auto input_span = std::span<const InputCommand>(&acceleration, 1U);
    const TemporalContactStepResult temporal_result = step_component_contacts_temporal(
        current, active, cache, PersistentManifoldState{}, input_span,
        contacts, temporal, ComponentStepOptions{.kernel_mode = FixedKernelMode::Scalar});
    ComponentStepResult integrated = step_component_active(
        current, active, input_span,
        ComponentStepOptions{.kernel_mode = FixedKernelMode::Scalar});
    std::vector<SweptContact> brute_force = brute_force_swept_aabb_contacts(
        current, integrated.state, contacts.half_extent);
    const ContactStepResult reference = solve_component_contact_constraints(
        current, std::move(integrated), std::move(brute_force), contacts);
    CHECK(name, temporal_result.contact.state.materialize() == reference.state.materialize());
    CHECK(name, temporal_result.contact.contacts == reference.contacts);
    CHECK(name, !reference.contacts.empty());
    CHECK(name, reference.contacts.front().initial_overlap);
    CHECK(name, temporal_result.temporal_stats.escaped_bodies > 0U);
}

void test_chain_isotonic_solver_and_warm_start() {
    constexpr std::string_view name = "chain_isotonic_solver_and_warm_start";
    WorldState world;
    for (std::size_t index = 0U; index < 64U; ++index) {
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(index + 1U),
            .position = {Fixed::from_integer(static_cast<Fixed::rep>(index)), {}},
            .velocity = index == 0U ? Vec2{Fixed::from_integer(6), {}} : Vec2{},
        });
    }
    InteractiveRollbackConfig cold_config;
    cold_config.page_size = 32U;
    cold_config.contacts.connected_solver_mode = ConnectedContactSolverMode::ChainIsotonic;
    cold_config.contacts.enable_chain_warm_start = false;
    InteractiveRollbackConfig warm_config = cold_config;
    warm_config.contacts.enable_chain_warm_start = true;
    InteractiveRollbackEngine cold(world, cold_config);
    InteractiveRollbackEngine warm(world, warm_config);
    for (std::size_t frame = 0U; frame < 8U; ++frame) {
        cold.advance({});
        warm.advance({});
    }
    CHECK(name, cold.materialized_state() == warm.materialized_state());
    const WorldState solved = warm.materialized_state();
    for (std::size_t index = 1U; index < solved.bodies.size(); ++index) {
        CHECK(name, solved.bodies[index].position.x - solved.bodies[index - 1U].position.x
            >= Fixed::from_integer(1));
        CHECK(name, solved.bodies[index - 1U].velocity.x <= solved.bodies[index].velocity.x);
    }
    const InteractiveRollbackStats before = warm.stats();
    CHECK(name, before.cumulative_solver.chain_solver_accepts >= 8U);
    CHECK(name, before.cumulative_solver.warm_start_accepts > 0U);
    CHECK(name, warm.correct_input_and_resimulate(0U, {}) == 8U);
    CHECK(name, warm.materialized_state() == solved);
}


void test_general_projection_certificates_and_reduction() {
    constexpr std::string_view name = "general_projection_certificates_and_reduction";
    std::vector<SweptContact> contacts{
        SweptContact{.first = 0U, .second = 1U, .axis = ContactAxis::X,
                     .toi = {}, .initial_overlap = true, .final_overlap = true},
        SweptContact{.first = 1U, .second = 2U, .axis = ContactAxis::X,
                     .toi = {}, .initial_overlap = true, .final_overlap = true},
        SweptContact{.first = 2U, .second = 3U, .axis = ContactAxis::X,
                     .toi = {}, .initial_overlap = true, .final_overlap = true},
        SweptContact{.first = 0U, .second = 2U, .axis = ContactAxis::X,
                     .toi = {}, .initial_overlap = true, .final_overlap = true},
        SweptContact{.first = 0U, .second = 3U, .axis = ContactAxis::X,
                     .toi = {}, .initial_overlap = true, .final_overlap = true},
    };
    ContactIslandWorkspace workspace(4U, contacts.size());
    workspace.classify(4U, contacts);
    GeneralProjectionScratch scratch(4U, contacts.size());
    GeneralProjectionWarmStart warm(contacts.size());
    std::vector<Fixed::rep> values{40, 30, 20, 10};
    const GeneralProjectionStats first = project_general_contact_islands(
        values, contacts, workspace, GeneralProjectionMethod::CertifiedAuto,
        {.maximum_iterations = 512U, .certification_tolerance_raw = 1U,
         .pcg_restart_interval = 8U}, scratch, &warm);
    CHECK(name, first.total_order_reductions == 1U);
    CHECK(name, first.iterative_fallbacks == 0U);
    CHECK(name, first.residuals.certified);
    CHECK(name, values[0] <= values[1]);
    CHECK(name, values[1] <= values[2]);
    CHECK(name, values[2] <= values[3]);

    std::vector<Fixed::rep> repeated{40, 30, 20, 10};
    const GeneralProjectionStats second = project_general_contact_islands(
        repeated, contacts, workspace, GeneralProjectionMethod::CertifiedAuto,
        {.maximum_iterations = 512U, .certification_tolerance_raw = 1U,
         .pcg_restart_interval = 8U}, scratch, &warm);
    CHECK(name, repeated == values);
    CHECK(name, second.warm_attempts == 1U);
    CHECK(name, second.warm_exact_accepts == 1U);
    CHECK(name, second.warm_rejects == 0U);
}

void test_general_projection_fallback_is_canonical() {
    constexpr std::string_view name = "general_projection_fallback_is_canonical";
    std::vector<SweptContact> contacts{
        SweptContact{.first = 0U, .second = 2U, .axis = ContactAxis::X,
                     .toi = {}, .initial_overlap = true, .final_overlap = true},
        SweptContact{.first = 1U, .second = 3U, .axis = ContactAxis::X,
                     .toi = {}, .initial_overlap = true, .final_overlap = true},
        SweptContact{.first = 0U, .second = 3U, .axis = ContactAxis::X,
                     .toi = {}, .initial_overlap = true, .final_overlap = true},
    };
    ContactIslandWorkspace workspace(4U, contacts.size());
    workspace.classify(4U, contacts);
    GeneralProjectionScratch scratch(4U, contacts.size());
    std::vector<Fixed::rep> automatic{30, 20, 10, 0};
    std::vector<Fixed::rep> dykstra = automatic;
    const GeneralProjectionStats automatic_stats = project_general_contact_islands(
        automatic, contacts, workspace, GeneralProjectionMethod::CertifiedAuto,
        {.maximum_iterations = 4'096U, .certification_tolerance_raw = 1U,
         .pcg_restart_interval = 8U}, scratch, nullptr);
    const GeneralProjectionStats dykstra_stats = project_general_contact_islands(
        dykstra, contacts, workspace, GeneralProjectionMethod::DykstraCoordinate,
        {.maximum_iterations = 4'096U, .certification_tolerance_raw = 1U,
         .pcg_restart_interval = 8U}, scratch, nullptr);
    CHECK(name, automatic_stats.total_order_reductions == 0U);
    CHECK(name, automatic_stats.iterative_fallbacks == 1U);
    CHECK(name, automatic_stats.residuals.certified);
    CHECK(name, dykstra_stats.residuals.certified);
    CHECK(name, automatic == dykstra);
}

void test_atomic_temporal_rollback_matches_rebuild() {
    constexpr std::string_view name = "atomic_temporal_rollback_matches_rebuild";
    constexpr std::size_t bodies = 20U;
    constexpr std::size_t contacts_count = bodies / 2U;
    std::vector<Fixed::rep> px(bodies), py(bodies), vx(bodies), vy(bodies);
    std::vector<std::uint32_t> masses(bodies, 1U);
    std::vector<NormalContact> contacts;
    contacts.reserve(contacts_count);
    for (std::size_t pair = 0U; pair < contacts_count; ++pair) {
        const std::size_t first = pair * 2U;
        const std::size_t second = first + 1U;
        const Fixed::rep center = Fixed::from_integer(static_cast<Fixed::rep>(pair * 4U)).raw();
        px[first] = center - Fixed::from_ratio(3, 8).raw();
        px[second] = center + Fixed::from_ratio(3, 8).raw();
        vx[first] = Fixed::from_ratio(1, 16).raw();
        vx[second] = -vx[first];
        masses[first] = 1U + static_cast<std::uint32_t>(pair % 5U);
        masses[second] = 1U + static_cast<std::uint32_t>((pair + 2U) % 5U);
        contacts.push_back({first, second, {1 << 30, 0}});
    }
    AtomicTemporalPhysicsConfig config{
        .bodies = bodies, .contacts = contacts_count, .maximum_candidate_pairs = 64U,
        .history_capacity = 32U, .horizon_frames = 16U,
        .maximum_velocity_mutations = 2U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U, .half_extent = Fixed::from_ratio(1, 2),
        .velocity_mutation_guard_raw = 4'096,
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
        .force_rebuild_each_frame = false,
    };
    AtomicTemporalPhysicsConfig rebuild_config = config;
    rebuild_config.force_rebuild_each_frame = true;
    AtomicTemporalPhysicsEngine rollback(config), clean(config), rebuild(rebuild_config);
    rollback.initialize(px, py, vx, vy, masses, contacts);
    clean.initialize(px, py, vx, vy, masses, contacts);
    rebuild.initialize(px, py, vx, vy, masses, contacts);
    std::array<VelocityMutation, 16> original{}, corrected{};
    for (std::uint64_t frame = 1U; frame <= original.size(); ++frame) {
        const std::size_t body = static_cast<std::size_t>((frame * 3U) % bodies);
        original[frame - 1U] = {body, static_cast<Fixed::rep>(frame * 11U),
                               -static_cast<Fixed::rep>(frame * 7U)};
        corrected[frame - 1U] = original[frame - 1U];
    }
    corrected[8].delta_x += 777;
    for (std::uint64_t frame = 1U; frame <= original.size(); ++frame) {
        rollback.set_input(frame, {.velocity = std::span<const VelocityMutation>(&original[frame - 1U], 1U)});
        clean.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
        rebuild.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
    }
    rollback.simulate_to(16U);
    clean.simulate_to(16U);
    rebuild.simulate_to(16U);
    rollback.correct_and_resimulate(9U,
        {.velocity = std::span<const VelocityMutation>(&corrected[8], 1U)}, 16U);
    CHECK(name, rollback.equivalent_to(clean));
    CHECK(name, rollback.physically_equivalent_to(rebuild));
    CHECK(name, rollback.stats().broadphase_reuses > 0U);
    CHECK(name, rollback.candidate_pair_count() == contacts_count);
}

AtomicTemporalStateView external_view(const AtomicTemporalExternalState& state) {
    return {.frame = state.frame, .valid_until_frame = state.valid_until_frame,
        .position_x = state.position_x, .position_y = state.position_y,
        .velocity_x = state.velocity_x, .velocity_y = state.velocity_y,
        .masses = state.masses, .dual = state.dual, .manifold = state.manifold,
        .contact_stable = state.contact_stable, .contact_candidate = state.contact_candidate,
        .fat_bounds = state.fat_bounds,
        .pairs = std::span<const BroadphasePair>(state.pairs.data(), state.pair_count)};
}


void test_paged_position_hints_copy_only_dirty_pages() {
    constexpr std::string_view name = "paged_position_hints_copy_only_dirty_pages";
    PagedAtomicHistoryConfig sparse_config{.bodies = 1024U, .contacts = 16U,
        .maximum_candidate_pairs = 32U, .history_capacity = 8U, .page_elements = 64U,
        .maximum_position_dirty_pages_per_frame = 1U,
        .maximum_velocity_dirty_pages_per_frame = 1U,
        .maximum_mass_dirty_pages_per_frame = 1U,
        .maximum_contact_dirty_pages_per_frame = 1U,
        .full_position_generations = 2U, .full_velocity_generations = 2U,
        .full_contact_generations = 2U, .maximum_cache_generations = 2U};
    PagedAtomicHistory sparse(sparse_config);
    PagedAtomicHistory full(PagedAtomicHistoryConfig{.bodies = 1024U, .contacts = 16U,
        .maximum_candidate_pairs = 32U, .history_capacity = 8U, .page_elements = 64U,
        .maximum_position_dirty_pages_per_frame = 0U,
        .maximum_velocity_dirty_pages_per_frame = 1U,
        .maximum_mass_dirty_pages_per_frame = 1U,
        .maximum_contact_dirty_pages_per_frame = 1U,
        .full_position_generations = 2U, .full_velocity_generations = 2U,
        .full_contact_generations = 2U, .maximum_cache_generations = 2U});
    AtomicTemporalExternalState state(1024U, 16U, 32U), restored(1024U, 16U, 32U);
    state.pair_count = 16U;
    for (std::size_t body = 0U; body < 1024U; ++body) {
        state.position_x[body] = static_cast<Fixed::rep>(body * 7U);
        state.position_y[body] = static_cast<Fixed::rep>(body * 11U);
        state.masses[body] = 1U;
    }
    for (std::size_t contact = 0U; contact < 16U; ++contact) {
        state.manifold[contact] = {contact * 2U, contact * 2U + 1U, {1 << 30, 0}};
        state.pairs[contact] = {contact * 2U, contact * 2U + 1U};
    }
    sparse.capture(state);
    const auto before = sparse.stats();
    state.frame = 1U;
    state.position_x[3] += 13;
    state.position_y[7] -= 17;
    const std::array<std::size_t, 2> changed_positions{3U, 7U};
    sparse.capture(external_view(state), {.changed_position_bodies = changed_positions,
        .position_hints_complete = true, .cache_rebuilt = false, .topology_changed = false});
    sparse.restore(1U, restored);
    const auto after = sparse.stats();
    CHECK(name, restored.position_x == state.position_x);
    CHECK(name, restored.position_y == state.position_y);
    CHECK(name, after.pages_copied - before.pages_copied == 2U);
    CHECK(name, after.pages_shared - before.pages_shared > 30U);
    CHECK(name, sparse.reserved_bytes() < full.reserved_bytes());
}

void test_axis_forest_projection_certifies_mixed_axis_tree() {
    constexpr std::string_view name = "axis_forest_projection_certifies_mixed_axis_tree";
    constexpr std::size_t bodies = 63U;
    std::vector<Fixed::rep> vx(bodies), vy(bodies), original_x, original_y;
    std::vector<std::uint32_t> masses(bodies);
    std::vector<NormalContact> contacts;
    contacts.reserve(bodies - 1U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        vx[body] = static_cast<Fixed::rep>((bodies - body) * 10'003U);
        vy[body] = static_cast<Fixed::rep>((body % 11U) * 13'007U);
        masses[body] = 1U + static_cast<std::uint32_t>((body * 7U) % 17U);
        if (body == 0U) continue;
        const std::size_t parent = (body - 1U) / 2U;
        std::size_t depth = 0U;
        for (std::size_t cursor = parent + 1U; cursor > 1U; cursor >>= 1U) ++depth;
        const NormalQ30 normal = (depth % 2U == 0U) ? NormalQ30{1 << 30, 0}
                                                    : NormalQ30{0, 1 << 30};
        contacts.push_back({parent, body, normal});
    }
    original_x = vx; original_y = vy;
    AxisForestScratch scratch(bodies, contacts.size());
    const AxisForestStats stats = project_axis_forest_inplace(vx, vy, masses, contacts,
        AxisForestConfig{.tree = {.maximum_active_set_iterations = 4096U,
            .feasibility_tolerance_raw = 4U, .stationarity_tolerance_raw = 16U}}, scratch);
    CHECK(name, stats.supported);
    CHECK(name, stats.certified);
    CHECK(name, stats.x_contacts + stats.y_contacts == contacts.size());
    CHECK(name, stats.x_components > 0U && stats.y_components > 0U);
    for (const NormalContact& contact : contacts) {
        const WideInteger violation = (static_cast<WideInteger>(vx[contact.first]) - vx[contact.second])
                * contact.normal.x
            + (static_cast<WideInteger>(vy[contact.first]) - vy[contact.second])
                * contact.normal.y;
        CHECK(name, violation <= static_cast<WideInteger>(4U) * (WideInteger{1} << 30U));
    }
    std::vector<Fixed::rep> second_x = original_x, second_y = original_y;
    AxisForestScratch second_scratch(bodies, contacts.size());
    const AxisForestStats second = project_axis_forest_inplace(second_x, second_y, masses, contacts,
        AxisForestConfig{.tree = {.maximum_active_set_iterations = 4096U,
            .feasibility_tolerance_raw = 4U, .stationarity_tolerance_raw = 16U}}, second_scratch);
    CHECK(name, second.certified);
    CHECK(name, vx == second_x && vy == second_y);

    std::vector<Fixed::rep> unsupported_x = original_x, unsupported_y = original_y;
    std::vector<NormalContact> diagonal = contacts;
    diagonal[0].normal = {759'250'125, 759'250'125};
    AxisForestScratch unsupported_scratch(bodies, contacts.size());
    const AxisForestStats unsupported = project_axis_forest_inplace(
        unsupported_x, unsupported_y, masses, diagonal, {}, unsupported_scratch);
    CHECK(name, !unsupported.supported && !unsupported.certified);
    CHECK(name, unsupported_x == original_x && unsupported_y == original_y);
}

void test_atomic_temporal_mixed_axis_tree_uses_certified_forest() {
    constexpr std::string_view name = "atomic_temporal_mixed_axis_tree_uses_certified_forest";
    constexpr std::size_t bodies = 31U;
    std::vector<Fixed::rep> px(bodies), py(bodies), vx(bodies), vy(bodies);
    std::vector<std::uint32_t> masses(bodies, 1U);
    std::vector<NormalContact> contacts;
    for (std::size_t body = 0U; body < bodies; ++body) {
        px[body] = Fixed::from_ratio(static_cast<Fixed::rep>(body % 4U), 16).raw();
        py[body] = Fixed::from_ratio(static_cast<Fixed::rep>((body / 4U) % 4U), 16).raw();
        vx[body] = static_cast<Fixed::rep>((bodies - body) * 20'003U);
        vy[body] = static_cast<Fixed::rep>((body % 7U) * 17'009U);
        masses[body] = 1U + static_cast<std::uint32_t>(body % 5U);
        if (body == 0U) continue;
        const std::size_t parent = (body - 1U) / 2U;
        std::size_t depth = 0U;
        for (std::size_t cursor = parent + 1U; cursor > 1U; cursor >>= 1U) ++depth;
        contacts.push_back({parent, body, depth % 2U == 0U
            ? NormalQ30{1 << 30, 0} : NormalQ30{0, 1 << 30}});
    }
    AtomicTemporalPhysicsEngine engine({.bodies = bodies, .contacts = contacts.size(),
        .maximum_candidate_pairs = bodies * bodies, .history_capacity = 4U,
        .horizon_frames = 4U, .maximum_velocity_mutations = 1U,
        .maximum_mass_mutations = 1U, .maximum_contact_mutations = 1U,
        .half_extent = Fixed::from_integer(2),
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
        .enable_single_tree_solver = true, .enable_axis_forest_solver = true});
    engine.initialize(px, py, vx, vy, masses, contacts);
    engine.simulate_to(1U);
    CHECK(name, engine.stats().axis_forest_calls == 1U);
    CHECK(name, engine.stats().axis_forest_certified == 1U);
    CHECK(name, engine.stats().axis_forest_fallbacks == 0U);
}

void test_paged_atomic_history_shares_restores_and_promotes() {
    constexpr std::string_view name = "paged_atomic_history_shares_restores_and_promotes";
    PagedAtomicHistory history({.bodies = 16U, .contacts = 8U, .maximum_candidate_pairs = 16U,
        .history_capacity = 3U, .page_elements = 4U,
        .maximum_velocity_dirty_pages_per_frame = 2U,
        .maximum_mass_dirty_pages_per_frame = 2U,
        .maximum_contact_dirty_pages_per_frame = 2U,
        .full_velocity_generations = 3U, .full_contact_generations = 3U,
        .maximum_cache_generations = 3U});
    AtomicTemporalExternalState state(16U, 8U, 16U), restored(16U, 8U, 16U);
    state.valid_until_frame = 8U; state.pair_count = 8U;
    std::vector<std::uint8_t> dirty_contacts(8U);
    for (std::size_t i = 0U; i < 16U; ++i) {
        state.position_x[i] = static_cast<Fixed::rep>(i * 10U);
        state.position_y[i] = static_cast<Fixed::rep>(i * 20U);
        state.velocity_x[i] = static_cast<Fixed::rep>(i);
        state.masses[i] = 1U + static_cast<std::uint32_t>(i % 4U);
        state.fat_bounds[i] = {.minimum_x = Fixed::from_raw(state.position_x[i] - 1),
            .maximum_x = Fixed::from_raw(state.position_x[i] + 1),
            .minimum_y = Fixed::from_raw(state.position_y[i] - 1),
            .maximum_y = Fixed::from_raw(state.position_y[i] + 1)};
    }
    for (std::size_t i = 0U; i < 8U; ++i) {
        state.manifold[i] = {i * 2U, i * 2U + 1U, {1 << 30, 0}};
        state.pairs[i] = {i * 2U, i * 2U + 1U};
    }
    history.capture(state);
    const AtomicTemporalExternalState frame0 = state;
    for (std::uint64_t frame = 1U; frame <= 4U; ++frame) {
        state.frame = frame; state.valid_until_frame = frame + 8U;
        for (auto& value : state.position_x) ++value;
        state.velocity_x[frame] += 7;
        std::fill(dirty_contacts.begin(), dirty_contacts.end(), 0U);
        dirty_contacts[frame % dirty_contacts.size()] = 1U;
        state.dual[frame % state.dual.size()] += 3;
        const std::array<std::size_t, 1> changed{static_cast<std::size_t>(frame)};
        history.capture(external_view(state), {.changed_bodies = changed,
            .changed_contacts = dirty_contacts, .cache_rebuilt = false, .topology_changed = false});
    }
    history.restore(4U, restored);
    CHECK(name, restored.position_x == state.position_x);
    CHECK(name, restored.velocity_x == state.velocity_x);
    CHECK(name, !history.contains(0U));
    CHECK(name, history.stats().pages_shared > 0U);
    CHECK(name, history.stats().zero_copy_promotions > 0U);
    CHECK(name, history.live_payload_bytes() < history.reserved_bytes());
    static_cast<void>(frame0);
}

void test_paged_atomic_rollback_matches_full_history() {
    constexpr std::string_view name = "paged_atomic_rollback_matches_full_history";
    constexpr std::size_t bodies = 32U, contacts_count = 16U;
    std::vector<Fixed::rep> px(bodies), py(bodies), vx(bodies), vy(bodies);
    std::vector<std::uint32_t> masses(bodies, 1U);
    std::vector<NormalContact> contacts;
    for (std::size_t pair = 0U; pair < contacts_count; ++pair) {
        const std::size_t a = pair * 2U, b = a + 1U;
        const auto center = Fixed::from_integer(static_cast<Fixed::rep>(pair * 4U)).raw();
        px[a] = center - Fixed::from_ratio(3, 8).raw(); px[b] = center + Fixed::from_ratio(3, 8).raw();
        vx[a] = Fixed::from_ratio(1, 16).raw(); vx[b] = -vx[a];
        contacts.push_back({a, b, {1 << 30, 0}});
    }
    AtomicTemporalPhysicsConfig physics{.bodies = bodies, .contacts = contacts_count,
        .maximum_candidate_pairs = 64U, .history_capacity = 32U, .horizon_frames = 16U,
        .maximum_velocity_mutations = 2U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U, .half_extent = Fixed::from_ratio(1, 2),
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U}};
    PagedAtomicTemporalConfig paged_config{.physics = physics, .history = {
        .bodies = bodies, .contacts = contacts_count, .maximum_candidate_pairs = 64U,
        .history_capacity = 32U, .page_elements = 8U,
        .maximum_velocity_dirty_pages_per_frame = 4U,
        .maximum_mass_dirty_pages_per_frame = 2U,
        .maximum_contact_dirty_pages_per_frame = 4U,
        .full_velocity_generations = 4U, .full_contact_generations = 4U,
        .maximum_cache_generations = 6U}};
    AtomicTemporalPhysicsEngine full(physics);
    PagedAtomicTemporalPhysicsEngine paged(paged_config);
    full.initialize(px, py, vx, vy, masses, contacts); paged.initialize(px, py, vx, vy, masses, contacts);
    std::array<VelocityMutation, 16> inputs{};
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        inputs[frame - 1U] = {static_cast<std::size_t>((frame * 3U) % bodies),
            static_cast<Fixed::rep>(frame * 11U), -static_cast<Fixed::rep>(frame * 5U)};
        const AtomicPhysicsFrameInput input{.velocity = std::span<const VelocityMutation>(&inputs[frame - 1U], 1U)};
        full.set_input(frame, input); paged.set_input(frame, input);
    }
    full.simulate_to(16U); paged.simulate_to(16U);
    VelocityMutation corrected = inputs[8]; corrected.delta_x += 777;
    const AtomicPhysicsFrameInput correction{.velocity = std::span<const VelocityMutation>(&corrected, 1U)};
    full.correct_and_resimulate(9U, correction, 16U); paged.correct_and_resimulate(9U, correction, 16U);
    CHECK(name, paged.physically_equivalent_to(full));
    CHECK(name, paged.hash() == full.hash());
    CHECK(name, paged.stats().history.pages_shared > 0U);
}


void test_paged_capture_failure_is_transactional() {
    constexpr std::string_view name = "paged_capture_failure_is_transactional";
    PagedAtomicHistory history({.bodies = 8U, .contacts = 4U, .maximum_candidate_pairs = 8U,
        .history_capacity = 4U, .page_elements = 4U,
        .maximum_velocity_dirty_pages_per_frame = 0U,
        .maximum_mass_dirty_pages_per_frame = 0U,
        .maximum_contact_dirty_pages_per_frame = 0U,
        .full_velocity_generations = 1U, .full_contact_generations = 1U,
        .maximum_cache_generations = 1U});
    AtomicTemporalExternalState state(8U, 4U, 8U), restored(8U, 4U, 8U);
    state.pair_count = 4U;
    for (std::size_t i = 0U; i < 8U; ++i) {
        state.position_x[i] = static_cast<Fixed::rep>(i * 100U);
        state.position_y[i] = static_cast<Fixed::rep>(i * 50U);
        state.masses[i] = 1U;
        state.fat_bounds[i] = {.minimum_x = Fixed::from_raw(state.position_x[i] - 2),
            .maximum_x = Fixed::from_raw(state.position_x[i] + 2),
            .minimum_y = Fixed::from_raw(state.position_y[i] - 2),
            .maximum_y = Fixed::from_raw(state.position_y[i] + 2)};
    }
    for (std::size_t i = 0U; i < 4U; ++i) {
        state.manifold[i] = {i * 2U, i * 2U + 1U, {1 << 30, 0}};
        state.pairs[i] = {i * 2U, i * 2U + 1U};
    }
    history.capture(state);
    for (std::uint64_t frame = 1U; frame <= 2U; ++frame) {
        state.frame = frame;
        state.valid_until_frame = frame + 1U;
        for (std::size_t body = 0U; body < 8U; ++body) {
            state.fat_bounds[body].minimum_x = Fixed::from_raw(state.fat_bounds[body].minimum_x.raw() + 1);
            state.fat_bounds[body].maximum_x = Fixed::from_raw(state.fat_bounds[body].maximum_x.raw() + 1);
        }
        history.capture(external_view(state), {.cache_rebuilt = true});
    }
    const std::size_t size_before = history.size();
    state.frame = 3U;
    state.valid_until_frame = 4U;
    for (std::size_t body = 0U; body < 8U; ++body) {
        state.fat_bounds[body].minimum_x = Fixed::from_raw(state.fat_bounds[body].minimum_x.raw() + 1);
        state.fat_bounds[body].maximum_x = Fixed::from_raw(state.fat_bounds[body].maximum_x.raw() + 1);
    }
    bool exhausted = false;
    try {
        history.capture(external_view(state), {.cache_rebuilt = true});
    } catch (const std::length_error&) {
        exhausted = true;
    }
    CHECK(name, exhausted);
    CHECK(name, history.size() == size_before);
    CHECK(name, history.contains(0U));
    CHECK(name, history.contains(1U));
    CHECK(name, history.contains(2U));
    CHECK(name, !history.contains(3U));
    history.restore(2U, restored);
    CHECK(name, restored.frame == 2U);
    CHECK(name, history.stats().page_pool_exhaustions > 0U);
}

void test_atomic_temporal_branched_tree_uses_general_solver() {
    constexpr std::string_view name = "atomic_temporal_branched_tree_uses_general_solver";
    constexpr std::size_t bodies = 7U;
    const std::array<NormalContact, 6> contacts{{
        {0U, 1U, {1 << 30, 0}}, {0U, 2U, {1 << 30, 0}},
        {1U, 3U, {1 << 30, 0}}, {1U, 4U, {1 << 30, 0}},
        {2U, 5U, {1 << 30, 0}}, {2U, 6U, {1 << 30, 0}}
    }};
    std::array<Fixed::rep, bodies> px{}, py{}, vx{}, vy{};
    std::array<std::uint32_t, bodies> masses{};
    for (std::size_t body = 0U; body < bodies; ++body) {
        vx[body] = Fixed::from_ratio(static_cast<Fixed::rep>(bodies - body), 64).raw();
        masses[body] = 1U + static_cast<std::uint32_t>(body % 3U);
    }
    AtomicTemporalPhysicsConfig config{.bodies = bodies, .contacts = contacts.size(),
        .maximum_candidate_pairs = 32U, .history_capacity = 4U, .horizon_frames = 4U,
        .maximum_velocity_mutations = 1U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U, .half_extent = Fixed::from_ratio(1, 2),
        .projection = {.maximum_iterations = 64U, .feasibility_tolerance_raw = 32U},
        .enable_single_tree_solver = true};
    AtomicTemporalPhysicsEngine engine(config);
    engine.initialize(px, py, vx, vy, masses, contacts);
    engine.simulate_to(1U);
    CHECK(name, engine.stats().tree_general_certified > 0U);
    CHECK(name, engine.stats().tree_solver_fallbacks == 0U);
    AtomicTemporalExternalState state(bodies, contacts.size(), 32U);
    engine.export_state(state);
    for (const NormalContact& contact : contacts) {
        CHECK(name, state.velocity_x[contact.first] <= state.velocity_x[contact.second] + 32);
    }
}

void test_weighted_chain_pav_stays_certified_after_mutations() {
    constexpr std::string_view name = "weighted_chain_pav_stays_certified_after_mutations";
    constexpr std::size_t bodies = 64U;
    std::vector<Fixed::rep> vx(bodies), vy(bodies);
    std::vector<std::uint32_t> masses(bodies);
    std::vector<DirectedTreeEdge> edges;
    for (std::size_t i = 0U; i < bodies; ++i) {
        vx[i] = Fixed::from_ratio(static_cast<Fixed::rep>(bodies - i), 65'536).raw();
        masses[i] = 1U + static_cast<std::uint32_t>((i * 7U) % 16U);
        if (i != 0U) edges.push_back({i - 1U, i});
    }
    WeightedTreeScratch scratch(bodies);
    for (std::size_t step = 1U; step <= 32U; ++step) {
        vx[(step * 11U) % bodies] += static_cast<Fixed::rep>(step * 17U);
        const auto stats = project_weighted_chain_common_normal_inplace(
            vx, vy, masses, edges, {1 << 30, 0}, {}, scratch);
        CHECK(name, stats.residuals.certified);
        CHECK(name, stats.residuals.primal_linf_raw == 0U);
    }
}


void test_island_pair_cache_is_physically_segmented() {
    constexpr std::string_view name = "island_pair_cache_is_physically_segmented";
    const std::array<NormalContact, 2> contacts{{
        {0U, 1U, {1 << 30, 0}}, {2U, 3U, {1 << 30, 0}}
    }};
    IslandPairCache cache({.bodies = 6U, .maximum_contacts = contacts.size(),
                           .extra_pairs_per_island = 1U});
    cache.initialize(contacts);
    CHECK(name, cache.island_count() == 4U);
    CHECK(name, cache.island_of_body(0U) == cache.island_of_body(1U));
    CHECK(name, cache.island_of_body(0U) != cache.island_of_body(2U));
    const BroadphasePair pair{0U, 1U};
    const std::uint64_t before = cache.hash();
    cache.replace_pairs_for_body(0U, std::span<const BroadphasePair>(&pair, 1U));
    CHECK(name, cache.hash() == before);
    CHECK(name, cache.stats().island_rebuilds == 1U);
    CHECK(name, cache.pairs_for_island(cache.island_of_body(2U)).size() == 1U);
}

void test_small_oblique_grid_oracle_is_exact_and_canonical() {
    constexpr std::string_view name = "small_oblique_grid_oracle_is_exact_and_canonical";
    const std::array<Fixed::rep, 2> input_x{{2, -2}};
    const std::array<Fixed::rep, 2> input_y{{2, -2}};
    const std::array<std::uint32_t, 2> masses{{1U, 1U}};
    std::array<NormalContact, 1> contacts{{
        {0U, 1U, {759'250'125, 759'250'125}}
    }};
    const auto first = solve_small_oblique_grid_oracle(input_x, input_y, masses, contacts,
        {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = 2U});
    const auto second = solve_small_oblique_grid_oracle(input_x, input_y, masses, contacts,
        {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = 2U});
    CHECK(name, first.feasible);
    CHECK(name, first.objective == second.objective);
    CHECK(name, first.velocity_x == second.velocity_x);
    CHECK(name, first.velocity_y == second.velocity_y);
    CHECK(name, first.objective == 15U);
    CHECK(name, first.velocity_x[0] == -1 && first.velocity_x[1] == -1);
    CHECK(name, first.velocity_y[0] == 0 && first.velocity_y[1] == -1);
}

void test_direct_paged_restore_reuses_topology() {
    constexpr std::string_view name = "direct_paged_restore_reuses_topology";
    constexpr std::size_t bodies = 8U;
    const std::array<NormalContact, 4> contacts{{
        {0U, 1U, {1 << 30, 0}}, {2U, 3U, {1 << 30, 0}},
        {4U, 5U, {1 << 30, 0}}, {6U, 7U, {1 << 30, 0}}
    }};
    std::array<Fixed::rep, bodies> px{}, py{}, vx{}, vy{};
    std::array<std::uint32_t, bodies> masses{};
    for (std::size_t body = 0U; body < bodies; ++body) {
        px[body] = Fixed::from_integer(static_cast<Fixed::rep>(body * 2U)).raw();
        masses[body] = 1U;
    }
    const AtomicTemporalPhysicsConfig physics{
        .bodies = bodies, .contacts = contacts.size(), .maximum_candidate_pairs = 16U,
        .history_capacity = 8U, .horizon_frames = 4U,
        .maximum_velocity_mutations = 1U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U, .half_extent = Fixed::from_ratio(1, 2),
    };
    PagedAtomicTemporalPhysicsEngine engine({.physics = physics, .history = {
        .bodies = bodies, .contacts = contacts.size(), .maximum_candidate_pairs = 16U,
        .history_capacity = 8U, .page_elements = 4U,
        .maximum_position_dirty_pages_per_frame = 2U,
        .maximum_velocity_dirty_pages_per_frame = 2U,
        .maximum_mass_dirty_pages_per_frame = 1U,
        .maximum_contact_dirty_pages_per_frame = 1U,
        .full_position_generations = 2U, .full_velocity_generations = 2U,
        .full_contact_generations = 2U, .maximum_cache_generations = 2U}});
    engine.initialize(px, py, vx, vy, masses, contacts);
    const VelocityMutation original{0U, 11, -7};
    const VelocityMutation corrected{0U, 19, -5};
    engine.set_input(1U, {.velocity = std::span<const VelocityMutation>(&original, 1U)});
    engine.simulate_to(2U);
    engine.correct_and_resimulate(1U,
        {.velocity = std::span<const VelocityMutation>(&corrected, 1U)}, 2U);
    CHECK(name, engine.stats().physics.topology_restore_reuses > 0U);
}


void test_versioned_island_pair_history_shares_and_restores() {
    constexpr std::string_view name = "versioned_island_pair_history_shares_and_restores";
    const std::array<NormalContact, 2> contacts{{
        {0U, 1U, {1 << 30, 0}}, {2U, 3U, {1 << 30, 0}}
    }};
    const std::array<BroadphasePair, 2> pairs{{{0U, 1U}, {2U, 3U}}};
    VersionedIslandPairHistory history({
        .bodies = 4U, .maximum_contacts = contacts.size(),
        .history_capacity = 4U, .extra_pairs_per_island = 1U});
    history.initialize(0U, contacts, pairs);
    history.capture(1U, pairs, {});
    const std::size_t dirty = 0U;
    const std::array<BroadphasePair, 1> changed{{{2U, 3U}}};
    history.capture(2U, changed, std::span<const std::size_t>(&dirty, 1U));
    std::array<BroadphasePair, 2> restored{};
    const std::size_t count0 = history.restore(0U, restored);
    CHECK(name, count0 == 2U);
    CHECK(name, restored[0] == pairs[0] && restored[1] == pairs[1]);
    const std::size_t count2 = history.restore(2U, restored);
    CHECK(name, count2 == 1U);
    CHECK(name, restored[0] == changed[0]);
    CHECK(name, history.stats().tables_shared == 1U);
    CHECK(name, history.stats().tables_copied == 1U);
}

void test_oblique_tree_grid_dp_matches_exhaustive_oracle() {
    constexpr std::string_view name = "oblique_tree_grid_dp_matches_exhaustive_oracle";
    const std::array<Fixed::rep, 3> input_x{{2, -2, 1}};
    const std::array<Fixed::rep, 3> input_y{{1, -1, 2}};
    const std::array<std::uint32_t, 3> masses{{1U, 2U, 3U}};
    const std::array<NormalContact, 2> contacts{{
        {0U, 1U, {759'250'125, 759'250'125}},
        {1U, 2U, {644'245'094, 858'993'459}}
    }};
    ObliqueTreeGridScratch scratch(3U, 25U);
    const auto result = solve_oblique_tree_grid_dp(input_x, input_y, masses, contacts,
        {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = 3U,
         .maximum_grid_states = 25U}, scratch);
    const auto oracle = solve_small_oblique_grid_oracle(input_x, input_y, masses, contacts,
        {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = 3U});
    CHECK(name, result.certified_on_grid);
    CHECK(name, result.primal_violation_raw == 0U);
    CHECK(name, oracle.feasible);
    CHECK(name, result.objective == oracle.objective);
}

void test_invalid_world_is_rejected() {
    constexpr std::string_view name = "invalid_world_is_rejected";
    bool threw = false;
    try {
        RollbackEngine engine(WorldState{
            .frame = 0,
            .bodies = {Body{.id = 2}, Body{.id = 1}}
        });
        static_cast<void>(engine);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(name, threw);
}

} // namespace

int main() {
    try {
        test_fixed_arithmetic();
        test_repeated_execution_is_identical();
        test_input_order_is_canonicalized();
        test_snapshot_strategies_restore_identically();
        test_snapshot_capacity_and_eviction();
        test_snapshot_frame_gaps_are_rejected();
        test_truncate_and_recapture();
        test_rollback_recovers_authoritative_state_for_all_strategies();
        test_cross_strategy_hash_equivalence();
        test_canonical_serialization_is_versioned();
        test_dirty_tracking_is_exact();
        test_dirty_audit_rejects_under_report();
        test_dirty_path_avoids_comparison_scan();
        test_configurable_page_and_checkpoint_families();
        test_hybrid_records_decisions_and_nonnegative_regret();
        test_immutable_transition_matches_aos();
        test_immutable_shares_untouched_chunks();
        test_immutable_rollback_matches_authoritative();
        test_persistent_checkpoints_restore_and_promote();
        test_offline_oracle_includes_transition_and_checkpoint_constraints();
        test_merkle_hash_is_deterministic_and_domain_separated();
        test_active_step_matches_full_scan_and_skips_inactive_bodies();
        test_active_rollback_matches_authoritative();
        test_component_world_matches_active_immutable();
        test_radix_fanouts_match_active_immutable();
        test_active_set_deactivates_on_exact_zero_velocity();
        test_batch_update_rejects_duplicate_indices();
        test_contextual_policy_uses_calibrated_contexts();
        test_fixed_simd_matches_scalar_bit_exactly();
        test_direct_component_kernel_matches_legacy_without_body_reconstruction();
        test_broadphase_is_conservative_and_wakes_contact_chain();
        test_broadphase_incremental_membership_update();
        test_indexed_ring_detects_aliases_and_supports_recapture();
        test_epoch_arena_reclaims_by_frame_slot();
        test_swept_aabb_prevents_tunneling();
        test_swept_grid_matches_bruteforce();
        test_interactive_rollback_matches_fresh_resimulation();
        test_temporal_matching_fused_matches_reference();
        test_temporal_cache_escapes_on_acceleration();
        test_temporal_escape_preserves_initial_contact();
        test_chain_isotonic_solver_and_warm_start();
        test_general_projection_certificates_and_reduction();
        test_general_projection_fallback_is_canonical();
        test_atomic_temporal_rollback_matches_rebuild();
        test_paged_position_hints_copy_only_dirty_pages();
        test_axis_forest_projection_certifies_mixed_axis_tree();
        test_atomic_temporal_mixed_axis_tree_uses_certified_forest();
        test_paged_atomic_history_shares_restores_and_promotes();
        test_paged_atomic_rollback_matches_full_history();
        test_paged_capture_failure_is_transactional();
        test_atomic_temporal_branched_tree_uses_general_solver();
        test_weighted_chain_pav_stays_certified_after_mutations();
        test_island_pair_cache_is_physically_segmented();
        test_small_oblique_grid_oracle_is_exact_and_canonical();
        test_direct_paged_restore_reuses_topology();
        test_versioned_island_pair_history_shares_and_restores();
        test_oblique_tree_grid_dp_matches_exhaustive_oracle();
        test_invalid_world_is_rejected();
    } catch (const std::exception& error) {
        std::cerr << "Unhandled exception: " << error.what() << '\n';
        return EXIT_FAILURE;
    }

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All NeoEng Core Lab v0.28 tests passed.\n";
    return EXIT_SUCCESS;
}
