#include "neoeng/core/hash.hpp"
#include "neoeng/core/immutable_world.hpp"
#include "neoeng/core/persistent_checkpoint.hpp"
#include "neoeng/core/simulation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace neoeng::core;

namespace {

[[nodiscard]] WorldState make_world(std::size_t count) {
    WorldState state;
    state.bodies.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        state.bodies.push_back(Body{.id = static_cast<EntityId>(index + 1U)});
    }
    return state;
}

struct ModelFrame final {
    WorldState world;
    ImmutableWorldState immutable;
};

void verify(const ModelFrame& frame, const PersistentCheckpointHistory& history) {
    const WorldState materialized = frame.immutable.materialize();
    if (materialized != frame.world) throw std::runtime_error("Immutable fuzz state mismatch");
    if (stable_hash(materialized) != stable_hash(frame.world)) {
        throw std::runtime_error("Immutable fuzz canonical hash mismatch");
    }
    if (!history.contains(frame.world.frame)) {
        throw std::runtime_error("Immutable fuzz history lost current frame");
    }
    if (history.restore(frame.world.frame).materialize() != frame.world) {
        throw std::runtime_error("Immutable fuzz checkpoint restore mismatch");
    }
}

} // namespace

int main() {
    try {
        constexpr std::size_t entity_count = 513U;
        const char* configured_iterations = std::getenv("NEOENG_FUZZ_ITERATIONS");
        const std::size_t iterations = configured_iterations == nullptr
            ? 20'000U
            : static_cast<std::size_t>(std::stoull(configured_iterations));
        if (iterations == 0U) throw std::invalid_argument("Fuzz iterations must be positive");
        constexpr std::size_t capacity = 64U;
        std::mt19937_64 random(0x4E454F454E475634ULL);

        for (const std::size_t chunk_size : {16U, 32U, 64U, 128U}) {
            ModelFrame current{
                .world = make_world(entity_count),
                .immutable = {},
            };
            current.immutable = make_immutable_world(current.world, chunk_size);
            PersistentCheckpointHistory history(PersistentCheckpointConfig{
                .capacity = capacity,
                .max_delta_depth = 8U,
                .policy = PersistentCheckpointPolicy::Adaptive,
                .adaptive_density_ppm = 100'000U,
            });
            history.capture(current.immutable, DirtySet::full(entity_count));
            std::deque<ModelFrame> retained{current};

            for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
                const bool branch = retained.size() > 1U && random() % 11U == 0U;
                if (branch) {
                    const std::size_t offset = static_cast<std::size_t>(random() % retained.size());
                    const std::size_t target = retained.size() - 1U - offset;
                    current = retained[target];
                    history.truncate_after(current.world.frame);
                    while (retained.size() > target + 1U) retained.pop_back();
                } else {
                    const std::size_t input_count = static_cast<std::size_t>(random() % 7U);
                    std::vector<InputCommand> inputs;
                    inputs.reserve(input_count);
                    for (std::size_t input = 0; input < input_count; ++input) {
                        const EntityId entity = static_cast<EntityId>(random() % entity_count + 1U);
                        const auto ax = static_cast<std::int64_t>(random() % 9U) - 4;
                        const auto ay = static_cast<std::int64_t>(random() % 9U) - 4;
                        inputs.push_back(InputCommand{
                            .entity = entity,
                            .acceleration = {
                                Fixed::from_ratio(ax, 7),
                                Fixed::from_ratio(ay, 11),
                            },
                        });
                    }
                    const StepResult model = step_with_dirty(current.world, inputs);
                    const ImmutableStepResult immutable = step_immutable(current.immutable, inputs);
                    current.world = model.state;
                    current.immutable = immutable.state;
                    if (model.dirty.changed_count() != immutable.dirty.changed_count()) {
                        throw std::runtime_error("Immutable fuzz dirty count mismatch");
                    }
                    history.capture(current.immutable, immutable.dirty);
                    retained.push_back(current);
                    while (retained.size() > capacity) retained.pop_front();
                }

                verify(current, history);
                if (!retained.empty() && random() % 5U == 0U) {
                    const std::size_t sample = static_cast<std::size_t>(random() % retained.size());
                    const ModelFrame& expected = retained[sample];
                    if (history.restore(expected.world.frame).materialize() != expected.world) {
                        throw std::runtime_error("Immutable fuzz sampled history mismatch");
                    }
                }
            }
        }

        std::cout << "NeoEng v0.4 immutable model fuzz passed: "
                  << iterations * 4U << " iterations\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "NeoEng v0.4 immutable fuzz failed: " << error.what() << '\n';
        return 1;
    }
}
