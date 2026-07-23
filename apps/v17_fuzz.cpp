#include "neoeng/core/paged_atomic_temporal_physics.hpp"

#include <array>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace neoeng::core;
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 4> kNormals{{
    {kOne, 0}, {0, kOne}, {759'250'125, 759'250'125}, {644'245'094, 858'993'459}
}};

struct Rng final {
    std::uint64_t state{0xD1B54A32D192ED03ULL};
    std::uint64_t next() noexcept { state ^= state >> 12U; state ^= state << 25U; state ^= state >> 27U; return state * 0x2545F4914F6CDD1DULL; }
    std::size_t index(std::size_t limit) noexcept { return static_cast<std::size_t>(next() % limit); }
    std::int64_t signed_small(std::int64_t limit) noexcept { return static_cast<std::int64_t>(next() % static_cast<std::uint64_t>(2 * limit + 1)) - limit; }
};

struct Dataset final {
    std::vector<Fixed::rep> px, py, vx, vy;
    std::vector<std::uint32_t> masses;
    std::vector<NormalContact> contacts;
    bool tree{};
};

Fixed::rep mul_q30(std::int32_t component, Fixed::rep magnitude) {
    return static_cast<Fixed::rep>(static_cast<WideInteger>(component) * magnitude / (WideInteger{1} << 30U));
}

Dataset matching_dataset(std::size_t bodies, Rng& rng) {
    Dataset d; d.px.resize(bodies); d.py.resize(bodies); d.vx.resize(bodies); d.vy.resize(bodies);
    d.masses.resize(bodies); d.contacts.reserve(bodies / 2U);
    const auto sep = Fixed::from_ratio(3, 4).raw();
    for (std::size_t pair = 0U; pair < bodies / 2U; ++pair) {
        const std::size_t a = pair * 2U, b = a + 1U; const auto n = kNormals[rng.index(kNormals.size())];
        const auto cx = Fixed::from_integer(static_cast<Fixed::rep>((pair % 8U) * 4U)).raw();
        const auto cy = Fixed::from_integer(static_cast<Fixed::rep>((pair / 8U) * 4U)).raw();
        const auto dx = mul_q30(n.x, sep), dy = mul_q30(n.y, sep);
        d.px[a] = cx - dx / 2; d.py[a] = cy - dy / 2; d.px[b] = cx + dx / 2; d.py[b] = cy + dy / 2;
        const auto speed = Fixed::from_ratio(1 + static_cast<Fixed::rep>(rng.index(4U)), 64).raw();
        d.vx[a] = mul_q30(n.x, speed); d.vy[a] = mul_q30(n.y, speed); d.vx[b] = -d.vx[a]; d.vy[b] = -d.vy[a];
        d.masses[a] = 1U + static_cast<std::uint32_t>(rng.index(16U)); d.masses[b] = 1U + static_cast<std::uint32_t>(rng.index(16U));
        d.contacts.push_back({a, b, n});
    }
    return d;
}

Dataset chain_dataset(std::size_t bodies, Rng& rng) {
    Dataset d; d.tree = true; d.px.resize(bodies); d.py.assign(bodies, 0); d.vx.resize(bodies); d.vy.assign(bodies, 0);
    d.masses.resize(bodies); d.contacts.reserve(bodies - 1U);
    const auto spacing = Fixed::from_ratio(3, 4).raw();
    for (std::size_t i = 0U; i < bodies; ++i) {
        d.px[i] = static_cast<Fixed::rep>(static_cast<WideInteger>(spacing) * i);
        d.vx[i] = Fixed::from_ratio(static_cast<Fixed::rep>(bodies - i), 65'536).raw();
        d.masses[i] = 1U + static_cast<std::uint32_t>(rng.index(16U));
        if (i != 0U) d.contacts.push_back({i - 1U, i, {kOne, 0}});
    }
    return d;
}

AtomicTemporalPhysicsConfig physics_config(const Dataset& d) {
    return {.bodies = d.vx.size(), .contacts = d.contacts.size(),
        .maximum_candidate_pairs = d.tree ? d.contacts.size() * 8U + 64U : d.contacts.size() * 2U + 64U,
        .history_capacity = 32U, .horizon_frames = 16U,
        .maximum_velocity_mutations = 2U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U, .half_extent = Fixed::from_ratio(1, 2),
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
        .enable_single_tree_solver = d.tree};
}

PagedAtomicTemporalConfig paged_config(const Dataset& d) {
    const auto p = physics_config(d);
    return {.physics = p, .history = {
        .bodies = p.bodies, .contacts = p.contacts,
        .maximum_candidate_pairs = p.maximum_candidate_pairs,
        .history_capacity = 32U, .page_elements = d.tree ? 16U : 32U,
        .maximum_velocity_dirty_pages_per_frame = 8U,
        .maximum_mass_dirty_pages_per_frame = 4U,
        .maximum_contact_dirty_pages_per_frame = 8U,
        .full_velocity_generations = 6U, .full_contact_generations = 6U,
        .maximum_cache_generations = 8U}};
}

void mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
}

std::uint64_t run(std::size_t scenarios) {
    Rng rng; std::uint64_t aggregate = 0xCBF29CE484222325ULL;
    for (std::size_t scenario = 0U; scenario < scenarios; ++scenario) {
        Dataset d = scenario % 4U == 0U ? chain_dataset(24U + rng.index(25U), rng)
                                         : matching_dataset(32U + 2U * rng.index(17U), rng);
        AtomicTemporalPhysicsEngine full(physics_config(d));
        PagedAtomicTemporalPhysicsEngine paged(paged_config(d));
        full.initialize(d.px, d.py, d.vx, d.vy, d.masses, d.contacts);
        paged.initialize(d.px, d.py, d.vx, d.vy, d.masses, d.contacts);
        std::array<VelocityMutation, 20> mutations{};
        for (std::uint64_t frame = 1U; frame <= mutations.size(); ++frame) {
            mutations[frame - 1U] = {rng.index(d.vx.size()),
                static_cast<Fixed::rep>(rng.signed_small(1'024)),
                d.tree ? 0 : static_cast<Fixed::rep>(rng.signed_small(1'024))};
            const AtomicPhysicsFrameInput input{.velocity = std::span<const VelocityMutation>(&mutations[frame - 1U], 1U)};
            full.set_input(frame, input); paged.set_input(frame, input);
        }
        full.simulate_to(20U); paged.simulate_to(20U);
        const std::uint64_t correction_frame = 9U + rng.index(8U);
        VelocityMutation correction = mutations[correction_frame - 1U];
        correction.delta_x += static_cast<Fixed::rep>(1 + rng.index(777U));
        if (!d.tree) correction.delta_y -= static_cast<Fixed::rep>(1 + rng.index(313U));
        const AtomicPhysicsFrameInput corrected{.velocity = std::span<const VelocityMutation>(&correction, 1U)};
        full.correct_and_resimulate(correction_frame, corrected, 20U);
        paged.correct_and_resimulate(correction_frame, corrected, 20U);
        if (!paged.physically_equivalent_to(full) || paged.hash() != full.hash()) {
            throw std::runtime_error("v0.17 paged/full divergence at scenario " + std::to_string(scenario));
        }
        mix(aggregate, paged.hash()); mix(aggregate, paged.physical_hash());
        mix(aggregate, paged.stats().history.pages_shared);
    }
    return aggregate;
}
}

int main(int argc, char** argv) {
    const std::size_t scenarios = argc > 1 ? static_cast<std::size_t>(std::stoull(argv[1])) : 5'000U;
    try {
        const std::uint64_t hash = run(scenarios);
        std::cout << "v0.17 fuzz scenarios=" << scenarios << " aggregate=0x"
                  << std::hex << std::uppercase << hash << std::dec << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
