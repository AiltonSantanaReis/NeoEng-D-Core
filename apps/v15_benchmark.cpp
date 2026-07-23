#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "neoeng/core/atomic_physics.hpp"
#include "neoeng/core/weighted_tree_projection.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
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
    if (void* pointer = std::malloc(size == 0U ? 1U : size)) { allocation_probe::record(size); return pointer; }
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
constexpr NormalQ30 kNormals[]{{kOne,0},{0,kOne},{644'245'094,858'993'459},{759'250'125,759'250'125}};

double percentile(std::vector<double> values, double p) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1U))];
}

struct AtomicDataset final {
    std::vector<Fixed::rep> px, py, vx, vy;
    std::vector<std::uint32_t> masses;
    std::vector<NormalContact> contacts;
};

AtomicDataset make_atomic_dataset(std::size_t bodies) {
    AtomicDataset data;
    data.px.resize(bodies); data.py.resize(bodies); data.vx.resize(bodies); data.vy.resize(bodies);
    data.masses.resize(bodies); data.contacts.reserve(bodies / 2U);
    for (std::size_t i = 0U; i < bodies; ++i) data.masses[i] = 1U + static_cast<std::uint32_t>((i * 17U) % 64U);
    for (std::size_t i = 0U; i < bodies; i += 2U) data.contacts.push_back({i, i + 1U, kNormals[(i / 2U) % 4U]});
    return data;
}

struct AtomicRow { double p50{}, p95{}; std::uint64_t allocations{}, bytes{}, hash{}; std::size_t reserved{}; };
AtomicRow measure_atomic(const AtomicDataset& data) {
    AtomicPhysicsConfig config{.bodies=data.vx.size(), .contacts=data.contacts.size(), .history_capacity=32U,
        .maximum_velocity_mutations=2U, .maximum_mass_mutations=1U, .maximum_contact_mutations=1U,
        .projection={.maximum_iterations=32U,.feasibility_tolerance_raw=16U}};
    AtomicPhysicsEngine rollback(config), clean(config);
    std::vector<VelocityMutation> original(16U), corrected(16U);
    for (std::uint64_t frame=1U; frame<=16U; ++frame) {
        const std::size_t body = static_cast<std::size_t>((frame * 613U) % data.vx.size()) & ~std::size_t{1};
        original[frame-1U] = {body, static_cast<Fixed::rep>(frame*101U), -static_cast<Fixed::rep>(frame*37U)};
        corrected[frame-1U] = original[frame-1U];
    }
    corrected[8].delta_x += 777;
    constexpr std::size_t warmup=8U, trials=80U;
    std::vector<double> samples; samples.reserve(trials);
    std::uint64_t max_alloc=0U,max_bytes=0U,last_hash=0U;
    for (std::size_t trial=0U; trial<warmup+trials; ++trial) {
        rollback.initialize(data.px,data.py,data.vx,data.vy,data.masses,data.contacts);
        clean.initialize(data.px,data.py,data.vx,data.vy,data.masses,data.contacts);
        for (std::uint64_t frame=1U; frame<=16U; ++frame) {
            rollback.set_input(frame,{.velocity=std::span<const VelocityMutation>(&original[frame-1U],1U)});
            clean.set_input(frame,{.velocity=std::span<const VelocityMutation>(&corrected[frame-1U],1U)});
        }
        rollback.simulate_to(16U); clean.simulate_to(16U);
        allocation_probe::allocations.store(0U); allocation_probe::bytes.store(0U);
        const auto begin=Clock::now(); allocation_probe::enabled.store(true);
        rollback.correct_and_resimulate(9U,{.velocity=std::span<const VelocityMutation>(&corrected[8],1U)},16U);
        allocation_probe::enabled.store(false); const auto end=Clock::now();
        if (!rollback.equivalent_to(clean)) throw std::runtime_error("Atomic benchmark rollback diverged");
        if (trial>=warmup) samples.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
        max_alloc=std::max(max_alloc,allocation_probe::allocations.load());
        max_bytes=std::max(max_bytes,allocation_probe::bytes.load());
        last_hash=rollback.hash();
    }
    return {percentile(samples,.50),percentile(samples,.95),max_alloc,max_bytes,last_hash,rollback.reserved_bytes()};
}

struct TreeRow { double p50{},p95{}; std::uint64_t allocations{},bytes{},primal{},stationarity{},hash{}; bool certified{}; };
TreeRow measure_tree(std::size_t bodies) {
    std::vector<DirectedTreeEdge> edges; edges.reserve(bodies-1U);
    for (std::size_t child=1U; child<bodies; ++child) edges.push_back({(child-1U)/2U,child});
    std::vector<Fixed::rep> base_x(bodies),base_y(bodies),vx(bodies),vy(bodies);
    std::vector<std::uint32_t> masses(bodies);
    for (std::size_t i=0U;i<bodies;++i) {
        base_x[i]=static_cast<Fixed::rep>((bodies-i)*1000U + (i%7U)*17U);
        base_y[i]=static_cast<Fixed::rep>((i%11U)*13U); masses[i]=1U+static_cast<std::uint32_t>(i%32U);
    }
    WeightedTreeScratch scratch(bodies);
    constexpr std::size_t warmup=8U,trials=80U;
    std::vector<double> samples; samples.reserve(trials);
    std::uint64_t max_alloc=0,max_bytes=0,last_hash=0; WeightedTreeStats last{};
    for(std::size_t trial=0;trial<warmup+trials;++trial){
        vx=base_x;vy=base_y;
        allocation_probe::allocations.store(0U);allocation_probe::bytes.store(0U);
        const auto begin=Clock::now();allocation_probe::enabled.store(true);
        last=project_weighted_tree_common_normal_inplace(vx,vy,masses,edges,kNormals[2],
            {.maximum_active_set_iterations=8192U,.feasibility_tolerance_raw=8U,.stationarity_tolerance_raw=64U},scratch);
        allocation_probe::enabled.store(false);const auto end=Clock::now();
        if(trial>=warmup)samples.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
        max_alloc=std::max(max_alloc,allocation_probe::allocations.load());max_bytes=std::max(max_bytes,allocation_probe::bytes.load());
        std::uint64_t h=0xCBF29CE484222325ULL;for(std::size_t i=0;i<bodies;++i){h^=static_cast<std::uint64_t>(vx[i]);h*=0x100000001B3ULL;h^=static_cast<std::uint64_t>(vy[i]);h*=0x100000001B3ULL;}last_hash=h;
    }
    return{percentile(samples,.50),percentile(samples,.95),max_alloc,max_bytes,last.residuals.primal_linf_raw,last.residuals.stationarity_linf_raw,last_hash,last.residuals.certified};
}
}

int main(int argc,char** argv){
    try{
        const std::filesystem::path output=argc>1?argv[1]:"artifacts/v0.15-benchmark";std::filesystem::create_directories(output);
        const auto atomic=measure_atomic(make_atomic_dataset(10'000U));
        const auto tree32=measure_tree(32U);const auto tree512=measure_tree(512U);
        std::ofstream csv(output/"atomic_rollback.csv");csv<<"dataset,p50_ms,p95_ms,allocations_max,bytes_max,reserved_bytes,hash\n"<<std::fixed<<std::setprecision(6);
        csv<<"atomic_matching_10000_5000_rollback8,"<<atomic.p50<<','<<atomic.p95<<','<<atomic.allocations<<','<<atomic.bytes<<','<<atomic.reserved<<",0x"<<std::hex<<std::uppercase<<atomic.hash<<std::dec<<'\n';
        std::ofstream trees(output/"weighted_tree.csv");trees<<"bodies,p50_ms,p95_ms,allocations_max,bytes_max,primal_raw,stationarity_raw,certified,hash\n"<<std::fixed<<std::setprecision(6);
        for(const auto& [n,row]:std::vector<std::pair<std::size_t,TreeRow>>{{32U,tree32},{512U,tree512}}){trees<<n<<','<<row.p50<<','<<row.p95<<','<<row.allocations<<','<<row.bytes<<','<<row.primal<<','<<row.stationarity<<','<<(row.certified?1:0)<<",0x"<<std::hex<<std::uppercase<<row.hash<<std::dec<<'\n';}
        std::ofstream summary(output/"summary.json");summary<<"{\n  \"version\": \"0.15\",\n  \"atomic_rollback_p95_ms\": "<<atomic.p95<<",\n  \"atomic_allocations\": "<<atomic.allocations<<",\n  \"atomic_gate_passed\": "<<(atomic.p95<=2.0?"true":"false")<<",\n  \"tree_512_p95_ms\": "<<tree512.p95<<",\n  \"tree_512_certified\": "<<(tree512.certified?"true":"false")<<"\n}\n";
        std::cout<<"atomic rollback p95="<<atomic.p95<<" ms allocations="<<atomic.allocations<<" hash=0x"<<std::hex<<std::uppercase<<atomic.hash<<std::dec<<'\n';
        std::cout<<"tree512 p95="<<tree512.p95<<" ms certified="<<tree512.certified<<" hash=0x"<<std::hex<<std::uppercase<<tree512.hash<<std::dec<<'\n';
        return 0;
    }catch(const std::exception& e){allocation_probe::enabled.store(false);std::cerr<<"v0.15 benchmark failure: "<<e.what()<<'\n';return 1;}
}
