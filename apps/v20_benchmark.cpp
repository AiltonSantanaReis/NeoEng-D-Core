#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "neoeng/core/oblique_tree_grid_dp.hpp"
#include "neoeng/core/paged_atomic_temporal_physics.hpp"
#include "neoeng/core/versioned_island_pair_history.hpp"
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
#include <span>
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

PagedAtomicTemporalConfig paged_config(const Dataset& data) {
    const AtomicTemporalPhysicsConfig physics{
        .bodies = data.vx.size(), .contacts = data.contacts.size(),
        .maximum_candidate_pairs = data.contacts.size() * 2U + 512U,
        .history_capacity = 32U, .horizon_frames = 16U,
        .maximum_velocity_mutations = 2U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U, .half_extent = Fixed::from_ratio(1, 2),
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
    };
    return {.physics = physics, .history = {
        .bodies = physics.bodies, .contacts = physics.contacts,
        .maximum_candidate_pairs = physics.maximum_candidate_pairs,
        .history_capacity = 32U, .page_elements = 256U,
        .maximum_position_dirty_pages_per_frame = 0U,
        .maximum_velocity_dirty_pages_per_frame = 4U,
        .maximum_mass_dirty_pages_per_frame = 2U,
        .maximum_contact_dirty_pages_per_frame = 4U,
        .full_position_generations = 2U, .full_velocity_generations = 4U,
        .full_contact_generations = 4U, .maximum_cache_generations = 6U}};
}

struct IntegratedResult final {
    double p50_ms{}, p95_ms{};
    std::uint64_t cpp_allocations{}, c_allocations{};
    std::uint64_t physical_hash{}, pair_hash{};
    std::size_t reserved_bytes{};
    VersionedIslandPairHistoryStats pair_stats{};
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
    PagedAtomicTemporalPhysicsEngine clean(paged_config(data));
    clean.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        clean.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
    }
    clean.simulate_to(16U);
    const std::uint64_t expected_physical_hash = clean.physical_hash();

    std::vector<double> samples; samples.reserve(trials);
    IntegratedResult result{};
    for (std::size_t trial = 0U; trial < warmup + trials; ++trial) {
        PagedAtomicTemporalPhysicsEngine engine(paged_config(data));
        engine.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        VersionedIslandPairHistory pairs({
            .bodies = data.vx.size(), .maximum_contacts = data.contacts.size(),
            .history_capacity = 32U, .extra_pairs_per_island = 1U,
            .segment_generations_per_island = 4U});
        const auto initial = engine.state_view();
        pairs.initialize(0U, data.contacts, initial.pairs);
        for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
            engine.set_input(frame, {.velocity = std::span<const VelocityMutation>(&original[frame - 1U], 1U)});
        }
        for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
            engine.simulate_to(frame);
            const auto hints = engine.capture_hints();
            const std::span<const std::size_t> dirty = hints.cache_rebuilt
                ? hints.changed_bodies : std::span<const std::size_t>{};
            pairs.capture(frame, engine.state_view().pairs, dirty, hints.topology_changed);
        }
        engine.set_input(9U, {.velocity = std::span<const VelocityMutation>(&corrected[8], 1U)});
        allocation_probe::cpp_allocations.store(0U);
        allocation_probe::c_allocations.store(0U);
        const auto begin = Clock::now();
        allocation_probe::enabled.store(true);
        engine.restore(8U);
        engine.truncate_after(8U);
        pairs.truncate_after(8U);
        for (std::uint64_t frame = 9U; frame <= 16U; ++frame) {
            engine.simulate_to(frame);
            const auto hints = engine.capture_hints();
            const std::span<const std::size_t> dirty = hints.cache_rebuilt
                ? hints.changed_bodies : std::span<const std::size_t>{};
            pairs.capture(frame, engine.state_view().pairs, dirty, hints.topology_changed);
        }
        allocation_probe::enabled.store(false);
        const auto end = Clock::now();
        if (engine.physical_hash() != expected_physical_hash) {
            throw std::runtime_error("v0.20 rollback diverged from clean execution");
        }
        std::vector<BroadphasePair> restored(data.contacts.size() * 2U + 512U);
        const std::size_t restored_count = pairs.restore(16U, restored);
        const auto state = engine.state_view();
        if (restored_count != state.pairs.size()
            || !std::equal(restored.begin(), restored.begin() + static_cast<std::ptrdiff_t>(restored_count),
                           state.pairs.begin(), state.pairs.end())) {
            throw std::runtime_error("v0.20 segmented pair history diverged from temporal cache");
        }
        if (trial >= warmup) {
            samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        }
        result.cpp_allocations += allocation_probe::cpp_allocations.load();
        result.c_allocations += allocation_probe::c_allocations.load();
        result.physical_hash = engine.physical_hash();
        result.pair_hash = pairs.hash(16U);
        result.reserved_bytes = engine.reserved_bytes() + pairs.reserved_bytes();
        result.pair_stats = pairs.stats();
    }
    result.p50_ms = percentile(samples, .50);
    result.p95_ms = percentile(samples, .95);
    return result;
}

struct GridResult final {
    double p50_ms{}, p95_ms{};
    std::uint64_t objective{}, tested{}, hash{};
    bool certified{};
    std::size_t reserved_bytes{};
};

GridResult measure_grid_tree() {
    constexpr std::size_t bodies = 2'047U;
    std::vector<Fixed::rep> x(bodies), y(bodies);
    std::vector<std::uint32_t> masses(bodies);
    std::vector<NormalContact> contacts; contacts.reserve(bodies - 1U);
    for (std::size_t body = 0U; body < bodies; ++body) {
        x[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>((body * 7U) % 5U) - 2);
        y[body] = static_cast<Fixed::rep>(static_cast<std::int64_t>((body * 11U) % 5U) - 2);
        masses[body] = 1U + static_cast<std::uint32_t>((body * 13U) % 7U);
        if (body != 0U) contacts.push_back({(body - 1U) / 2U, body, kNormals[body % kNormals.size()]});
    }
    ObliqueTreeGridScratch scratch(bodies, 25U);
    std::vector<double> samples; samples.reserve(40U);
    GridResult result{};
    for (std::size_t trial = 0U; trial < 44U; ++trial) {
        const auto begin = Clock::now();
        const auto solved = solve_oblique_tree_grid_dp(x, y, masses, contacts,
            {.minimum_raw = -2, .maximum_raw = 2, .maximum_bodies = bodies,
             .maximum_grid_states = 25U}, scratch);
        const auto end = Clock::now();
        if (!solved.certified_on_grid) throw std::runtime_error("v0.20 grid tree was not certified");
        if (trial >= 4U) samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        result.objective = solved.objective;
        result.tested = solved.state_pairs_tested;
        result.certified = solved.certified_on_grid;
        std::uint64_t hash = 0xCBF29CE484222325ULL;
        for (Fixed::rep value : solved.velocity_x) { hash ^= static_cast<std::uint64_t>(value); hash *= 0x100000001B3ULL; }
        for (Fixed::rep value : solved.velocity_y) { hash ^= static_cast<std::uint64_t>(value); hash *= 0x100000001B3ULL; }
        result.hash = hash;
    }
    result.p50_ms = percentile(samples, .50);
    result.p95_ms = percentile(samples, .95);
    result.reserved_bytes = scratch.reserved_bytes();
    return result;
}

void write_outputs(const std::filesystem::path& directory,
                   const IntegratedResult& integrated, const GridResult& grid) {
    std::filesystem::create_directories(directory);
    std::ofstream csv(directory / "v20_cycle.csv");
    csv << "workload,method,p50_ms,p95_ms,reserved_bytes,cpp_allocations,c_allocations,hash,certified\n";
    csv << "dense_matching_10k_5k,paged_plus_versioned_island_pairs," << std::fixed << std::setprecision(6)
        << integrated.p50_ms << ',' << integrated.p95_ms << ',' << integrated.reserved_bytes << ','
        << integrated.cpp_allocations << ',' << integrated.c_allocations << ",0x" << std::hex
        << std::uppercase << integrated.physical_hash << std::dec << ",true\n";
    csv << "oblique_tree_2047,finite_grid_message_passing," << grid.p50_ms << ',' << grid.p95_ms << ','
        << grid.reserved_bytes << ",0,0,0x" << std::hex << std::uppercase << grid.hash << std::dec
        << ',' << (grid.certified ? "true" : "false") << '\n';

    std::ofstream json(directory / "summary.json");
    json << "{\n"
         << "  \"version\": \"0.20\",\n"
         << "  \"dense_p95_ms\": " << std::fixed << std::setprecision(6) << integrated.p95_ms << ",\n"
         << "  \"dense_cpp_allocations\": " << integrated.cpp_allocations << ",\n"
         << "  \"dense_c_allocations\": " << integrated.c_allocations << ",\n"
         << "  \"island_segments_written\": " << integrated.pair_stats.segments_written << ",\n"
         << "  \"island_segments_reused\": " << integrated.pair_stats.segments_reused << ",\n"
         << "  \"island_pairs_written\": " << integrated.pair_stats.pairs_written << ",\n"
         << "  \"oblique_grid_p95_ms\": " << grid.p95_ms << ",\n"
         << "  \"oblique_grid_objective\": " << grid.objective << ",\n"
         << "  \"oblique_grid_state_pairs_tested\": " << grid.tested << ",\n"
         << "  \"oblique_grid_certified\": " << (grid.certified ? "true" : "false") << "\n"
         << "}\n";
}
} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "artifacts/v0.20-benchmark";
    try {
        const Dataset data = make_matching(10'000U, 5'000U);
        const IntegratedResult integrated = measure_integrated(data);
        const GridResult grid = measure_grid_tree();
        write_outputs(output, integrated, grid);
        std::cout << std::fixed << std::setprecision(6)
                  << "v0.20 dense p95=" << integrated.p95_ms << " ms physical=0x"
                  << std::hex << std::uppercase << integrated.physical_hash << std::dec
                  << " segments_reused=" << integrated.pair_stats.segments_reused << '\n'
                  << "v0.20 oblique-grid p95=" << grid.p95_ms << " ms objective="
                  << grid.objective << " certified=" << grid.certified << '\n';
    } catch (const std::exception& error) {
        std::cerr << "v0.20 benchmark failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
