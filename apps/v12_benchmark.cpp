#include "neoeng/core/general_lcp_solver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <random>
#include <set>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace allocation_probe {
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> allocations{0};
std::atomic<std::uint64_t> bytes{0};
void record(std::size_t size) noexcept {
    if (enabled.load(std::memory_order_relaxed)) {
        allocations.fetch_add(1U, std::memory_order_relaxed);
        bytes.fetch_add(size, std::memory_order_relaxed);
    }
}
}

namespace {

void* probe_allocate_aligned(std::size_t size, std::size_t alignment) noexcept {
    const std::size_t allocation_size = size == 0U ? 1U : size;
#if defined(_WIN32)
    return _aligned_malloc(allocation_size, alignment);
#else
    void* pointer = nullptr;
    return posix_memalign(&pointer, alignment, allocation_size) == 0 ? pointer : nullptr;
#endif
}

void probe_free_aligned(void* pointer) noexcept {
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

}  // namespace

void* operator new(std::size_t size) {
    if (void* pointer = std::malloc(size == 0U ? 1U : size)) {
        allocation_probe::record(size);
        return pointer;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    void* pointer = probe_allocate_aligned(size, static_cast<std::size_t>(alignment));
    if (pointer == nullptr) throw std::bad_alloc();
    allocation_probe::record(size);
    return pointer;
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void* pointer, std::align_val_t) noexcept { probe_free_aligned(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { probe_free_aligned(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { probe_free_aligned(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { probe_free_aligned(pointer); }

namespace {
using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

struct Dataset final {
    std::string name;
    std::size_t bodies{};
    std::vector<SweptContact> contacts;
    std::vector<Fixed::rep> values;
};

void add_edge(std::vector<SweptContact>& contacts, std::size_t first, std::size_t second) {
    if (second < first) std::swap(first, second);
    contacts.push_back(SweptContact{
        .first = first, .second = second, .axis = ContactAxis::X,
        .toi = {}, .initial_overlap = true, .final_overlap = true,
    });
}

Dataset make_dataset(std::string name, std::size_t islands,
                     std::size_t bodies_per_island, std::size_t edges_per_island,
                     std::uint64_t seed) {
    Dataset data{.name = std::move(name), .bodies = 0U, .contacts = {}, .values = {}};
    data.bodies = islands * bodies_per_island;
    std::mt19937_64 random(seed);
    for (std::size_t island = 0U; island < islands; ++island) {
        const std::size_t base = island * bodies_per_island;
        std::set<std::pair<std::size_t, std::size_t>> edges;
        for (std::size_t body = 0U; body + 1U < bodies_per_island; ++body) {
            edges.emplace(base + body, base + body + 1U);
        }
        while (edges.size() < edges_per_island) {
            std::size_t first = base + random() % bodies_per_island;
            std::size_t second = base + random() % bodies_per_island;
            if (first == second) continue;
            if (second < first) std::swap(first, second);
            edges.emplace(first, second);
        }
        for (const auto& [first, second] : edges) add_edge(data.contacts, first, second);
    }
    data.values.resize(data.bodies);
    for (Fixed::rep& value : data.values) {
        value = static_cast<std::int64_t>(random() % 20'000'001ULL) - 10'000'000;
    }
    return data;
}

double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U))];
}

std::uint64_t hash_values(std::span<const Fixed::rep> values) {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (Fixed::rep value : values) {
        hash ^= static_cast<std::uint64_t>(value) + 0x9E3779B97F4A7C15ULL
            + (hash << 6U) + (hash >> 2U);
    }
    return hash;
}

struct Row final {
    std::string dataset;
    std::string method;
    double p50_ms{};
    double p95_ms{};
    std::uint64_t allocations_max{};
    std::uint64_t bytes_max{};
    std::uint64_t iterations{};
    std::uint64_t updates{};
    std::uint64_t total_order_reductions{};
    std::uint64_t iterative_fallbacks{};
    std::uint64_t primal{};
    std::uint64_t stationarity{};
    std::uint64_t complementarity{};
    std::uint64_t projected_dual{};
    std::uint64_t quantization{};
    bool certified{};
    std::uint64_t hash{};
};

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "v0.12-benchmark";
        std::filesystem::create_directories(output);
        std::vector<Dataset> datasets;
        datasets.reserve(3U);
        datasets.push_back(make_dataset("general_64x32", 64U, 32U, 72U, 0x1201ULL));
        datasets.push_back(make_dataset("general_16x128", 16U, 128U, 384U, 0x1202ULL));
        datasets.push_back(make_dataset("general_4x512", 4U, 512U, 1'536U, 0x1203ULL));
        std::vector<Row> rows;
        std::ofstream convergence(output / "convergence_sweep.csv");
        convergence << "dataset,method,iteration_budget,iterations_used,primal_linf_raw,"
                       "stationarity_linf_raw,complementarity_linf_scaled_raw,"
                       "projected_dual_linf_raw,quantization_linf_raw,certified,hash\n";
        std::ofstream warm_csv(output / "warm_validation.csv");
        warm_csv << "dataset,method,warm_attempts,warm_exact_accepts,warm_rejects,elapsed_ms,hash\n";

        for (const Dataset& data : datasets) {
            ContactIslandWorkspace workspace(data.bodies, data.contacts.size());
            GeneralProjectionScratch scratch(data.bodies, data.contacts.size());
            GeneralProjectionWarmStart warm(data.contacts.size());
            allocation_probe::allocations.store(0U, std::memory_order_relaxed);
            allocation_probe::bytes.store(0U, std::memory_order_relaxed);
            allocation_probe::enabled.store(true, std::memory_order_relaxed);
            workspace.classify(data.bodies, data.contacts);
            allocation_probe::enabled.store(false, std::memory_order_relaxed);
            if (allocation_probe::allocations.load(std::memory_order_relaxed) != 0U) {
                throw std::runtime_error("Classification allocated in steady-state");
            }

            for (GeneralProjectionMethod method : {
                     GeneralProjectionMethod::DykstraCoordinate,
                     GeneralProjectionMethod::ActiveSetCoordinate,
                     GeneralProjectionMethod::ProjectedConjugateGradient,
                     GeneralProjectionMethod::CertifiedAuto}) {
                std::vector<double> times;
                std::uint64_t allocations_max = 0U;
                std::uint64_t bytes_max = 0U;
                GeneralProjectionStats last;
                std::uint64_t hash = 0U;
                for (std::size_t trial = 0U; trial < 12U; ++trial) {
                    std::vector<Fixed::rep> values = data.values;
                    allocation_probe::allocations.store(0U, std::memory_order_relaxed);
                    allocation_probe::bytes.store(0U, std::memory_order_relaxed);
                    allocation_probe::enabled.store(true, std::memory_order_relaxed);
                    const auto begin = Clock::now();
                    last = project_general_contact_islands(
                        values, data.contacts, workspace, method,
                        {.maximum_iterations = 512U, .certification_tolerance_raw = 1U,
                         .pcg_restart_interval = 16U}, scratch, nullptr);
                    const auto end = Clock::now();
                    allocation_probe::enabled.store(false, std::memory_order_relaxed);
                    times.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
                    allocations_max = std::max(
                        allocations_max, allocation_probe::allocations.load(std::memory_order_relaxed));
                    bytes_max = std::max(
                        bytes_max, allocation_probe::bytes.load(std::memory_order_relaxed));
                    hash = hash_values(values);
                }
                rows.push_back(Row{
                    .dataset = data.name,
                    .method = to_string(method),
                    .p50_ms = percentile(times, 0.50),
                    .p95_ms = percentile(times, 0.95),
                    .allocations_max = allocations_max,
                    .bytes_max = bytes_max,
                    .iterations = last.iterations,
                    .updates = last.coordinate_updates,
                    .total_order_reductions = last.total_order_reductions,
                    .iterative_fallbacks = last.iterative_fallbacks,
                    .primal = last.residuals.primal_linf_raw,
                    .stationarity = last.residuals.stationarity_linf_raw,
                    .complementarity = last.residuals.complementarity_linf_scaled_raw,
                    .projected_dual = last.residuals.projected_dual_linf_raw,
                    .quantization = last.residuals.quantization_linf_raw,
                    .certified = last.residuals.certified,
                    .hash = hash,
                });

                for (std::size_t budget : {64U, 128U, 256U, 512U, 1'024U, 2'048U, 4'096U}) {
                    std::vector<Fixed::rep> values = data.values;
                    const auto stats = project_general_contact_islands(
                        values, data.contacts, workspace, method,
                        {.maximum_iterations = budget, .certification_tolerance_raw = 1U,
                         .pcg_restart_interval = 16U}, scratch, nullptr);
                    convergence << data.name << ',' << to_string(method) << ',' << budget << ','
                                << stats.iterations << ',' << stats.residuals.primal_linf_raw << ','
                                << stats.residuals.stationarity_linf_raw << ','
                                << stats.residuals.complementarity_linf_scaled_raw << ','
                                << stats.residuals.projected_dual_linf_raw << ','
                                << stats.residuals.quantization_linf_raw << ','
                                << (stats.residuals.certified ? 1 : 0) << ",0x"
                                << std::hex << std::uppercase << hash_values(values) << std::dec << '\n';
                }

                warm.clear();
                std::vector<Fixed::rep> populate = data.values;
                (void)project_general_contact_islands(
                    populate, data.contacts, workspace, method,
                    {.maximum_iterations = 4'096U, .certification_tolerance_raw = 1U,
                     .pcg_restart_interval = 16U}, scratch, &warm);
                std::vector<Fixed::rep> verified = data.values;
                const auto warm_begin = Clock::now();
                const auto warm_stats = project_general_contact_islands(
                    verified, data.contacts, workspace, method,
                    {.maximum_iterations = 4'096U, .certification_tolerance_raw = 1U,
                     .pcg_restart_interval = 16U}, scratch, &warm);
                const auto warm_end = Clock::now();
                warm_csv << data.name << ',' << to_string(method) << ','
                         << warm_stats.warm_attempts << ',' << warm_stats.warm_exact_accepts << ','
                         << warm_stats.warm_rejects << ',' << std::fixed << std::setprecision(6)
                         << std::chrono::duration<double, std::milli>(warm_end - warm_begin).count()
                         << ",0x" << std::hex << std::uppercase << hash_values(verified)
                         << std::dec << '\n';
            }
        }

        std::ofstream csv(output / "general_solver.csv");
        csv << "dataset,method,p50_ms,p95_ms,allocations_max,bytes_max,iterations,updates,total_order_reductions,iterative_fallbacks,"
               "primal_linf_raw,stationarity_linf_raw,complementarity_linf_scaled_raw,"
               "projected_dual_linf_raw,quantization_linf_raw,certified,hash\n";
        csv << std::fixed << std::setprecision(6);
        for (const Row& row : rows) {
            csv << row.dataset << ',' << row.method << ',' << row.p50_ms << ',' << row.p95_ms
                << ',' << row.allocations_max << ',' << row.bytes_max << ',' << row.iterations
                << ',' << row.updates << ',' << row.total_order_reductions << ','
                << row.iterative_fallbacks << ',' << row.primal << ',' << row.stationarity << ','
                << row.complementarity << ',' << row.projected_dual << ',' << row.quantization << ','
                << (row.certified ? 1 : 0) << ",0x" << std::hex << std::uppercase << row.hash
                << std::dec << '\n';
        }
        std::ofstream summary(output / "summary.json");
        summary << "{\n  \"version\": \"0.12\",\n  \"rows\": " << rows.size() << ",\n"
                << "  \"scratch_policy\": \"fixed_capacity\",\n"
                << "  \"cold_output_authoritative\": true,\n"
                << "  \"warm_requires_bit_identity\": true\n}\n";
        std::cout << "v0.12 benchmark rows=" << rows.size()
                  << " output=" << output << '\n';
        return 0;
    } catch (const std::exception& error) {
        allocation_probe::enabled.store(false, std::memory_order_relaxed);
        std::cerr << "v0.12 benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
