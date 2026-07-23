#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/hash.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
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
        allocation_probe::record(size); return pointer;
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
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* pointer, std::align_val_t) noexcept { probe_free_aligned(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { probe_free_aligned(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { probe_free_aligned(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { probe_free_aligned(pointer); }

namespace {
using namespace neoeng::core;
using Clock = std::chrono::steady_clock;
constexpr std::int32_t kOne = 1 << 30;
constexpr NormalQ30 kNormals[]{{kOne, 0}, {0, kOne}, {644'245'094, 858'993'459}, {759'250'125, 759'250'125}};

struct Dataset final {
    WorldState world;
    std::vector<std::uint32_t> masses;
    std::vector<NormalContact> contacts;
};

Dataset make_matching(std::size_t bodies) {
    Dataset data;
    data.world.bodies.reserve(bodies);
    data.masses.resize(bodies);
    data.contacts.reserve(bodies / 2U);
    for (std::size_t i = 0U; i < bodies; ++i) {
        const bool first = (i & 1U) == 0U;
        data.world.bodies.push_back({
            .id = static_cast<EntityId>(i + 1U), .position = {},
            .velocity = {Fixed::from_raw(first ? 8'000'000 : -6'000'000),
                         Fixed::from_raw(first ? 5'000'000 : -4'000'000)},
        });
        data.masses[i] = 1U + static_cast<std::uint32_t>((i * 17U) % 64U);
    }
    for (std::size_t i = 0U; i < bodies; i += 2U) {
        data.contacts.push_back({i, i + 1U, kNormals[(i / 2U) % 4U]});
    }
    return data;
}

Dataset make_connected(std::size_t bodies) {
    Dataset data;
    data.world.bodies.reserve(bodies);
    data.masses.resize(bodies);
    data.contacts.reserve(bodies - 1U);
    for (std::size_t i = 0U; i < bodies; ++i) {
        data.world.bodies.push_back({
            .id = static_cast<EntityId>(i + 1U), .position = {},
            .velocity = {Fixed::from_raw(static_cast<Fixed::rep>((bodies - i) * 10'000U)),
                         Fixed::from_raw(static_cast<Fixed::rep>((i % 7U) * 3'000U))},
        });
        data.masses[i] = 1U + static_cast<std::uint32_t>(i % 16U);
    }
    for (std::size_t i = 0U; i + 1U < bodies; ++i) {
        data.contacts.push_back({i, i + 1U, kNormals[i % 4U]});
    }
    return data;
}

double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U))];
}

struct AllocationSample { std::uint64_t count{}; std::uint64_t bytes{}; };
template <class Function> AllocationSample measured_allocations(Function&& function) {
    allocation_probe::allocations.store(0U); allocation_probe::bytes.store(0U);
    allocation_probe::enabled.store(true); function(); allocation_probe::enabled.store(false);
    return {allocation_probe::allocations.load(), allocation_probe::bytes.load()};
}

struct Row final {
    std::string dataset;
    std::string path;
    double p50_ms{};
    double p95_ms{};
    std::uint64_t allocations{};
    std::uint64_t bytes{};
    std::uint64_t primal{};
    bool certified{};
    std::uint64_t hash{};
};

Row measure(const std::string& name, const Dataset& data, bool full_pipeline) {
    const ComponentWorldState component = make_component_world(data.world, 256U);
    ArbitraryNormalScratch scratch(data.world.bodies.size(), data.contacts.size());
    std::vector<Fixed::rep> base_x(data.world.bodies.size()), base_y(data.world.bodies.size());
    for (std::size_t i = 0; i < data.world.bodies.size(); ++i) {
        base_x[i] = component.velocity_x_at(i).raw(); base_y[i] = component.velocity_y_at(i).raw();
    }
    constexpr std::size_t warmup = 8U;
    constexpr std::size_t trials = 80U;
    std::vector<double> samples; samples.reserve(trials);
    ArbitraryNormalStats last_stats{};
    std::uint64_t last_hash{};
    std::uint64_t allocation_max{};
    std::uint64_t bytes_max{};
    for (std::size_t trial = 0U; trial < warmup + trials; ++trial) {
        std::copy(base_x.begin(), base_x.end(), scratch.velocity_x_.begin());
        std::copy(base_y.begin(), base_y.end(), scratch.velocity_y_.begin());
        const auto begin = Clock::now();
        AllocationSample alloc{};
        if (full_pipeline) {
            ArbitraryNormalProjectionResult result;
            alloc = measured_allocations([&] {
                result = project_arbitrary_normal_contacts_2d(
                    component, data.masses, data.contacts,
                    {.maximum_iterations = 128U, .feasibility_tolerance_raw = 32U}, scratch);
            });
            last_stats = result.stats;
            last_hash = stable_hash(result.state.materialize());
        } else {
            alloc = measured_allocations([&] {
                last_stats = project_arbitrary_normals_inplace(
                    std::span<Fixed::rep>(scratch.velocity_x_.data(), base_x.size()),
                    std::span<Fixed::rep>(scratch.velocity_y_.data(), base_y.size()),
                    data.masses, data.contacts,
                    {.maximum_iterations = 128U, .feasibility_tolerance_raw = 32U}, scratch);
            });
            std::uint64_t hash = 0xCBF29CE484222325ULL;
            for (std::size_t i = 0; i < base_x.size(); ++i) {
                hash ^= static_cast<std::uint64_t>(scratch.velocity_x_[i]); hash *= 0x100000001B3ULL;
                hash ^= static_cast<std::uint64_t>(scratch.velocity_y_[i]); hash *= 0x100000001B3ULL;
            }
            last_hash = hash;
        }
        const auto end = Clock::now();
        if (trial >= warmup) samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        allocation_max = std::max(allocation_max, alloc.count);
        bytes_max = std::max(bytes_max, alloc.bytes);
    }
    return {name, full_pipeline ? "persistent_pipeline" : "allocation_free_kernel",
            percentile(samples, 0.50), percentile(samples, 0.95), allocation_max, bytes_max,
            last_stats.residuals.primal_linf_raw, last_stats.residuals.certified, last_hash};
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "artifacts/v0.14-benchmark";
        std::filesystem::create_directories(output);
        const Dataset matching = make_matching(10'000U);
        const Dataset connected = make_connected(1'000U);
        std::vector<Row> rows;
        rows.push_back(measure("arbitrary_matching_10000_5000", matching, false));
        rows.push_back(measure("arbitrary_matching_10000_5000", matching, true));
        rows.push_back(measure("connected_chain_1000", connected, false));
        rows.push_back(measure("connected_chain_1000", connected, true));

        std::ofstream csv(output / "arbitrary_normal_physics.csv");
        csv << "dataset,path,p50_ms,p95_ms,allocations_max,bytes_max,primal_raw,certified,hash\n";
        csv << std::fixed << std::setprecision(6);
        for (const Row& row : rows) {
            csv << row.dataset << ',' << row.path << ',' << row.p50_ms << ',' << row.p95_ms
                << ',' << row.allocations << ',' << row.bytes << ',' << row.primal
                << ',' << (row.certified ? 1 : 0) << ",0x" << std::hex << std::uppercase
                << row.hash << std::dec << '\n';
            std::cout << row.dataset << ' ' << row.path << " p95=" << row.p95_ms
                      << " ms allocations=" << row.allocations << " certified=" << row.certified
                      << " hash=0x" << std::hex << std::uppercase << row.hash << std::dec << '\n';
        }
        std::ofstream summary(output / "summary.json");
        summary << "{\n  \"version\": \"0.14\",\n"
                << "  \"matching_kernel_p95_ms\": " << rows[0].p95_ms << ",\n"
                << "  \"matching_pipeline_p95_ms\": " << rows[1].p95_ms << ",\n"
                << "  \"kernel_allocations\": " << rows[0].allocations << ",\n"
                << "  \"pipeline_allocations\": " << rows[1].allocations << ",\n"
                << "  \"matching_gate_passed\": " << (rows[1].p95_ms <= 2.0 ? "true" : "false") << "\n}\n";
        return 0;
    } catch (const std::exception& error) {
        allocation_probe::enabled.store(false);
        std::cerr << "v0.14 benchmark failure: " << error.what() << '\n';
        return 1;
    }
}
