#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "neoeng/core/axis_forest_projection.hpp"
#include "neoeng/core/paged_atomic_temporal_physics.hpp"
#include "neoeng/core/weighted_tree_projection.hpp"

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
std::atomic<std::uint64_t> cpp_allocations{0}, c_allocations{0};
void record_cpp() noexcept { if (enabled.load(std::memory_order_relaxed)) cpp_allocations.fetch_add(1U); }
void record_c() noexcept { if (enabled.load(std::memory_order_relaxed)) c_allocations.fetch_add(1U); }
}
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(std::size_t size) { allocation_probe::record_c(); return __real_malloc(size == 0U ? 1U : size); }
extern "C" void* __wrap_calloc(std::size_t count, std::size_t size) { allocation_probe::record_c(); return __real_calloc(count, size); }
extern "C" void* __wrap_realloc(void* pointer, std::size_t size) { allocation_probe::record_c(); return __real_realloc(pointer, size); }
extern "C" void __wrap_free(void* pointer) { __real_free(pointer); }
void* operator new(std::size_t size) { if (void* p = __real_malloc(size == 0U ? 1U : size)) { allocation_probe::record_cpp(); return p; } throw std::bad_alloc(); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { __real_free(p); }
void operator delete[](void* p) noexcept { __real_free(p); }
void operator delete(void* p, std::size_t) noexcept { __real_free(p); }
void operator delete[](void* p, std::size_t) noexcept { __real_free(p); }
void* operator new(std::size_t size, std::align_val_t alignment) { void* p = nullptr; if (posix_memalign(&p, static_cast<std::size_t>(alignment), size == 0U ? 1U : size) != 0) throw std::bad_alloc(); allocation_probe::record_cpp(); return p; }
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* p, std::align_val_t) noexcept { __real_free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { __real_free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { __real_free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { __real_free(p); }

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

std::uint64_t hash_vectors(std::span<const Fixed::rep> first, std::span<const Fixed::rep> second) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (unsigned byte = 0U; byte < 8U; ++byte) {
            hash ^= (value >> (byte * 8U)) & 0xFFU;
            hash *= 0x100000001B3ULL;
        }
    };
    for (Fixed::rep value : first) mix(static_cast<std::uint64_t>(value));
    for (Fixed::rep value : second) mix(static_cast<std::uint64_t>(value));
    return hash;
}

std::size_t depth_of(std::size_t body) {
    std::size_t depth = 0U;
    for (std::size_t cursor = body + 1U; cursor > 1U; cursor >>= 1U) ++depth;
    return depth;
}

struct Dataset final {
    std::vector<Fixed::rep> px, py, vx, vy;
    std::vector<std::uint32_t> masses;
    std::vector<NormalContact> contacts;
};

Dataset make_matching(std::size_t bodies, std::size_t active_pairs) {
    Dataset data;
    data.px.resize(bodies); data.py.resize(bodies); data.vx.resize(bodies); data.vy.resize(bodies);
    data.masses.resize(bodies); data.contacts.reserve(active_pairs);
    const Fixed::rep separation = Fixed::from_ratio(3, 4).raw();
    const Fixed::rep speed = Fixed::from_ratio(1, 16).raw();
    constexpr std::size_t columns = 100U;
    for (std::size_t body = 0U; body < bodies; ++body) {
        data.px[body] = Fixed::from_integer(static_cast<Fixed::rep>((body % columns) * 4U)).raw();
        data.py[body] = Fixed::from_integer(static_cast<Fixed::rep>((body / columns) * 4U)).raw();
        data.masses[body] = 1U + static_cast<std::uint32_t>((body * 17U) % 64U);
    }
    for (std::size_t pair = 0U; pair < active_pairs; ++pair) {
        const std::size_t first = pair * 2U, second = first + 1U;
        const NormalQ30 normal = kNormals[pair % kNormals.size()];
        const Fixed::rep center_x = Fixed::from_integer(static_cast<Fixed::rep>((pair % columns) * 4U)).raw();
        const Fixed::rep center_y = Fixed::from_integer(static_cast<Fixed::rep>((pair / columns) * 4U)).raw();
        const Fixed::rep dx = mul_q30(normal.x, separation), dy = mul_q30(normal.y, separation);
        data.px[first] = center_x - dx / 2; data.py[first] = center_y - dy / 2;
        data.px[second] = center_x + dx / 2; data.py[second] = center_y + dy / 2;
        data.vx[first] = mul_q30(normal.x, speed); data.vy[first] = mul_q30(normal.y, speed);
        data.vx[second] = -data.vx[first]; data.vy[second] = -data.vy[first];
        data.contacts.push_back({first, second, normal});
    }
    return data;
}

AtomicTemporalPhysicsConfig physics_config(const Dataset& data) {
    return AtomicTemporalPhysicsConfig{
        .bodies = data.vx.size(), .contacts = data.contacts.size(),
        .maximum_candidate_pairs = data.contacts.size() * 2U + 512U,
        .history_capacity = 32U, .horizon_frames = 16U,
        .maximum_velocity_mutations = 2U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U, .half_extent = Fixed::from_ratio(1, 2),
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
        .force_rebuild_each_frame = false,
    };
}

PagedAtomicTemporalConfig paged_config(const Dataset& data, bool sparse_positions) {
    const auto physics = physics_config(data);
    return PagedAtomicTemporalConfig{.physics = physics, .history = {
        .bodies = physics.bodies, .contacts = physics.contacts,
        .maximum_candidate_pairs = physics.maximum_candidate_pairs,
        .history_capacity = 32U, .page_elements = 256U,
        .maximum_position_dirty_pages_per_frame = sparse_positions ? 2U : 0U,
        .maximum_velocity_dirty_pages_per_frame = 4U,
        .maximum_mass_dirty_pages_per_frame = 2U,
        .maximum_contact_dirty_pages_per_frame = 4U,
        .full_position_generations = 2U, .full_velocity_generations = 4U,
        .full_contact_generations = 4U, .maximum_cache_generations = 6U}};
}

struct Row final {
    std::string workload, method;
    double p50_ms{}, p95_ms{};
    std::size_t reserved_bytes{}, live_payload_bytes{};
    std::uint64_t pages_copied{}, pages_shared{}, cpp_allocations{}, c_allocations{};
    std::uint64_t islands_expanded{}, island_bodies_added{}, certified{}, hash{};
};

Row measure_rollback(const Dataset& data, std::string workload, bool sparse_positions) {
    PagedAtomicTemporalPhysicsEngine rollback(paged_config(data, sparse_positions));
    PagedAtomicTemporalPhysicsEngine clean(paged_config(data, sparse_positions));
    std::array<VelocityMutation, 16> original{}, corrected{};
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        const std::size_t active_bodies = data.contacts.size() * 2U;
        const std::size_t body = static_cast<std::size_t>((frame * 17U) % active_bodies);
        original[frame - 1U] = {body, static_cast<Fixed::rep>(frame * 101U), -static_cast<Fixed::rep>(frame * 37U)};
        corrected[frame - 1U] = original[frame - 1U];
    }
    corrected[8].delta_x += 777; corrected[8].delta_y -= 313;
    constexpr std::size_t warmup = 8U, trials = 80U;
    std::vector<double> samples; samples.reserve(trials);
    Row row{std::move(workload), sparse_positions ? "dirty_position_pages" : "conservative_position_pages"};
    for (std::size_t trial = 0U; trial < warmup + trials; ++trial) {
        rollback.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        clean.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
            rollback.set_input(frame, {.velocity = std::span<const VelocityMutation>(&original[frame - 1U], 1U)});
            clean.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
        }
        rollback.simulate_to(16U); clean.simulate_to(16U);
        const auto before = rollback.stats();
        allocation_probe::cpp_allocations.store(0U); allocation_probe::c_allocations.store(0U);
        const auto begin = Clock::now(); allocation_probe::enabled.store(true);
        rollback.correct_and_resimulate(9U,
            {.velocity = std::span<const VelocityMutation>(&corrected[8], 1U)}, 16U);
        allocation_probe::enabled.store(false); const auto end = Clock::now();
        if (!rollback.equivalent_to(clean)) throw std::runtime_error("v0.18 rollback divergence");
        if (trial >= warmup) samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        row.cpp_allocations = std::max(row.cpp_allocations, allocation_probe::cpp_allocations.load());
        row.c_allocations = std::max(row.c_allocations, allocation_probe::c_allocations.load());
        const auto after = rollback.stats();
        row.pages_copied = after.history.pages_copied - before.history.pages_copied;
        row.pages_shared = after.history.pages_shared - before.history.pages_shared;
        row.islands_expanded = after.physics.islands_expanded - before.physics.islands_expanded;
        row.island_bodies_added = after.physics.island_bodies_added - before.physics.island_bodies_added;
        row.reserved_bytes = rollback.reserved_bytes(); row.live_payload_bytes = rollback.history_live_payload_bytes();
        row.hash = rollback.hash(); row.certified = 1U;
    }
    row.p50_ms = percentile(samples, .50); row.p95_ms = percentile(samples, .95);
    return row;
}

Row measure_common_tree(std::size_t bodies) {
    std::vector<Fixed::rep> vx(bodies), vy(bodies);
    std::vector<std::uint32_t> masses(bodies);
    std::vector<DirectedTreeEdge> edges; edges.reserve(bodies - 1U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        vx[body] = static_cast<Fixed::rep>((bodies - body) * 10'003U);
        masses[body] = 1U + static_cast<std::uint32_t>((body * 13U) % 31U);
        if (body != 0U) edges.push_back({(body - 1U) / 2U, body});
    }
    const auto source_x = vx; const auto source_y = vy;
    WeightedTreeScratch scratch(bodies);
    constexpr std::size_t warmup = 8U, trials = 80U;
    std::vector<double> samples; samples.reserve(trials);
    Row row{"branched_tree_2047", "common_normal_active_set"};
    for (std::size_t trial = 0U; trial < warmup + trials; ++trial) {
        vx = source_x; vy = source_y;
        const auto begin = Clock::now();
        const auto stats = project_weighted_tree_common_normal_inplace(vx, vy, masses, edges,
            {kOne, 0}, {.maximum_active_set_iterations = 8192U,
                .feasibility_tolerance_raw = 4U, .stationarity_tolerance_raw = 16U}, scratch);
        const auto end = Clock::now();
        if (!stats.residuals.certified) throw std::runtime_error("branched tree was not certified");
        if (trial >= warmup) samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        row.certified = stats.residuals.certified; row.hash = hash_vectors(vx, vy);
    }
    row.p50_ms = percentile(samples, .50); row.p95_ms = percentile(samples, .95);
    row.reserved_bytes = scratch.reserved_bytes();
    return row;
}

Row measure_mixed_axis_tree(std::size_t bodies) {
    std::vector<Fixed::rep> vx(bodies), vy(bodies);
    std::vector<std::uint32_t> masses(bodies);
    std::vector<NormalContact> contacts; contacts.reserve(bodies - 1U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        vx[body] = static_cast<Fixed::rep>((bodies - body) * 11'003U);
        vy[body] = static_cast<Fixed::rep>(((body * 19U) % bodies) * 7'019U);
        masses[body] = 1U + static_cast<std::uint32_t>((body * 11U) % 29U);
        if (body == 0U) continue;
        const std::size_t parent = (body - 1U) / 2U;
        contacts.push_back({parent, body, depth_of(parent) % 2U == 0U
            ? NormalQ30{kOne, 0} : NormalQ30{0, kOne}});
    }
    const auto source_x = vx; const auto source_y = vy;
    AxisForestScratch scratch(bodies, contacts.size());
    constexpr std::size_t warmup = 8U, trials = 80U;
    std::vector<double> samples; samples.reserve(trials);
    Row row{"mixed_axis_tree_2047", "axis_forest_kkt"};
    for (std::size_t trial = 0U; trial < warmup + trials; ++trial) {
        vx = source_x; vy = source_y;
        const auto begin = Clock::now();
        const auto stats = project_axis_forest_inplace(vx, vy, masses, contacts,
            {.tree = {.maximum_active_set_iterations = 4096U,
                .feasibility_tolerance_raw = 4U, .stationarity_tolerance_raw = 16U}}, scratch);
        const auto end = Clock::now();
        if (!stats.certified) throw std::runtime_error("mixed-axis tree was not certified");
        if (trial >= warmup) samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        row.certified = stats.certified; row.hash = hash_vectors(vx, vy);
    }
    row.p50_ms = percentile(samples, .50); row.p95_ms = percentile(samples, .95);
    row.reserved_bytes = scratch.reserved_bytes();
    return row;
}

void write_outputs(const std::filesystem::path& directory, const std::vector<Row>& rows) {
    std::filesystem::create_directories(directory);
    std::ofstream csv(directory / "v18_cycle.csv");
    csv << "workload,method,p50_ms,p95_ms,reserved_bytes,live_payload_bytes,pages_copied,pages_shared,cpp_allocations,c_allocations,islands_expanded,island_bodies_added,certified,hash\n";
    for (const Row& row : rows) {
        csv << row.workload << ',' << row.method << ',' << std::fixed << std::setprecision(6)
            << row.p50_ms << ',' << row.p95_ms << ',' << row.reserved_bytes << ',' << row.live_payload_bytes
            << ',' << row.pages_copied << ',' << row.pages_shared << ',' << row.cpp_allocations << ','
            << row.c_allocations << ',' << row.islands_expanded << ',' << row.island_bodies_added << ','
            << row.certified << ",0x" << std::hex << std::uppercase << row.hash << std::dec << '\n';
    }
    const Row& sparse = rows[0]; const Row& conservative = rows[1];
    std::ofstream json(directory / "summary.json");
    json << "{\n  \"version\": \"0.18\",\n  \"gate_ms\": 2.0,\n"
         << "  \"sparse_islands_p95_ms\": " << std::fixed << std::setprecision(6) << sparse.p95_ms << ",\n"
         << "  \"sparse_reserved_bytes\": " << sparse.reserved_bytes << ",\n"
         << "  \"conservative_reserved_bytes\": " << conservative.reserved_bytes << ",\n"
         << "  \"position_memory_reduction_percent\": "
         << 100.0 * (1.0 - static_cast<double>(sparse.reserved_bytes) / static_cast<double>(conservative.reserved_bytes)) << ",\n"
         << "  \"common_tree_p95_ms\": " << rows[3].p95_ms << ",\n"
         << "  \"mixed_axis_tree_p95_ms\": " << rows[4].p95_ms << "\n}\n";
}
} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path directory = argc > 1 ? argv[1] : "artifacts/v0.18-benchmark";
    const Dataset sparse = make_matching(10'000U, 64U);
    const Dataset dense = make_matching(10'000U, 5'000U);
    std::vector<Row> rows;
    rows.push_back(measure_rollback(sparse, "sparse_islands_10k_64", true));
    rows.push_back(measure_rollback(sparse, "sparse_islands_10k_64", false));
    rows.push_back(measure_rollback(dense, "dense_matching_10k_5k", false));
    rows.push_back(measure_common_tree(2'047U));
    rows.push_back(measure_mixed_axis_tree(2'047U));
    write_outputs(directory, rows);
    for (const Row& row : rows) {
        std::cout << row.workload << ' ' << row.method << " p95=" << row.p95_ms
                  << " ms reserved=" << row.reserved_bytes << " copied=" << row.pages_copied
                  << " shared=" << row.pages_shared << " certified=" << row.certified
                  << " hash=0x" << std::hex << std::uppercase << row.hash << std::dec << '\n';
    }
    return EXIT_SUCCESS;
}
