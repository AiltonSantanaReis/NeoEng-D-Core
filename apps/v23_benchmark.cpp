#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "neoeng/core/exact_oblique_tree_oracle.hpp"
#include "neoeng/core/paged_segmented_pair_history.hpp"
#include "neoeng/core/segmented_authoritative_paged_temporal_physics.hpp"
#include "neoeng/core/segmented_dynamic_pair_history.hpp"

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
#include <random>
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

SegmentedAuthoritativeTemporalConfig engine_config(const Dataset& data) {
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
    const std::size_t pages = (physics.bodies + 255U) / 256U;
    return {.physics = paged, .pair_history = {
        .bodies = physics.bodies, .maximum_contacts = physics.contacts,
        .maximum_pairs = physics.maximum_candidate_pairs,
        .maximum_pairs_per_segment = 1U, .history_capacity = 32U,
        .table_page_elements = 256U,
        .segment_generations = physics.contacts + 96U,
        .spill_generations = 6U, .table_generations = 34U,
        .body_key_page_generations = pages + 8U,
        .segment_map_page_generations = pages + 40U}};
}

struct IntegratedResult final {
    double p50_ms{}, p95_ms{};
    std::uint64_t physical_hash{}, pair_hash{};
    std::uint64_t cpp_allocations{}, c_allocations{};
    std::size_t reserved_bytes{};
    PagedSegmentedPairHistoryStats pair_stats{};
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

    SegmentedAuthoritativePagedTemporalPhysicsEngine clean(engine_config(data));
    clean.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        clean.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
    }
    clean.simulate_to(16U);
    const std::uint64_t expected_physical = clean.physical_hash();
    const std::uint64_t expected_pairs = clean.authoritative_pair_hash();

    IntegratedResult result{};
    std::vector<double> samples; samples.reserve(trials);
    for (std::size_t trial = 0U; trial < warmup + trials; ++trial) {
        SegmentedAuthoritativePagedTemporalPhysicsEngine engine(engine_config(data));
        engine.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
            engine.set_input(frame, {.velocity = std::span<const VelocityMutation>(&original[frame - 1U], 1U)});
        }
        engine.simulate_to(16U);
        allocation_probe::cpp_allocations.store(0U); allocation_probe::c_allocations.store(0U);
        const auto begin = Clock::now(); allocation_probe::enabled.store(true);
        engine.correct_and_resimulate(9U,
            {.velocity = std::span<const VelocityMutation>(&corrected[8], 1U)}, 16U);
        allocation_probe::enabled.store(false); const auto end = Clock::now();
        if (engine.physical_hash() != expected_physical || engine.authoritative_pair_hash() != expected_pairs) {
            throw std::runtime_error("v0.23 segmented authoritative rollback diverged from clean execution");
        }
        if (trial >= warmup) samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        result.cpp_allocations += allocation_probe::cpp_allocations.load();
        result.c_allocations += allocation_probe::c_allocations.load();
        result.physical_hash = engine.physical_hash(); result.pair_hash = engine.authoritative_pair_hash();
        result.reserved_bytes = engine.reserved_bytes(); result.pair_stats = engine.stats().pair_history;
    }
    result.p50_ms = percentile(samples, .50); result.p95_ms = percentile(samples, .95);
    return result;
}

struct HistoryComparison final {
    double paged_p95_us{};
    std::size_t paged_reserved{}, dense_reserved{};
    std::uint64_t hash{};
    PagedSegmentedPairHistoryStats stats{};
};
HistoryComparison compare_histories() {
    constexpr std::size_t bodies = 10'000U, pairs_count = 5'000U, capacity = 32U;
    std::vector<NormalContact> contacts(pairs_count);
    std::vector<BroadphasePair> full(pairs_count);
    for (std::size_t pair = 0U; pair < pairs_count; ++pair) {
        contacts[pair] = {pair * 2U, pair * 2U + 1U, {kOne, 0}};
        full[pair] = {pair * 2U, pair * 2U + 1U};
    }
    const std::size_t pages = (bodies + 255U) / 256U;
    SegmentedDynamicPairHistory dense({.bodies=bodies,.maximum_contacts=pairs_count,
        .maximum_pairs=pairs_count,.maximum_pairs_per_segment=1U,.history_capacity=capacity,
        .segment_generations=pairs_count+64U,.spill_generations=4U,.table_generations=34U});
    PagedSegmentedPairHistory paged({.bodies=bodies,.maximum_contacts=pairs_count,
        .maximum_pairs=pairs_count,.maximum_pairs_per_segment=1U,.history_capacity=capacity,
        .table_page_elements=256U,.segment_generations=pairs_count+96U,.spill_generations=4U,
        .table_generations=34U,.body_key_page_generations=pages+8U,
        .segment_map_page_generations=pages+40U});
    dense.initialize(0U, contacts, full); paged.initialize(0U, contacts, full);
    for (std::uint64_t frame = 1U; frame < capacity; ++frame) {
        dense.capture(frame, contacts, full, {}, false, true, true);
        paged.capture(frame, contacts, full, {}, false, true, true);
    }
    std::vector<BroadphasePair> reduced(full.begin() + 1, full.end());
    std::array<std::size_t,1> dirty{0U};
    std::vector<double> samples; samples.reserve(160U);
    std::uint64_t frame = capacity;
    for (std::size_t trial = 0U; trial < 168U; ++trial, ++frame) {
        const auto& pairs = (trial & 1U) == 0U ? reduced : full;
        dense.capture(frame, contacts, pairs, dirty, false, true, true);
        const auto start = Clock::now();
        paged.capture(frame, contacts, pairs, dirty, false, true, true);
        const auto stop = Clock::now();
        if (trial >= 8U) samples.push_back(std::chrono::duration<double,std::micro>(stop-start).count());
    }
    std::vector<BroadphasePair> a(pairs_count), b(pairs_count);
    a.resize(dense.restore_pairs(frame-1U,a)); b.resize(paged.restore_pairs(frame-1U,b));
    if (a != b || paged.hash(frame-1U) != dense.hash(frame-1U)) {
        throw std::runtime_error("v0.23 paged and dense segmented histories diverged");
    }
    return {.paged_p95_us=percentile(samples,.95),.paged_reserved=paged.reserved_bytes(),
        .dense_reserved=dense.reserved_bytes(),.hash=paged.hash(frame-1U),.stats=paged.stats()};
}

struct RepairResult final {
    double oracle_p50_us{}, oracle_p95_us{}, repair_p50_us{}, repair_p95_us{};
    std::size_t cases{}, radius0{}, radius1{}, radius2{}, failures{}, rounded_violations{};
    std::uint64_t aggregate{0xCBF29CE484222325ULL};
};
void mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte=0U; byte<8U; ++byte) { hash ^= (value>>(byte*8U))&0xFFU; hash*=0x100000001B3ULL; }
}
RepairResult benchmark_adaptive_repair() {
    std::mt19937_64 rng(0x4E454F454E475633ULL);
    RepairResult result{}; std::vector<double> oracle_samples, repair_samples;
    for (std::size_t trial=0U; trial<256U; ++trial) {
        constexpr std::size_t bodies=8U;
        std::array<Fixed::rep,bodies> x{},y{}; std::array<std::uint32_t,bodies> masses{};
        std::array<NormalContact,bodies-1U> contacts{};
        for(std::size_t body=0U;body<bodies;++body){
            x[body]=static_cast<Fixed::rep>(static_cast<std::int64_t>(rng()%17U)-8);
            y[body]=static_cast<Fixed::rep>(static_cast<std::int64_t>(rng()%17U)-8);
            masses[body]=1U+static_cast<std::uint32_t>(rng()%9U);
            if(body) contacts[body-1U]={static_cast<std::size_t>(rng()%body),body,kNormals[rng()%kNormals.size()]};
        }
        const auto oracle_begin=Clock::now();
        const auto continuous=solve_exact_oblique_tree_active_sets(x,y,masses,contacts,
            {.maximum_bodies=8U,.maximum_contacts=7U,.quantized_repair_radius=0U,.perform_quantized_repair=false});
        const auto oracle_end=Clock::now();
        if(!continuous.certified_continuous) throw std::runtime_error("v0.23 continuous oracle failed");
        oracle_samples.push_back(std::chrono::duration<double,std::micro>(oracle_end-oracle_begin).count());
        if(continuous.rounded_primal_violation_raw!=0U) ++result.rounded_violations;

        const auto repair_begin=Clock::now();
        QuantizedTreeRepairResult repair{}; std::size_t radius=0U;
        for(;radius<=2U;++radius){ repair=repair_exact_oblique_tree_neighbourhood(continuous,masses,contacts,radius); if(repair.certified_neighbourhood) break; }
        const auto repair_end=Clock::now();
        repair_samples.push_back(std::chrono::duration<double,std::micro>(repair_end-repair_begin).count());
        ++result.cases;
        if(!repair.certified_neighbourhood || repair.primal_violation_raw!=0U){++result.failures;continue;}
        if(radius==0U)++result.radius0;else if(radius==1U)++result.radius1;else ++result.radius2;
        mix(result.aggregate,continuous.hash);mix(result.aggregate,repair.hash);
    }
    result.oracle_p50_us=percentile(oracle_samples,.50);result.oracle_p95_us=percentile(oracle_samples,.95);
    result.repair_p50_us=percentile(repair_samples,.50);result.repair_p95_us=percentile(repair_samples,.95);
    return result;
}
} // namespace

int main(int argc,char** argv){
    const std::filesystem::path output=argc>1?argv[1]:"artifacts/v0.23-benchmark";
    std::filesystem::create_directories(output);
    const Dataset data=make_matching(10'000U,5'000U);
    const IntegratedResult integrated=measure_integrated(data);
    const HistoryComparison history=compare_histories();
    const RepairResult repair=benchmark_adaptive_repair();

    std::ofstream csv(output/"v23_cycle.csv");
    csv<<"operation,p50,p95,reserved_bytes,comparison_bytes,cpp_allocations,c_allocations,hash\n";
    csv<<"integrated_rollback_ms,"<<std::fixed<<std::setprecision(6)<<integrated.p50_ms<<','<<integrated.p95_ms<<','
       <<integrated.reserved_bytes<<",0,"<<integrated.cpp_allocations<<','<<integrated.c_allocations<<",0x"
       <<std::hex<<std::uppercase<<integrated.physical_hash<<std::dec<<"\n";
    csv<<"paged_segment_capture_us,0,"<<std::fixed<<std::setprecision(3)<<history.paged_p95_us<<','
       <<history.paged_reserved<<','<<history.dense_reserved<<",0,0,0x"<<std::hex<<std::uppercase<<history.hash<<std::dec<<"\n";
    csv<<"continuous_oracle_us,"<<repair.oracle_p50_us<<','<<repair.oracle_p95_us<<",0,0,0,0,0x"
       <<std::hex<<std::uppercase<<repair.aggregate<<std::dec<<"\n";
    csv<<"adaptive_repair_us,"<<repair.repair_p50_us<<','<<repair.repair_p95_us<<",0,0,0,0,0x"
       <<std::hex<<std::uppercase<<repair.aggregate<<std::dec<<"\n";

    std::ofstream json(output/"summary.json");
    json<<"{\n"
        <<"  \"rollback_p95_ms\": "<<std::fixed<<std::setprecision(6)<<integrated.p95_ms<<",\n"
        <<"  \"rollback_reserved_bytes\": "<<integrated.reserved_bytes<<",\n"
        <<"  \"rollback_cpp_allocations\": "<<integrated.cpp_allocations<<",\n"
        <<"  \"rollback_c_allocations\": "<<integrated.c_allocations<<",\n"
        <<"  \"paged_pair_reserved_bytes\": "<<history.paged_reserved<<",\n"
        <<"  \"dense_pair_reserved_bytes\": "<<history.dense_reserved<<",\n"
        <<"  \"body_pages_written\": "<<history.stats.body_pages_written<<",\n"
        <<"  \"body_pages_shared\": "<<history.stats.body_pages_shared<<",\n"
        <<"  \"segment_pages_written\": "<<history.stats.segment_pages_written<<",\n"
        <<"  \"segment_pages_shared\": "<<history.stats.segment_pages_shared<<",\n"
        <<"  \"oracle_p95_us\": "<<repair.oracle_p95_us<<",\n"
        <<"  \"repair_p95_us\": "<<repair.repair_p95_us<<",\n"
        <<"  \"repair_radius_0\": "<<repair.radius0<<",\n"
        <<"  \"repair_radius_1\": "<<repair.radius1<<",\n"
        <<"  \"repair_radius_2\": "<<repair.radius2<<",\n"
        <<"  \"repair_failures\": "<<repair.failures<<",\n"
        <<"  \"aggregate\": \"0x"<<std::hex<<std::uppercase<<repair.aggregate<<std::dec<<"\"\n}\n";

    std::cout<<"v0.23 rollback p95_ms="<<integrated.p95_ms<<" reserve="<<integrated.reserved_bytes
             <<" alloc="<<integrated.cpp_allocations<<'/'<<integrated.c_allocations
             <<" physical=0x"<<std::hex<<std::uppercase<<integrated.physical_hash
             <<" pairs=0x"<<integrated.pair_hash<<std::dec<<'\n';
    std::cout<<"v0.23 paged segments p95_us="<<history.paged_p95_us<<" reserve="<<history.paged_reserved
             <<" dense="<<history.dense_reserved<<" body_pages="<<history.stats.body_pages_written
             <<'/'<<history.stats.body_pages_shared<<" segment_pages="<<history.stats.segment_pages_written
             <<'/'<<history.stats.segment_pages_shared<<'\n';
    std::cout<<"v0.23 oracle p95_us="<<repair.oracle_p95_us<<" repair p95_us="<<repair.repair_p95_us
             <<" radii="<<repair.radius0<<'/'<<repair.radius1<<'/'<<repair.radius2
             <<" failures="<<repair.failures<<" aggregate=0x"<<std::hex<<std::uppercase<<repair.aggregate<<std::dec<<'\n';
    return EXIT_SUCCESS;
}
