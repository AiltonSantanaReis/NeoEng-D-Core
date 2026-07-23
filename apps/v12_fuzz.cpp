#include "neoeng/core/general_lcp_solver.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace neoeng::core;

void add_edge(std::vector<SweptContact>& contacts, std::size_t first, std::size_t second) {
    if (second < first) std::swap(first, second);
    contacts.push_back(SweptContact{
        .first = first, .second = second, .axis = ContactAxis::X,
        .toi = {}, .initial_overlap = true, .final_overlap = true,
    });
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

std::uint64_t hash_values(std::span<const Fixed::rep> values) {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (Fixed::rep value : values) hash = mix(hash, static_cast<std::uint64_t>(value));
    return hash;
}

WideInteger objective(
    std::span<const Fixed::rep> input,
    std::span<const Fixed::rep> output) {
    WideInteger sum = 0;
    for (std::size_t index = 0U; index < input.size(); ++index) {
        const WideInteger delta = static_cast<WideInteger>(output[index]) - input[index];
        sum += delta * delta;
    }
    return sum;
}

bool feasible(
    std::span<const Fixed::rep> values,
    std::span<const SweptContact> contacts) {
    for (const SweptContact& contact : contacts) {
        const std::size_t first = std::min(contact.first, contact.second);
        const std::size_t second = std::max(contact.first, contact.second);
        if (values[first] > values[second]) return false;
    }
    return true;
}

WideInteger brute_force_integer_objective(
    std::span<const Fixed::rep> input,
    std::span<const SweptContact> contacts) {
    Fixed::rep minimum = *std::min_element(input.begin(), input.end());
    Fixed::rep maximum = *std::max_element(input.begin(), input.end());
    std::vector<Fixed::rep> candidate(input.size(), minimum);
    WideInteger best = -1;
    for (;;) {
        if (feasible(candidate, contacts)) {
            const WideInteger value = objective(input, candidate);
            if (best < 0 || value < best) best = value;
        }
        std::size_t digit = 0U;
        while (digit < candidate.size()) {
            if (candidate[digit] < maximum) {
                ++candidate[digit];
                break;
            }
            candidate[digit] = minimum;
            ++digit;
        }
        if (digit == candidate.size()) break;
    }
    return best;
}

void verify_known_projection() {
    std::vector<SweptContact> contacts;
    add_edge(contacts, 0U, 1U);
    add_edge(contacts, 1U, 2U);
    add_edge(contacts, 0U, 2U);
    ContactIslandWorkspace workspace(3U, contacts.size());
    workspace.classify(3U, contacts);
    for (GeneralProjectionMethod method : {
             GeneralProjectionMethod::DykstraCoordinate,
             GeneralProjectionMethod::ActiveSetCoordinate,
             GeneralProjectionMethod::ProjectedConjugateGradient,
             GeneralProjectionMethod::CertifiedAuto}) {
        GeneralProjectionScratch scratch(3U, contacts.size());
        GeneralProjectionWarmStart warm(contacts.size());
        std::vector<Fixed::rep> values{30, 20, 10};
        const auto first = project_general_contact_islands(
            values, contacts, workspace, method,
            {.maximum_iterations = 256U, .certification_tolerance_raw = 1U,
             .pcg_restart_interval = 8U}, scratch, &warm);
        if (values != std::vector<Fixed::rep>({20, 20, 20}) || !first.residuals.certified) {
            throw std::runtime_error("Known general projection failed");
        }
        std::vector<Fixed::rep> repeated{30, 20, 10};
        const auto second = project_general_contact_islands(
            repeated, contacts, workspace, method,
            {.maximum_iterations = 256U, .certification_tolerance_raw = 1U,
             .pcg_restart_interval = 8U}, scratch, &warm);
        if (repeated != values || second.warm_attempts != 1U
            || second.warm_exact_accepts != 1U || second.warm_rejects != 0U) {
            throw std::runtime_error("Certified warm projection is not bit-identical");
        }
    }
}

void verify_small_integer_oracle(std::mt19937_64& random) {
    for (std::size_t scenario = 0U; scenario < 160U; ++scenario) {
        const std::size_t bodies = 2U + random() % 4U;
        std::set<std::pair<std::size_t, std::size_t>> unique;
        for (std::size_t body = 0U; body + 1U < bodies; ++body) {
            unique.emplace(body, body + 1U);
        }
        const std::size_t maximum_edges = bodies * (bodies - 1U) / 2U;
        const std::size_t requested = std::min<std::size_t>(
            maximum_edges, bodies - 1U + random() % (bodies + 1U));
        while (unique.size() < requested) {
            std::size_t first = random() % bodies;
            std::size_t second = random() % bodies;
            if (first == second) continue;
            if (second < first) std::swap(first, second);
            unique.emplace(first, second);
        }
        std::vector<SweptContact> contacts;
        for (const auto& [first, second] : unique) add_edge(contacts, first, second);
        ContactIslandWorkspace workspace(bodies, contacts.size());
        workspace.classify(bodies, contacts);
        GeneralProjectionScratch scratch(bodies, contacts.size());
        std::vector<Fixed::rep> input(bodies);
        for (Fixed::rep& value : input) value = static_cast<Fixed::rep>(random() % 5U) - 2;
        const WideInteger oracle = brute_force_integer_objective(input, contacts);
        for (GeneralProjectionMethod method : {
                 GeneralProjectionMethod::DykstraCoordinate,
                 GeneralProjectionMethod::ActiveSetCoordinate,
                 GeneralProjectionMethod::CertifiedAuto}) {
            std::vector<Fixed::rep> output = input;
            const auto stats = project_general_contact_islands(
                output, contacts, workspace, method,
                {.maximum_iterations = 2'048U, .certification_tolerance_raw = 1U,
                 .pcg_restart_interval = 8U}, scratch, nullptr);
            if (!feasible(output, contacts) || !stats.residuals.certified) {
                throw std::runtime_error("Coordinate solver failed integer oracle feasibility");
            }
            const WideInteger measured = objective(input, output);
            const WideInteger rounding_allowance = static_cast<WideInteger>(bodies);
            if (measured < oracle || measured - oracle > rounding_allowance) {
                std::cerr << "quant bound scenario=" << scenario << " method=" << to_string(method)
                          << " bodies=" << bodies << " measured=" << static_cast<long long>(measured)
                          << " oracle=" << static_cast<long long>(oracle) << " input=";
                for (auto v : input) std::cerr << v << ':';
                std::cerr << " output=";
                for (auto v : output) std::cerr << v << ':';
                std::cerr << " edges=";
                for (const auto& c : contacts) std::cerr << c.first << '-' << c.second << ':';
                std::cerr << '\n';
                throw std::runtime_error("Coordinate solver exceeded quantization objective bound");
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::size_t iterations = argc > 1
            ? static_cast<std::size_t>(std::stoull(argv[1])) : 5'000U;
        verify_known_projection();
        std::mt19937_64 random(0x4E454F454E475631ULL ^ 0x12ULL);
        verify_small_integer_oracle(random);

        ContactIslandWorkspace workspace(128U, 512U);
        GeneralProjectionScratch scratch(128U, 512U);
        GeneralProjectionWarmStart warm(512U);
        std::uint64_t aggregate = 0xCBF29CE484222325ULL;
        std::uint64_t pcg_certified = 0U;
        std::uint64_t active_matches = 0U;
        std::uint64_t auto_matches = 0U;
        std::uint64_t auto_reductions = 0U;

        for (std::size_t scenario = 0U; scenario < iterations; ++scenario) {
            const std::size_t body_count = 2U + random() % 127U;
            const std::size_t maximum_edges = body_count * (body_count - 1U) / 2U;
            const std::size_t requested_edges = std::min<std::size_t>(
                maximum_edges, 1U + random() % (std::min<std::size_t>(512U, body_count * 4U)));
            std::set<std::pair<std::size_t, std::size_t>> unique;
            while (unique.size() < requested_edges) {
                std::size_t first = random() % body_count;
                std::size_t second = random() % body_count;
                if (first == second) continue;
                if (second < first) std::swap(first, second);
                unique.emplace(first, second);
            }
            std::vector<SweptContact> contacts;
            contacts.reserve(unique.size());
            for (const auto& [first, second] : unique) {
                contacts.push_back(SweptContact{
                    .first = first, .second = second,
                    .axis = (random() & 1U) == 0U ? ContactAxis::X : ContactAxis::Y,
                    .toi = {}, .initial_overlap = true, .final_overlap = true,
                });
            }
            std::shuffle(contacts.begin(), contacts.end(), random);
            workspace.classify(body_count, contacts);
            std::vector<Fixed::rep> input(body_count);
            for (Fixed::rep& value : input) {
                value = static_cast<std::int64_t>(random() % 2'000'001ULL) - 1'000'000;
            }

            std::vector<Fixed::rep> dykstra = input;
            warm.clear();
            const auto dykstra_stats = project_general_contact_islands(
                dykstra, contacts, workspace,
                GeneralProjectionMethod::DykstraCoordinate,
                {.maximum_iterations = 4'096U, .certification_tolerance_raw = 1U,
                 .pcg_restart_interval = 16U}, scratch, &warm);
            if (!dykstra_stats.residuals.certified || !feasible(dykstra, contacts)) {
                std::cerr << "dykstra uncert scenario=" << scenario
                          << " bodies=" << body_count << " contacts=" << contacts.size()
                          << " iter=" << dykstra_stats.iterations
                          << " p=" << dykstra_stats.residuals.primal_linf_raw
                          << " stat=" << dykstra_stats.residuals.stationarity_linf_raw
                          << " comp=" << dykstra_stats.residuals.complementarity_linf_scaled_raw
                          << " proj=" << dykstra_stats.residuals.projected_dual_linf_raw
                          << " quant=" << dykstra_stats.residuals.quantization_linf_raw
                          << " feasible=" << feasible(dykstra, contacts) << '\n';
                throw std::runtime_error("Dykstra failed certification in random scenario");
            }
            std::vector<Fixed::rep> warm_repeat = input;
            const auto warm_stats = project_general_contact_islands(
                warm_repeat, contacts, workspace,
                GeneralProjectionMethod::DykstraCoordinate,
                {.maximum_iterations = 4'096U, .certification_tolerance_raw = 1U,
                 .pcg_restart_interval = 16U}, scratch, &warm);
            if (warm_repeat != dykstra || warm_stats.warm_exact_accepts != 1U) {
                throw std::runtime_error("Cold/warm equality gate failed");
            }

            std::vector<Fixed::rep> active = input;
            const auto active_stats = project_general_contact_islands(
                active, contacts, workspace,
                GeneralProjectionMethod::ActiveSetCoordinate,
                {.maximum_iterations = 4'096U, .certification_tolerance_raw = 1U,
                 .pcg_restart_interval = 16U}, scratch, nullptr);
            if (!active_stats.residuals.certified || !feasible(active, contacts)) {
                throw std::runtime_error("Active-set coordinate failed certification");
            }
            active_matches += active == dykstra ? 1U : 0U;

            std::vector<Fixed::rep> automatic = input;
            const auto automatic_stats = project_general_contact_islands(
                automatic, contacts, workspace,
                GeneralProjectionMethod::CertifiedAuto,
                {.maximum_iterations = 4'096U, .certification_tolerance_raw = 1U,
                 .pcg_restart_interval = 16U}, scratch, nullptr);
            if (!automatic_stats.residuals.certified || !feasible(automatic, contacts)) {
                throw std::runtime_error("Certified-auto failed certification");
            }
            auto_matches += automatic == dykstra ? 1U : 0U;
            auto_reductions += automatic_stats.total_order_reductions;

            std::vector<Fixed::rep> pcg = input;
            const auto pcg_stats = project_general_contact_islands(
                pcg, contacts, workspace,
                GeneralProjectionMethod::ProjectedConjugateGradient,
                {.maximum_iterations = 4'096U, .certification_tolerance_raw = 1U,
                 .pcg_restart_interval = 8U}, scratch, nullptr);
            if (pcg_stats.residuals.certified) {
                ++pcg_certified;
                if (!feasible(pcg, contacts)) {
                    throw std::runtime_error("Certified projected-CG output is infeasible");
                }
            }

            aggregate = mix(aggregate, hash_values(dykstra));
            aggregate = mix(aggregate, hash_values(active));
            aggregate = mix(aggregate, hash_values(pcg));
            aggregate = mix(aggregate, hash_values(automatic));
            aggregate = mix(aggregate, dykstra_stats.iterations);
            aggregate = mix(aggregate, active_stats.active_edges_peak);
            aggregate = mix(aggregate, pcg_stats.residuals.projected_dual_linf_raw);
        }

        std::cout << "v0.12 fuzz scenarios=" << iterations
                  << " aggregate=0x" << std::hex << std::uppercase << aggregate
                  << std::dec << " active_matches=" << active_matches
                  << " auto_matches=" << auto_matches
                  << " auto_reductions=" << auto_reductions
                  << " pcg_certified=" << pcg_certified
                  << " scratch_bytes=" << scratch.reserved_bytes() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v0.12 fuzz failed: " << error.what() << '\n';
        return 1;
    }
}
