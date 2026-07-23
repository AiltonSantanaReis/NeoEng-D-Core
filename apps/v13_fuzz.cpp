#include "neoeng/core/hash.hpp"
#include "neoeng/core/weighted_contact_projection.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace neoeng::core;

void add_contact(std::vector<SweptContact>& contacts, std::size_t first,
                 std::size_t second, ContactAxis axis) {
    if (second < first) std::swap(first, second);
    contacts.push_back(SweptContact{
        .first = first, .second = second, .axis = axis,
        .toi = {}, .initial_overlap = true, .final_overlap = true,
    });
}

WorldState make_world(std::span<const Fixed::rep> vx,
                      std::span<const Fixed::rep> vy) {
    if (vx.size() != vy.size()) throw std::invalid_argument("velocity dimensions differ");
    WorldState world;
    world.bodies.reserve(vx.size());
    for (std::size_t i = 0; i < vx.size(); ++i) {
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(i + 1U),
            .position = {},
            .velocity = {Fixed::from_raw(vx[i]), Fixed::from_raw(vy[i])},
        });
    }
    return world;
}

WideInteger objective(std::span<const Fixed::rep> input,
                      std::span<const Fixed::rep> output,
                      std::span<const std::uint32_t> masses) {
    WideInteger result = 0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const WideInteger delta = static_cast<WideInteger>(output[i]) - input[i];
        result += static_cast<WideInteger>(masses[i]) * delta * delta;
    }
    return result;
}

void enumerate_monotone(std::size_t index, Fixed::rep minimum,
                        Fixed::rep maximum, std::vector<Fixed::rep>& candidate,
                        std::span<const Fixed::rep> input,
                        std::span<const std::uint32_t> masses,
                        WideInteger& best) {
    if (index == candidate.size()) {
        best = std::min(best, objective(input, candidate, masses));
        return;
    }
    for (Fixed::rep value = minimum; value <= maximum; ++value) {
        candidate[index] = value;
        enumerate_monotone(index + 1U, value, maximum, candidate, input, masses, best);
    }
}

std::vector<Fixed::rep> velocity_axis(const ComponentWorldState& state, ContactAxis axis) {
    std::vector<Fixed::rep> values(state.body_count());
    for (std::size_t i = 0; i < state.body_count(); ++i) {
        values[i] = axis == ContactAxis::X
            ? state.velocity_x_at(i).raw() : state.velocity_y_at(i).raw();
    }
    return values;
}

std::uint64_t hash_component(const ComponentWorldState& state) {
    return stable_hash(state.materialize());
}

void verify_known_weighted_projection() {
    std::vector<Fixed::rep> vx{30, 20, 10};
    std::vector<Fixed::rep> vy(3U, 7);
    const auto component = make_component_world(make_world(vx, vy), 8U);
    std::vector<std::uint32_t> masses{1U, 2U, 1U};
    std::vector<SweptContact> contacts;
    add_contact(contacts, 0U, 1U, ContactAxis::X);
    add_contact(contacts, 1U, 2U, ContactAxis::X);
    add_contact(contacts, 0U, 2U, ContactAxis::X);
    WeightedProjectionScratch scratch(3U, contacts.size());
    const auto result = project_weighted_contact_velocities_2d(
        component, masses, contacts, WeightedProjectionMethod::CertifiedAuto,
        {.maximum_iterations = 256U, .certification_tolerance_raw = 4U}, scratch);
    if (velocity_axis(result.state, ContactAxis::X)
            != std::vector<Fixed::rep>({20, 20, 20})
        || velocity_axis(result.state, ContactAxis::Y) != vy
        || result.stats.total_order_reductions != 1U
        || !result.stats.residuals.certified) {
        throw std::runtime_error("Known weighted projection failed");
    }
}

void verify_integer_oracle(std::mt19937_64& random) {
    for (std::size_t scenario = 0; scenario < 240U; ++scenario) {
        const std::size_t count = 2U + random() % 4U;
        std::vector<Fixed::rep> input(count);
        std::vector<Fixed::rep> zero(count, 0);
        std::vector<std::uint32_t> masses(count);
        Fixed::rep minimum = 0;
        Fixed::rep maximum = 0;
        for (std::size_t i = 0; i < count; ++i) {
            input[i] = static_cast<Fixed::rep>(random() % 7U) - 3;
            masses[i] = 1U + static_cast<std::uint32_t>(random() % 4U);
            minimum = std::min(minimum, input[i]);
            maximum = std::max(maximum, input[i]);
        }
        std::vector<SweptContact> contacts;
        for (std::size_t i = 0; i + 1U < count; ++i) {
            add_contact(contacts, i, i + 1U, ContactAxis::X);
        }
        if (count > 2U) add_contact(contacts, 0U, count - 1U, ContactAxis::X);
        const auto component = make_component_world(make_world(input, zero), 8U);
        WeightedProjectionScratch scratch(count, contacts.size());
        const auto result = project_weighted_contact_velocities_2d(
            component, masses, contacts, WeightedProjectionMethod::CertifiedAuto,
            {.maximum_iterations = 512U, .certification_tolerance_raw = 16U}, scratch);
        const auto output = velocity_axis(result.state, ContactAxis::X);
        if (!std::is_sorted(output.begin(), output.end()) || !result.stats.residuals.certified) {
            throw std::runtime_error("Weighted PAV failed feasibility or certification");
        }
        std::vector<Fixed::rep> candidate(count);
        WideInteger best = static_cast<WideInteger>(1) << 120U;
        enumerate_monotone(0U, minimum - 1, maximum + 1, candidate, input, masses, best);
        if (objective(input, output, masses) != best) {
            throw std::runtime_error("Weighted PAV differs from integer oracle");
        }
    }
}


bool feasible_general(std::span<const Fixed::rep> values,
                      std::span<const SweptContact> contacts) {
    for (const SweptContact& contact : contacts) {
        if (values[contact.first] > values[contact.second]) return false;
    }
    return true;
}

void enumerate_feasible(std::size_t index, Fixed::rep minimum, Fixed::rep maximum,
                        std::vector<Fixed::rep>& candidate,
                        std::span<const Fixed::rep> input,
                        std::span<const std::uint32_t> masses,
                        std::span<const SweptContact> contacts,
                        WideInteger& best) {
    if (index == candidate.size()) {
        if (feasible_general(candidate, contacts)) {
            best = std::min(best, objective(input, candidate, masses));
        }
        return;
    }
    for (Fixed::rep value = minimum; value <= maximum; ++value) {
        candidate[index] = value;
        enumerate_feasible(index + 1U, minimum, maximum, candidate,
                           input, masses, contacts, best);
    }
}

void verify_weighted_star_oracle(std::mt19937_64& random) {
    for (std::size_t scenario = 0U; scenario < 160U; ++scenario) {
        const std::size_t count = 3U + random() % 3U;
        const bool outward = (random() & 1U) == 0U;
        const std::size_t center = outward ? 0U : count - 1U;
        std::vector<Fixed::rep> input(count), zero(count, 0);
        std::vector<std::uint32_t> masses(count);
        Fixed::rep minimum = 0;
        Fixed::rep maximum = 0;
        for (std::size_t i = 0U; i < count; ++i) {
            input[i] = static_cast<Fixed::rep>(random() % 7U) - 3;
            masses[i] = 1U + static_cast<std::uint32_t>(random() % 4U);
            minimum = std::min(minimum, input[i]);
            maximum = std::max(maximum, input[i]);
        }
        std::vector<SweptContact> contacts;
        for (std::size_t body = 0U; body < count; ++body) {
            if (body == center) continue;
            add_contact(contacts, center, body, ContactAxis::X);
        }
        const auto component = make_component_world(make_world(input, zero), 8U);
        WeightedProjectionScratch scratch(count, contacts.size());
        const auto result = project_weighted_contact_velocities_2d(
            component, masses, contacts, WeightedProjectionMethod::CertifiedAuto,
            {.maximum_iterations = 512U, .certification_tolerance_raw = 32U}, scratch);
        const auto output = velocity_axis(result.state, ContactAxis::X);
        if (!feasible_general(output, contacts) || !result.stats.residuals.certified
            || result.stats.star_reductions != 1U) {
            std::cerr << "star scenario=" << scenario << " outward=" << outward
                      << " center=" << center
                      << " primal=" << result.stats.residuals.primal_linf_raw
                      << " dual=" << result.stats.residuals.dual_linf_weighted_raw
                      << " stat=" << result.stats.residuals.stationarity_linf_weighted_raw
                      << " comp=" << result.stats.residuals.complementarity_linf_scaled_raw
                      << " quant=" << result.stats.residuals.quantization_linf_weighted_raw
                      << " reductions=" << result.stats.star_reductions << " input=";
            for (auto v : input) std::cerr << v << ':';
            std::cerr << " masses=";
            for (auto m : masses) std::cerr << m << ':';
            std::cerr << " output=";
            for (auto v : output) std::cerr << v << ':';
            std::cerr << '\n';
            throw std::runtime_error("Weighted star reduction failed certification");
        }
        std::vector<Fixed::rep> candidate(count);
        WideInteger best = static_cast<WideInteger>(1) << 120U;
        enumerate_feasible(0U, minimum - 1, maximum + 1, candidate,
                           input, masses, contacts, best);
        if (objective(input, output, masses) != best) {
            throw std::runtime_error("Weighted star reduction differs from integer oracle");
        }
    }
}

void verify_axis_independence() {
    std::vector<Fixed::rep> vx{9, 1, 5, 2};
    std::vector<Fixed::rep> vy{0, 8, 1, 7};
    const auto component = make_component_world(make_world(vx, vy), 8U);
    std::vector<std::uint32_t> masses{1, 2, 3, 4};
    std::vector<SweptContact> contacts;
    add_contact(contacts, 0U, 1U, ContactAxis::X);
    add_contact(contacts, 2U, 3U, ContactAxis::Y);
    WeightedProjectionScratch scratch(4U, contacts.size());
    const auto result = project_weighted_contact_velocities_2d(
        component, masses, contacts, WeightedProjectionMethod::CertifiedAuto,
        {.maximum_iterations = 128U, .certification_tolerance_raw = 8U}, scratch);
    const auto out_x = velocity_axis(result.state, ContactAxis::X);
    const auto out_y = velocity_axis(result.state, ContactAxis::Y);
    if (out_x[2] != vx[2] || out_x[3] != vx[3]
        || out_y[0] != vy[0] || out_y[1] != vy[1]
        || out_x[0] > out_x[1] || out_y[2] > out_y[3]) {
        throw std::runtime_error("Axis independence failed");
    }
}

void verify_fallback_is_honest() {
    std::vector<Fixed::rep> vx{10, 9, 8, 7};
    std::vector<Fixed::rep> vy(4U, 0);
    const auto component = make_component_world(make_world(vx, vy), 8U);
    std::vector<std::uint32_t> masses(4U, 1U);
    std::vector<SweptContact> contacts;
    add_contact(contacts, 0U, 2U, ContactAxis::X);
    add_contact(contacts, 0U, 3U, ContactAxis::X);
    add_contact(contacts, 1U, 3U, ContactAxis::X);
    WeightedProjectionScratch scratch(4U, contacts.size());
    const auto result = project_weighted_contact_velocities_2d(
        component, masses, contacts, WeightedProjectionMethod::CertifiedAuto,
        {.maximum_iterations = 1'024U, .certification_tolerance_raw = 8U}, scratch);
    const auto output = velocity_axis(result.state, ContactAxis::X);
    for (const auto& contact : contacts) {
        if (output[contact.first] > output[contact.second]) {
            throw std::runtime_error("Quantized Dykstra fallback remained infeasible");
        }
    }
    if (result.stats.iterative_fallbacks != 1U || result.stats.residuals.certified) {
        throw std::runtime_error("Fallback was incorrectly represented as certified");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::size_t iterations = argc > 1
            ? static_cast<std::size_t>(std::stoull(argv[1])) : 5'000U;
        verify_known_weighted_projection();
        std::mt19937_64 random(0x4E454F454E475631ULL ^ 0x13ULL);
        verify_integer_oracle(random);
        verify_weighted_star_oracle(random);
        verify_axis_independence();
        verify_fallback_is_honest();

        std::uint64_t aggregate = 0xCBF29CE484222325ULL;
        for (std::size_t scenario = 0; scenario < iterations; ++scenario) {
            const std::size_t bodies = 2U + random() % 126U;
            std::vector<Fixed::rep> vx(bodies), vy(bodies);
            std::vector<std::uint32_t> masses(bodies);
            for (std::size_t i = 0; i < bodies; ++i) {
                vx[i] = static_cast<Fixed::rep>(random() % 2'000'001ULL) - 1'000'000;
                vy[i] = static_cast<Fixed::rep>(random() % 2'000'001ULL) - 1'000'000;
                masses[i] = 1U + static_cast<std::uint32_t>(random() % 32U);
            }
            std::vector<SweptContact> contacts;
            const std::size_t split = 1U + random() % (bodies - 1U);
            for (std::size_t i = 0; i + 1U < split; ++i) {
                add_contact(contacts, i, i + 1U, ContactAxis::X);
            }
            for (std::size_t i = split; i + 1U < bodies; ++i) {
                add_contact(contacts, i, i + 1U, ContactAxis::Y);
            }
            if (split > 2U) add_contact(contacts, 0U, split - 1U, ContactAxis::X);
            if (bodies - split > 2U) add_contact(contacts, split, bodies - 1U, ContactAxis::Y);
            const auto component = make_component_world(make_world(vx, vy), 32U);
            WeightedProjectionScratch scratch(bodies, std::max<std::size_t>(1U, contacts.size()));
            const auto first = project_weighted_contact_velocities_2d(
                component, masses, contacts, WeightedProjectionMethod::CertifiedAuto,
                {.maximum_iterations = 2'048U, .certification_tolerance_raw = 4'096U}, scratch);
            std::shuffle(contacts.begin(), contacts.end(), random);
            const auto second = project_weighted_contact_velocities_2d(
                component, masses, contacts, WeightedProjectionMethod::CertifiedAuto,
                {.maximum_iterations = 2'048U, .certification_tolerance_raw = 4'096U}, scratch);
            if (first.state.materialize() != second.state.materialize()) {
                throw std::runtime_error("Contact order changed weighted projection");
            }
            aggregate ^= hash_component(first.state) + 0x9E3779B97F4A7C15ULL
                + (aggregate << 6U) + (aggregate >> 2U);
        }
        std::cout << "v0.13 weighted fuzz passed: " << iterations
                  << " scenarios, aggregate=0x" << std::hex << std::uppercase
                  << aggregate << std::dec << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v0.13 fuzz failure: " << error.what() << '\n';
        return 1;
    }
}
