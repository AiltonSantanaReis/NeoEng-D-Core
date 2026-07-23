#include "neoeng/core/advanced_island_solver.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
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

Dataset make_mixed_dataset() {
    Dataset data;
    data.bodies = 10'000U;
    std::size_t cursor = 0U;
    for (std::size_t island = 0U; island < 1'000U; ++island) {
        add_edge(data.contacts, cursor, cursor + 1U);
        cursor += 2U;
    }
    for (std::size_t island = 0U; island < 100U; ++island) {
        for (std::size_t edge = 0U; edge < 19U; ++edge) {
            add_edge(data.contacts, cursor + edge, cursor + edge + 1U);
        }
        cursor += 20U;
    }
    for (std::size_t island = 0U; island < 100U; ++island) {
        for (std::size_t leaf = 1U; leaf < 10U; ++leaf) {
            add_edge(data.contacts, cursor, cursor + leaf);
        }
        cursor += 10U;
    }
    for (std::size_t island = 0U; island < 100U; ++island) {
        for (std::size_t edge = 0U; edge < 10U; ++edge) {
            add_edge(data.contacts, cursor + edge, cursor + ((edge + 1U) % 10U));
        }
        cursor += 10U;
    }
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
        value = (static_cast<std::int64_t>(
            (state * 2685821657736338717ULL) % 2'000'001ULL) - 1'000'000) * 4'096;
    }
    return data;
}

Dataset make_topology_dataset(std::string_view topology, std::size_t bodies) {
    Dataset data;
    data.bodies = bodies;
    if (topology == "chain") {
        for (std::size_t index = 0U; index + 1U < bodies; ++index) {
            add_edge(data.contacts, index, index + 1U);
        }
    } else if (topology == "cycle") {
        for (std::size_t index = 0U; index < bodies; ++index) {
            add_edge(data.contacts, index, (index + 1U) % bodies);
        }
    } else if (topology == "star") {
        for (std::size_t leaf = 1U; leaf < bodies; ++leaf) add_edge(data.contacts, 0U, leaf);
    } else {
        throw std::invalid_argument("Unknown topology dataset");
    }
    std::sort(data.contacts.begin(), data.contacts.end());
    data.values.resize(bodies);
    for (std::size_t index = 0U; index < bodies; ++index) {
        data.values[index] = static_cast<Fixed::rep>((bodies - index) * 8'192U);
    }
    if (topology == "star") data.values[0] = static_cast<Fixed::rep>(bodies * 16'384U);
    return data;
}

double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U))];
}

struct AllocationSample final { std::uint64_t count{}; std::uint64_t bytes{}; };

template <typename Function>
AllocationSample measure_allocations(Function&& function) {
    allocation_probe::allocations.store(0U, std::memory_order_relaxed);
    allocation_probe::bytes.store(0U, std::memory_order_relaxed);
    allocation_probe::enabled.store(true, std::memory_order_release);
    function();
    allocation_probe::enabled.store(false, std::memory_order_release);
    return {
        allocation_probe::allocations.load(std::memory_order_relaxed),
        allocation_probe::bytes.load(std::memory_order_relaxed),
    };
}

struct RuntimeRow final {
    std::string operation;
    std::size_t workers{};
    double p50_ms{};
    double p95_ms{};
    std::uint64_t hash{};
    std::uint64_t violations_after{};
    std::uint64_t allocations_max{};
    std::uint64_t bytes_max{};
};

RuntimeRow benchmark_serial(
    const Dataset& data,
    ContactIslandWorkspace& workspace,
    std::size_t workers,
    PersistentIslandWorkerPool* pool) {
    std::vector<Fixed::rep> values(data.values.size());
    std::vector<double> times;
    times.reserve(160U);
    std::uint64_t hash = 0U;
    std::uint64_t violations = 0U;
    std::uint64_t allocations_max = 0U;
    std::uint64_t bytes_max = 0U;
    for (std::size_t sample = 0U; sample < 160U; ++sample) {
        std::memcpy(values.data(), data.values.data(), values.size() * sizeof(values[0]));
        IslandProjectionStats stats;
        const auto begin = Clock::now();
        const AllocationSample allocation = measure_allocations([&] {
            stats = pool == nullptr
                ? project_contact_islands_monotone(values, data.contacts, workspace, 4U, workers)
                : project_contact_islands_monotone_pooled(
                    values, data.contacts, workspace, 4U, *pool, workers);
        });
        const auto end = Clock::now();
        if (sample >= 10U) {
            times.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        }
        hash = island_projection_hash(values, workspace);
        violations = stats.violations_after;
        allocations_max = std::max(allocations_max, allocation.count);
        bytes_max = std::max(bytes_max, allocation.bytes);
    }
    return RuntimeRow{
        .operation = pool == nullptr ? "ephemeral_threads" : "persistent_pool",
        .workers = workers,
        .p50_ms = percentile(times, 0.50),
        .p95_ms = percentile(times, 0.95),
        .hash = hash,
        .violations_after = violations,
        .allocations_max = allocations_max,
        .bytes_max = bytes_max,
    };
}

RuntimeRow benchmark_specialized(
    const Dataset& data,
    ContactIslandWorkspace& workspace,
    IslandSolverScratch& scratch,
    GeneralImpulseWarmStart* warm) {
    std::vector<Fixed::rep> values(data.values.size());
    std::vector<double> times;
    times.reserve(160U);
    std::uint64_t hash = 0U;
    std::uint64_t violations = 0U;
    std::uint64_t allocations_max = 0U;
    std::uint64_t bytes_max = 0U;
    if (warm != nullptr) {
        std::memcpy(values.data(), data.values.data(), values.size() * sizeof(values[0]));
        (void)project_contact_islands_specialized(
            values, data.contacts, workspace, 4U, scratch, warm);
    }
    for (std::size_t sample = 0U; sample < 160U; ++sample) {
        std::memcpy(values.data(), data.values.data(), values.size() * sizeof(values[0]));
        SpecializedIslandProjectionStats stats;
        const auto begin = Clock::now();
        const AllocationSample allocation = measure_allocations([&] {
            stats = project_contact_islands_specialized(
                values, data.contacts, workspace, 4U, scratch, warm);
        });
        const auto end = Clock::now();
        if (sample >= 10U) {
            times.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        }
        hash = island_projection_hash(values, workspace);
        violations = stats.violations_after;
        allocations_max = std::max(allocations_max, allocation.count);
        bytes_max = std::max(bytes_max, allocation.bytes);
    }
    return RuntimeRow{
        .operation = warm == nullptr ? "specialized_cold" : "specialized_warm",
        .workers = 1U,
        .p50_ms = percentile(times, 0.50),
        .p95_ms = percentile(times, 0.95),
        .hash = hash,
        .violations_after = violations,
        .allocations_max = allocations_max,
        .bytes_max = bytes_max,
    };
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "artifacts/v0.11-benchmark";
        std::filesystem::create_directories(output);
        Dataset data = make_mixed_dataset();
        ContactIslandWorkspace workspace(data.bodies, data.contacts.size());
        IslandSolverScratch scratch(data.bodies, data.contacts.size());
        GeneralImpulseWarmStart warm(data.contacts.size());
        PersistentIslandWorkerPool pool(8U);
        workspace.classify(data.bodies, data.contacts);

        std::uint64_t classify_allocations = 0U;
        std::uint64_t classify_bytes = 0U;
        std::vector<double> classify_times;
        classify_times.reserve(100U);
        for (std::size_t sample = 0U; sample < 100U; ++sample) {
            const auto begin = Clock::now();
            const AllocationSample allocation = measure_allocations([&] {
                workspace.classify(data.bodies, data.contacts);
            });
            const auto end = Clock::now();
            classify_times.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
            classify_allocations = std::max(classify_allocations, allocation.count);
            classify_bytes = std::max(classify_bytes, allocation.bytes);
        }

        std::vector<RuntimeRow> rows;
        rows.push_back(benchmark_serial(data, workspace, 1U, nullptr));
        rows.push_back(benchmark_serial(data, workspace, 2U, nullptr));
        rows.push_back(benchmark_serial(data, workspace, 4U, nullptr));
        rows.push_back(benchmark_serial(data, workspace, 2U, &pool));
        rows.push_back(benchmark_serial(data, workspace, 4U, &pool));
        rows.push_back(benchmark_serial(data, workspace, 8U, &pool));
        rows.push_back(benchmark_specialized(data, workspace, scratch, nullptr));
        rows.push_back(benchmark_specialized(data, workspace, scratch, &warm));

        std::ofstream runtime(output / "island_runtime.csv");
        runtime << "operation,workers,p50_ms,p95_ms,hash,violations_after,allocations_max,bytes_max\n";
        runtime << std::fixed << std::setprecision(6);
        for (const RuntimeRow& row : rows) {
            runtime << row.operation << ',' << row.workers << ',' << row.p50_ms << ','
                    << row.p95_ms << ",0x" << std::hex << std::uppercase << row.hash
                    << std::dec << ',' << row.violations_after << ',' << row.allocations_max
                    << ',' << row.bytes_max << '\n';
        }

        std::ofstream allocations(output / "allocation_gate.csv");
        allocations << "operation,allocations_max,bytes_max\n";
        allocations << "classification," << classify_allocations << ',' << classify_bytes << '\n';
        for (const RuntimeRow& row : rows) {
            allocations << row.operation << "_w" << row.workers << ','
                        << row.allocations_max << ',' << row.bytes_max << '\n';
        }

        std::ofstream topology(output / "topology_solver.csv");
        topology << "topology,bodies,general_p50_ms,general_p95_ms,specialized_p50_ms,specialized_p95_ms,general_violations,specialized_violations,specialized_hash\n";
        topology << std::fixed << std::setprecision(6);
        for (const std::string name : {"chain", "star", "cycle"}) {
            Dataset topology_data = make_topology_dataset(name, 1'000U);
            ContactIslandWorkspace topology_workspace(
                topology_data.bodies, topology_data.contacts.size());
            IslandSolverScratch topology_scratch(
                topology_data.bodies, topology_data.contacts.size());
            topology_workspace.classify(topology_data.bodies, topology_data.contacts);
            const RuntimeRow general = benchmark_serial(
                topology_data, topology_workspace, 1U, nullptr);
            const RuntimeRow specialized = benchmark_specialized(
                topology_data, topology_workspace, topology_scratch, nullptr);
            topology << name << ',' << topology_data.bodies << ','
                     << general.p50_ms << ',' << general.p95_ms << ','
                     << specialized.p50_ms << ',' << specialized.p95_ms << ','
                     << general.violations_after << ',' << specialized.violations_after
                     << ",0x" << std::hex << std::uppercase << specialized.hash << std::dec << '\n';
        }

        std::ofstream summary(output / "summary.json");
        summary << std::fixed << std::setprecision(6)
                << "{\n"
                << "  \"version\": \"0.11\",\n"
                << "  \"classify_p50_ms\": " << percentile(classify_times, 0.50) << ",\n"
                << "  \"classify_p95_ms\": " << percentile(classify_times, 0.95) << ",\n"
                << "  \"classify_allocations_max\": " << classify_allocations << ",\n"
                << "  \"workspace_bytes\": " << workspace.reserved_bytes() << ",\n"
                << "  \"solver_scratch_bytes\": " << scratch.reserved_bytes() << ",\n"
                << "  \"warm_cache_bytes\": " << warm.reserved_bytes() << ",\n"
                << "  \"worker_pool_bytes_excluding_os_stacks\": " << pool.reserved_bytes() << "\n"
                << "}\n";

        std::cout << "v0.11 benchmark rows=" << rows.size()
                  << " classify_allocations=" << classify_allocations
                  << " specialized_hash=0x" << std::hex << std::uppercase
                  << rows.back().hash << std::dec << '\n';
        return 0;
    } catch (const std::exception& error) {
        allocation_probe::enabled.store(false, std::memory_order_relaxed);
        std::cerr << "v0.11 benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
