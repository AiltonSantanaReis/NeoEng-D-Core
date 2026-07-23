#include "neoeng/core/axis_forest_projection.hpp"
#include "neoeng/core/paged_atomic_history.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <vector>

namespace {
using namespace neoeng::core;
constexpr std::int32_t kOne = 1 << 30;

void mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

bool axis_forest_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    const std::size_t bodies = 2U + static_cast<std::size_t>(rng() % 63U);
    std::vector<Fixed::rep> vx(bodies), vy(bodies), second_x, second_y;
    std::vector<std::uint32_t> masses(bodies);
    std::vector<NormalContact> contacts; contacts.reserve(bodies - 1U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        vx[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 2'000'001U) - 1'000'000);
        vy[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 2'000'001U) - 1'000'000);
        masses[body] = 1U + static_cast<std::uint32_t>(rng() % 64U);
        if (body == 0U) continue;
        const std::size_t parent = static_cast<std::size_t>(rng() % body);
        contacts.push_back({parent, body, (rng() & 1U) == 0U
            ? NormalQ30{kOne, 0} : NormalQ30{0, kOne}});
    }
    second_x = vx; second_y = vy;
    AxisForestScratch first_scratch(bodies, contacts.size()), second_scratch(bodies, contacts.size());
    const AxisForestConfig config{.tree = {.maximum_active_set_iterations = 4096U,
        .feasibility_tolerance_raw = 4U, .stationarity_tolerance_raw = 16U}};
    const auto first = project_axis_forest_inplace(vx, vy, masses, contacts, config, first_scratch);
    const auto second = project_axis_forest_inplace(second_x, second_y, masses, contacts, config, second_scratch);
    if (!first.certified || !second.certified || vx != second_x || vy != second_y) return false;
    for (const NormalContact& contact : contacts) {
        const WideInteger violation = (static_cast<WideInteger>(vx[contact.first]) - vx[contact.second])
                * contact.normal.x
            + (static_cast<WideInteger>(vy[contact.first]) - vy[contact.second])
                * contact.normal.y;
        if (violation > static_cast<WideInteger>(4U) * (WideInteger{1} << 30U)) return false;
    }
    for (Fixed::rep value : vx) mix(aggregate, static_cast<std::uint64_t>(value));
    for (Fixed::rep value : vy) mix(aggregate, static_cast<std::uint64_t>(value));
    mix(aggregate, first.x_components); mix(aggregate, first.y_components);
    return true;
}

bool paged_history_case(std::mt19937_64& rng, std::uint64_t& aggregate) {
    constexpr std::size_t bodies = 256U, contacts = 16U, pairs = 32U;
    PagedAtomicHistory history({.bodies = bodies, .contacts = contacts,
        .maximum_candidate_pairs = pairs, .history_capacity = 16U, .page_elements = 32U,
        .maximum_position_dirty_pages_per_frame = 3U,
        .maximum_velocity_dirty_pages_per_frame = 3U,
        .maximum_mass_dirty_pages_per_frame = 2U,
        .maximum_contact_dirty_pages_per_frame = 2U,
        .full_position_generations = 2U, .full_velocity_generations = 2U,
        .full_contact_generations = 2U, .maximum_cache_generations = 2U});
    AtomicTemporalExternalState state(bodies, contacts, pairs), restored(bodies, contacts, pairs);
    state.pair_count = contacts;
    for (std::size_t body = 0U; body < bodies; ++body) {
        state.position_x[body] = static_cast<Fixed::rep>(body * 101U);
        state.position_y[body] = static_cast<Fixed::rep>(body * 53U);
        state.masses[body] = 1U + static_cast<std::uint32_t>(body % 7U);
    }
    for (std::size_t contact = 0U; contact < contacts; ++contact) {
        state.manifold[contact] = {contact * 2U, contact * 2U + 1U, {kOne, 0}};
        state.pairs[contact] = {contact * 2U, contact * 2U + 1U};
    }
    history.capture(state);
    std::vector<std::vector<Fixed::rep>> models(12U);
    models[0] = state.position_x;
    for (std::uint64_t frame = 1U; frame < 12U; ++frame) {
        state.frame = frame;
        std::vector<std::size_t> changed;
        const std::size_t page = static_cast<std::size_t>(rng() % 8U);
        const std::size_t changes = 1U + static_cast<std::size_t>(rng() % 4U);
        for (std::size_t i = 0U; i < changes; ++i) {
            const std::size_t body = page * 32U + static_cast<std::size_t>(rng() % 32U);
            state.position_x[body] += static_cast<Fixed::rep>(1U + rng() % 997U);
            state.position_y[body] -= static_cast<Fixed::rep>(1U + rng() % 499U);
            changed.push_back(body);
        }
        std::sort(changed.begin(), changed.end());
        changed.erase(std::unique(changed.begin(), changed.end()), changed.end());
        history.capture(AtomicTemporalStateView{.frame = state.frame, .valid_until_frame = 0U,
            .position_x = state.position_x, .position_y = state.position_y,
            .velocity_x = state.velocity_x, .velocity_y = state.velocity_y, .masses = state.masses,
            .dual = state.dual, .manifold = state.manifold, .contact_stable = state.contact_stable, .contact_candidate = state.contact_candidate,
            .fat_bounds = state.fat_bounds,
            .pairs = std::span<const BroadphasePair>(state.pairs.data(), state.pair_count)},
            {.changed_position_bodies = changed, .position_hints_complete = true});
        models[frame] = state.position_x;
    }
    const std::uint64_t frame = 1U + rng() % 11U;
    history.restore(frame, restored);
    if (restored.position_x != models[frame]) return false;
    history.truncate_after(frame);
    if (!history.contains(frame)) return false;
    mix(aggregate, frame); mix(aggregate, history.stats().pages_shared);
    for (Fixed::rep value : restored.position_x) mix(aggregate, static_cast<std::uint64_t>(value));
    return true;
}
} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1 ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10)) : 5'000U;
    std::mt19937_64 rng(0x4E454F454E475631ULL);
    std::uint64_t aggregate = 0xCBF29CE484222325ULL;
    for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
        if (!axis_forest_case(rng, aggregate) || !paged_history_case(rng, aggregate)) {
            std::cerr << "v0.18 fuzz failed at iteration " << iteration << '\n';
            return EXIT_FAILURE;
        }
    }
    std::cout << "v0.18 fuzz iterations=" << iterations << " aggregate=0x"
              << std::hex << std::uppercase << aggregate << std::dec << '\n';
    return EXIT_SUCCESS;
}
