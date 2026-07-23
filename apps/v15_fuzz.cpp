#include "neoeng/core/atomic_physics.hpp"
#include "neoeng/core/weighted_tree_projection.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace neoeng::core;
constexpr std::int32_t kOne = 1 << 30;
constexpr NormalQ30 kAxisX{kOne, 0};
constexpr NormalQ30 kThreeFour{644'245'094, 858'993'459};

void verify_atomic_rollback() {
    constexpr std::size_t bodies = 64U;
    constexpr std::size_t contacts = bodies / 2U;
    AtomicPhysicsConfig config{.bodies = bodies, .contacts = contacts, .history_capacity = 32U,
        .maximum_velocity_mutations = 4U, .maximum_mass_mutations = 2U,
        .maximum_contact_mutations = 2U,
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 8U}};
    AtomicPhysicsEngine rollback(config), clean(config);
    std::vector<Fixed::rep> px(bodies), py(bodies), vx(bodies), vy(bodies);
    std::vector<std::uint32_t> masses(bodies, 1U);
    std::vector<NormalContact> manifold;
    for (std::size_t i = 0U; i < bodies; i += 2U) manifold.push_back({i, i + 1U, kThreeFour});
    rollback.initialize(px, py, vx, vy, masses, manifold);
    clean.initialize(px, py, vx, vy, masses, manifold);
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        const std::size_t body = static_cast<std::size_t>((frame * 2U) % bodies);
        VelocityMutation original{body, static_cast<Fixed::rep>(frame * 101U), -static_cast<Fixed::rep>(frame * 37U)};
        VelocityMutation corrected = original;
        if (frame == 9U) corrected.delta_x += 777;
        rollback.set_input(frame, {.velocity = std::span<const VelocityMutation>(&original, 1U)});
        clean.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected, 1U)});
    }
    rollback.simulate_to(16U);
    VelocityMutation correction{static_cast<std::size_t>((9U * 2U) % bodies),
        static_cast<Fixed::rep>(9U * 101U + 777U), -static_cast<Fixed::rep>(9U * 37U)};
    rollback.correct_and_resimulate(9U, {.velocity = std::span<const VelocityMutation>(&correction, 1U)}, 16U);
    clean.simulate_to(16U);
    if (!rollback.equivalent_to(clean) || rollback.hash() != clean.hash()) {
        throw std::runtime_error("Atomic corrected rollback diverged from clean execution");
    }
}

void verify_tree_known() {
    std::vector<Fixed::rep> vx{30, 20, 10, 0, -10};
    std::vector<Fixed::rep> vy(vx.size());
    std::vector<std::uint32_t> masses{1, 2, 3, 4, 5};
    std::vector<DirectedTreeEdge> edges{{0,1},{0,2},{1,3},{1,4}};
    WeightedTreeScratch scratch(vx.size());
    const auto stats = project_weighted_tree_common_normal_inplace(
        vx, vy, masses, edges, kAxisX,
        {.maximum_active_set_iterations = 256U, .feasibility_tolerance_raw = 4U,
         .stationarity_tolerance_raw = 8U}, scratch);
    if (!stats.residuals.certified) throw std::runtime_error("Known weighted tree was not certified");
    for (const auto& edge : edges) if (vx[edge.parent] > vx[edge.child] + 4) {
        throw std::runtime_error("Weighted tree output is infeasible");
    }
}

std::uint64_t hash_values(std::span<const Fixed::rep> x, std::span<const Fixed::rep> y) {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (std::size_t i = 0; i < x.size(); ++i) {
        hash ^= static_cast<std::uint64_t>(x[i]); hash *= 0x100000001B3ULL;
        hash ^= static_cast<std::uint64_t>(y[i]); hash *= 0x100000001B3ULL;
    }
    return hash;
}
}

int main(int argc, char** argv) {
    try {
        const std::size_t iterations = argc > 1 ? static_cast<std::size_t>(std::stoull(argv[1])) : 2'000U;
        verify_atomic_rollback();
        verify_tree_known();
        std::mt19937_64 random(0x4E454F454E475631ULL ^ 0x15ULL);
        std::uint64_t aggregate = 0xCBF29CE484222325ULL;
        for (std::size_t scenario = 0U; scenario < iterations; ++scenario) {
            const std::size_t bodies = 2U + random() % 31U;
            std::vector<DirectedTreeEdge> edges;
            edges.reserve(bodies - 1U);
            for (std::size_t child = 1U; child < bodies; ++child) {
                edges.push_back({static_cast<std::size_t>(random() % child), child});
            }
            std::vector<Fixed::rep> vx(bodies), vy(bodies);
            std::vector<std::uint32_t> masses(bodies);
            for (std::size_t i = 0U; i < bodies; ++i) {
                vx[i] = static_cast<Fixed::rep>(random() % 20001U) - 10000;
                vy[i] = static_cast<Fixed::rep>(random() % 20001U) - 10000;
                masses[i] = 1U + static_cast<std::uint32_t>(random() % 16U);
            }
            auto vx2 = vx, vy2 = vy;
            WeightedTreeScratch scratch1(bodies), scratch2(bodies);
            const NormalQ30 normal = (scenario & 1U) == 0U ? kAxisX : kThreeFour;
            const auto first = project_weighted_tree_common_normal_inplace(
                vx, vy, masses, edges, normal,
                {.maximum_active_set_iterations = 2048U, .feasibility_tolerance_raw = 8U,
                 .stationarity_tolerance_raw = 32U}, scratch1);
            const auto second = project_weighted_tree_common_normal_inplace(
                vx2, vy2, masses, edges, normal,
                {.maximum_active_set_iterations = 2048U, .feasibility_tolerance_raw = 8U,
                 .stationarity_tolerance_raw = 32U}, scratch2);
            if (first.residuals.certified != second.residuals.certified || vx != vx2 || vy != vy2) {
                throw std::runtime_error("Weighted tree projection is not repeatable");
            }
            if (first.residuals.certified) {
                aggregate ^= hash_values(vx, vy) + 0x9E3779B97F4A7C15ULL + (aggregate << 6U) + (aggregate >> 2U);
            }
        }
        std::cout << "v0.15 fuzz passed: " << iterations << " scenarios, aggregate=0x"
                  << std::hex << std::uppercase << aggregate << std::dec << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v0.15 fuzz failure: " << error.what() << '\n';
        return 1;
    }
}
