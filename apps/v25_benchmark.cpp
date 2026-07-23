#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "neoeng/core/arbitrary_normal_projection.hpp"
#include "neoeng/core/exact_oblique_tree_oracle.hpp"
#include "neoeng/core/oblique_star_projection.hpp"
#include "neoeng/core/paged_segmented_pair_history.hpp"
#include "neoeng/core/segmented_authoritative_paged_temporal_physics.hpp"

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/multiprecision/rational_adaptor.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <random>
#include <span>
#include <stdexcept>
#include <vector>

namespace allocation_probe {
std::atomic<bool> enabled{false}; std::atomic<std::uint64_t> cpp_allocations{0},c_allocations{0};
void cpp() noexcept {if(enabled.load(std::memory_order_relaxed))cpp_allocations.fetch_add(1U);}
void c() noexcept {if(enabled.load(std::memory_order_relaxed))c_allocations.fetch_add(1U);}
}
extern "C" void* __real_malloc(std::size_t); extern "C" void* __real_calloc(std::size_t,std::size_t);
extern "C" void* __real_realloc(void*,std::size_t); extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(std::size_t n){allocation_probe::c();return __real_malloc(n?n:1U);} extern "C" void* __wrap_calloc(std::size_t n,std::size_t s){allocation_probe::c();return __real_calloc(n,s);}
extern "C" void* __wrap_realloc(void*p,std::size_t n){allocation_probe::c();return __real_realloc(p,n);} extern "C" void __wrap_free(void*p){__real_free(p);}
void* operator new(std::size_t n){if(void*p=__real_malloc(n?n:1U)){allocation_probe::cpp();return p;}throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);} void operator delete(void*p) noexcept{__real_free(p);} void operator delete[](void*p) noexcept{__real_free(p);}
void operator delete(void*p,std::size_t) noexcept{__real_free(p);} void operator delete[](void*p,std::size_t) noexcept{__real_free(p);}

namespace {
using namespace neoeng::core; using Clock=std::chrono::steady_clock;
constexpr std::int32_t kOne=1<<30;
constexpr std::array<NormalQ30,6> kNormals{{{kOne,0},{0,kOne},{759250125,759250125},{-759250125,759250125},{644245094,858993459},{-644245094,858993459}}};
constexpr std::array<NormalQ30,4> kMatchingNormals{{{kOne,0},{0,kOne},{759250125,759250125},{644245094,858993459}}};
void mix_u64(std::uint64_t& hash,std::uint64_t value) noexcept {for(unsigned byte=0U;byte<8U;++byte){hash^=(value>>(byte*8U))&0xFFU;hash*=0x100000001B3ULL;}}
double percentile(std::vector<double> v,double p){std::sort(v.begin(),v.end());return v[static_cast<std::size_t>(p*static_cast<double>(v.size()-1U))];}
Fixed::rep mul_q30(std::int32_t n,Fixed::rep m){return static_cast<Fixed::rep>(static_cast<WideInteger>(n)*m/(WideInteger{1}<<30U));}
struct Dataset{std::vector<Fixed::rep>px,py,vx,vy;std::vector<std::uint32_t>m;std::vector<NormalContact>contacts;};
Dataset matching(){constexpr std::size_t bodies=10'000U,pairs=5'000U,cols=100U;Dataset d;d.px.resize(bodies);d.py.resize(bodies);d.vx.resize(bodies);d.vy.resize(bodies);d.m.resize(bodies);d.contacts.reserve(pairs);
 const auto sep=Fixed::from_ratio(3,4).raw(),speed=Fixed::from_ratio(1,16).raw();
 for(std::size_t b=0;b<bodies;++b){d.px[b]=Fixed::from_integer(static_cast<Fixed::rep>((b%cols)*4U)).raw();d.py[b]=Fixed::from_integer(static_cast<Fixed::rep>((b/cols)*4U)).raw();d.m[b]=1U+static_cast<std::uint32_t>((b*17U)%64U);}
 for(std::size_t p=0;p<pairs;++p){const auto n=kMatchingNormals[p%kMatchingNormals.size()];const std::size_t a=p*2U,b=a+1U;const auto cx=Fixed::from_integer(static_cast<Fixed::rep>((p%cols)*4U)).raw(),cy=Fixed::from_integer(static_cast<Fixed::rep>((p/cols)*4U)).raw();const auto dx=mul_q30(n.x,sep),dy=mul_q30(n.y,sep);d.px[a]=cx-dx/2;d.py[a]=cy-dy/2;d.px[b]=cx+dx/2;d.py[b]=cy+dy/2;d.vx[a]=mul_q30(n.x,speed);d.vy[a]=mul_q30(n.y,speed);d.vx[b]=-d.vx[a];d.vy[b]=-d.vy[a];d.contacts.push_back({a,b,n});}return d;}
SegmentedAuthoritativeTemporalConfig config(const Dataset&d){const AtomicTemporalPhysicsConfig p{.bodies=d.vx.size(),.contacts=d.contacts.size(),.maximum_candidate_pairs=d.contacts.size()*2U+512U,.history_capacity=32U,.horizon_frames=16U,.maximum_velocity_mutations=2U,.maximum_mass_mutations=1U,.maximum_contact_mutations=1U,.half_extent=Fixed::from_ratio(1,2),.projection={.maximum_iterations=32U,.feasibility_tolerance_raw=16U}};PagedAtomicTemporalConfig paged{.physics=p,.history={.bodies=p.bodies,.contacts=p.contacts,.maximum_candidate_pairs=p.maximum_candidate_pairs,.history_capacity=32U,.page_elements=256U,.maximum_position_dirty_pages_per_frame=0U,.maximum_velocity_dirty_pages_per_frame=4U,.maximum_mass_dirty_pages_per_frame=2U,.maximum_contact_dirty_pages_per_frame=4U,.full_position_generations=2U,.full_velocity_generations=4U,.full_contact_generations=4U,.maximum_cache_generations=6U}};const std::size_t pages=(p.bodies+255U)/256U;return {.physics=paged,.pair_history={.bodies=p.bodies,.maximum_contacts=p.contacts,.maximum_pairs=p.maximum_candidate_pairs,.maximum_pairs_per_segment=1U,.history_capacity=32U,.table_page_elements=256U,.segment_generations=p.contacts+96U,.spill_generations=6U,.table_generations=34U,.body_key_page_generations=pages+8U,.segment_map_page_generations=pages+40U}};}
struct Rollback{double p50{},p95{};std::uint64_t hash{},pairs{},cpp{},c{};std::size_t bytes{};PagedSegmentedPairHistoryStats stats{};};
Rollback rollback_bench(const Dataset&d){constexpr std::size_t warm=8U,trials=80U;std::array<VelocityMutation,16> original{},corrected{};for(std::uint64_t f=1;f<=16;++f){const std::size_t body=(f*17U)%(d.contacts.size()*2U);original[f-1]={body,static_cast<Fixed::rep>(f*101U),-static_cast<Fixed::rep>(f*37U)};corrected[f-1]=original[f-1];}corrected[8].delta_x+=777;corrected[8].delta_y-=313;
 SegmentedAuthoritativePagedTemporalPhysicsEngine clean(config(d));clean.initialize(d.px,d.py,d.vx,d.vy,d.m,d.contacts);for(std::uint64_t f=1;f<=16;++f)clean.set_input(f,{.velocity=std::span<const VelocityMutation>(&corrected[f-1],1U)});clean.simulate_to(16U);const auto expected=clean.physical_hash(),expected_pairs=clean.authoritative_pair_hash();Rollback r{};std::vector<double>s;s.reserve(trials);
 for(std::size_t t=0;t<warm+trials;++t){SegmentedAuthoritativePagedTemporalPhysicsEngine e(config(d));e.initialize(d.px,d.py,d.vx,d.vy,d.m,d.contacts);for(std::uint64_t f=1;f<=16;++f)e.set_input(f,{.velocity=std::span<const VelocityMutation>(&original[f-1],1U)});e.simulate_to(16U);allocation_probe::cpp_allocations=0;allocation_probe::c_allocations=0;const auto b=Clock::now();allocation_probe::enabled=true;e.correct_and_resimulate(9U,{.velocity=std::span<const VelocityMutation>(&corrected[8],1U)},16U);allocation_probe::enabled=false;const auto end=Clock::now();if(e.physical_hash()!=expected||e.authoritative_pair_hash()!=expected_pairs)throw std::runtime_error("v0.24 rollback mismatch");if(t>=warm)s.push_back(std::chrono::duration<double,std::milli>(end-b).count());r.cpp+=allocation_probe::cpp_allocations;r.c+=allocation_probe::c_allocations;r.hash=e.physical_hash();r.pairs=e.authoritative_pair_hash();r.bytes=e.reserved_bytes();r.stats=e.stats().pair_history;}
 r.p50=percentile(s,.50);r.p95=percentile(s,.95);return r;}
struct Restore{double p95{};std::uint64_t direct{},merged{},pushes{},hash{};};
Restore restore_bench(){constexpr std::size_t bodies=10'000U,pairs=5'000U;const std::size_t pages=(bodies+255U)/256U;std::vector<NormalContact>c(pairs);std::vector<BroadphasePair>p(pairs),out(pairs);for(std::size_t i=0;i<pairs;++i){c[i]={i*2U,i*2U+1U,{kOne,0}};p[i]={i*2U,i*2U+1U};}PagedSegmentedPairHistory h({.bodies=bodies,.maximum_contacts=pairs,.maximum_pairs=pairs,.maximum_pairs_per_segment=1U,.history_capacity=4U,.table_page_elements=256U,.segment_generations=pairs+8U,.spill_generations=4U,.table_generations=6U,.body_key_page_generations=pages+4U,.segment_map_page_generations=pages+4U});h.initialize(0U,c,p);std::vector<double>s;s.reserve(200U);for(std::size_t i=0;i<220U;++i){const auto b=Clock::now();const auto n=h.restore_pairs(0U,out);const auto e=Clock::now();if(n!=pairs||out!=p)throw std::runtime_error("v0.24 direct restore mismatch");if(i>=20U)s.push_back(std::chrono::duration<double,std::micro>(e-b).count());}const auto st=h.stats();return {.p95=percentile(s,.95),.direct=st.direct_ordered_restores,.merged=st.merged_restores,.pushes=st.merge_heap_pushes,.hash=h.hash(0U)};}
struct Dynamic{double capture_p95{},restore_p95{};std::uint64_t hash{},direct{},merged{};};
Dynamic dynamic_bench(){constexpr std::size_t bodies=10'000U,base_pairs=5'000U,merge_pairs=16U,frames=8U;const std::size_t pages=(bodies+255U)/256U;std::vector<NormalContact>split_c(base_pairs),merged_c;std::vector<BroadphasePair>split_p(base_pairs),merged_p;for(std::size_t i=0;i<base_pairs;++i){split_c[i]={i*2U,i*2U+1U,{kOne,0}};split_p[i]={i*2U,i*2U+1U};}merged_c=split_c;merged_p=split_p;for(std::size_t i=1;i<merge_pairs;++i){merged_c.push_back({i*2U-1U,i*2U,{kOne,0}});merged_p.push_back({i*2U-1U,i*2U});}std::sort(merged_p.begin(),merged_p.end());PagedSegmentedPairHistory h({.bodies=bodies,.maximum_contacts=merged_c.size(),.maximum_pairs=merged_p.size(),.maximum_pairs_per_segment=merge_pairs*2U,.history_capacity=32U,.table_page_elements=256U,.segment_generations=45'000U,.spill_generations=8U,.table_generations=34U,.body_key_page_generations=pages*10U+16U,.segment_map_page_generations=pages*10U+32U});h.initialize(0U,split_c,split_p);std::vector<double>cap,rest;std::vector<BroadphasePair>out(merged_p.size());cap.reserve(frames);rest.reserve(frames);std::array<std::size_t,64>dirty{};std::iota(dirty.begin(),dirty.end(),0U);
 for(std::uint64_t f=1;f<=frames;++f){const bool merge=(f&1U)!=0U;const auto&c=merge?merged_c:split_c;const auto&p=merge?merged_p:split_p;auto b=Clock::now();h.capture(f,c,p,dirty,true,true,false);auto e=Clock::now();cap.push_back(std::chrono::duration<double,std::micro>(e-b).count());b=Clock::now();out.resize(merged_p.size());out.resize(h.restore_pairs(f,out));e=Clock::now();rest.push_back(std::chrono::duration<double,std::micro>(e-b).count());if(out!=p)throw std::runtime_error("v0.24 dynamic restore mismatch");}const auto st=h.stats();return {.capture_p95=percentile(cap,.95),.restore_p95=percentile(rest,.95),.hash=h.hash(frames),.direct=st.direct_ordered_restores,.merged=st.merged_restores};}
struct Star{double p50{},p95{};std::size_t iterations{},active{};std::uint64_t hash{};bool certified{};};
Star star_bench(){constexpr std::size_t bodies=65U;std::vector<Fixed::rep>x(bodies),y(bodies);std::vector<std::uint32_t>m(bodies);std::vector<NormalContact>c;c.reserve(bodies-1U);std::mt19937_64 rng(0x24002400ULL);for(std::size_t b=0;b<bodies;++b){x[b]=static_cast<Fixed::rep>(static_cast<std::int64_t>(rng()%101U)-50);y[b]=static_cast<Fixed::rep>(static_cast<std::int64_t>(rng()%101U)-50);m[b]=1U+static_cast<std::uint32_t>(rng()%16U);if(b)c.push_back({0U,b,kNormals[rng()%kNormals.size()]});}std::vector<double>s;s.reserve(24U);ObliqueStarProjectionResult last{};for(std::size_t t=0;t<28U;++t){const auto b=Clock::now();last=solve_oblique_star_leaf_elimination(x,y,m,c,{.maximum_iterations=128U});const auto e=Clock::now();if(!last.certified_continuous)throw std::runtime_error("v0.24 star solver did not certify");if(t>=4U)s.push_back(std::chrono::duration<double,std::micro>(e-b).count());}return {.p50=percentile(s,.50),.p95=percentile(s,.95),.iterations=last.iterations,.active=last.active_leaves,.hash=last.hash,.certified=last.certified_continuous};}

struct MultiDirty final {
    std::size_t dirty_islands{};
    double capture_p95_us{};
    double restore_p95_us{};
    std::uint64_t segments_written{};
    std::uint64_t segment_pages_written{};
    std::uint64_t hash{};
};

MultiDirty multi_dirty_bench(std::size_t dirty_islands) {
    constexpr std::size_t bodies = 10'000U, base_pairs = 5'000U, frames = 16U;
    const std::size_t maximum_pairs = base_pairs + dirty_islands;
    const std::size_t pages = (bodies + 255U) / 256U;
    std::vector<NormalContact> split_contacts(base_pairs), merged_contacts;
    std::vector<BroadphasePair> split_pairs(base_pairs), merged_pairs;
    for (std::size_t i = 0U; i < base_pairs; ++i) {
        split_contacts[i] = {i * 2U, i * 2U + 1U, {kOne, 0}};
        split_pairs[i] = {i * 2U, i * 2U + 1U};
    }
    merged_contacts = split_contacts;
    merged_pairs = split_pairs;
    std::vector<std::size_t> dirty;
    dirty.reserve(dirty_islands * 4U);
    for (std::size_t island = 0U; island < dirty_islands; ++island) {
        const std::size_t first_pair = island * 2U;
        const std::size_t bridge_first = first_pair * 2U + 1U;
        const std::size_t bridge_second = bridge_first + 1U;
        merged_contacts.push_back({bridge_first, bridge_second, {kOne, 0}});
        merged_pairs.push_back({bridge_first, bridge_second});
        for (std::size_t body = first_pair * 2U; body < first_pair * 2U + 4U; ++body) dirty.push_back(body);
    }
    std::sort(merged_pairs.begin(), merged_pairs.end());
    PagedSegmentedPairHistory history({
        .bodies = bodies,
        .maximum_contacts = maximum_pairs,
        .maximum_pairs = maximum_pairs,
        .maximum_pairs_per_segment = 3U,
        .history_capacity = 20U,
        .table_page_elements = 256U,
        .segment_generations = base_pairs + dirty_islands * frames * 3U + 1'024U,
        .spill_generations = 8U,
        .table_generations = 24U,
        .body_key_page_generations = pages * frames + 64U,
        .segment_map_page_generations = pages * frames + 128U,
    });
    history.initialize(0U, split_contacts, split_pairs);
    std::vector<BroadphasePair> restored(maximum_pairs);
    std::vector<double> capture_samples, restore_samples;
    capture_samples.reserve(frames); restore_samples.reserve(frames);
    for (std::uint64_t frame = 1U; frame <= frames; ++frame) {
        const bool merged = (frame & 1U) != 0U;
        const auto& contacts = merged ? merged_contacts : split_contacts;
        const auto& pairs = merged ? merged_pairs : split_pairs;
        auto begin = Clock::now();
        history.capture(frame, contacts, pairs, dirty, true, true, false);
        auto end = Clock::now();
        capture_samples.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        begin = Clock::now();
        const std::size_t count = history.restore_pairs(frame, restored);
        end = Clock::now();
        restore_samples.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        if (count != pairs.size() || !std::equal(pairs.begin(), pairs.end(), restored.begin())) {
            throw std::runtime_error("v0.25 multi-dirty restore mismatch");
        }
    }
    const auto stats = history.stats();
    return {
        .dirty_islands = dirty_islands,
        .capture_p95_us = percentile(capture_samples, .95),
        .restore_p95_us = percentile(restore_samples, .95),
        .segments_written = stats.segments_written,
        .segment_pages_written = stats.segment_pages_written,
        .hash = history.hash(frames),
    };
}

using Rational = boost::multiprecision::cpp_rational;
using Integer = boost::multiprecision::cpp_int;

Rational parse_rational(const std::string& numerator_text, const std::string& denominator_text) {
    return Rational(Integer(numerator_text)) / Integer(denominator_text);
}

struct CandidateAudit final {
    std::size_t cases{};
    std::size_t exact_feasible{};
    std::size_t exact_optimal{};
    std::size_t infeasible{};
    double p95_us{};
    std::uint64_t hash{};
};

CandidateAudit candidate_audit() {
    constexpr std::size_t cases = 256U;
    std::mt19937_64 rng(0x2500250025002500ULL);
    ArbitraryNormalScratch scratch(8U, 7U);
    CandidateAudit audit{.cases = cases, .hash = 0xCBF29CE484222325ULL};
    std::vector<double> samples; samples.reserve(cases);
    for (std::size_t sample = 0U; sample < cases; ++sample) {
        const std::size_t bodies = 3U + static_cast<std::size_t>(rng() % 6U);
        std::vector<Fixed::rep> input_x(bodies), input_y(bodies), vx(bodies), vy(bodies);
        std::vector<std::uint32_t> masses(bodies);
        std::vector<NormalContact> contacts; contacts.reserve(bodies - 1U);
        for (std::size_t body = 0U; body < bodies; ++body) {
            input_x[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 41U) - 20);
            input_y[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng() % 41U) - 20);
            masses[body] = 1U + static_cast<std::uint32_t>(rng() % 9U);
            if (body != 0U) {
                const std::size_t parent = static_cast<std::size_t>(rng() % body);
                const NormalQ30 normal = kNormals[rng() % kNormals.size()];
                contacts.push_back((rng() & 1U) == 0U
                    ? NormalContact{parent, body, normal}
                    : NormalContact{body, parent, normal});
            }
        }
        std::shuffle(contacts.begin(), contacts.end(), rng);
        vx = input_x; vy = input_y;
        const auto begin = Clock::now();
        const auto stats = project_arbitrary_normals_inplace(vx, vy, masses, contacts,
            {.maximum_iterations = 64U, .feasibility_tolerance_raw = 0U,
             .unit_norm_tolerance_q60 = std::numeric_limits<std::uint64_t>::max()}, scratch);
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
        const auto oracle = solve_exact_oblique_tree_active_sets(input_x, input_y, masses, contacts,
            {.maximum_bodies = 8U, .maximum_contacts = 7U,
             .quantized_repair_radius = 0U, .perform_quantized_repair = false});
        if (!oracle.certified_continuous) throw std::runtime_error("v0.25 oracle failed to certify");
        bool feasible = true;
        for (const NormalContact& edge : contacts) {
            const WideInteger violation = WideInteger(edge.normal.x) * (vx[edge.first] - vx[edge.second])
                + WideInteger(edge.normal.y) * (vy[edge.first] - vy[edge.second]);
            if (violation > 0) { feasible = false; break; }
        }
        if (feasible) ++audit.exact_feasible; else ++audit.infeasible;
        Integer objective = 0;
        for (std::size_t body = 0U; body < bodies; ++body) {
            const Integer dx = Integer(vx[body]) - input_x[body];
            const Integer dy = Integer(vy[body]) - input_y[body];
            objective += Integer(masses[body]) * (dx * dx + dy * dy);
        }
        if (feasible && Rational(objective) == parse_rational(oracle.objective_numerator, oracle.objective_denominator)) {
            ++audit.exact_optimal;
        }
        mix_u64(audit.hash, stats.coordinate_updates);
        mix_u64(audit.hash, static_cast<std::uint64_t>(feasible));
        mix_u64(audit.hash, static_cast<std::uint64_t>(vx[0]));
        mix_u64(audit.hash, static_cast<std::uint64_t>(vy[0]));
    }
    audit.p95_us = percentile(samples, .95);
    return audit;
}

} // namespace
int main(int argc,char**argv){
 const std::filesystem::path out=argc>1?argv[1]:"artifacts/v0.25-benchmark";
 std::filesystem::create_directories(out);
 const auto d=matching(); const auto rb=rollback_bench(d); const auto rs=restore_bench();
 const auto dy=dynamic_bench(); const auto st=star_bench();
 std::array<MultiDirty,4> dirty{{multi_dirty_bench(1U),multi_dirty_bench(8U),multi_dirty_bench(64U),multi_dirty_bench(256U)}};
 const auto audit=candidate_audit();
 std::ofstream csv(out/"v25_cycle.csv");
 csv<<"operation,p50,p95,reserved_bytes,cpp_allocations,c_allocations,hash,detail_a,detail_b\n";
 csv<<"integrated_rollback_ms,"<<std::fixed<<std::setprecision(6)<<rb.p50<<','<<rb.p95<<','<<rb.bytes<<','<<rb.cpp<<','<<rb.c<<",0x"<<std::hex<<std::uppercase<<rb.hash<<std::dec<<','<<rb.stats.direct_ordered_restores<<','<<rb.stats.merged_restores<<"\n";
 csv<<"direct_restore_us,0,"<<std::fixed<<std::setprecision(3)<<rs.p95<<",0,0,0,0x"<<std::hex<<std::uppercase<<rs.hash<<std::dec<<','<<rs.direct<<','<<rs.merged<<"\n";
 csv<<"dynamic_capture_us,0,"<<dy.capture_p95<<",0,0,0,0x"<<std::hex<<std::uppercase<<dy.hash<<std::dec<<','<<dy.direct<<','<<dy.merged<<"\n";
 csv<<"dynamic_restore_us,0,"<<dy.restore_p95<<",0,0,0,0x"<<std::hex<<std::uppercase<<dy.hash<<std::dec<<','<<dy.direct<<','<<dy.merged<<"\n";
 csv<<"oblique_star_us,"<<st.p50<<','<<st.p95<<",0,0,0,0x"<<std::hex<<std::uppercase<<st.hash<<std::dec<<','<<st.iterations<<','<<st.active<<"\n";
 for(const auto& row:dirty) csv<<"multi_dirty_capture_"<<row.dirty_islands<<"_us,0,"<<row.capture_p95_us<<",0,0,0,0x"<<std::hex<<std::uppercase<<row.hash<<std::dec<<','<<row.segments_written<<','<<row.segment_pages_written<<"\n";
 csv<<"tree_candidate_audit_us,0,"<<audit.p95_us<<",0,0,0,0x"<<std::hex<<std::uppercase<<audit.hash<<std::dec<<','<<audit.exact_feasible<<','<<audit.exact_optimal<<"\n";
 std::ofstream json(out/"summary.json");
 json<<"{\n  \"rollback_p95_ms\": "<<std::fixed<<std::setprecision(6)<<rb.p95
     <<",\n  \"reserved_bytes\": "<<rb.bytes
     <<",\n  \"direct_restore_p95_us\": "<<rs.p95
     <<",\n  \"multi_dirty_256_capture_p95_us\": "<<dirty.back().capture_p95_us
     <<",\n  \"candidate_cases\": "<<audit.cases
     <<",\n  \"candidate_exact_feasible\": "<<audit.exact_feasible
     <<",\n  \"candidate_exact_optimal\": "<<audit.exact_optimal
     <<",\n  \"cpp_allocations\": "<<rb.cpp<<",\n  \"c_allocations\": "<<rb.c<<"\n}\n";
 std::cout<<"v0.25 rollback="<<rb.p95<<" ms reserve="<<rb.bytes<<" direct_restore="<<rs.p95
          <<" us dirty256="<<dirty.back().capture_p95_us<<" us candidate="<<audit.exact_feasible
          <<'/'<<audit.exact_optimal<<" hash=0x"<<std::hex<<std::uppercase<<audit.hash<<std::dec<<'\n';
 return EXIT_SUCCESS;
}
