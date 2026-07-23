#include "neoeng/core/active_world.hpp"
#include "neoeng/core/component_world.hpp"
#include "neoeng/core/radix_world.hpp"
#include "neoeng/core/hash.hpp"
#include "neoeng/core/immutable_rollback.hpp"
#include "neoeng/core/rollback.hpp"
#include "neoeng/core/snapshot_store.hpp"

#include <array>
#include <cstdint>
#include <iostream>
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

std::uint64_t next_random(std::uint64_t& state) {
    state = state * 6'364'136'223'846'793'005ULL + 1'442'695'040'888'963'407ULL;
    return state;
}

WorldState make_world() {
    std::vector<Body> bodies;
    bodies.reserve(256);
    for (std::uint32_t id = 1; id <= 256; ++id) {
        bodies.push_back(Body{
            .id = id,
            .position = {Fixed::from_integer(static_cast<std::int64_t>(id)), {}},
            .velocity = {}
        });
    }
    return WorldState{.frame = 0, .bodies = std::move(bodies)};
}

} // namespace

int main() {
    std::uint64_t reference_hash = 0;
    for (const SnapshotStrategy strategy : kStrategies) {
        RollbackEngine engine(make_world(), 300, strategy);
        std::uint64_t random_state = 0xC0FFEE1234567890ULL;

        for (std::uint64_t frame = 0; frame < 10'000; ++frame) {
            std::vector<InputCommand> inputs;
            inputs.reserve(8);
            for (int i = 0; i < 8; ++i) {
                const auto sample = next_random(random_state);
                const auto id = static_cast<EntityId>((sample % 256U) + 1U);
                const auto ax = static_cast<std::int64_t>((sample >> 16U) % 7U) - 3;
                const auto ay = static_cast<std::int64_t>((sample >> 24U) % 7U) - 3;
                inputs.push_back(InputCommand{
                    .entity = id,
                    .acceleration = {Fixed::from_integer(ax), Fixed::from_integer(ay)}
                });
            }
            engine.advance(inputs);
        }

        const std::uint64_t hash = stable_hash(engine.state());
        if (reference_hash == 0U) {
            reference_hash = hash;
        } else if (hash != reference_hash) {
            std::cerr << "Cross-strategy state divergence in determinism probe\n";
            return 1;
        }
        std::cout << to_string(strategy) << '=' << hash_hex(hash) << '\n';
    }

    ImmutableRollbackEngine immutable(make_world(), 300U, 32U);
    std::uint64_t random_state = 0xC0FFEE1234567890ULL;
    for (std::uint64_t frame = 0; frame < 10'000; ++frame) {
        std::vector<InputCommand> inputs;
        inputs.reserve(8);
        for (int i = 0; i < 8; ++i) {
            const auto sample = next_random(random_state);
            const auto id = static_cast<EntityId>((sample % 256U) + 1U);
            const auto ax = static_cast<std::int64_t>((sample >> 16U) % 7U) - 3;
            const auto ay = static_cast<std::int64_t>((sample >> 24U) % 7U) - 3;
            inputs.push_back(InputCommand{
                .entity = id,
                .acceleration = {Fixed::from_integer(ax), Fixed::from_integer(ay)}
            });
        }
        immutable.advance(inputs);
    }
    const std::uint64_t immutable_canonical = stable_hash(immutable.materialized_state());
    if (immutable_canonical != reference_hash) {
        std::cerr << "Immutable state divergence in determinism probe\n";
        return 1;
    }
    std::cout << "immutable_chunked_canonical=" << hash_hex(immutable_canonical) << '\n';
    std::cout << "immutable_chunked_merkle=" << hash_hex(immutable.state().merkle_hash()) << '\n';

    ActiveRollbackEngine active_engine(make_world(), 300U, 32U);
    ComponentWorldState component = make_component_world(make_world(), 32U);
    DeterministicActiveSet component_active;
    std::array<RadixWorldState, 3> radix_states{
        make_radix_world(make_world(), 32U, 16U),
        make_radix_world(make_world(), 32U, 32U),
        make_radix_world(make_world(), 32U, 64U),
    };
    std::array<DeterministicActiveSet, 3> radix_active{};
    random_state = 0xC0FFEE1234567890ULL;
    for (std::uint64_t frame = 0; frame < 10'000; ++frame) {
        std::vector<InputCommand> inputs;
        inputs.reserve(8);
        for (int i = 0; i < 8; ++i) {
            const auto sample = next_random(random_state);
            const auto id = static_cast<EntityId>((sample % 256U) + 1U);
            const auto ax = static_cast<std::int64_t>((sample >> 16U) % 7U) - 3;
            const auto ay = static_cast<std::int64_t>((sample >> 24U) % 7U) - 3;
            inputs.push_back(InputCommand{
                .entity = id,
                .acceleration = {Fixed::from_integer(ax), Fixed::from_integer(ay)}
            });
        }
        active_engine.advance(inputs);
        auto component_result = step_component_active(component, component_active, inputs);
        component = std::move(component_result.state);
        component_active = std::move(component_result.active);
        for (std::size_t index = 0; index < radix_states.size(); ++index) {
            auto radix_result = step_radix_active(radix_states[index], radix_active[index], inputs);
            radix_states[index] = std::move(radix_result.state);
            radix_active[index] = std::move(radix_result.active);
        }
    }
    const std::uint64_t active_hash = stable_hash(active_engine.materialized_state());
    const std::uint64_t component_hash = stable_hash(component.materialize());
    if (active_hash != reference_hash || component_hash != reference_hash) {
        std::cerr << "Active/component state divergence in determinism probe\n";
        return 1;
    }
    std::cout << "active_binary_canonical=" << hash_hex(active_hash) << '\n';
    std::cout << "active_component_soa_canonical=" << hash_hex(component_hash) << '\n';
    constexpr std::array<std::size_t, 3> fanouts{16U, 32U, 64U};
    for (std::size_t index = 0; index < radix_states.size(); ++index) {
        const std::uint64_t radix_hash = stable_hash(radix_states[index].materialize());
        if (radix_hash != reference_hash) {
            std::cerr << "Radix state divergence in determinism probe\n";
            return 1;
        }
        std::cout << "active_radix" << fanouts[index] << "_canonical="
                  << hash_hex(radix_hash) << '\n';
    }
    return 0;
}
