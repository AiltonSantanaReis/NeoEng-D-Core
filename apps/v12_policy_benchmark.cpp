#include "neoeng/core/island_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {
using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

struct Scenario final {
    std::string name;
    std::size_t islands{};
    std::size_t bodies_per_island{};
    std::size_t bodies{};
    std::size_t contacts_count{};
    std::vector<SweptContact> contacts;
    std::vector<Fixed::rep> values;
    double serial_ms{};
    double pool8_ms{};
    std::uint64_t hash{};
};

void add_edge(std::vector<SweptContact>& contacts, std::size_t first, std::size_t second) {
    contacts.push_back(SweptContact{
        .first = std::min(first, second), .second = std::max(first, second),
        .axis = ContactAxis::X, .toi = {},
        .initial_overlap = true, .final_overlap = true,
    });
}

Scenario make_scenario(std::size_t islands, std::size_t bodies_per_island, std::uint64_t seed) {
    Scenario scenario;
    scenario.name = std::to_string(islands) + "x" + std::to_string(bodies_per_island);
    scenario.islands = islands;
    scenario.bodies_per_island = bodies_per_island;
    scenario.bodies = islands * bodies_per_island;
    for (std::size_t island = 0U; island < islands; ++island) {
        const std::size_t base = island * bodies_per_island;
        for (std::size_t body = 0U; body + 1U < bodies_per_island; ++body) {
            add_edge(scenario.contacts, base + body, base + body + 1U);
        }
    }
    scenario.contacts_count = scenario.contacts.size();
    scenario.values.resize(scenario.bodies);
    std::mt19937_64 random(seed);
    for (Fixed::rep& value : scenario.values) {
        value = static_cast<std::int64_t>(random() % 2'000'001ULL) - 1'000'000;
    }
    return scenario;
}

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2U];
}

std::uint64_t hash_values(std::span<const Fixed::rep> values) {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (Fixed::rep value : values) {
        hash ^= static_cast<std::uint64_t>(value) + 0x9E3779B97F4A7C15ULL
            + (hash << 6U) + (hash >> 2U);
    }
    return hash;
}

struct Policy final {
    std::size_t work_threshold{};
    std::size_t island_threshold{};
};

bool choose_pool(const Policy& policy, const Scenario& scenario) {
    const std::size_t work = scenario.contacts_count * 4U;
    return work >= policy.work_threshold && scenario.islands >= policy.island_threshold;
}

double selected_cost(const Policy& policy, const Scenario& scenario) {
    return choose_pool(policy, scenario) ? scenario.pool8_ms : scenario.serial_ms;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "v0.12-policy";
        std::filesystem::create_directories(output);
        std::vector<Scenario> scenarios;
        for (std::size_t islands : {1U, 4U, 16U, 64U, 256U, 1'024U}) {
            for (std::size_t bodies_per_island : {2U, 8U, 32U}) {
                scenarios.push_back(make_scenario(
                    islands, bodies_per_island,
                    0x120000ULL + islands * 131U + bodies_per_island));
            }
        }
        const std::size_t maximum_bodies = std::max_element(
            scenarios.begin(), scenarios.end(),
            [](const Scenario& lhs, const Scenario& rhs) { return lhs.bodies < rhs.bodies; })->bodies;
        const std::size_t maximum_contacts = std::max_element(
            scenarios.begin(), scenarios.end(),
            [](const Scenario& lhs, const Scenario& rhs) {
                return lhs.contacts_count < rhs.contacts_count;
            })->contacts_count;
        ContactIslandWorkspace workspace(maximum_bodies, maximum_contacts);
        PersistentIslandWorkerPool pool(8U);

        for (Scenario& scenario : scenarios) {
            workspace.classify(scenario.bodies, scenario.contacts);
            std::vector<double> serial_times;
            std::vector<double> pool_times;
            std::uint64_t serial_hash = 0U;
            std::uint64_t pool_hash = 0U;
            for (std::size_t trial = 0U; trial < 12U; ++trial) {
                std::vector<Fixed::rep> serial = scenario.values;
                const auto serial_begin = Clock::now();
                (void)project_contact_islands_monotone(
                    serial, scenario.contacts, workspace, 4U, 1U);
                const auto serial_end = Clock::now();
                serial_times.push_back(
                    std::chrono::duration<double, std::milli>(serial_end - serial_begin).count());
                serial_hash = hash_values(serial);

                std::vector<Fixed::rep> pooled = scenario.values;
                const auto pool_begin = Clock::now();
                (void)project_contact_islands_monotone_pooled(
                    pooled, scenario.contacts, workspace, 4U, pool, 8U);
                const auto pool_end = Clock::now();
                pool_times.push_back(
                    std::chrono::duration<double, std::milli>(pool_end - pool_begin).count());
                pool_hash = hash_values(pooled);
            }
            if (serial_hash != pool_hash) {
                throw std::runtime_error("Serial and pooled policy candidates diverged");
            }
            scenario.serial_ms = median(serial_times);
            scenario.pool8_ms = median(pool_times);
            scenario.hash = serial_hash;
        }

        std::vector<std::size_t> work_candidates{0U, std::numeric_limits<std::size_t>::max()};
        for (const Scenario& scenario : scenarios) {
            work_candidates.push_back(scenario.contacts_count * 4U);
        }
        std::sort(work_candidates.begin(), work_candidates.end());
        work_candidates.erase(
            std::unique(work_candidates.begin(), work_candidates.end()), work_candidates.end());

        Policy best;
        double best_calibration = std::numeric_limits<double>::infinity();
        for (std::size_t work_threshold : work_candidates) {
            for (std::size_t island_threshold : {1U, 2U, 4U, 8U, 16U, 64U, 256U}) {
                const Policy candidate{work_threshold, island_threshold};
                double total = 0.0;
                for (std::size_t index = 0U; index < scenarios.size(); index += 2U) {
                    total += selected_cost(candidate, scenarios[index]);
                }
                const bool strictly_better = total < best_calibration - 1e-12;
                const bool conservative_tie = std::abs(total - best_calibration) <= 1e-12
                    && (candidate.work_threshold > best.work_threshold
                        || (candidate.work_threshold == best.work_threshold
                            && candidate.island_threshold > best.island_threshold));
                if (strictly_better || conservative_tie) {
                    best_calibration = total;
                    best = candidate;
                }
            }
        }

        double selected_total = 0.0;
        double oracle_total = 0.0;
        std::ofstream csv(output / "policy_evaluation.csv");
        csv << "split,name,islands,bodies_per_island,contacts,work,serial_ms,pool8_ms,"
               "oracle_ms,selected_mode,selected_ms,regret_ms,hash\n";
        csv << std::fixed << std::setprecision(6);
        for (std::size_t index = 0U; index < scenarios.size(); ++index) {
            const Scenario& scenario = scenarios[index];
            const bool evaluation = (index % 2U) == 1U;
            const double oracle = std::min(scenario.serial_ms, scenario.pool8_ms);
            const double selected = selected_cost(best, scenario);
            if (evaluation) {
                selected_total += selected;
                oracle_total += oracle;
            }
            csv << (evaluation ? "evaluation" : "calibration") << ',' << scenario.name << ','
                << scenario.islands << ',' << scenario.bodies_per_island << ','
                << scenario.contacts_count << ',' << scenario.contacts_count * 4U << ','
                << scenario.serial_ms << ',' << scenario.pool8_ms << ',' << oracle << ','
                << (choose_pool(best, scenario) ? "pool8" : "serial") << ',' << selected << ','
                << selected - oracle << ",0x" << std::hex << std::uppercase << scenario.hash
                << std::dec << '\n';
        }
        const double regret_percent = oracle_total == 0.0
            ? 0.0 : (selected_total - oracle_total) / oracle_total * 100.0;
        std::ofstream summary(output / "policy_summary.json");
        summary << std::fixed << std::setprecision(6)
                << "{\n  \"work_threshold\": " << best.work_threshold << ",\n"
                << "  \"island_threshold\": " << best.island_threshold << ",\n"
                << "  \"evaluation_selected_ms\": " << selected_total << ",\n"
                << "  \"evaluation_oracle_ms\": " << oracle_total << ",\n"
                << "  \"evaluation_regret_percent\": " << regret_percent << "\n}\n";
        std::cout << "v0.12 policy threshold_work=" << best.work_threshold
                  << " threshold_islands=" << best.island_threshold
                  << " regret_percent=" << regret_percent << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v0.12 policy benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
