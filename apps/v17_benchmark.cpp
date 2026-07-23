#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "neoeng/core/paged_atomic_temporal_physics.hpp"

#include <algorithm>
#include <array>
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

namespace allocation_probe {
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> cpp_allocations{0}, cpp_bytes{0}, c_allocations{0}, c_bytes{0};
void record_cpp(std::size_t size) noexcept { if (enabled.load(std::memory_order_relaxed)) {
    cpp_allocations.fetch_add(1U, std::memory_order_relaxed); cpp_bytes.fetch_add(size, std::memory_order_relaxed); }}
void record_c(std::size_t size) noexcept { if (enabled.load(std::memory_order_relaxed)) {
    c_allocations.fetch_add(1U, std::memory_order_relaxed); c_bytes.fetch_add(size, std::memory_order_relaxed); }}
}
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(std::size_t size) { allocation_probe::record_c(size); return __real_malloc(size == 0U ? 1U : size); }
extern "C" void* __wrap_calloc(std::size_t count, std::size_t size) { allocation_probe::record_c(count * size); return __real_calloc(count, size); }
extern "C" void* __wrap_realloc(void* pointer, std::size_t size) { allocation_probe::record_c(size); return __real_realloc(pointer, size); }
extern "C" void __wrap_free(void* pointer) { __real_free(pointer); }
void* operator new(std::size_t size) { if (void* pointer = __real_malloc(size == 0U ? 1U : size)) { allocation_probe::record_cpp(size); return pointer; } throw std::bad_alloc(); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { __real_free(pointer); }
void operator delete[](void* pointer) noexcept { __real_free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { __real_free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) { void* pointer = nullptr; if (posix_memalign(&pointer, static_cast<std::size_t>(alignment), size == 0U ? 1U : size) != 0) throw std::bad_alloc(); allocation_probe::record_cpp(size); return pointer; }
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* pointer, std::align_val_t) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { __real_free(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { __real_free(pointer); }

namespace {
using namespace neoeng::core;
using Clock = std::chrono::steady_clock;
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 4> kNormals{{
    {kOne, 0}, {0, kOne}, {759'250'125, 759'250'125}, {644'245'094, 858'993'459}
}};

Fixed::rep mul_q30(std::int32_t normal, Fixed::rep magnitude) {
    return static_cast<Fixed::rep>(static_cast<WideInteger>(normal) * magnitude / (WideInteger{1} << 30U));
}
double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U))];
}

struct Dataset final {
    std::vector<Fixed::rep> px, py, vx, vy;
    std::vector<std::uint32_t> masses;
    std::vector<NormalContact> contacts;
    bool tree{};
};

Dataset make_matching(std::size_t bodies) {
    Dataset data;
    data.px.resize(bodies); data.py.resize(bodies); data.vx.resize(bodies); data.vy.resize(bodies);
    data.masses.resize(bodies); data.contacts.reserve(bodies / 2U);
    const Fixed::rep separation = Fixed::from_ratio(3, 4).raw();
    const Fixed::rep speed = Fixed::from_ratio(1, 16).raw();
    constexpr std::size_t columns = 100U;
    for (std::size_t pair = 0U; pair < bodies / 2U; ++pair) {
        const std::size_t first = pair * 2U, second = first + 1U;
        const NormalQ30 normal = kNormals[pair % kNormals.size()];
        const Fixed::rep center_x = Fixed::from_integer(static_cast<Fixed::rep>((pair % columns) * 4U)).raw();
        const Fixed::rep center_y = Fixed::from_integer(static_cast<Fixed::rep>((pair / columns) * 4U)).raw();
        const Fixed::rep dx = mul_q30(normal.x, separation), dy = mul_q30(normal.y, separation);
        data.px[first] = center_x - dx / 2; data.py[first] = center_y - dy / 2;
        data.px[second] = center_x + dx / 2; data.py[second] = center_y + dy / 2;
        data.vx[first] = mul_q30(normal.x, speed); data.vy[first] = mul_q30(normal.y, speed);
        data.vx[second] = -data.vx[first]; data.vy[second] = -data.vy[first];
        data.masses[first] = 1U + static_cast<std::uint32_t>((first * 17U) % 64U);
        data.masses[second] = 1U + static_cast<std::uint32_t>((second * 17U) % 64U);
        data.contacts.push_back({first, second, normal});
    }
    return data;
}

Dataset make_tree(std::size_t bodies) {
    Dataset data;
    data.tree = true;
    data.px.resize(bodies); data.py.assign(bodies, 0); data.vx.resize(bodies); data.vy.assign(bodies, 0);
    data.masses.resize(bodies); data.contacts.reserve(bodies - 1U);
    const Fixed::rep spacing = Fixed::from_ratio(3, 4).raw();
    const Fixed::rep velocity_unit = Fixed::from_ratio(1, 65'536).raw();
    for (std::size_t body = 0U; body < bodies; ++body) {
        data.px[body] = static_cast<Fixed::rep>(static_cast<WideInteger>(spacing) * body);
        data.vx[body] = static_cast<Fixed::rep>(static_cast<WideInteger>(bodies - body) * velocity_unit);
        data.masses[body] = 1U + static_cast<std::uint32_t>((body * 13U) % 16U);
        if (body != 0U) data.contacts.push_back({body - 1U, body, {kOne, 0}});
    }
    return data;
}

AtomicTemporalPhysicsConfig physics_config(const Dataset& data, std::size_t history) {
    return AtomicTemporalPhysicsConfig{
        .bodies = data.vx.size(), .contacts = data.contacts.size(),
        .maximum_candidate_pairs = data.tree ? data.contacts.size() * 8U + 256U : data.contacts.size() * 2U + 256U,
        .history_capacity = history, .horizon_frames = 16U,
        .maximum_velocity_mutations = 2U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U, .half_extent = Fixed::from_ratio(1, 2),
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
        .force_rebuild_each_frame = false, .enable_single_tree_solver = data.tree,
    };
}

PagedAtomicTemporalConfig paged_config(const Dataset& data) {
    const auto physics = physics_config(data, 32U);
    return PagedAtomicTemporalConfig{
        .physics = physics,
        .history = PagedAtomicHistoryConfig{
            .bodies = physics.bodies, .contacts = physics.contacts,
            .maximum_candidate_pairs = physics.maximum_candidate_pairs,
            .history_capacity = 32U, .page_elements = data.tree ? 64U : 256U,
            .maximum_velocity_dirty_pages_per_frame = data.tree ? 8U : 4U,
            .maximum_mass_dirty_pages_per_frame = 2U,
            .maximum_contact_dirty_pages_per_frame = data.tree ? 8U : 4U,
            .full_velocity_generations = 4U, .full_contact_generations = 4U,
            .maximum_cache_generations = 6U,
        },
    };
}

struct Row final {
    std::string workload, mode;
    double p50{}, p95{};
    std::uint64_t cpp_allocations{}, c_allocations{}, hash{}, physical_hash{};
    std::uint64_t pages_copied{}, pages_shared{}, promotions{}, tree_certified{};
    std::size_t reserved{}, live_payload{};
};

Row measure_paged(const Dataset& data, const std::string& workload) {
    PagedAtomicTemporalPhysicsEngine rollback(paged_config(data)), clean(paged_config(data));
    AtomicTemporalPhysicsEngine full_clean(physics_config(data, 32U));
    std::array<VelocityMutation, 16> original{}, corrected{};
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        const std::size_t body = data.tree ? static_cast<std::size_t>((frame * 29U) % data.vx.size())
                                           : static_cast<std::size_t>((frame * 613U) % data.vx.size());
        original[frame - 1U] = {body, static_cast<Fixed::rep>(frame * 101U), -static_cast<Fixed::rep>(frame * 37U)};
        if (data.tree) original[frame - 1U].delta_y = 0;
        corrected[frame - 1U] = original[frame - 1U];
    }
    corrected[8].delta_x += 777;
    if (!data.tree) corrected[8].delta_y -= 313;
    constexpr std::size_t warmup = 4U, trials = 40U;
    std::vector<double> samples; samples.reserve(trials);
    Row row{workload, "paged_cow"};
    for (std::size_t trial = 0U; trial < warmup + trials; ++trial) {
        rollback.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        clean.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        full_clean.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
            const auto r = AtomicPhysicsFrameInput{.velocity = std::span<const VelocityMutation>(&original[frame - 1U], 1U)};
            const auto c = AtomicPhysicsFrameInput{.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)};
            rollback.set_input(frame, r); clean.set_input(frame, c); full_clean.set_input(frame, c);
        }
        rollback.simulate_to(16U); clean.simulate_to(16U); full_clean.simulate_to(16U);
        const auto before_stats = rollback.stats();
        allocation_probe::cpp_allocations.store(0U); allocation_probe::c_allocations.store(0U);
        const auto begin = Clock::now(); allocation_probe::enabled.store(true);
        rollback.correct_and_resimulate(9U,
            {.velocity = std::span<const VelocityMutation>(&corrected[8], 1U)}, 16U);
        allocation_probe::enabled.store(false); const auto end = Clock::now();
        if (!rollback.equivalent_to(clean) || !rollback.physically_equivalent_to(full_clean)) {
            throw std::runtime_error("v0.17 paged rollback diverged from authoritative execution");
        }
        if (trial >= warmup) samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        row.cpp_allocations = std::max(row.cpp_allocations, allocation_probe::cpp_allocations.load());
        row.c_allocations = std::max(row.c_allocations, allocation_probe::c_allocations.load());
        row.hash = rollback.hash(); row.physical_hash = rollback.physical_hash();
        row.reserved = rollback.reserved_bytes(); row.live_payload = rollback.history_live_payload_bytes();
        const auto stats = rollback.stats();
        row.pages_copied = stats.history.pages_copied - before_stats.history.pages_copied;
        row.pages_shared = stats.history.pages_shared - before_stats.history.pages_shared;
        row.promotions = stats.history.zero_copy_promotions - before_stats.history.zero_copy_promotions;
        row.tree_certified = stats.physics.tree_solver_certified - before_stats.physics.tree_solver_certified;

    }
    row.p50 = percentile(samples, .50); row.p95 = percentile(samples, .95);
    return row;
}

Row measure_full_memory(const Dataset& data, const std::string& workload) {
    AtomicTemporalPhysicsEngine engine(physics_config(data, 32U));
    engine.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
    std::array<VelocityMutation, 16> corrected{};
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        const std::size_t body = data.tree ? static_cast<std::size_t>((frame * 29U) % data.vx.size())
                                           : static_cast<std::size_t>((frame * 613U) % data.vx.size());
        corrected[frame - 1U] = {body, static_cast<Fixed::rep>(frame * 101U),
            data.tree ? 0 : -static_cast<Fixed::rep>(frame * 37U)};
    }
    corrected[8].delta_x += 777;
    if (!data.tree) corrected[8].delta_y -= 313;
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        engine.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
    }
    engine.simulate_to(16U);
    return Row{workload, "full_snapshot", 0.0, 0.0, 0U, 0U, engine.hash(), engine.physical_hash(),
        0U, 0U, 0U, engine.stats().tree_solver_certified, engine.reserved_bytes(), engine.reserved_bytes()};
}

void write_outputs(const std::filesystem::path& directory, const std::vector<Row>& rows) {
    std::filesystem::create_directories(directory);
    std::ofstream csv(directory / "paged_atomic_rollback.csv");
    csv << "workload,mode,p50_ms,p95_ms,cpp_allocations,c_allocations,reserved_bytes,live_payload_bytes,pages_copied,pages_shared,zero_copy_promotions,tree_certified,hash,physical_hash\n";
    for (const Row& row : rows) {
        csv << row.workload << ',' << row.mode << ',' << std::fixed << std::setprecision(6)
            << row.p50 << ',' << row.p95 << ',' << row.cpp_allocations << ',' << row.c_allocations << ','
            << row.reserved << ',' << row.live_payload << ',' << row.pages_copied << ',' << row.pages_shared
            << ',' << row.promotions << ',' << row.tree_certified << ",0x" << std::hex << std::uppercase
            << row.hash << ",0x" << row.physical_hash << std::dec << '\n';
    }
    std::ofstream json(directory / "summary.json");
    const Row& matching = rows[0]; const Row& matching_full = rows[1]; const Row& tree = rows[2];
    json << "{\n  \"version\": \"0.17\",\n  \"gate_ms\": 2.0,\n"
         << "  \"matching_p95_ms\": " << std::fixed << std::setprecision(6) << matching.p95 << ",\n"
         << "  \"tree_p95_ms\": " << tree.p95 << ",\n"
         << "  \"paged_reserved_bytes\": " << matching.reserved << ",\n"
         << "  \"full_reserved_bytes\": " << matching_full.reserved << ",\n"
         << "  \"memory_reduction_percent\": "
         << (100.0 * (1.0 - static_cast<double>(matching.reserved) / static_cast<double>(matching_full.reserved))) << ",\n"
         << "  \"matching_hash\": \"0x" << std::hex << std::uppercase << matching.hash << "\",\n"
         << "  \"tree_hash\": \"0x" << tree.hash << std::dec << "\"\n}\n";
}
}

int main(int argc, char** argv) {
    const std::filesystem::path directory = argc > 1 ? argv[1] : "artifacts/v0.17-benchmark";
    const Dataset matching = make_matching(10'000U);
    const Dataset tree = make_tree(512U);
    std::vector<Row> rows;
    rows.push_back(measure_paged(matching, "matching_10k_5k"));
    rows.push_back(measure_full_memory(matching, "matching_10k_5k"));
    rows.push_back(measure_paged(tree, "tree_512"));
    rows.push_back(measure_full_memory(tree, "tree_512"));
    write_outputs(directory, rows);
    for (const Row& row : rows) {
        std::cout << row.workload << ' ' << row.mode << " p95=" << row.p95
                  << " ms reserved=" << row.reserved << " live=" << row.live_payload
                  << " alloc=" << row.cpp_allocations << '/' << row.c_allocations
                  << " hash=0x" << std::hex << std::uppercase << row.hash << std::dec << '\n';
    }
    return EXIT_SUCCESS;
}
