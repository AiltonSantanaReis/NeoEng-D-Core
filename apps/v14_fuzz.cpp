#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/hash.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace neoeng::core;
constexpr std::int32_t kOne = 1 << 30;
constexpr NormalQ30 kAxisX{kOne, 0};
constexpr NormalQ30 kAxisY{0, kOne};
constexpr NormalQ30 kThreeFour{644'245'094, 858'993'459};
constexpr NormalQ30 kDiagonal{759'250'125, 759'250'125};
constexpr NormalQ30 kNormals[]{kAxisX, kAxisY, kThreeFour, kDiagonal};

WorldState make_world(std::span<const Fixed::rep> vx, std::span<const Fixed::rep> vy) {
    WorldState world;
    world.bodies.reserve(vx.size());
    for (std::size_t i = 0; i < vx.size(); ++i) {
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(i + 1U), .position = {},
            .velocity = {Fixed::from_raw(vx[i]), Fixed::from_raw(vy[i])},
        });
    }
    return world;
}

std::uint64_t hash_component(const ComponentWorldState& state) {
    return stable_hash(state.materialize());
}

Fixed::rep dot_raw(Fixed::rep vx, Fixed::rep vy, NormalQ30 normal) {
    const WideInteger numerator = static_cast<WideInteger>(vx) * normal.x
                                + static_cast<WideInteger>(vy) * normal.y;
    WideInteger q = numerator / (static_cast<WideInteger>(1) << 30U);
    WideInteger r = numerator % (static_cast<WideInteger>(1) << 30U);
    if (r < 0) { r += static_cast<WideInteger>(1) << 30U; --q; }
    if (r * 2 > (static_cast<WideInteger>(1) << 30U)) ++q;
    return static_cast<Fixed::rep>(q);
}

void verify_known_diagonal() {
    std::vector<Fixed::rep> vx{20, -10};
    std::vector<Fixed::rep> vy{15, -5};
    std::vector<std::uint32_t> masses{1U, 3U};
    std::vector<NormalContact> contacts{{0U, 1U, kThreeFour}};
    auto component = make_component_world(make_world(vx, vy), 8U);
    ArbitraryNormalScratch scratch(2U, 1U);
    const auto result = project_arbitrary_normal_contacts_2d(
        component, masses, contacts,
        {.maximum_iterations = 16U, .feasibility_tolerance_raw = 4U}, scratch);
    const Fixed::rep first = dot_raw(result.state.velocity_x_at(0).raw(),
                                     result.state.velocity_y_at(0).raw(), kThreeFour);
    const Fixed::rep second = dot_raw(result.state.velocity_x_at(1).raw(),
                                      result.state.velocity_y_at(1).raw(), kThreeFour);
    if (first > second + 4 || !result.stats.residuals.certified
        || result.stats.method != ArbitraryNormalMethod::CertifiedMatching) {
        throw std::runtime_error("Known diagonal matching projection failed");
    }
}

void verify_axis_equivalence() {
    std::vector<Fixed::rep> vx{30, -10};
    std::vector<Fixed::rep> vy{7, 9};
    std::vector<std::uint32_t> masses{1U, 3U};
    std::vector<NormalContact> contacts{{0U, 1U, kAxisX}};
    auto component = make_component_world(make_world(vx, vy), 8U);
    ArbitraryNormalScratch scratch(2U, 1U);
    const auto result = project_arbitrary_normal_contacts_2d(
        component, masses, contacts, {}, scratch);
    if (result.state.velocity_x_at(0).raw() != 0
        || result.state.velocity_x_at(1).raw() != 0
        || result.state.velocity_y_at(0).raw() != vy[0]
        || result.state.velocity_y_at(1).raw() != vy[1]) {
        throw std::runtime_error("Axis normal failed weighted two-body projection");
    }
}

void verify_fallback_is_honest() {
    std::vector<Fixed::rep> vx{20, 10, -10};
    std::vector<Fixed::rep> vy{10, 0, -20};
    std::vector<std::uint32_t> masses{1U, 2U, 3U};
    std::vector<NormalContact> contacts{{0U, 1U, kAxisX}, {1U, 2U, kDiagonal}};
    auto component = make_component_world(make_world(vx, vy), 8U);
    ArbitraryNormalScratch scratch(3U, 2U);
    const auto result = project_arbitrary_normal_contacts_2d(
        component, masses, contacts,
        {.maximum_iterations = 128U, .feasibility_tolerance_raw = 8U}, scratch);
    if (result.stats.method != ArbitraryNormalMethod::CoordinateFallback
        || result.stats.residuals.certified || !result.stats.residuals.feasible) {
        throw std::runtime_error("Connected arbitrary-normal fallback was misrepresented");
    }
}

void verify_aux_history() {
    PhysicsAuxHistory history(16U);
    auto masses = std::make_shared<const std::vector<std::uint32_t>>(
        std::vector<std::uint32_t>{1U, 2U});
    for (std::uint64_t frame = 0U; frame < 10U; ++frame) {
        auto dual = std::make_shared<const std::vector<Fixed::rep>>(
            std::vector<Fixed::rep>{static_cast<Fixed::rep>(frame * 3U)});
        auto manifold = std::make_shared<const std::vector<NormalContact>>(
            std::vector<NormalContact>{{0U, 1U, (frame & 1U) == 0U ? kAxisX : kDiagonal}});
        history.capture({frame, masses, dual, manifold});
    }
    const std::uint64_t preserved = hash_physics_aux(history.at(5U));
    history.truncate_after(5U);
    for (std::uint64_t frame = 6U; frame < 10U; ++frame) {
        auto dual = std::make_shared<const std::vector<Fixed::rep>>(
            std::vector<Fixed::rep>{-static_cast<Fixed::rep>(frame * 5U)});
        auto manifold = std::make_shared<const std::vector<NormalContact>>(
            std::vector<NormalContact>{{0U, 1U, kThreeFour}});
        history.capture({frame, masses, dual, manifold});
    }
    if (hash_physics_aux(history.at(5U)) != preserved || history.size() != 10U
        || history.at(9U).dual_impulses->front() != -45) {
        throw std::runtime_error("Auxiliary rollback history failed branch reconstruction");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::size_t iterations = argc > 1
            ? static_cast<std::size_t>(std::stoull(argv[1])) : 5'000U;
        verify_known_diagonal();
        verify_axis_equivalence();
        verify_fallback_is_honest();
        verify_aux_history();

        std::mt19937_64 random(0x4E454F454E475631ULL ^ 0x14ULL);
        std::uint64_t aggregate = 0xCBF29CE484222325ULL;
        for (std::size_t scenario = 0U; scenario < iterations; ++scenario) {
            const std::size_t pairs = 1U + random() % 64U;
            const std::size_t bodies = pairs * 2U;
            std::vector<Fixed::rep> vx(bodies), vy(bodies);
            std::vector<std::uint32_t> masses(bodies);
            std::vector<NormalContact> contacts;
            contacts.reserve(pairs);
            for (std::size_t body = 0U; body < bodies; ++body) {
                vx[body] = static_cast<Fixed::rep>(random() % 2'000'001ULL) - 1'000'000;
                vy[body] = static_cast<Fixed::rep>(random() % 2'000'001ULL) - 1'000'000;
                masses[body] = 1U + static_cast<std::uint32_t>(random() % 64U);
            }
            for (std::size_t pair = 0U; pair < pairs; ++pair) {
                contacts.push_back({2U * pair, 2U * pair + 1U, kNormals[random() % 4U]});
            }
            const auto component = make_component_world(make_world(vx, vy), 32U);
            ArbitraryNormalScratch scratch(bodies, pairs);
            const auto first = project_arbitrary_normal_contacts_2d(
                component, masses, contacts,
                {.maximum_iterations = 16U, .feasibility_tolerance_raw = 16U}, scratch);
            std::shuffle(contacts.begin(), contacts.end(), random);
            const auto second = project_arbitrary_normal_contacts_2d(
                component, masses, contacts,
                {.maximum_iterations = 16U, .feasibility_tolerance_raw = 16U}, scratch);
            if (first.state.materialize() != second.state.materialize()
                || !first.stats.residuals.certified) {
                throw std::runtime_error("Matching projection lost order invariance or certificate");
            }
            aggregate ^= hash_component(first.state) + 0x9E3779B97F4A7C15ULL
                + (aggregate << 6U) + (aggregate >> 2U);
        }
        std::cout << "v0.14 arbitrary-normal fuzz passed: " << iterations
                  << " scenarios, aggregate=0x" << std::hex << std::uppercase
                  << aggregate << std::dec << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v0.14 fuzz failure: " << error.what() << '\n';
        return 1;
    }
}
