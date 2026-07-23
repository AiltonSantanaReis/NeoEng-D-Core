#include "neoeng/core/broadphase.hpp"
#include "neoeng/core/component_world.hpp"
#include "neoeng/core/hash.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <queue>
#include <vector>

namespace {

using namespace neoeng::core;

class Generator final {
public:
    explicit Generator(std::uint64_t seed) : state_(seed) {}
    [[nodiscard]] std::uint64_t next() noexcept {
        state_ ^= state_ << 13U;
        state_ ^= state_ >> 7U;
        state_ ^= state_ << 17U;
        return state_;
    }
    [[nodiscard]] std::size_t bounded(std::size_t bound) noexcept {
        return static_cast<std::size_t>(next() % bound);
    }
private:
    std::uint64_t state_;
};

[[nodiscard]] DeterministicActiveSet brute_closure(
    std::size_t body_count,
    const std::vector<BroadphasePair>& pairs,
    const DeterministicActiveSet& seeds) {
    std::vector<std::vector<std::size_t>> graph(body_count);
    for (const BroadphasePair& pair : pairs) {
        graph[pair.first].push_back(pair.second);
        graph[pair.second].push_back(pair.first);
    }
    std::vector<bool> visited(body_count, false);
    std::queue<std::size_t> queue;
    for (const std::size_t seed : seeds.indices()) {
        visited[seed] = true;
        queue.push(seed);
    }
    while (!queue.empty()) {
        const std::size_t current = queue.front();
        queue.pop();
        for (const std::size_t next : graph[current]) {
            if (!visited[next]) {
                visited[next] = true;
                queue.push(next);
            }
        }
    }
    std::vector<std::size_t> result;
    for (std::size_t index = 0U; index < body_count; ++index) {
        if (visited[index]) result.push_back(index);
    }
    return DeterministicActiveSet(std::move(result));
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1
        ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
        : 2'000U;
    Generator generator(0xC0FFEE123456789ULL);
    std::uint64_t aggregate_hash = 0U;
    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        WorldState world;
        world.frame = iteration;
        constexpr std::size_t body_count = 97U;
        world.bodies.reserve(body_count);
        for (std::size_t index = 0U; index < body_count; ++index) {
            const auto x = static_cast<Fixed::rep>(generator.bounded(41U)) - 20;
            const auto y = static_cast<Fixed::rep>(generator.bounded(41U)) - 20;
            world.bodies.push_back(Body{
                .id = static_cast<EntityId>(index + 1U),
                .position = {Fixed::from_ratio(x, 2), Fixed::from_ratio(y, 2)},
                .velocity = {},
            });
        }
        ComponentWorldState direct = make_component_world(world, 16U);
        ComponentWorldState legacy = direct;
        const Fixed half_extent = Fixed::from_ratio(1, 2);
        const GridBroadphaseState grid = make_grid_broadphase(
            direct, Fixed::from_integer(1), half_extent);
        std::vector<std::size_t> seeds;
        for (std::size_t count = 0U; count < 3U; ++count) seeds.push_back(generator.bounded(body_count));
        std::sort(seeds.begin(), seeds.end());
        seeds.erase(std::unique(seeds.begin(), seeds.end()), seeds.end());
        const DeterministicActiveSet seed_set(seeds);
        const std::vector<BroadphasePair> brute_pairs = brute_force_overlap_pairs(direct, half_extent);
        const DeterministicActiveSet expected_closure = brute_closure(body_count, brute_pairs, seed_set);
        std::vector<BroadphasePair> expected_pairs;
        for (const BroadphasePair& pair : brute_pairs) {
            if (expected_closure.contains(pair.first) && expected_closure.contains(pair.second)) {
                expected_pairs.push_back(pair);
            }
        }
        const IslandClosure grid_closure = conservative_island_closure(direct, grid, seed_set);
        if (grid_closure.overlaps != expected_pairs
            || grid_closure.bodies != expected_closure) {
            std::cerr << "Broadphase mismatch at iteration " << iteration << '\n';
            return EXIT_FAILURE;
        }

        std::vector<InputCommand> inputs;
        for (std::size_t command = 0U; command < 5U; ++command) {
            inputs.push_back(InputCommand{
                .entity = static_cast<EntityId>(generator.bounded(body_count) + 1U),
                .acceleration = {
                    Fixed::from_ratio(static_cast<Fixed::rep>(generator.bounded(7U)) - 3, 17),
                    Fixed::from_ratio(static_cast<Fixed::rep>(generator.bounded(7U)) - 3, 19)},
            });
        }
        const ComponentStepResult direct_result = step_component_active(
            direct, seed_set, inputs, ComponentStepOptions{.kernel_mode = FixedKernelMode::Auto});
        const ComponentStepResult legacy_result = step_component_active_legacy(legacy, seed_set, inputs);
        if (direct_result.state.materialize() != legacy_result.state.materialize()
            || direct_result.active != legacy_result.active) {
            std::cerr << "Direct/legacy mismatch at iteration " << iteration << '\n';
            return EXIT_FAILURE;
        }
        aggregate_hash ^= stable_hash(direct_result.state.materialize())
            + static_cast<std::uint64_t>(iteration);
    }
    std::cout << "v0.6 model fuzz passed " << iterations
              << " iterations; hash=0x" << std::hex << std::uppercase
              << aggregate_hash << '\n';
    return EXIT_SUCCESS;
}
