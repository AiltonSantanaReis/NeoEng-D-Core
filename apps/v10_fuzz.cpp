#include "neoeng/core/island_runtime.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <queue>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
using namespace neoeng::core;

IslandTopology reference_topology(
    std::span<const std::size_t> bodies,
    std::span<const std::pair<std::size_t, std::size_t>> edges) {
    if (bodies.size() == 2U && edges.size() == 1U) return IslandTopology::Matching;
    std::vector<std::size_t> degree(bodies.size(), 0U);
    for (const auto& [first, second] : edges) {
        const auto fi = std::lower_bound(bodies.begin(), bodies.end(), first) - bodies.begin();
        const auto si = std::lower_bound(bodies.begin(), bodies.end(), second) - bodies.begin();
        ++degree[static_cast<std::size_t>(fi)];
        ++degree[static_cast<std::size_t>(si)];
    }
    if (degree.empty()) return IslandTopology::General;
    const std::size_t maximum = *std::max_element(degree.begin(), degree.end());
    const bool all_two = std::all_of(degree.begin(), degree.end(), [](std::size_t value) {
        return value == 2U;
    });
    if (edges.size() + 1U == bodies.size()) {
        return maximum <= 2U ? IslandTopology::Chain : IslandTopology::Tree;
    }
    if (edges.size() == bodies.size() && all_two) return IslandTopology::Cycle;
    return IslandTopology::General;
}


void verify_general_manifold_persistence() {
    WorldState world;
    world.frame = 0U;
    world.bodies = {
        Body{.id = 1U, .position = {Fixed::from_ratio(0, 1), {}},
             .velocity = {Fixed::from_ratio(1, 10), {}}},
        Body{.id = 2U, .position = {Fixed::from_ratio(9, 10), {}},
             .velocity = {Fixed::from_ratio(-1, 10), {}}},
    };
    const ComponentWorldState current = make_component_world(world, 16U);
    const DeterministicActiveSet active = DeterministicActiveSet::from_world(world);
    ContactSolverConfig config;
    config.connected_solver_mode = ConnectedContactSolverMode::GeneralColored;

    ComponentStepResult integrated_first = step_component_active(current, active, {});
    ContactSolverStats broadphase_first;
    std::vector<SweptContact> contacts_first = swept_aabb_contacts(
        current, integrated_first.state, config, &broadphase_first);
    PersistentManifoldState first;
    ContactStepResult solved_first = solve_component_contact_constraints(
        current, std::move(integrated_first), std::move(contacts_first), config,
        broadphase_first, nullptr, &first);
    if (first.points.empty() || solved_first.stats.manifold_points_created == 0U) {
        throw std::runtime_error("General manifold did not create canonical points");
    }

    ComponentStepResult integrated_second = step_component_active(current, active, {});
    ContactSolverStats broadphase_second;
    std::vector<SweptContact> contacts_second = swept_aabb_contacts(
        current, integrated_second.state, config, &broadphase_second);
    PersistentManifoldState second;
    ContactStepResult solved_second = solve_component_contact_constraints(
        current, std::move(integrated_second), std::move(contacts_second), config,
        broadphase_second, &first, &second);
    if (second.points != first.points || solved_second.stats.manifold_points_reused == 0U) {
        throw std::runtime_error("General manifold identity was not reused");
    }
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) {
    hash ^= value + 0x9E3779B97F4A7C15ULL + (hash << 6U) + (hash >> 2U);
    return hash;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::size_t iterations = argc > 1
            ? static_cast<std::size_t>(std::stoull(argv[1])) : 5'000U;
        verify_general_manifold_persistence();
        std::mt19937_64 random(0x4E454F454E475631ULL);
        ContactIslandWorkspace workspace(128U, 512U);
        std::uint64_t aggregate = 0xCBF29CE484222325ULL;

        for (std::size_t scenario = 0U; scenario < iterations; ++scenario) {
            const std::size_t body_count = 2U + random() % 127U;
            const std::size_t requested_edges = random() % std::min<std::size_t>(512U, body_count * 4U + 1U);
            std::set<std::pair<std::size_t, std::size_t>> unique;
            while (unique.size() < requested_edges) {
                std::size_t first = random() % body_count;
                std::size_t second = random() % body_count;
                if (first == second) continue;
                if (second < first) std::swap(first, second);
                unique.emplace(first, second);
                if (unique.size() == body_count * (body_count - 1U) / 2U) break;
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

            // Reference connected components.
            std::vector<std::vector<std::size_t>> adjacency(body_count);
            for (const SweptContact& contact : contacts) {
                adjacency[contact.first].push_back(contact.second);
                adjacency[contact.second].push_back(contact.first);
            }
            std::vector<std::uint8_t> visited(body_count, 0U);
            struct ReferenceIsland final {
                std::vector<std::size_t> bodies;
                std::vector<std::pair<std::size_t, std::size_t>> edges;
            };
            std::vector<ReferenceIsland> reference;
            for (std::size_t seed = 0U; seed < body_count; ++seed) {
                if (visited[seed] != 0U || adjacency[seed].empty()) continue;
                ReferenceIsland island;
                std::queue<std::size_t> queue;
                queue.push(seed);
                visited[seed] = 1U;
                while (!queue.empty()) {
                    const std::size_t body = queue.front();
                    queue.pop();
                    island.bodies.push_back(body);
                    for (const std::size_t neighbor : adjacency[body]) {
                        if (visited[neighbor] == 0U) {
                            visited[neighbor] = 1U;
                            queue.push(neighbor);
                        }
                    }
                }
                std::sort(island.bodies.begin(), island.bodies.end());
                for (const auto& edge : unique) {
                    if (std::binary_search(island.bodies.begin(), island.bodies.end(), edge.first)) {
                        island.edges.push_back(edge);
                    }
                }
                reference.push_back(std::move(island));
            }
            std::sort(reference.begin(), reference.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.bodies.front() < rhs.bodies.front();
            });
            if (reference.size() != workspace.islands().size()) {
                throw std::runtime_error("Island count differs from reference");
            }
            for (std::size_t island_index = 0U; island_index < reference.size(); ++island_index) {
                const auto& expected = reference[island_index];
                const ContactIslandDescriptor& actual = workspace.islands()[island_index];
                if (actual.minimum_body != expected.bodies.front()
                    || actual.body_count != expected.bodies.size()
                    || actual.contact_count != expected.edges.size()
                    || actual.topology != reference_topology(expected.bodies, expected.edges)) {
                    throw std::runtime_error("Island classification differs from reference");
                }
                for (std::size_t color = 0U; color < actual.color_count; ++color) {
                    std::set<std::size_t> used;
                    for (std::size_t offset = 0U; offset < actual.contact_count; ++offset) {
                        const std::size_t ordered = actual.contact_begin + offset;
                        if (workspace.contact_colors()[ordered] != color) continue;
                        const SweptContact& contact = contacts[workspace.contact_order()[ordered]];
                        if (!used.insert(contact.first).second || !used.insert(contact.second).second) {
                            throw std::runtime_error("Edge coloring contains a body conflict");
                        }
                    }
                }
            }

            std::vector<Fixed::rep> serial(body_count);
            for (Fixed::rep& value : serial) {
                value = static_cast<std::int64_t>(random() % 2'000'001ULL) - 1'000'000;
            }
            std::vector<Fixed::rep> parallel(serial.size());
            std::memcpy(parallel.data(), serial.data(), serial.size() * sizeof(Fixed::rep));
            const IslandProjectionStats serial_stats = project_contact_islands_monotone(
                serial, contacts, workspace, 4U, 1U);
            const IslandProjectionStats parallel_stats = project_contact_islands_monotone(
                parallel, contacts, workspace, 4U, 4U);
            if (serial != parallel) {
                throw std::runtime_error("Parallel projection differs from serial projection");
            }
            if (serial_stats.violations_after > serial_stats.violations_before
                || parallel_stats.violations_after != serial_stats.violations_after) {
                throw std::runtime_error("Projection violation count is inconsistent");
            }
            aggregate = mix(aggregate, island_projection_hash(serial, workspace));
            aggregate = mix(aggregate, workspace.islands().size());
        }

        bool capacity_rejected = false;
        try {
            ContactIslandWorkspace small(2U, 1U);
            const std::vector<SweptContact> too_many{
                {.first = 0U, .second = 1U},
                {.first = 1U, .second = 2U},
            };
            small.classify(3U, too_many);
        } catch (const std::length_error&) {
            capacity_rejected = true;
        }
        if (!capacity_rejected) throw std::runtime_error("Capacity overflow was not rejected");

        std::cout << "v0.10 fuzz scenarios=" << iterations
                  << " aggregate=0x" << std::hex << std::uppercase << aggregate
                  << std::dec << " workspace_bytes=" << workspace.reserved_bytes()
                  << " capacity_failures=" << workspace.stats().capacity_failures << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v0.10 fuzz failed: " << error.what() << '\n';
        return 1;
    }
}
