#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "neoeng/core/authoritative_paged_temporal_physics.hpp"
#include "neoeng/core/dynamic_island_pair_history.hpp"
#include "neoeng/core/exact_oblique_tree_oracle.hpp"
#include "neoeng/core/oblique_tree_grid_dp.hpp"

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
#include <span>
#include <stdexcept>
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

namespace {
using namespace neoeng::core;
using Clock = std::chrono::steady_clock;
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 4> kNormals{{
    {kOne, 0}, {0, kOne}, {759'250'125, 759'250'125}, {644'245'094, 858'993'459}
}};

double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U))];
}
Fixed::rep mul_q30(std::int32_t normal, Fixed::rep magnitude) {
    return static_cast<Fixed::rep>(static_cast<WideInteger>(normal) * magnitude / (WideInteger{1} << 30U));
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

AuthoritativePagedTemporalConfig authoritative_config(const Dataset& data) {
    const AtomicTemporalPhysicsConfig physics{
        .bodies = data.vx.size(), .contacts = data.contacts.size(),
        .maximum_candidate_pairs = data.contacts.size() * 2U + 512U,
        .history_capacity = 32U, .horizon_frames = 16U,
        .maximum_velocity_mutations = 2U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U, .half_extent = Fixed::from_ratio(1, 2),
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
    };
    PagedAtomicTemporalConfig paged{.physics = physics, .history = {
        .bodies = physics.bodies, .contacts = physics.contacts,
        .maximum_candidate_pairs = physics.maximum_candidate_pairs,
        .history_capacity = 32U, .page_elements = 256U,
        .maximum_position_dirty_pages_per_frame = 0U,
        .maximum_velocity_dirty_pages_per_frame = 4U,
        .maximum_mass_dirty_pages_per_frame = 2U,
        .maximum_contact_dirty_pages_per_frame = 4U,
        .full_position_generations = 2U, .full_velocity_generations = 4U,
        .full_contact_generations = 4U, .maximum_cache_generations = 6U}};
    return {.physics = paged, .pair_history = {
        .bodies = physics.bodies, .maximum_contacts = physics.contacts,
        .maximum_pairs = physics.maximum_candidate_pairs, .history_capacity = 32U,
        .pair_generations = 4U, .topology_generations = 4U}};
}

struct IntegratedResult final {
    double p50_ms{}, p95_ms{};
    std::uint64_t physical_hash{}, pair_hash{};
    std::uint64_t cpp_allocations{}, c_allocations{};
    std::size_t reserved_bytes{};
    DynamicIslandPairHistoryStats pair_stats{};
};

IntegratedResult measure_integrated(const Dataset& data) {
    constexpr std::size_t warmup = 8U, trials = 80U;
    std::array<VelocityMutation, 16> original{}, corrected{};
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        const std::size_t body = static_cast<std::size_t>((frame * 17U) % (data.contacts.size() * 2U));
        original[frame - 1U] = {body, static_cast<Fixed::rep>(frame * 101U), -static_cast<Fixed::rep>(frame * 37U)};
        corrected[frame - 1U] = original[frame - 1U];
    }
    corrected[8].delta_x += 777; corrected[8].delta_y -= 313;

    AuthoritativePagedTemporalPhysicsEngine clean(authoritative_config(data));
    clean.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        clean.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
    }
    clean.simulate_to(16U);
    const std::uint64_t expected_physical = clean.physical_hash();
    const std::uint64_t expected_pairs = clean.authoritative_pair_hash();

    std::vector<double> samples; samples.reserve(trials);
    IntegratedResult result{};
    for (std::size_t trial = 0U; trial < warmup + trials; ++trial) {
        AuthoritativePagedTemporalPhysicsEngine engine(authoritative_config(data));
        engine.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
            engine.set_input(frame, {.velocity = std::span<const VelocityMutation>(&original[frame - 1U], 1U)});
        }
        engine.simulate_to(16U);
        allocation_probe::cpp_allocations.store(0U);
        allocation_probe::c_allocations.store(0U);
        const auto begin = Clock::now();
        allocation_probe::enabled.store(true);
        engine.correct_and_resimulate(9U,
            {.velocity = std::span<const VelocityMutation>(&corrected[8], 1U)}, 16U);
        allocation_probe::enabled.store(false);
        const auto end = Clock::now();
        if (engine.physical_hash() != expected_physical
            || engine.authoritative_pair_hash() != expected_pairs) {
            throw std::runtime_error("v0.21 authoritative rollback diverged from clean execution");
        }
        if (trial >= warmup) samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        result.cpp_allocations += allocation_probe::cpp_allocations.load();
        result.c_allocations += allocation_probe::c_allocations.load();
        result.physical_hash = engine.physical_hash();
        result.pair_hash = engine.authoritative_pair_hash();
        result.reserved_bytes = engine.reserved_bytes();
        result.pair_stats = engine.stats().pair_history;
    }
    result.p50_ms = percentile(samples, .50);
    result.p95_ms = percentile(samples, .95);
    return result;
}

struct TopologyResult final {
    std::uint64_t hash{};
    std::size_t initial_islands{}, merged_islands{}, split_islands{};
    std::size_t initial_spill{}, merged_spill{}, split_spill{};
    std::size_t reserved_bytes{};
};
TopologyResult exercise_dynamic_topology() {
    std::vector<NormalContact> split{{0U,1U,{kOne,0}}, {2U,3U,{kOne,0}}, {4U,5U,{kOne,0}}};
    std::vector<NormalContact> merged{{0U,1U,{kOne,0}}, {1U,2U,{kOne,0}},
        {2U,3U,{kOne,0}}, {4U,5U,{kOne,0}}};
    std::vector<BroadphasePair> pairs{{0U,1U},{1U,2U},{2U,3U},{4U,5U}};
    DynamicIslandPairHistory history({.bodies=6U,.maximum_contacts=5U,.maximum_pairs=8U,
        .history_capacity=8U,.pair_generations=6U,.topology_generations=6U});
    history.initialize(0U, split, pairs);
    const std::size_t dirty = 1U;
    history.capture(1U, split, pairs, {}, false, true);
    history.capture(2U, merged, pairs, std::span<const std::size_t>(&dirty,1U), true, true);
    history.capture(3U, split, pairs, std::span<const std::size_t>(&dirty,1U), true, true);
    std::array<BroadphasePair,8> spill{};
    if (history.restore_spill_pairs(0U, spill) != 1U
        || history.restore_spill_pairs(2U, spill) != 0U
        || history.restore_spill_pairs(3U, spill) != 1U) {
        throw std::runtime_error("v0.21 spill segment restore failed");
    }
    return {.hash=history.hash(3U), .initial_islands=history.island_count(0U),
        .merged_islands=history.island_count(2U), .split_islands=history.island_count(3U),
        .initial_spill=history.spill_pair_count(0U), .merged_spill=history.spill_pair_count(2U),
        .split_spill=history.spill_pair_count(3U), .reserved_bytes=history.reserved_bytes()};
}

struct ExactResult final {
    double p50_ms{}, p95_ms{};
    std::uint64_t hash{}, active_sets{};
    std::string numerator{}, denominator{};
    std::uint64_t grid_objective{}, grid_pairs{};
    bool certified{}, grid_certified{};
};
ExactResult measure_exact_oracle() {
    constexpr std::size_t bodies = 8U;
    std::array<Fixed::rep,bodies> x{{2,-2,1,-1,2,0,-2,1}};
    std::array<Fixed::rep,bodies> y{{-2,2,-1,1,0,2,1,-2}};
    std::array<std::uint32_t,bodies> masses{{1,2,3,4,5,6,7,8}};
    std::array<NormalContact,bodies-1U> contacts{};
    for (std::size_t body=1U; body<bodies; ++body) {
        contacts[body-1U] = {(body-1U)/2U, body, kNormals[body%kNormals.size()]};
    }
    std::vector<double> samples; samples.reserve(24U);
    ExactObliqueTreeOracleResult exact{};
    for (std::size_t trial=0U; trial<28U; ++trial) {
        const auto begin=Clock::now();
        exact=solve_exact_oblique_tree_active_sets(x,y,masses,contacts,
            {.maximum_bodies=bodies,.maximum_contacts=bodies-1U});
        const auto end=Clock::now();
        if (!exact.certified_continuous) throw std::runtime_error("v0.21 exact oracle did not certify");
        if (trial>=4U) samples.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
    }
    ObliqueTreeGridScratch scratch(bodies,81U);
    const auto grid=solve_oblique_tree_grid_dp(x,y,masses,contacts,
        {.minimum_raw=-4,.maximum_raw=4,.maximum_bodies=bodies,.maximum_grid_states=81U},scratch);
    if (!grid.certified_on_grid) throw std::runtime_error("v0.21 comparison grid did not certify");
    return {.p50_ms=percentile(samples,.50),.p95_ms=percentile(samples,.95),
        .hash=exact.hash,.active_sets=exact.active_sets_tested,
        .numerator=exact.objective_numerator,.denominator=exact.objective_denominator,
        .grid_objective=grid.objective,.grid_pairs=grid.state_pairs_tested,
        .certified=exact.certified_continuous,.grid_certified=grid.certified_on_grid};
}

void write_outputs(const std::filesystem::path& directory, const IntegratedResult& integrated,
                   const TopologyResult& topology, const ExactResult& exact) {
    std::filesystem::create_directories(directory);
    std::ofstream csv(directory/"v21_cycle.csv");
    csv << "workload,method,p50_ms,p95_ms,reserved_bytes,cpp_allocations,c_allocations,hash,certified\n";
    csv << "dense_matching_10k_5k,authoritative_dynamic_island_history," << std::fixed << std::setprecision(6)
        << integrated.p50_ms << ',' << integrated.p95_ms << ',' << integrated.reserved_bytes << ','
        << integrated.cpp_allocations << ',' << integrated.c_allocations << ",0x" << std::hex
        << std::uppercase << integrated.physical_hash << std::dec << ",true\n";
    csv << "oblique_tree_8,exact_rational_active_set," << exact.p50_ms << ',' << exact.p95_ms
        << ",0,0,0,0x" << std::hex << std::uppercase << exact.hash << std::dec << ','
        << (exact.certified?"true":"false") << "\n";
    std::ofstream json(directory/"summary.json");
    json << "{\n  \"version\": \"0.21\",\n"
         << "  \"dense_p95_ms\": " << std::fixed << std::setprecision(6) << integrated.p95_ms << ",\n"
         << "  \"dense_pair_hash\": \"0x" << std::hex << std::uppercase << integrated.pair_hash << std::dec << "\",\n"
         << "  \"pair_generations_shared\": " << integrated.pair_stats.pair_generations_shared << ",\n"
         << "  \"topology_initial_islands\": " << topology.initial_islands << ",\n"
         << "  \"topology_merged_islands\": " << topology.merged_islands << ",\n"
         << "  \"topology_split_islands\": " << topology.split_islands << ",\n"
         << "  \"spill_initial\": " << topology.initial_spill << ",\n"
         << "  \"spill_merged\": " << topology.merged_spill << ",\n"
         << "  \"spill_split\": " << topology.split_spill << ",\n"
         << "  \"exact_p95_ms\": " << exact.p95_ms << ",\n"
         << "  \"exact_objective_numerator\": \"" << exact.numerator << "\",\n"
         << "  \"exact_objective_denominator\": \"" << exact.denominator << "\",\n"
         << "  \"quantized_grid_objective\": " << exact.grid_objective << ",\n"
         << "  \"grid_uses_same_constraint_semantics\": false\n}\n";
}
} // namespace

int main(int argc,char** argv) {
    const std::filesystem::path output=argc>1?argv[1]:"artifacts/v0.21-benchmark";
    try {
        const Dataset data=make_matching(10'000U,5'000U);
        const IntegratedResult integrated=measure_integrated(data);
        const TopologyResult topology=exercise_dynamic_topology();
        const ExactResult exact=measure_exact_oracle();
        write_outputs(output,integrated,topology,exact);
        std::cout<<std::fixed<<std::setprecision(6)
                 <<"v0.21 dense p95="<<integrated.p95_ms<<" ms physical=0x"<<std::hex<<std::uppercase
                 <<integrated.physical_hash<<std::dec<<" pair_shared="<<integrated.pair_stats.pair_generations_shared<<'\n'
                 <<"v0.21 exact p95="<<exact.p95_ms<<" ms objective="<<exact.numerator<<'/'<<exact.denominator
                 <<" grid="<<exact.grid_objective<<" certified="<<exact.certified<<'\n';
    } catch(const std::exception& error) {
        std::cerr<<"v0.21 benchmark failed: "<<error.what()<<'\n'; return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
