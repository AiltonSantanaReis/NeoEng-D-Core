#include "neoeng/core/dynamic_island_pair_history.hpp"
#include "neoeng/core/exact_oblique_tree_oracle.hpp"
#include "neoeng/core/segmented_dynamic_pair_history.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using namespace neoeng::core;
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 4> kNormals{{
    {kOne, 0}, {0, kOne}, {759250125, 759250125}, {644245094, 858993459}
}};

double percentile(std::vector<double> values, double quantile) {
    std::sort(values.begin(), values.end());
    const std::size_t index = static_cast<std::size_t>(quantile * static_cast<double>(values.size() - 1U));
    return values[index];
}

struct HistoryRow final {
    std::string operation;
    double p50_us{};
    double p95_us{};
    std::size_t reserved_bytes{};
    std::size_t global_history_reserved_bytes{};
    std::uint64_t segments_written{};
    std::uint64_t segments_reused{};
    std::uint64_t tables_shared{};
    std::uint64_t hash{};
};

HistoryRow benchmark_segmented_history() {
    constexpr std::size_t bodies = 10'000U;
    constexpr std::size_t pairs_count = 5'000U;
    constexpr std::size_t history_capacity = 32U;
    std::vector<NormalContact> contacts(pairs_count);
    std::vector<BroadphasePair> full_pairs(pairs_count);
    for (std::size_t pair = 0U; pair < pairs_count; ++pair) {
        contacts[pair] = {pair * 2U, pair * 2U + 1U, {kOne, 0}};
        full_pairs[pair] = {pair * 2U, pair * 2U + 1U};
    }
    DynamicIslandPairHistory global_history({
        .bodies = bodies,
        .maximum_contacts = pairs_count,
        .maximum_pairs = pairs_count,
        .history_capacity = history_capacity,
        .pair_generations = history_capacity + 2U,
        .topology_generations = history_capacity + 2U,
    });
    SegmentedDynamicPairHistory history({
        .bodies = bodies,
        .maximum_contacts = pairs_count,
        .maximum_pairs = pairs_count,
        .maximum_pairs_per_segment = 1U,
        .history_capacity = history_capacity,
        .segment_generations = pairs_count + history_capacity + 8U,
        .spill_generations = 4U,
        .table_generations = history_capacity + 2U,
    });
    history.initialize(0U, contacts, full_pairs);
    for (std::uint64_t frame = 1U; frame < history_capacity; ++frame) {
        history.capture(frame, contacts, full_pairs, {}, false, true, true);
    }

    std::vector<BroadphasePair> reduced(full_pairs.begin() + 1, full_pairs.end());
    std::array<std::size_t, 1> dirty{0U};
    std::vector<double> samples;
    samples.reserve(160U);
    std::uint64_t frame = history_capacity;
    for (std::size_t trial = 0U; trial < 168U; ++trial, ++frame) {
        const bool remove = (trial & 1U) == 0U;
        const auto& pairs = remove ? reduced : full_pairs;
        const auto start = Clock::now();
        history.capture(frame, contacts, pairs, dirty, false, true, true);
        const auto stop = Clock::now();
        if (trial >= 8U) {
            samples.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
        }
    }
    std::vector<BroadphasePair> restored(pairs_count);
    const std::size_t restored_count = history.restore_pairs(frame - 1U, restored);
    restored.resize(restored_count);
    const std::uint64_t last_frame = frame - 1U;
    const auto& expected = ((last_frame - history_capacity) & 1U) == 0U ? reduced : full_pairs;
    if (restored != expected) throw std::runtime_error("Segmented benchmark restore mismatch");
    return {
        .operation = "segmented_cow_dirty_island",
        .p50_us = percentile(samples, 0.50),
        .p95_us = percentile(samples, 0.95),
        .reserved_bytes = history.reserved_bytes(),
        .global_history_reserved_bytes = global_history.reserved_bytes(),
        .segments_written = history.stats().segments_written,
        .segments_reused = history.stats().segments_reused,
        .tables_shared = history.stats().tables_shared,
        .hash = history.hash(frame - 1U),
    };
}

struct RepairSummary final {
    double p50_us{};
    double p95_us{};
    std::size_t cases{};
    std::size_t rounded_violations{};
    std::size_t repaired{};
    std::size_t repair_failures{};
    std::uint64_t aggregate{};
};

RepairSummary benchmark_repair() {
    std::mt19937_64 rng(0x4E454F454E475632ULL);
    std::vector<double> samples;
    RepairSummary summary{.aggregate = 0xCBF29CE484222325ULL};
    const auto mix = [&summary](std::uint64_t value) {
        for (unsigned byte = 0U; byte < 8U; ++byte) {
            summary.aggregate ^= (value >> (byte * 8U)) & 0xFFU;
            summary.aggregate *= 0x100000001B3ULL;
        }
    };
    for (std::size_t trial = 0U; trial < 256U; ++trial) {
        constexpr std::size_t bodies = 8U;
        std::array<Fixed::rep, bodies> x{}, y{};
        std::array<std::uint32_t, bodies> masses{};
        std::array<NormalContact, bodies - 1U> contacts{};
        for (std::size_t body = 0U; body < bodies; ++body) {
            x[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 17U) - 8);
            y[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 17U) - 8);
            masses[body] = 1U + static_cast<std::uint32_t>(rng() % 9U);
            if (body != 0U) contacts[body - 1U] = {
                static_cast<std::size_t>(rng() % body), body, kNormals[rng() % kNormals.size()]};
        }
        const auto start = Clock::now();
        const auto result = solve_exact_oblique_tree_active_sets(x, y, masses, contacts,
            {.maximum_bodies = 8U, .maximum_contacts = 7U, .quantized_repair_radius = 1U});
        const auto stop = Clock::now();
        if (!result.certified_continuous) throw std::runtime_error("Exact repair benchmark failed");
        samples.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
        ++summary.cases;
        if (result.rounded_primal_violation_raw != 0U) ++summary.rounded_violations;
        if (result.repair_certified_neighbourhood) {
            ++summary.repaired;
            if (result.repaired_primal_violation_raw != 0U) ++summary.repair_failures;
        }
        mix(result.hash);
    }
    summary.p50_us = percentile(samples, 0.50);
    summary.p95_us = percentile(samples, 0.95);
    return summary;
}
} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "artifacts/v0.22-benchmark";
    std::filesystem::create_directories(output);
    const HistoryRow history = benchmark_segmented_history();
    const RepairSummary repair = benchmark_repair();

    std::ofstream csv(output / "v22_cycle.csv");
    csv << "operation,p50_us,p95_us,reserved_bytes,global_history_reserved_bytes,segments_written,segments_reused,tables_shared,hash\n";
    csv << history.operation << ',' << std::fixed << std::setprecision(3)
        << history.p50_us << ',' << history.p95_us << ',' << history.reserved_bytes << ','
        << history.global_history_reserved_bytes << ',' << history.segments_written << ',' << history.segments_reused << ','
        << history.tables_shared << ",0x" << std::hex << std::uppercase << history.hash << std::dec << "\n";
    csv << "quantized_repair," << repair.p50_us << ',' << repair.p95_us
        << ",0,0,0," << repair.repaired << ",0,0x" << std::hex << std::uppercase
        << repair.aggregate << std::dec << "\n";

    std::ofstream json(output / "summary.json");
    json << "{\n"
         << "  \"segmented_capture_p95_us\": " << std::fixed << std::setprecision(3) << history.p95_us << ",\n"
         << "  \"segmented_reserved_bytes\": " << history.reserved_bytes << ",\n"
         << "  \"global_history_reserved_bytes\": " << history.global_history_reserved_bytes << ",\n"
         << "  \"segments_written\": " << history.segments_written << ",\n"
         << "  \"segments_reused\": " << history.segments_reused << ",\n"
         << "  \"tables_shared\": " << history.tables_shared << ",\n"
         << "  \"repair_p95_us\": " << repair.p95_us << ",\n"
         << "  \"repair_cases\": " << repair.cases << ",\n"
         << "  \"rounded_violations\": " << repair.rounded_violations << ",\n"
         << "  \"repair_certified\": " << repair.repaired << ",\n"
         << "  \"repair_failures\": " << repair.repair_failures << ",\n"
         << "  \"repair_aggregate\": \"0x" << std::hex << std::uppercase
         << repair.aggregate << std::dec << "\"\n}\n";

    std::cout << "v0.22 segmented p95_us=" << history.p95_us
              << " reserve=" << history.reserved_bytes
              << " global_reserve=" << history.global_history_reserved_bytes
              << " written=" << history.segments_written
              << " reused=" << history.segments_reused << '\n';
    std::cout << "v0.22 repair p95_us=" << repair.p95_us
              << " rounded_violations=" << repair.rounded_violations
              << " certified=" << repair.repaired
              << " failures=" << repair.repair_failures
              << " aggregate=0x" << std::hex << std::uppercase << repair.aggregate << std::dec << '\n';
    return EXIT_SUCCESS;
}
