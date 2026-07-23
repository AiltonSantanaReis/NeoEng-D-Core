#include "neoeng/core/advanced_island_solver.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
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

void verify_specialized_topologies() {
    for (const int topology : {0, 1, 2}) {
        constexpr std::size_t bodies = 256U;
        std::vector<SweptContact> contacts;
        if (topology == 0) {
            for (std::size_t index = 0U; index + 1U < bodies; ++index) {
                add_edge(contacts, index, index + 1U);
            }
        } else if (topology == 1) {
            for (std::size_t leaf = 1U; leaf < bodies; ++leaf) add_edge(contacts, 0U, leaf);
        } else {
            for (std::size_t index = 0U; index < bodies; ++index) {
                add_edge(contacts, index, (index + 1U) % bodies);
            }
        }
        std::sort(contacts.begin(), contacts.end());
        ContactIslandWorkspace workspace(bodies, contacts.size());
        IslandSolverScratch scratch(bodies, contacts.size());
        workspace.classify(bodies, contacts);
        std::vector<Fixed::rep> values(bodies);
        for (std::size_t index = 0U; index < bodies; ++index) {
            values[index] = static_cast<Fixed::rep>((bodies - index) * 4096U);
        }
        if (topology == 1) values[0] = static_cast<Fixed::rep>(bodies * 8192U);
        const SpecializedIslandProjectionStats stats = project_contact_islands_specialized(
            values, contacts, workspace, 4U, scratch, nullptr);
        if (stats.violations_after != 0U) {
            throw std::runtime_error("Specialized topology left a violated constraint");
        }
        if (topology == 0 && stats.chain_islands != 1U) {
            throw std::runtime_error("Canonical chain specialization was not selected");
        }
        if (topology == 1 && stats.star_tree_islands != 1U) {
            throw std::runtime_error("Star-tree specialization was not selected");
        }
        if (topology == 2 && stats.reduced_cycle_islands != 1U) {
            throw std::runtime_error("Cycle reduction specialization was not selected");
        }
    }
}

void verify_general_warm_start() {
    constexpr std::size_t bodies = 8U;
    std::vector<SweptContact> contacts;
    for (std::size_t index = 0U; index < 6U; ++index) {
        add_edge(contacts, index, (index + 1U) % 6U);
    }
    add_edge(contacts, 0U, 3U);
    add_edge(contacts, 2U, 7U);
    std::sort(contacts.begin(), contacts.end());
    ContactIslandWorkspace workspace(bodies, contacts.size());
    IslandSolverScratch scratch(bodies, contacts.size());
    GeneralImpulseWarmStart warm(contacts.size());
    workspace.classify(bodies, contacts);
    std::vector<Fixed::rep> input{800, 700, 600, 500, 400, 300, 200, 100};
    std::vector<Fixed::rep> first = input;
    const auto first_stats = project_contact_islands_specialized(
        first, contacts, workspace, 4U, scratch, &warm);
    if (first_stats.general_fallback_islands == 0U || warm.size() != contacts.size()) {
        throw std::runtime_error("General warm-start cache was not populated");
    }
    std::vector<Fixed::rep> second = input;
    const auto second_stats = project_contact_islands_specialized(
        second, contacts, workspace, 4U, scratch, &warm);
    if (second_stats.warm_start_attempts != 1U
        || second_stats.warm_start_accepts == 0U
        || second_stats.warm_contacts_applied == 0U) {
        throw std::runtime_error("General accumulated impulses were not applied");
    }
    if (second_stats.violations_after > first_stats.violations_after) {
        throw std::runtime_error("General warm start increased final violations");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::size_t iterations = argc > 1
            ? static_cast<std::size_t>(std::stoull(argv[1])) : 5'000U;
        verify_specialized_topologies();
        verify_general_warm_start();

        std::mt19937_64 random(0x4E454F454E475631ULL);
        ContactIslandWorkspace workspace(128U, 512U);
        IslandSolverScratch scratch(128U, 512U);
        GeneralImpulseWarmStart warm(512U);
        PersistentIslandWorkerPool pool(8U);
        std::uint64_t aggregate = 0xCBF29CE484222325ULL;

        for (std::size_t scenario = 0U; scenario < iterations; ++scenario) {
            const std::size_t body_count = 2U + random() % 127U;
            const std::size_t maximum_edges = body_count * (body_count - 1U) / 2U;
            const std::size_t requested_edges = random()
                % (std::min<std::size_t>(512U, body_count * 4U) + 1U);
            std::set<std::pair<std::size_t, std::size_t>> unique;
            while (unique.size() < requested_edges && unique.size() < maximum_edges) {
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
            std::vector<Fixed::rep> serial = input;
            std::vector<Fixed::rep> pooled = input;
            const IslandProjectionStats serial_stats = project_contact_islands_monotone(
                serial, contacts, workspace, 4U, 1U);
            const std::size_t workers = std::min<std::size_t>(8U,
                std::max<std::size_t>(1U, workspace.islands().size()));
            const IslandProjectionStats pooled_stats = project_contact_islands_monotone_pooled(
                pooled, contacts, workspace, 4U, pool, workers);
            if (serial != pooled || serial_stats.violations_after != pooled_stats.violations_after) {
                throw std::runtime_error("Persistent worker pool differs from serial projection");
            }

            std::vector<Fixed::rep> specialized = input;
            std::vector<Fixed::rep> specialized_repeat = input;
            warm.clear();
            const SpecializedIslandProjectionStats specialized_stats =
                project_contact_islands_specialized(
                    specialized, contacts, workspace, 4U, scratch, &warm);
            warm.clear();
            const SpecializedIslandProjectionStats repeat_stats =
                project_contact_islands_specialized(
                    specialized_repeat, contacts, workspace, 4U, scratch, &warm);
            if (specialized != specialized_repeat
                || specialized_stats.violations_after != repeat_stats.violations_after) {
                throw std::runtime_error("Specialized solver is not repeatable");
            }
            aggregate = mix(aggregate, island_projection_hash(serial, workspace));
            aggregate = mix(aggregate, island_projection_hash(specialized, workspace));
            aggregate = mix(aggregate, specialized_stats.chain_islands);
            aggregate = mix(aggregate, specialized_stats.star_tree_islands);
            aggregate = mix(aggregate, specialized_stats.reduced_cycle_islands);
            aggregate = mix(aggregate, specialized_stats.general_fallback_islands);
        }

        std::cout << "v0.11 fuzz scenarios=" << iterations
                  << " aggregate=0x" << std::hex << std::uppercase << aggregate
                  << std::dec << " pool_workers=" << pool.worker_count()
                  << " scratch_bytes=" << scratch.reserved_bytes() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v0.11 fuzz failed: " << error.what() << '\n';
        return 1;
    }
}
