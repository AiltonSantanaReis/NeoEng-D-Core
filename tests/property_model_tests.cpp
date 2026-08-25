#include "neoeng/core/hash.hpp"
#include "neoeng/core/rollback.hpp"
#include "neoeng/core/simulation.hpp"
#include "neoeng/core/snapshot_store.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace neoeng::core;

constexpr std::uint64_t kSeed = 0xC5021E0400000001ULL;
constexpr std::size_t kScenarioCount = 128U;
constexpr std::size_t kSequenceFrames = 4U;
constexpr std::size_t kRollbackFrames = 5U;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

void require(bool condition, std::string_view message) {
    if (!condition) {
        fail(std::string{message});
    }
}

std::span<const InputCommand> as_span(
    const std::vector<InputCommand>& inputs) noexcept {
    return {inputs.data(), inputs.size()};
}

class DeterministicRng final {
public:
    explicit DeterministicRng(std::uint64_t seed) noexcept : state_(seed) {}

    [[nodiscard]] std::uint64_t next() noexcept {
        state_ += 0x9E3779B97F4A7C15ULL;
        std::uint64_t value = state_;
        value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31U);
    }

    [[nodiscard]] std::size_t index(std::size_t bound) {
        if (bound == 0U) {
            fail("deterministic RNG bound must be nonzero");
        }
        return static_cast<std::size_t>(
            next() % static_cast<std::uint64_t>(bound));
    }

    [[nodiscard]] std::int64_t signed_value(
        std::int64_t minimum,
        std::int64_t maximum) {
        if (minimum > maximum) {
            fail("deterministic RNG invalid signed range");
        }
        const auto width = static_cast<std::uint64_t>(
            (maximum - minimum) + std::int64_t{1});
        return minimum + static_cast<std::int64_t>(next() % width);
    }

private:
    std::uint64_t state_{};
};

Fixed generated_ratio(DeterministicRng& rng) {
    return Fixed::from_ratio(
        rng.signed_value(-4, 4),
        rng.signed_value(1, 8));
}

WorldState make_world(DeterministicRng& rng) {
    WorldState world{};
    world.frame = rng.next() % std::uint64_t{32};

    const std::size_t body_count = std::size_t{1} + rng.index(6U);
    world.bodies.reserve(body_count);

    for (std::size_t index = 0U; index < body_count; ++index) {
        const auto id_value =
            std::uint64_t{10}
            + static_cast<std::uint64_t>(index) * std::uint64_t{7};

        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(id_value),
            .position = {
                .x = Fixed::from_integer(rng.signed_value(-8, 8)),
                .y = Fixed::from_integer(rng.signed_value(-8, 8)),
            },
            .velocity = {
                .x = generated_ratio(rng),
                .y = generated_ratio(rng),
            },
        });
    }

    return world;
}

std::vector<InputCommand> make_inputs(
    DeterministicRng& rng,
    const WorldState& world) {
    require(!world.bodies.empty(), "generated world unexpectedly empty");

    const std::size_t maximum_extra =
        world.bodies.size() * std::size_t{2} + std::size_t{1};
    const std::size_t command_count =
        std::size_t{2} + rng.index(maximum_extra);

    std::vector<InputCommand> inputs;
    inputs.reserve(command_count);

    for (std::size_t index = 0U; index < command_count; ++index) {
        const Body& body = world.bodies[rng.index(world.bodies.size())];
        inputs.push_back(InputCommand{
            .entity = body.id,
            .acceleration = {
                .x = generated_ratio(rng),
                .y = generated_ratio(rng),
            },
        });
    }

    return inputs;
}

bool reference_input_less(
    const InputCommand& lhs,
    const InputCommand& rhs) noexcept {
    if (lhs.entity != rhs.entity) {
        return lhs.entity < rhs.entity;
    }
    if (lhs.acceleration.x != rhs.acceleration.x) {
        return lhs.acceleration.x < rhs.acceleration.x;
    }
    return lhs.acceleration.y < rhs.acceleration.y;
}

void reference_insert(
    std::vector<InputCommand>& ordered,
    const InputCommand& input) {
    auto position = ordered.begin();
    while (position != ordered.end() && !reference_input_less(input, *position)) {
        ++position;
    }
    ordered.insert(position, input);
}

WorldState reference_step(
    const WorldState& current,
    std::span<const InputCommand> inputs) {
    if (current.frame == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("reference model frame maximum reached");
    }

    WorldState next = current;
    next.frame = current.frame + std::uint64_t{1};

    for (Body& body : next.bodies) {
        std::vector<InputCommand> ordered;
        for (const InputCommand& input : inputs) {
            if (input.entity == body.id) {
                reference_insert(ordered, input);
            }
        }

        Fixed total_x{};
        Fixed total_y{};
        for (const InputCommand& input : ordered) {
            total_x += input.acceleration.x;
            total_y += input.acceleration.y;
        }

        body.velocity.x += total_x * kSimulationDelta;
        body.velocity.y += total_y * kSimulationDelta;
        body.position.x += body.velocity.x * kSimulationDelta;
        body.position.y += body.velocity.y * kSimulationDelta;
    }

    return next;
}

void require_state_equal(
    const WorldState& actual,
    const WorldState& expected,
    std::string_view label) {
    if (actual != expected) {
        fail(std::string{label} + ": WorldState mismatch");
    }

    const auto actual_bytes = canonical_serialize(actual);
    const auto expected_bytes = canonical_serialize(expected);
    if (actual_bytes != expected_bytes) {
        fail(std::string{label} + ": canonical bytes mismatch");
    }

    if (stable_hash(actual) != stable_hash(expected)) {
        fail(std::string{label} + ": stable hash mismatch");
    }
}

void deterministic_shuffle(
    std::vector<InputCommand>& values,
    DeterministicRng& rng) {
    for (std::size_t remaining = values.size(); remaining > 1U; --remaining) {
        const std::size_t selected = rng.index(remaining);
        std::swap(values[remaining - 1U], values[selected]);
    }
}

void run_overflow_sensitive_order_property() {
    const Fixed::rep maximum = std::numeric_limits<Fixed::rep>::max();

    const WorldState initial{
        .frame = std::uint64_t{3},
        .bodies = {
            Body{
                .id = EntityId{1},
                .position = {},
                .velocity = {},
            },
        },
    };

    const std::vector<InputCommand> inputs{
        InputCommand{
            .entity = EntityId{1},
            .acceleration = {
                .x = Fixed::from_raw(maximum - Fixed::rep{10}),
                .y = {},
            },
        },
        InputCommand{
            .entity = EntityId{1},
            .acceleration = {
                .x = Fixed::from_raw(Fixed::rep{20}),
                .y = {},
            },
        },
        InputCommand{
            .entity = EntityId{1},
            .acceleration = {
                .x = Fixed::from_raw(Fixed::rep{-20}),
                .y = {},
            },
        },
    };

    const WorldState expected = reference_step(initial, as_span(inputs));

    std::vector<std::vector<InputCommand>> permutations;
    permutations.push_back(inputs);

    auto reversed = inputs;
    std::reverse(reversed.begin(), reversed.end());
    permutations.push_back(reversed);

    permutations.push_back({inputs[1], inputs[0], inputs[2]});
    permutations.push_back({inputs[2], inputs[0], inputs[1]});

    for (const auto& permutation : permutations) {
        const WorldState actual = step(initial, as_span(permutation));
        require_state_equal(
            actual,
            expected,
            "overflow-sensitive canonical input order");
    }
}

void run_permutation_and_model_properties(
    const WorldState& initial,
    const std::vector<InputCommand>& inputs,
    DeterministicRng& rng) {
    const WorldState expected = reference_step(initial, as_span(inputs));

    std::vector<std::vector<InputCommand>> permutations;
    permutations.push_back(inputs);

    auto reversed = inputs;
    std::reverse(reversed.begin(), reversed.end());
    permutations.push_back(reversed);

    std::vector<InputCommand> rotated;
    rotated.reserve(inputs.size());
    const std::size_t rotation = rng.index(inputs.size());
    for (std::size_t index = 0U; index < inputs.size(); ++index) {
        rotated.push_back(inputs[(index + rotation) % inputs.size()]);
    }
    permutations.push_back(rotated);

    auto shuffled = inputs;
    deterministic_shuffle(shuffled, rng);
    permutations.push_back(shuffled);

    WorldState canonical_result{};
    bool canonical_result_initialized = false;

    for (const auto& permutation : permutations) {
        const WorldState actual = step(initial, as_span(permutation));
        require_state_equal(actual, expected, "generated reference differential");

        const auto first_serialization = canonical_serialize(actual);
        const auto second_serialization = canonical_serialize(actual);
        require(
            first_serialization == second_serialization,
            "canonical serialization is not repeatable");

        if (!canonical_result_initialized) {
            canonical_result = actual;
            canonical_result_initialized = true;
        } else {
            require_state_equal(
                actual,
                canonical_result,
                "generated input permutation invariance");
        }
    }
}

constexpr std::array<SnapshotStrategy, 6> kStrategies{
    SnapshotStrategy::FullCopy,
    SnapshotStrategy::DeltaLog,
    SnapshotStrategy::PagedCopyOnWrite,
    SnapshotStrategy::PersistentChunkTree,
    SnapshotStrategy::ComponentSoA,
    SnapshotStrategy::HybridAdaptive,
};

void run_snapshot_properties(
    const WorldState& initial,
    DeterministicRng& rng) {
    std::vector<std::vector<InputCommand>> sequence_inputs;
    sequence_inputs.reserve(kSequenceFrames);
    while (sequence_inputs.size() < kSequenceFrames) {
        sequence_inputs.push_back(make_inputs(rng, initial));
    }

    std::vector<WorldState> expected_states;
    expected_states.reserve(kSequenceFrames + std::size_t{1});
    expected_states.push_back(initial);

    WorldState expected = initial;
    for (const auto& frame_inputs : sequence_inputs) {
        expected = reference_step(expected, as_span(frame_inputs));
        expected_states.push_back(expected);
    }

    for (const SnapshotStrategy strategy : kStrategies) {
        auto store = make_snapshot_store(strategy, 16U);
        const DirtySet initial_dirty = DirtySet::full(initial.bodies.size());
        store->capture(initial, &initial_dirty);

        WorldState current = initial;
        for (std::size_t frame_index = 0U;
             frame_index < sequence_inputs.size();
             ++frame_index) {
            const StepResult result = step_with_dirty(
                current,
                as_span(sequence_inputs[frame_index]));
            require_state_equal(
                result.state,
                expected_states[frame_index + std::size_t{1}],
                "snapshot sequence optimized/reference");
            store->capture(result.state, &result.dirty);
            current = result.state;
        }

        for (const WorldState& expected_state : expected_states) {
            require(
                store->contains(expected_state.frame),
                "snapshot strategy omitted retained frame");
            const WorldState restored = store->restore(expected_state.frame);
            require_state_equal(
                restored,
                expected_state,
                "snapshot restore strategy equivalence");
        }
    }
}

void run_rollback_properties(
    const WorldState& initial,
    DeterministicRng& rng) {
    std::vector<std::vector<InputCommand>> frame_inputs;
    frame_inputs.reserve(kRollbackFrames);
    while (frame_inputs.size() < kRollbackFrames) {
        frame_inputs.push_back(make_inputs(rng, initial));
    }

    const std::size_t correction_offset =
        std::size_t{1} + rng.index(std::size_t{3});
    const std::vector<InputCommand> corrected = make_inputs(rng, initial);

    WorldState expected = initial;
    for (std::size_t frame_index = 0U;
         frame_index < frame_inputs.size();
         ++frame_index) {
        const auto& selected =
            frame_index == correction_offset
                ? corrected
                : frame_inputs[frame_index];
        expected = reference_step(expected, as_span(selected));
    }

    WorldState strategy_reference{};
    bool strategy_reference_initialized = false;

    for (const SnapshotStrategy strategy : kStrategies) {
        RollbackEngine engine{initial, 16U, strategy};

        for (const auto& inputs : frame_inputs) {
            engine.advance(as_span(inputs));
        }

        const std::uint64_t correction_frame =
            initial.frame + static_cast<std::uint64_t>(correction_offset);
        const std::size_t resimulated =
            engine.correct_input_and_resimulate(
                correction_frame,
                as_span(corrected));

        require(
            resimulated == kRollbackFrames - correction_offset,
            "rollback resimulation cardinality mismatch");
        require_state_equal(
            engine.state(),
            expected,
            "rollback/replay reference differential");

        if (!strategy_reference_initialized) {
            strategy_reference = engine.state();
            strategy_reference_initialized = true;
        } else {
            require_state_equal(
                engine.state(),
                strategy_reference,
                "rollback strategy equivalence");
        }
    }
}

void run_campaign() {
    run_overflow_sensitive_order_property();

    DeterministicRng rng{kSeed};
    for (std::size_t scenario = 0U; scenario < kScenarioCount; ++scenario) {
        const WorldState initial = make_world(rng);
        const std::vector<InputCommand> inputs = make_inputs(rng, initial);

        run_permutation_and_model_properties(initial, inputs, rng);
        run_snapshot_properties(initial, rng);
        run_rollback_properties(initial, rng);
    }
}

} // namespace

int main() {
    try {
        run_campaign();
        std::cout
            << "cs021_property_model=PASS "
            << "seed=0xc5021e0400000001 "
            << "scenarios=128 "
            << "permutations=4 "
            << "model=reference "
            << "snapshots=6 "
            << "rollback=6 "
            << "serialize=covered "
            << "deserialize=not_applicable\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cs021_property_model=FAIL reason=" << error.what() << '\n';
        return 1;
    }
}
