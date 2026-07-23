#include "neoeng/core/hash.hpp"
#include "neoeng/core/weighted_contact_projection.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace {
using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

struct Dataset final {
    std::string name;
    WorldState world;
    std::vector<std::uint32_t> masses;
    std::vector<SweptContact> contacts;
};

void add_contact(std::vector<SweptContact>& contacts, std::size_t first,
                 std::size_t second, ContactAxis axis) {
    if (second < first) std::swap(first, second);
    contacts.push_back(SweptContact{
        .first = first, .second = second, .axis = axis,
        .toi = {}, .initial_overlap = true, .final_overlap = true,
    });
}

Dataset make_order_dataset(std::string name, std::size_t islands,
                           std::size_t bodies_per_island, std::uint64_t seed) {
    Dataset data{.name = std::move(name), .world = {}, .masses = {}, .contacts = {}};
    const std::size_t bodies = islands * bodies_per_island;
    std::mt19937_64 random(seed);
    data.world.bodies.reserve(bodies);
    data.masses.resize(bodies);
    for (std::size_t i = 0; i < bodies; ++i) {
        data.world.bodies.push_back(Body{
            .id = static_cast<EntityId>(i + 1U), .position = {},
            .velocity = {
                Fixed::from_raw(static_cast<std::int64_t>(random() % 20'000'001ULL) - 10'000'000),
                Fixed::from_raw(static_cast<std::int64_t>(random() % 20'000'001ULL) - 10'000'000),
            },
        });
        data.masses[i] = 1U + static_cast<std::uint32_t>(random() % 32U);
    }
    for (std::size_t island = 0; island < islands; ++island) {
        const std::size_t base = island * bodies_per_island;
        const ContactAxis axis = (island & 1U) == 0U ? ContactAxis::X : ContactAxis::Y;
        for (std::size_t i = 0; i + 1U < bodies_per_island; ++i) {
            add_contact(data.contacts, base + i, base + i + 1U, axis);
        }
        if (bodies_per_island > 4U) {
            add_contact(data.contacts, base, base + bodies_per_island - 1U, axis);
            add_contact(data.contacts, base + 1U, base + bodies_per_island - 2U, axis);
        }
    }
    return data;
}

Dataset make_matching_dataset() {
    Dataset data{.name = "physical_10000_5000_weighted", .world = {}, .masses = {}, .contacts = {}};
    constexpr std::size_t bodies = 10'000U;
    data.world.bodies.reserve(bodies);
    data.masses.resize(bodies);
    for (std::size_t i = 0; i < bodies; ++i) {
        const bool first = (i & 1U) == 0U;
        data.world.bodies.push_back(Body{
            .id = static_cast<EntityId>(i + 1U), .position = {},
            .velocity = {
                Fixed::from_raw(first ? 5'000'000 : -5'000'000),
                Fixed::from_raw(first ? 3'000'000 : -3'000'000),
            },
        });
        data.masses[i] = 1U + static_cast<std::uint32_t>((i * 17U) % 64U);
    }
    data.contacts.reserve(bodies / 2U);
    for (std::size_t i = 0; i < bodies; i += 2U) {
        add_contact(data.contacts, i, i + 1U,
                    ((i / 2U) & 1U) == 0U ? ContactAxis::X : ContactAxis::Y);
    }
    return data;
}

Dataset make_fallback_dataset() {
    Dataset data{.name = "nonreducible_64_stars", .world = {}, .masses = {}, .contacts = {}};
    constexpr std::size_t islands = 64U;
    constexpr std::size_t per = 33U;
    const std::size_t bodies = islands * per;
    data.world.bodies.reserve(bodies);
    data.masses.resize(bodies);
    for (std::size_t i = 0; i < bodies; ++i) {
        data.world.bodies.push_back(Body{
            .id = static_cast<EntityId>(i + 1U), .position = {},
            .velocity = {Fixed::from_raw(static_cast<std::int64_t>((i * 7919U) % 2'000'001U) - 1'000'000), {}},
        });
        data.masses[i] = 1U + static_cast<std::uint32_t>((i * 13U) % 16U);
    }
    for (std::size_t island = 0; island < islands; ++island) {
        const std::size_t base = island * per;
        for (std::size_t leaf = 1U; leaf < per; ++leaf) {
            add_contact(data.contacts, base, base + leaf, ContactAxis::X);
        }
    }
    return data;
}

double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U))];
}

struct Row final {
    std::string dataset;
    std::string method;
    double p50_ms{};
    double p95_ms{};
    std::uint64_t reductions{};
    std::uint64_t star_reductions{};
    std::uint64_t fallbacks{};
    std::uint64_t changed{};
    std::uint64_t primal{};
    std::uint64_t quantization{};
    bool certified{};
    std::uint64_t pages{};
    std::uint64_t hash{};
};

Row measure(const Dataset& data, WeightedProjectionMethod method) {
    const auto component = make_component_world(data.world, 256U);
    WeightedProjectionScratch scratch(data.world.bodies.size(), data.contacts.size());
    constexpr std::size_t warmup = 8U;
    constexpr std::size_t trials = 80U;
    std::vector<double> samples;
    samples.reserve(trials);
    WeightedVelocityProjectionResult last;
    for (std::size_t trial = 0; trial < warmup + trials; ++trial) {
        const auto begin = Clock::now();
        auto result = project_weighted_contact_velocities_2d(
            component, data.masses, data.contacts, method,
            {.maximum_iterations = 2'048U, .certification_tolerance_raw = 65'536U}, scratch);
        const auto end = Clock::now();
        if (trial >= warmup) {
            samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        }
        last = std::move(result);
    }
    return Row{
        .dataset = data.name,
        .method = to_string(method),
        .p50_ms = percentile(samples, 0.50),
        .p95_ms = percentile(samples, 0.95),
        .reductions = last.stats.total_order_reductions,
        .star_reductions = last.stats.star_reductions,
        .fallbacks = last.stats.iterative_fallbacks,
        .changed = last.stats.changed_bodies,
        .primal = last.stats.residuals.primal_linf_raw,
        .quantization = last.stats.residuals.quantization_linf_weighted_raw,
        .certified = last.stats.residuals.certified,
        .pages = last.allocation.component_pages_allocated,
        .hash = stable_hash(last.state.materialize()),
    };
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "artifacts/v0.13-benchmark";
        std::filesystem::create_directories(output);
        std::vector<Dataset> datasets;
        datasets.push_back(make_order_dataset("weighted_order_64x32_2d", 64U, 32U, 0x1301ULL));
        datasets.push_back(make_order_dataset("weighted_order_4x512_2d", 4U, 512U, 0x1302ULL));
        datasets.push_back(make_matching_dataset());
        datasets.push_back(make_fallback_dataset());

        std::vector<Row> rows;
        for (const Dataset& data : datasets) {
            rows.push_back(measure(data, WeightedProjectionMethod::CertifiedAuto));
            if (data.name == "nonreducible_64_stars") {
                rows.push_back(measure(data, WeightedProjectionMethod::QuantizedDykstra));
            }
        }
        std::ofstream csv(output / "weighted_physics.csv");
        csv << "dataset,method,p50_ms,p95_ms,total_order_reductions,star_reductions,fallbacks,changed_bodies,primal_raw,quantization_weighted_raw,certified,component_pages,hash\n";
        csv << std::fixed << std::setprecision(6);
        for (const Row& row : rows) {
            csv << row.dataset << ',' << row.method << ',' << row.p50_ms << ',' << row.p95_ms
                << ',' << row.reductions << ',' << row.star_reductions << ',' << row.fallbacks << ',' << row.changed
                << ',' << row.primal << ',' << row.quantization << ',' << (row.certified ? 1 : 0)
                << ',' << row.pages << ",0x" << std::hex << std::uppercase << row.hash
                << std::dec << '\n';
        }
        std::ofstream summary(output / "summary.json");
        summary << "{\n  \"version\": \"0.13\",\n  \"rows\": " << rows.size() << ",\n"
                << "  \"physical_gate_p95_ms\": "
                << rows[2].p95_ms << ",\n  \"physical_gate_passed\": "
                << (rows[2].p95_ms <= 2.0 ? "true" : "false") << "\n}\n";
        for (const Row& row : rows) {
            std::cout << row.dataset << ' ' << row.method
                      << " p50=" << row.p50_ms << " ms p95=" << row.p95_ms
                      << " ms certified=" << row.certified
                      << " hash=0x" << std::hex << std::uppercase << row.hash
                      << std::dec << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "v0.13 benchmark failure: " << error.what() << '\n';
        return 1;
    }
}
