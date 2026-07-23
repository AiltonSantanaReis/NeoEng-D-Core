#include "neoeng/core/offline_oracle.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <vector>

namespace neoeng::core {
namespace {

constexpr std::uint64_t kInfinity = std::numeric_limits<std::uint64_t>::max() / 4U;

[[nodiscard]] constexpr std::size_t encoding_index(OracleEncoding encoding) noexcept {
    return static_cast<std::size_t>(encoding);
}

[[nodiscard]] std::uint64_t add_saturated(std::uint64_t lhs, std::uint64_t rhs) noexcept {
    return lhs >= kInfinity - rhs ? kInfinity : lhs + rhs;
}

struct State final {
    std::uint64_t cost{kInfinity};
    std::uint16_t previous_encoding{};
    std::uint16_t previous_run{};
    bool reachable{};
};

} // namespace

OracleResult solve_offline_oracle(
    std::span<const OracleFrameCost> frames,
    const OracleConfig& config) {
    if (config.max_delta_run == 0U) {
        throw std::invalid_argument("Oracle maximum delta run must be positive");
    }
    if (frames.empty()) return {};

    const std::size_t runs = config.max_delta_run + 1U;
    const std::size_t states_per_frame = kOracleEncodingCount * runs;
    std::vector<State> table(frames.size() * states_per_frame);
    auto at = [&](std::size_t frame, std::size_t encoding, std::size_t run) -> State& {
        return table[frame * states_per_frame + encoding * runs + run];
    };

    for (std::size_t encoding = 0; encoding < kOracleEncodingCount; ++encoding) {
        if (!frames[0].allowed[encoding]) continue;
        const std::size_t run = encoding == encoding_index(OracleEncoding::Delta) ? 1U : 0U;
        State& state = at(0U, encoding, run);
        state.cost = frames[0].operation_cost[encoding];
        state.reachable = true;
    }

    for (std::size_t frame = 1U; frame < frames.size(); ++frame) {
        for (std::size_t previous_encoding = 0; previous_encoding < kOracleEncodingCount;
             ++previous_encoding) {
            for (std::size_t previous_run = 0; previous_run < runs; ++previous_run) {
                const State& previous = at(frame - 1U, previous_encoding, previous_run);
                if (!previous.reachable) continue;
                for (std::size_t encoding = 0; encoding < kOracleEncodingCount; ++encoding) {
                    if (!frames[frame].allowed[encoding]) continue;
                    std::size_t run = 0U;
                    if (encoding == encoding_index(OracleEncoding::Delta)) {
                        run = previous_encoding == encoding ? previous_run + 1U : 1U;
                        if (run > config.max_delta_run) continue;
                    }
                    const std::uint64_t candidate = add_saturated(
                        add_saturated(previous.cost,
                            config.transition_cost[previous_encoding][encoding]),
                        frames[frame].operation_cost[encoding]);
                    State& destination = at(frame, encoding, run);
                    if (!destination.reachable || candidate < destination.cost) {
                        destination.cost = candidate;
                        destination.previous_encoding = static_cast<std::uint16_t>(previous_encoding);
                        destination.previous_run = static_cast<std::uint16_t>(previous_run);
                        destination.reachable = true;
                    }
                }
            }
        }
    }

    std::size_t best_encoding = 0U;
    std::size_t best_run = 0U;
    std::uint64_t best_cost = kInfinity;
    const std::size_t last = frames.size() - 1U;
    for (std::size_t encoding = 0; encoding < kOracleEncodingCount; ++encoding) {
        for (std::size_t run = 0; run < runs; ++run) {
            const State& state = at(last, encoding, run);
            if (state.reachable && state.cost < best_cost) {
                best_cost = state.cost;
                best_encoding = encoding;
                best_run = run;
            }
        }
    }
    if (best_cost == kInfinity) throw std::runtime_error("Oracle trace has no feasible encoding sequence");

    OracleResult result;
    result.total_cost = best_cost;
    result.sequence.resize(frames.size());
    for (std::size_t frame = frames.size(); frame-- > 0U;) {
        result.sequence[frame] = static_cast<OracleEncoding>(best_encoding);
        if (frame == 0U) break;
        const State& state = at(frame, best_encoding, best_run);
        best_encoding = state.previous_encoding;
        best_run = state.previous_run;
    }
    return result;
}

std::uint64_t evaluate_encoding_sequence(
    std::span<const OracleFrameCost> frames,
    std::span<const OracleEncoding> sequence,
    const OracleConfig& config) {
    if (frames.size() != sequence.size()) {
        throw std::invalid_argument("Oracle frames and sequence have different lengths");
    }
    std::uint64_t total = 0U;
    std::size_t delta_run = 0U;
    for (std::size_t frame = 0; frame < frames.size(); ++frame) {
        const std::size_t encoding = encoding_index(sequence[frame]);
        if (!frames[frame].allowed[encoding]) {
            throw std::invalid_argument("Encoding sequence uses a forbidden operation");
        }
        if (sequence[frame] == OracleEncoding::Delta) {
            ++delta_run;
            if (delta_run > config.max_delta_run) {
                throw std::invalid_argument("Encoding sequence exceeds maximum delta run");
            }
        } else {
            delta_run = 0U;
        }
        if (frame != 0U) {
            total = add_saturated(total, config.transition_cost[
                encoding_index(sequence[frame - 1U])][encoding]);
        }
        total = add_saturated(total, frames[frame].operation_cost[encoding]);
    }
    return total;
}

} // namespace neoeng::core
