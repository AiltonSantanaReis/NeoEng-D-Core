#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "neoeng/core/island_pair_cache.hpp"
#include "neoeng/core/paged_atomic_temporal_physics.hpp"
#include "neoeng/core/small_oblique_grid_oracle.hpp"
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
#include <numeric>
#include <random>
#include <span>
#include <string>
#include <sys/resource.h>
#include <vector>
#if defined(__linux__)
#include <sched.h>
#endif

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

bool pin_to_first_allowed_cpu() noexcept {
#if defined(__linux__)
    cpu_set_t allowed;
    CPU_ZERO(&allowed);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) return false;
    std::size_t selected = static_cast<std::size_t>(CPU_SETSIZE);
    for (std::size_t cpu = 0U; cpu < static_cast<std::size_t>(CPU_SETSIZE); ++cpu) {
        if (CPU_ISSET(cpu, &allowed)) { selected = cpu; break; }
    }
    if (selected == static_cast<std::size_t>(CPU_SETSIZE)) return false;
    cpu_set_t target;
    CPU_ZERO(&target);
    CPU_SET(selected, &target);
    return sched_setaffinity(0, sizeof(target), &target) == 0;
#else
    return false;
#endif
}

std::size_t peak_rss_kib() noexcept {
    rusage usage{};
    return getrusage(RUSAGE_SELF, &usage) == 0 ? static_cast<std::size_t>(usage.ru_maxrss) : 0U;
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

PagedAtomicTemporalConfig paged_config(const Dataset& data, std::size_t page_elements = 256U) {
    const AtomicTemporalPhysicsConfig physics{
        .bodies = data.vx.size(), .contacts = data.contacts.size(),
        .maximum_candidate_pairs = data.contacts.size() * 2U + 512U,
        .history_capacity = 32U, .horizon_frames = 16U,
        .maximum_velocity_mutations = 2U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U, .half_extent = Fixed::from_ratio(1, 2),
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
        .force_rebuild_each_frame = false,
    };
    return PagedAtomicTemporalConfig{.physics = physics, .history = {
        .bodies = physics.bodies, .contacts = physics.contacts,
        .maximum_candidate_pairs = physics.maximum_candidate_pairs,
        .history_capacity = 32U, .page_elements = page_elements,
        .maximum_position_dirty_pages_per_frame = 0U,
        .maximum_velocity_dirty_pages_per_frame = 4U,
        .maximum_mass_dirty_pages_per_frame = 2U,
        .maximum_contact_dirty_pages_per_frame = 4U,
        .full_position_generations = 2U, .full_velocity_generations = 4U,
        .full_contact_generations = 4U, .maximum_cache_generations = 6U}};
}

struct RollbackResult final {
    double p50_ms{}, p95_ms{}, maximum_batch_p95_ms{};
    std::size_t reserved_bytes{}, live_payload_bytes{}, peak_rss{};
    std::uint64_t cpp_allocations{}, c_allocations{}, hash{};
};

RollbackResult measure_dense_rollback(const Dataset& data, std::size_t page_elements) {
    PagedAtomicTemporalPhysicsEngine rollback(paged_config(data, page_elements));
    PagedAtomicTemporalPhysicsEngine clean(paged_config(data, page_elements));
    std::array<VelocityMutation, 16> original{}, corrected{};
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        const std::size_t body = static_cast<std::size_t>((frame * 17U) % (data.contacts.size() * 2U));
        original[frame - 1U] = {body, static_cast<Fixed::rep>(frame * 101U), -static_cast<Fixed::rep>(frame * 37U)};
        corrected[frame - 1U] = original[frame - 1U];
    }
    corrected[8].delta_x += 777; corrected[8].delta_y -= 313;
    constexpr std::size_t warmup = 12U, trials = 160U, batch = 20U;
    std::vector<double> samples; samples.reserve(trials);
    RollbackResult result{};
    for (std::size_t trial = 0U; trial < warmup + trials; ++trial) {
        rollback.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        clean.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
            rollback.set_input(frame, {.velocity = std::span<const VelocityMutation>(&original[frame - 1U], 1U)});
            clean.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
        }
        rollback.simulate_to(16U); clean.simulate_to(16U);
        allocation_probe::cpp_allocations.store(0U); allocation_probe::c_allocations.store(0U);
        const auto begin = Clock::now(); allocation_probe::enabled.store(true);
        rollback.correct_and_resimulate(9U,
            {.velocity = std::span<const VelocityMutation>(&corrected[8], 1U)}, 16U);
        allocation_probe::enabled.store(false); const auto end = Clock::now();
        if (!rollback.equivalent_to(clean)) throw std::runtime_error("v0.19 direct restore rollback divergence");
        if (trial >= warmup) samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        result.cpp_allocations = std::max(result.cpp_allocations, allocation_probe::cpp_allocations.load());
        result.c_allocations = std::max(result.c_allocations, allocation_probe::c_allocations.load());
        result.reserved_bytes = rollback.reserved_bytes();
        result.live_payload_bytes = rollback.history_live_payload_bytes();
        result.hash = rollback.hash();
    }
    result.p50_ms = percentile(samples, .50);
    result.p95_ms = percentile(samples, .95);
    for (std::size_t offset = 0U; offset < samples.size(); offset += batch) {
        std::vector<double> portion(samples.begin() + static_cast<std::ptrdiff_t>(offset),
            samples.begin() + static_cast<std::ptrdiff_t>(std::min(samples.size(), offset + batch)));
        result.maximum_batch_p95_ms = std::max(result.maximum_batch_p95_ms, percentile(portion, .95));
    }
    result.peak_rss = peak_rss_kib();
    return result;
}

struct IslandCacheResult final {
    double update_p50_us{}, update_p95_us{}, global_scan_p50_us{}, global_scan_p95_us{};
    std::size_t reserved_bytes{}, islands{};
    std::uint64_t allocations{}, hash{}, checksum{};
};

IslandCacheResult measure_island_cache(const Dataset& data) {
    IslandPairCache cache({.bodies = data.vx.size(), .maximum_contacts = data.contacts.size(),
                           .extra_pairs_per_island = 1U});
    cache.initialize(data.contacts);
    std::vector<BroadphasePair> global;
    global.reserve(data.contacts.size());
    for (const NormalContact& contact : data.contacts) global.push_back({contact.first, contact.second});
    constexpr std::size_t warmup = 100U, trials = 5'000U;
    std::vector<double> update_samples, scan_samples;
    update_samples.reserve(trials); scan_samples.reserve(trials);
    std::uint64_t checksum = 0U;
    allocation_probe::cpp_allocations.store(0U); allocation_probe::c_allocations.store(0U);
    allocation_probe::enabled.store(true);
    for (std::size_t trial = 0U; trial < warmup + trials; ++trial) {
        const std::size_t pair_index = trial % global.size();
        const BroadphasePair pair = global[pair_index];
        const auto update_begin = Clock::now();
        cache.replace_pairs_for_body(pair.first, std::span<const BroadphasePair>(&pair, 1U));
        const auto update_end = Clock::now();
        const std::size_t target_island = cache.island_of_body(pair.first);
        const auto scan_begin = Clock::now();
        for (const BroadphasePair& item : global) {
            if (cache.island_of_body(item.first) == target_island) checksum += item.first + item.second;
        }
        const auto scan_end = Clock::now();
        if (trial >= warmup) {
            update_samples.push_back(std::chrono::duration<double, std::micro>(update_end - update_begin).count());
            scan_samples.push_back(std::chrono::duration<double, std::micro>(scan_end - scan_begin).count());
        }
    }
    allocation_probe::enabled.store(false);
    IslandCacheResult result{};
    result.update_p50_us = percentile(update_samples, .50);
    result.update_p95_us = percentile(update_samples, .95);
    result.global_scan_p50_us = percentile(scan_samples, .50);
    result.global_scan_p95_us = percentile(scan_samples, .95);
    result.reserved_bytes = cache.reserved_bytes();
    result.islands = cache.island_count();
    result.allocations = allocation_probe::cpp_allocations.load() + allocation_probe::c_allocations.load();
    result.hash = cache.hash(); result.checksum = checksum;
    return result;
}

struct WarmResult final {
    double cold_p95_us{}, warm_p95_us{};
    std::size_t accepted{}, rejected{};
    std::uint64_t hash{};
};

std::uint64_t hash_velocity(std::span<const Fixed::rep> vx, std::span<const Fixed::rep> vy) noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (unsigned byte = 0U; byte < 8U; ++byte) { hash ^= (value >> (byte * 8U)) & 0xFFU; hash *= 0x100000001B3ULL; }
    };
    for (Fixed::rep value : vx) mix(static_cast<std::uint64_t>(value));
    for (Fixed::rep value : vy) mix(static_cast<std::uint64_t>(value));
    return hash;
}

WarmResult measure_bit_exact_warm_tree() {
    constexpr std::size_t bodies = 2'047U;
    std::vector<Fixed::rep> source_x(bodies), source_y(bodies), cold_x(bodies), cold_y(bodies), warm_x(bodies), warm_y(bodies);
    std::vector<std::uint32_t> masses(bodies);
    std::vector<DirectedTreeEdge> edges; edges.reserve(bodies - 1U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        source_x[body] = static_cast<Fixed::rep>((bodies - body) * 10'003U);
        source_y[body] = static_cast<Fixed::rep>((body * 37U) % 10'000U);
        masses[body] = 1U + static_cast<std::uint32_t>((body * 13U) % 31U);
        if (body != 0U) edges.push_back({(body - 1U) / 2U, body});
    }
    WeightedTreeScratch cold_scratch(bodies), warm_scratch(bodies);
    std::vector<double> cold_samples, warm_samples;
    cold_samples.reserve(128U); warm_samples.reserve(128U);
    WarmResult result{};
    for (std::size_t frame = 0U; frame < 136U; ++frame) {
        source_x[bodies - 1U - (frame % 64U)] += static_cast<Fixed::rep>(
            static_cast<std::int64_t>(frame % 7U) - 3);
        cold_x = source_x; cold_y = source_y;
        warm_x = source_x; warm_y = source_y;
        const auto cold_begin = Clock::now();
        const WeightedTreeStats cold = project_weighted_tree_common_normal_inplace(
            cold_x, cold_y, masses, edges, {kOne, 0},
            {.maximum_active_set_iterations = 8192U, .feasibility_tolerance_raw = 4U,
             .stationarity_tolerance_raw = 16U, .use_warm_start = false}, cold_scratch);
        const auto cold_end = Clock::now();
        const auto warm_begin = Clock::now();
        const WeightedTreeStats warm = project_weighted_tree_common_normal_inplace(
            warm_x, warm_y, masses, edges, {kOne, 0},
            {.maximum_active_set_iterations = 8192U, .feasibility_tolerance_raw = 4U,
             .stationarity_tolerance_raw = 16U, .use_warm_start = true}, warm_scratch);
        const auto warm_end = Clock::now();
        if (!cold.residuals.certified || !warm.residuals.certified) {
            throw std::runtime_error("v0.19 warm tree was not certified");
        }
        if (cold_x == warm_x && cold_y == warm_y) ++result.accepted;
        else ++result.rejected;
        if (frame >= 8U) {
            cold_samples.push_back(std::chrono::duration<double, std::micro>(cold_end - cold_begin).count());
            warm_samples.push_back(std::chrono::duration<double, std::micro>(warm_end - warm_begin).count());
        }
        result.hash = hash_velocity(cold_x, cold_y);
    }
    result.cold_p95_us = percentile(cold_samples, .95);
    result.warm_p95_us = percentile(warm_samples, .95);
    return result;
}

std::uint64_t finite_objective(std::span<const Fixed::rep> vx, std::span<const Fixed::rep> vy,
    std::span<const Fixed::rep> input_x, std::span<const Fixed::rep> input_y,
    std::span<const std::uint32_t> masses) {
    WideInteger total = 0;
    for (std::size_t body = 0U; body < vx.size(); ++body) {
        const WideInteger dx = static_cast<WideInteger>(vx[body]) - input_x[body];
        const WideInteger dy = static_cast<WideInteger>(vy[body]) - input_y[body];
        total += static_cast<WideInteger>(masses[body]) * (dx * dx + dy * dy);
    }
    return static_cast<std::uint64_t>(total);
}

struct OracleResult final {
    std::size_t cases{}, exact{}, outside_grid{}, fallback_feasible{}, fallback_infeasible{};
    std::uint64_t maximum_gap{}, candidates{}, hash{};
};

OracleResult measure_oblique_oracle() {
    std::mt19937_64 rng(0x4E454F5631394F52ULL);
    constexpr std::array<NormalQ30, 3> normals{{
        {759'250'125, 759'250'125}, {644'245'094, 858'993'459}, {858'993'459, -644'245'094}
    }};
    OracleResult result{};
    ArbitraryNormalScratch scratch(3U, 2U);
    for (std::size_t case_index = 0U; case_index < 128U; ++case_index) {
        std::array<Fixed::rep, 3> input_x{}, input_y{}, solved_x{}, solved_y{};
        std::array<std::uint32_t, 3> masses{};
        for (std::size_t body = 0U; body < 3U; ++body) {
            input_x[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 5U) - 2);
            input_y[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 5U) - 2);
            masses[body] = 1U + static_cast<std::uint32_t>(rng() % 3U);
        }
        solved_x = input_x; solved_y = input_y;
        const std::array<NormalContact, 2> contacts{{
            {0U, 1U, normals[case_index % normals.size()]},
            {1U, 2U, normals[(case_index + 1U) % normals.size()]},
        }};
        const ObliqueGridOracleResult oracle = solve_small_oblique_grid_oracle(
            input_x, input_y, masses, contacts, {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = 3U});
        if (!oracle.feasible) throw std::runtime_error("v0.19 finite-grid oracle found no feasible state");
        const ArbitraryNormalStats fallback = project_arbitrary_normals_inplace(
            solved_x, solved_y, masses, contacts,
            {.maximum_iterations = 4096U, .feasibility_tolerance_raw = 0U,
             .unit_norm_tolerance_q60 = 1ULL << 42U}, scratch);
        if (!fallback.residuals.feasible) {
            ++result.fallback_infeasible;
        } else {
            ++result.fallback_feasible;
            const bool in_grid = std::all_of(solved_x.begin(), solved_x.end(), [](Fixed::rep v) { return v >= -2 && v <= 2; })
                && std::all_of(solved_y.begin(), solved_y.end(), [](Fixed::rep v) { return v >= -2 && v <= 2; });
            if (!in_grid) ++result.outside_grid;
            else {
                const std::uint64_t solved_objective = finite_objective(solved_x, solved_y, input_x, input_y, masses);
                if (solved_objective < oracle.objective) throw std::runtime_error("v0.19 oracle objective lower-bound violation");
                const std::uint64_t gap = solved_objective - oracle.objective;
                result.maximum_gap = std::max(result.maximum_gap, gap);
                if (gap == 0U) ++result.exact;
            }
        }
        result.candidates += oracle.candidates_tested;
        result.hash ^= hash_velocity(oracle.velocity_x, oracle.velocity_y) + 0x9E3779B97F4A7C15ULL
            + (result.hash << 6U) + (result.hash >> 2U);
        ++result.cases;
    }
    return result;
}

void write_outputs(const std::filesystem::path& directory, const RollbackResult& rollback,
    const IslandCacheResult& island, const WarmResult& warm, const OracleResult& oracle,
    bool affinity_pinned) {
    std::filesystem::create_directories(directory);
    std::ofstream csv(directory / "v19_cycle.csv");
    csv << "workload,method,p50,p95,max_batch_p95,reserved_bytes,live_payload_bytes,cpp_allocations,c_allocations,hash\n";
    csv << "dense_matching_10k_5k,direct_page_restore_ms," << std::fixed << std::setprecision(6)
        << rollback.p50_ms << ',' << rollback.p95_ms << ',' << rollback.maximum_batch_p95_ms << ','
        << rollback.reserved_bytes << ',' << rollback.live_payload_bytes << ',' << rollback.cpp_allocations
        << ',' << rollback.c_allocations << ",0x" << std::hex << std::uppercase << rollback.hash << std::dec << '\n';
    csv << "island_pair_cache_5k,segment_update_us," << island.update_p50_us << ',' << island.update_p95_us
        << ",0," << island.reserved_bytes << ",0," << island.allocations << ",0,0x"
        << std::hex << std::uppercase << island.hash << std::dec << '\n';
    csv << "island_pair_cache_5k,global_scan_us," << island.global_scan_p50_us << ',' << island.global_scan_p95_us
        << ",0,0,0,0,0,0x" << std::hex << std::uppercase << island.checksum << std::dec << '\n';
    csv << "weighted_tree_2047,cold_us,0," << warm.cold_p95_us << ",0,0,0,0,0,0x"
        << std::hex << std::uppercase << warm.hash << std::dec << '\n';
    csv << "weighted_tree_2047,warm_verified_us,0," << warm.warm_p95_us << ",0,0,0,0,0,0x"
        << std::hex << std::uppercase << warm.hash << std::dec << '\n';

    std::ofstream json(directory / "summary.json");
    json << "{\n"
         << "  \"version\": \"0.19\",\n"
         << "  \"affinity_pinned\": " << (affinity_pinned ? "true" : "false") << ",\n"
         << "  \"dense_p95_ms\": " << std::fixed << std::setprecision(6) << rollback.p95_ms << ",\n"
         << "  \"dense_max_batch_p95_ms\": " << rollback.maximum_batch_p95_ms << ",\n"
         << "  \"peak_rss_kib\": " << rollback.peak_rss << ",\n"
         << "  \"island_update_p95_us\": " << island.update_p95_us << ",\n"
         << "  \"global_scan_p95_us\": " << island.global_scan_p95_us << ",\n"
         << "  \"warm_accepted\": " << warm.accepted << ",\n"
         << "  \"warm_rejected\": " << warm.rejected << ",\n"
         << "  \"oblique_oracle_cases\": " << oracle.cases << ",\n"
         << "  \"oblique_fallback_grid_optimal\": " << oracle.exact << ",\n"
         << "  \"oblique_fallback_feasible\": " << oracle.fallback_feasible << ",\n"
         << "  \"oblique_fallback_infeasible\": " << oracle.fallback_infeasible << ",\n"
         << "  \"oblique_fallback_outside_grid\": " << oracle.outside_grid << ",\n"
         << "  \"oblique_maximum_grid_objective_gap\": " << oracle.maximum_gap << "\n"
         << "}\n";
}
} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path directory = argc > 1 ? argv[1] : "artifacts/v0.19-benchmark";
    const bool pinned = pin_to_first_allowed_cpu();
    const Dataset dense = make_matching(10'000U, 5'000U);
    const RollbackResult rollback = measure_dense_rollback(dense, 256U);
    const IslandCacheResult island = measure_island_cache(dense);
    const WarmResult warm = measure_bit_exact_warm_tree();
    const OracleResult oracle = measure_oblique_oracle();
    write_outputs(directory, rollback, island, warm, oracle, pinned);
    std::cout << "v0.19 dense p95=" << rollback.p95_ms << " ms max_batch_p95="
              << rollback.maximum_batch_p95_ms << " ms pinned=" << pinned
              << " rss_kib=" << rollback.peak_rss << " hash=0x" << std::hex << std::uppercase
              << rollback.hash << std::dec << '\n';
    std::cout << "island update p95=" << island.update_p95_us << " us global scan p95="
              << island.global_scan_p95_us << " us islands=" << island.islands
              << " allocations=" << island.allocations << '\n';
    std::cout << "warm accepted=" << warm.accepted << " rejected=" << warm.rejected
              << " cold_p95=" << warm.cold_p95_us << " us warm_p95=" << warm.warm_p95_us << " us\n";
    std::cout << "oblique oracle cases=" << oracle.cases << " grid_optimal=" << oracle.exact
              << " feasible=" << oracle.fallback_feasible << " infeasible=" << oracle.fallback_infeasible
              << " outside=" << oracle.outside_grid << " max_gap=" << oracle.maximum_gap
              << " candidates=" << oracle.candidates << '\n';
    return EXIT_SUCCESS;
}
