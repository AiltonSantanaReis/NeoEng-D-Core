#include "neoeng/core/island_runtime.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

namespace {
using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

struct Dataset final {
    std::size_t bodies{};
    std::vector<SweptContact> contacts{};
    std::vector<Fixed::rep> values{};
};

void add_edge(std::vector<SweptContact>& contacts, std::size_t first, std::size_t second) {
    if (second < first) std::swap(first, second);
    contacts.push_back(SweptContact{
        .first = first, .second = second, .axis = ContactAxis::X,
        .toi = {}, .initial_overlap = true, .final_overlap = true,
    });
}

Dataset make_dataset() {
    Dataset data;
    data.bodies = 10'000U;
    std::size_t cursor = 0U;
    // 1,000 independent matching islands.
    for (std::size_t island = 0U; island < 1'000U; ++island) {
        add_edge(data.contacts, cursor, cursor + 1U);
        cursor += 2U;
    }
    // 100 chains of 20 bodies.
    for (std::size_t island = 0U; island < 100U; ++island) {
        for (std::size_t edge = 0U; edge < 19U; ++edge) {
            add_edge(data.contacts, cursor + edge, cursor + edge + 1U);
        }
        cursor += 20U;
    }
    // 100 trees (stars) of 10 bodies.
    for (std::size_t island = 0U; island < 100U; ++island) {
        for (std::size_t leaf = 1U; leaf < 10U; ++leaf) {
            add_edge(data.contacts, cursor, cursor + leaf);
        }
        cursor += 10U;
    }
    // 100 cycles of 10 bodies.
    for (std::size_t island = 0U; island < 100U; ++island) {
        for (std::size_t edge = 0U; edge < 10U; ++edge) {
            add_edge(data.contacts, cursor + edge, cursor + ((edge + 1U) % 10U));
        }
        cursor += 10U;
    }
    // 100 general graphs: a six-body cycle plus a chord.
    for (std::size_t island = 0U; island < 100U; ++island) {
        for (std::size_t edge = 0U; edge < 6U; ++edge) {
            add_edge(data.contacts, cursor + edge, cursor + ((edge + 1U) % 6U));
        }
        add_edge(data.contacts, cursor, cursor + 3U);
        cursor += 6U;
    }
    std::sort(data.contacts.begin(), data.contacts.end());
    data.values.resize(data.bodies);
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    for (Fixed::rep& value : data.values) {
        state ^= state >> 12U;
        state ^= state << 25U;
        state ^= state >> 27U;
        const std::int64_t small = static_cast<std::int64_t>((state * 2685821657736338717ULL) % 2'000'001ULL) - 1'000'000;
        value = small * 4'096;
    }
    return data;
}

double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U));
    return values[index];
}

struct Row final {
    std::string operation;
    std::size_t workers{};
    double p50_ms{};
    double p95_ms{};
    std::uint64_t hash{};
    IslandProjectionStats projection{};
};

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "artifacts/v0.10-benchmark";
        std::filesystem::create_directories(output);
        Dataset data = make_dataset();
        ContactIslandWorkspace workspace(data.bodies, data.contacts.size());
        workspace.classify(data.bodies, data.contacts);

        std::array<std::size_t, 5> topology_counts{};
        std::size_t maximum_colors = 0U;
        for (const ContactIslandDescriptor& island : workspace.islands()) {
            ++topology_counts[static_cast<std::size_t>(island.topology)];
            maximum_colors = std::max(maximum_colors, island.color_count);
        }

        std::vector<double> classify_times;
        classify_times.reserve(200U);
        for (std::size_t sample = 0U; sample < 200U; ++sample) {
            const auto begin = Clock::now();
            workspace.classify(data.bodies, data.contacts);
            const auto end = Clock::now();
            classify_times.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        }

        std::vector<Row> rows;
        std::vector<Fixed::rep> values(data.values.size());
        std::vector<Fixed::rep> serial_reference(data.values.size());
        for (const std::size_t workers : {1U, 2U, 4U}) {
            std::vector<double> times;
            times.reserve(100U);
            std::uint64_t hash = 0U;
            IslandProjectionStats last;
            for (std::size_t sample = 0U; sample < 100U; ++sample) {
                std::memcpy(values.data(), data.values.data(),
                    data.values.size() * sizeof(Fixed::rep));
                const auto begin = Clock::now();
                last = project_contact_islands_monotone(
                    values, data.contacts, workspace, 4U, workers);
                const auto end = Clock::now();
                times.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
                hash = island_projection_hash(values, workspace);
                if (workers == 1U && sample == 0U) {
                    std::memcpy(serial_reference.data(), values.data(),
                        values.size() * sizeof(Fixed::rep));
                }
                if (workers > 1U && values != serial_reference) {
                    throw std::runtime_error("Parallel island projection diverged from serial result");
                }
            }
            rows.push_back(Row{
                .operation = "projection",
                .workers = workers,
                .p50_ms = percentile(times, 0.50),
                .p95_ms = percentile(times, 0.95),
                .hash = hash,
                .projection = last,
            });
        }

        std::ofstream csv(output / "island_runtime.csv");
        csv << "operation,workers,bodies,contacts,islands,matching,chain,tree,cycle,general,max_colors,p50_ms,p95_ms,violations_before,violations_after,adjustments,hash,workspace_bytes,capacity_failures\n";
        csv << std::fixed << std::setprecision(6);
        csv << "classification,1," << data.bodies << ',' << data.contacts.size() << ','
            << workspace.islands().size() << ',' << topology_counts[0] << ','
            << topology_counts[1] << ',' << topology_counts[2] << ','
            << topology_counts[3] << ',' << topology_counts[4] << ',' << maximum_colors << ','
            << percentile(classify_times, 0.50) << ',' << percentile(classify_times, 0.95)
            << ",0,0,0,0," << workspace.reserved_bytes() << ','
            << workspace.stats().capacity_failures << '\n';
        for (const Row& row : rows) {
            csv << row.operation << ',' << row.workers << ',' << data.bodies << ','
                << data.contacts.size() << ',' << workspace.islands().size() << ','
                << topology_counts[0] << ',' << topology_counts[1] << ','
                << topology_counts[2] << ',' << topology_counts[3] << ','
                << topology_counts[4] << ',' << maximum_colors << ','
                << row.p50_ms << ',' << row.p95_ms << ','
                << row.projection.violations_before << ','
                << row.projection.violations_after << ','
                << row.projection.pair_adjustments << ",0x" << std::hex << std::uppercase
                << row.hash << std::dec << ',' << workspace.reserved_bytes() << ','
                << workspace.stats().capacity_failures << '\n';
        }

        std::ofstream json(output / "summary.json");
        json << "{\n"
             << "  \"bodies\": " << data.bodies << ",\n"
             << "  \"contacts\": " << data.contacts.size() << ",\n"
             << "  \"islands\": " << workspace.islands().size() << ",\n"
             << "  \"classification_p95_ms\": " << percentile(classify_times, 0.95) << ",\n"
             << "  \"serial_p95_ms\": " << rows[0].p95_ms << ",\n"
             << "  \"parallel4_p95_ms\": " << rows[2].p95_ms << ",\n"
             << "  \"workspace_bytes\": " << workspace.reserved_bytes() << ",\n"
             << "  \"capacity_failures\": " << workspace.stats().capacity_failures << ",\n"
             << "  \"hash\": \"0x" << std::hex << std::uppercase << rows[0].hash << std::dec << "\"\n"
             << "}\n";

        std::cout << "v0.10 island benchmark\n"
                  << "bodies=" << data.bodies << " contacts=" << data.contacts.size()
                  << " islands=" << workspace.islands().size() << '\n'
                  << "classification p95=" << percentile(classify_times, 0.95) << " ms\n";
        for (const Row& row : rows) {
            std::cout << "workers=" << row.workers << " p95=" << row.p95_ms
                      << " ms hash=0x" << std::hex << std::uppercase << row.hash
                      << std::dec << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v0.10 benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
