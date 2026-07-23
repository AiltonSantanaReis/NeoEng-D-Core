#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/exact_oblique_tree_oracle.hpp"
#include "neoeng/core/fixed_raa_microkernel.hpp"
#include "neoeng/core/fixed_slot_allocator.hpp"

#include <algorithm>
#include <array>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/rational_adaptor.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string_view>
#include <vector>

namespace {
using namespace neoeng::core;
using Clock = std::chrono::steady_clock;
using Rational = boost::multiprecision::cpp_rational;
using Integer = boost::multiprecision::cpp_int;
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 6U> kNormals{{
    {kOne, 0}, {0, kOne}, {759250125, 759250125}, {644245094, 858993459},
    {-759250125, 759250125}, {858993459, -644245094},
}};

[[nodiscard]] double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1U))];
}
[[nodiscard]] std::string_view mode_name(FixedSlotAllocatorMode mode) noexcept {
    switch (mode) {
    case FixedSlotAllocatorMode::NextFit: return "next_fit";
    case FixedSlotAllocatorMode::HierarchicalBitmap: return "hierarchical_bitmap";
    case FixedSlotAllocatorMode::Adaptive: return "adaptive";
    }
    return "unknown";
}
void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) { hash ^= (value >> (byte * 8U)) & 0xFFU; hash *= 0x100000001B3ULL; }
}

struct AllocatorStress final {
    FixedSlotAllocatorMode mode{};
    double occupancy{};
    double p50_us{};
    double p95_us{};
    std::uint64_t probes{};
    std::uint64_t next_dispatches{};
    std::uint64_t bitmap_dispatches{};
    std::uint64_t hash{};
};

AllocatorStress allocator_stress(FixedSlotAllocatorMode mode, double occupancy) {
    constexpr std::size_t capacity = 8'192U, batches = 500U, operations = 2'000U;
    FixedSlotAllocator allocator(capacity, mode);
    const std::size_t initial = std::min(capacity - 1U, static_cast<std::size_t>(occupancy * static_cast<double>(capacity)));
    std::vector<std::uint32_t> active; active.reserve(capacity);
    for (std::size_t index = 0U; index < initial; ++index) active.push_back(allocator.acquire());
    std::uint64_t state = 0x2700270027002700ULL ^ static_cast<std::uint64_t>(initial);
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    std::vector<double> samples; samples.reserve(batches);
    for (std::size_t batch = 0U; batch < batches; ++batch) {
        const auto begin = Clock::now();
        for (std::size_t operation = 0U; operation < operations; ++operation) {
            state ^= state << 7U; state ^= state >> 9U; state ^= state << 8U;
            const std::size_t position = static_cast<std::size_t>(state % active.size());
            allocator.release(active[position]);
            active[position] = allocator.acquire();
            mix_u64(hash, active[position]);
        }
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
    }
    const auto stats = allocator.stats();
    return {.mode = mode, .occupancy = occupancy, .p50_us = percentile(samples, .50), .p95_us = percentile(samples, .95),
        .probes = stats.probes, .next_dispatches = stats.next_fit_dispatches,
        .bitmap_dispatches = stats.bitmap_dispatches, .hash = hash};
}

struct SweepAudit final { std::size_t iterations{}; std::size_t feasible{}; std::size_t exact{}; };

SweepAudit audit_sweeps(std::size_t maximum_iterations, std::size_t cases) {
    std::mt19937_64 rng(0x270027ULL);
    SweepAudit result{.iterations = maximum_iterations};
    for (std::size_t sample = 0U; sample < cases; ++sample) {
        const std::size_t bodies = 3U + static_cast<std::size_t>(rng() % 6U);
        std::vector<Fixed::rep> input_x(bodies), input_y(bodies), vx(bodies), vy(bodies);
        std::vector<std::uint32_t> masses(bodies);
        std::vector<NormalContact> contacts; contacts.reserve(bodies - 1U);
        for (std::size_t body = 0U; body < bodies; ++body) {
            input_x[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 31U) - 15);
            input_y[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 31U) - 15);
            masses[body] = 1U + static_cast<std::uint32_t>(rng() % 7U);
            if (body != 0U) {
                const std::size_t parent = static_cast<std::size_t>(rng() % body);
                const NormalQ30 normal = kNormals[rng() % kNormals.size()];
                contacts.push_back((rng() & 1U) == 0U ? NormalContact{parent, body, normal} : NormalContact{body, parent, normal});
            }
        }
        const auto oracle = solve_exact_oblique_tree_active_sets(input_x, input_y, masses, contacts,
            {.maximum_bodies = 8U, .maximum_contacts = 7U, .quantized_repair_radius = 0U, .perform_quantized_repair = false});
        if (!oracle.certified_continuous) continue;
        vx = input_x; vy = input_y;
        ArbitraryNormalScratch scratch(8U, 7U);
        (void)project_arbitrary_normals_inplace(vx, vy, masses, contacts,
            {.maximum_iterations = maximum_iterations, .feasibility_tolerance_raw = 0U,
             .unit_norm_tolerance_q60 = std::numeric_limits<std::uint64_t>::max()}, scratch);
        bool feasible = true;
        for (const auto& edge : contacts) {
            const WideInteger residual = WideInteger(edge.normal.x) * (vx[edge.first] - vx[edge.second])
                + WideInteger(edge.normal.y) * (vy[edge.first] - vy[edge.second]);
            if (residual > 0) { feasible = false; break; }
        }
        if (!feasible) continue;
        ++result.feasible;
        Integer objective = 0;
        for (std::size_t body = 0U; body < bodies; ++body) {
            const Integer dx = Integer(vx[body]) - input_x[body], dy = Integer(vy[body]) - input_y[body];
            objective += Integer(masses[body]) * (dx * dx + dy * dy);
        }
        const Rational optimum = Rational(Integer(oracle.objective_numerator)) / Integer(oracle.objective_denominator);
        if (Rational(objective) == optimum) ++result.exact;
    }
    return result;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "artifacts/v0.27-benchmark";
    std::filesystem::create_directories(output);

    std::vector<AllocatorStress> allocator_rows;
    for (const double occupancy : {0.25, 0.75, 0.995}) {
        for (const auto mode : {FixedSlotAllocatorMode::NextFit, FixedSlotAllocatorMode::HierarchicalBitmap, FixedSlotAllocatorMode::Adaptive}) {
            allocator_rows.push_back(allocator_stress(mode, occupancy));
        }
    }
    std::vector<FixedRaaMicrokernelResult> raa_rows;
    for (const std::size_t terms : {8U, 12U, 16U}) {
        raa_rows.push_back(run_fixed_raa_microkernel({
            .bodies = 2'000U, .steps = 8U, .maximum_terms = terms,
            .monte_carlo_samples = 2'048U, .timing_repetitions = 8U,
            .seed = 0x270100ULL + terms,
        }));
        if (raa_rows.back().empirical_violations != 0U) throw std::runtime_error("v0.27 RAA missed Monte Carlo sample");
    }
    std::vector<SweepAudit> sweeps;
    for (const std::size_t iterations : {1U, 2U, 4U, 8U, 16U, 64U}) sweeps.push_back(audit_sweeps(iterations, 128U));

    std::ofstream allocator_csv(output / "allocator_policy.csv");
    allocator_csv << "mode,occupancy,p50_batch_us,p95_batch_us,probes,next_dispatches,bitmap_dispatches,hash\n";
    for (const auto& row : allocator_rows) allocator_csv << mode_name(row.mode) << ',' << row.occupancy << ','
        << std::fixed << std::setprecision(6) << row.p50_us << ',' << row.p95_us << ',' << row.probes << ','
        << row.next_dispatches << ',' << row.bitmap_dispatches << ",0x" << std::hex << std::uppercase << row.hash << std::dec << '\n';

    std::ofstream raa_csv(output / "fixed_raa_microkernel.csv");
    raa_csv << "maximum_terms,p50_us,p95_us,ns_per_body_step,ns_per_contact_step,final_total_width,average_total_width,compressions,rounding_guard_raw,empirical_violations,hash\n";
    for (std::size_t index = 0U; index < raa_rows.size(); ++index) {
        const auto& row = raa_rows[index]; const std::size_t terms = std::array<std::size_t,3U>{8U,12U,16U}[index];
        raa_csv << terms << ',' << std::fixed << std::setprecision(9) << row.p50_us << ',' << row.p95_us << ','
            << row.ns_per_body_step << ',' << row.ns_per_contact_step << ',' << row.final_total_width << ','
            << row.average_total_width << ',' << row.compressions << ',' << row.rounding_guard_raw << ','
            << row.empirical_violations << ",0x" << std::hex << std::uppercase << row.hash << std::dec << '\n';
    }

    std::ofstream sweep_csv(output / "oblique_sweep_audit.csv");
    sweep_csv << "iterations,cases,feasible,exact_optimum\n";
    for (const auto& row : sweeps) sweep_csv << row.iterations << ",128," << row.feasible << ',' << row.exact << '\n';

    std::ofstream summary(output / "summary.json");
    summary << "{\n"
        << "  \"allocator_operations_per_row\": 1000000,\n"
        << "  \"raa12_p95_us\": " << raa_rows[1].p95_us << ",\n"
        << "  \"raa12_ns_per_body_step\": " << raa_rows[1].ns_per_body_step << ",\n"
        << "  \"raa12_final_width\": " << raa_rows[1].final_total_width << ",\n"
        << "  \"raa12_compressions\": " << raa_rows[1].compressions << ",\n"
        << "  \"raa12_hash\": \"0x" << std::hex << std::uppercase << raa_rows[1].hash << std::dec << "\",\n"
        << "  \"sweep64_feasible\": " << sweeps.back().feasible << ",\n"
        << "  \"sweep64_exact\": " << sweeps.back().exact << "\n"
        << "}\n";

    std::cout << "v0.27 adaptive allocator rows=" << allocator_rows.size() << " raa12=" << raa_rows[1].p95_us
        << " us sweep64=" << sweeps.back().exact << '/' << sweeps.back().feasible << " hash=0x" << std::hex
        << std::uppercase << raa_rows[1].hash << std::dec << '\n';
    return EXIT_SUCCESS;
}
