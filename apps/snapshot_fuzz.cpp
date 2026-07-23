#include "neoeng/core/hash.hpp"
#include "neoeng/core/snapshot_store.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace neoeng::core;

namespace {

[[nodiscard]] std::uint64_t next_random(std::uint64_t& state) noexcept {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
}

[[nodiscard]] WorldState make_world(std::size_t count) {
    WorldState world;
    world.bodies.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        world.bodies.push_back(Body{.id = static_cast<EntityId>(index + 1U)});
    }
    return world;
}

[[nodiscard]] DirtySet random_mutation(WorldState& world, std::uint64_t& random) {
    DirtySet dirty(world.bodies.size());
    world.frame += 1U;
    const std::size_t changes = static_cast<std::size_t>(next_random(random) % 40U);
    for (std::size_t item = 0; item < changes; ++item) {
        const std::size_t index = static_cast<std::size_t>(next_random(random) % world.bodies.size());
        Body& body = world.bodies[index];
        const Fixed value = Fixed::from_raw(static_cast<Fixed::rep>((next_random(random) % 10'001U) + 1U));
        switch (next_random(random) % 4U) {
        case 0U:
            body.position.x += value;
            dirty.mark(index, DirtyComponent::PositionX);
            break;
        case 1U:
            body.position.y -= value;
            dirty.mark(index, DirtyComponent::PositionY);
            break;
        case 2U:
            body.velocity.x += value;
            dirty.mark(index, DirtyComponent::VelocityX);
            break;
        default:
            body.velocity.y -= value;
            dirty.mark(index, DirtyComponent::VelocityY);
            break;
        }
    }
    return dirty;
}

void run_config(SnapshotStoreConfig config, std::uint64_t seed) {
    constexpr std::size_t body_count = 257;
    constexpr std::size_t operations = 2'000;
    auto store = make_snapshot_store(config);
    WorldState current = make_world(body_count);
    DirtySet initial = DirtySet::full(body_count);
    store->capture(current, &initial);
    std::vector<WorldState> history{current};
    std::uint64_t random = seed;

    for (std::size_t operation = 0; operation < operations; ++operation) {
        const bool can_truncate = history.size() > 4U;
        const bool truncate = can_truncate && next_random(random) % 23U == 0U;
        if (truncate) {
            const std::size_t keep_index = static_cast<std::size_t>(
                next_random(random) % (history.size() - 1U));
            const std::uint64_t frame = history[keep_index].frame;
            store->truncate_after(frame);
            history.erase(history.begin() + static_cast<std::ptrdiff_t>(keep_index + 1U), history.end());
            current = history.back();
        } else {
            DirtySet dirty = random_mutation(current, random);
            store->capture(current, &dirty);
            history.push_back(current);
            if (history.size() > config.capacity) history.erase(history.begin());
        }

        const std::size_t probe_index = static_cast<std::size_t>(next_random(random) % history.size());
        const WorldState& expected = history[probe_index];
        const WorldState restored = store->restore(expected.frame);
        if (restored != expected || store->scan_hash(expected.frame) != stable_hash(expected)) {
            throw std::runtime_error("Model-based snapshot fuzz mismatch");
        }
        const std::size_t body_index = static_cast<std::size_t>(next_random(random) % body_count);
        const auto body = store->lookup(expected.frame, static_cast<EntityId>(body_index + 1U));
        if (!body.has_value() || *body != expected.bodies[body_index]) {
            throw std::runtime_error("Model-based snapshot lookup mismatch");
        }
    }
}

} // namespace

int main() {
    try {
        constexpr std::array<SnapshotStrategy, 6> strategies{
            SnapshotStrategy::FullCopy,
            SnapshotStrategy::DeltaLog,
            SnapshotStrategy::PagedCopyOnWrite,
            SnapshotStrategy::PersistentChunkTree,
            SnapshotStrategy::ComponentSoA,
            SnapshotStrategy::HybridAdaptive,
        };
        std::uint64_t seed = 0x4E454F454E470003ULL;
        for (const SnapshotStrategy strategy : strategies) {
            run_config(SnapshotStoreConfig{
                .strategy = strategy,
                .capacity = 64,
                .page_bodies = 16,
                .checkpoint_interval = 4,
                .persistent_leaf_bodies = 32,
                .audit_dirty_contract = true,
            }, seed++);
            run_config(SnapshotStoreConfig{
                .strategy = strategy,
                .capacity = 64,
                .page_bodies = 256,
                .checkpoint_interval = 32,
                .persistent_leaf_bodies = 16,
                .audit_dirty_contract = true,
            }, seed++);
        }
        std::cout << "NeoEng v0.3 model-based snapshot fuzz passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Snapshot fuzz failed: " << error.what() << '\n';
        return 1;
    }
}
