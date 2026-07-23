#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
#include "neoeng/core/atomic_temporal_physics.hpp"

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
#include <vector>

namespace allocation_probe {
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> cpp_allocations{0};
std::atomic<std::uint64_t> cpp_bytes{0};
std::atomic<std::uint64_t> c_allocations{0};
std::atomic<std::uint64_t> c_bytes{0};
void record_cpp(std::size_t size) noexcept {
    if (enabled.load(std::memory_order_relaxed)) {
        cpp_allocations.fetch_add(1U, std::memory_order_relaxed);
        cpp_bytes.fetch_add(size, std::memory_order_relaxed);
    }
}
void record_c(std::size_t size) noexcept {
    if (enabled.load(std::memory_order_relaxed)) {
        c_allocations.fetch_add(1U, std::memory_order_relaxed);
        c_bytes.fetch_add(size, std::memory_order_relaxed);
    }
}
}

extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(std::size_t size) {
    allocation_probe::record_c(size); return __real_malloc(size == 0U ? 1U : size);
}
extern "C" void* __wrap_calloc(std::size_t count, std::size_t size) {
    allocation_probe::record_c(count * size); return __real_calloc(count, size);
}
extern "C" void* __wrap_realloc(void* pointer, std::size_t size) {
    allocation_probe::record_c(size); return __real_realloc(pointer, size);
}
extern "C" void __wrap_free(void* pointer) { __real_free(pointer); }

void* operator new(std::size_t size) {
    if (void* pointer = __real_malloc(size == 0U ? 1U : size)) {
        allocation_probe::record_cpp(size); return pointer;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { __real_free(pointer); }
void operator delete[](void* pointer) noexcept { __real_free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { __real_free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    void* pointer = nullptr;
    if (posix_memalign(&pointer, static_cast<std::size_t>(alignment), size == 0U ? 1U : size) != 0) throw std::bad_alloc();
    allocation_probe::record_cpp(size); return pointer;
}
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
};

Dataset make_dataset(std::size_t bodies) {
    Dataset data;
    data.px.resize(bodies); data.py.resize(bodies); data.vx.resize(bodies); data.vy.resize(bodies);
    data.masses.resize(bodies); data.contacts.reserve(bodies / 2U);
    const Fixed::rep separation = Fixed::from_ratio(3, 4).raw();
    const Fixed::rep speed = Fixed::from_ratio(1, 16).raw();
    constexpr std::size_t columns = 100U;
    for (std::size_t pair = 0U; pair < bodies / 2U; ++pair) {
        const std::size_t first = pair * 2U; const std::size_t second = first + 1U;
        const NormalQ30 normal = kNormals[pair % kNormals.size()];
        const Fixed::rep center_x = Fixed::from_integer(static_cast<Fixed::rep>((pair % columns) * 4U)).raw();
        const Fixed::rep center_y = Fixed::from_integer(static_cast<Fixed::rep>((pair / columns) * 4U)).raw();
        const Fixed::rep dx = mul_q30(normal.x, separation); const Fixed::rep dy = mul_q30(normal.y, separation);
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

struct Row final {
    const char* mode{}; double p50{}, p95{};
    std::uint64_t cpp_allocations{}, cpp_bytes{}, c_allocations{}, c_bytes{};
    std::uint64_t builds{}, reuses{}, narrow_tests{}, contacts{}, hash{}, physical_hash{};
    std::size_t pairs{}, reserved{};
};

Row measure(const Dataset& data, bool force_rebuild) {
    AtomicTemporalPhysicsConfig config{
        .bodies = data.vx.size(), .contacts = data.contacts.size(),
        .maximum_candidate_pairs = data.contacts.size() * 2U + 256U,
        .history_capacity = 32U, .horizon_frames = 16U,
        .maximum_velocity_mutations = 2U, .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U,
        .half_extent = Fixed::from_ratio(1, 2),
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
        .force_rebuild_each_frame = force_rebuild,
    };
    AtomicTemporalPhysicsEngine rollback(config), clean(config);
    std::array<VelocityMutation, 16> original{}, corrected{};
    for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
        const std::size_t body = static_cast<std::size_t>((frame * 613U) % data.vx.size());
        original[frame - 1U] = {body, static_cast<Fixed::rep>(frame * 101U), -static_cast<Fixed::rep>(frame * 37U)};
        corrected[frame - 1U] = original[frame - 1U];
    }
    corrected[8].delta_x += 777; corrected[8].delta_y -= 313;
    constexpr std::size_t warmup = 6U, trials = 60U;
    std::vector<double> samples; samples.reserve(trials);
    std::uint64_t max_cpp = 0U, max_cpp_bytes = 0U, max_c = 0U, max_c_bytes = 0U;
    std::uint64_t builds = 0U, reuses = 0U, narrow = 0U, contacts = 0U, hash = 0U, physical_hash = 0U;
    std::size_t pairs = 0U, reserved = 0U;
    for (std::size_t trial = 0U; trial < warmup + trials; ++trial) {
        rollback.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        clean.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
            rollback.set_input(frame, {.velocity = std::span<const VelocityMutation>(&original[frame - 1U], 1U)});
            clean.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
        }
        rollback.simulate_to(16U); clean.simulate_to(16U);
        const AtomicTemporalPhysicsStats before = rollback.stats();
        allocation_probe::cpp_allocations.store(0U); allocation_probe::cpp_bytes.store(0U);
        allocation_probe::c_allocations.store(0U); allocation_probe::c_bytes.store(0U);
        const auto begin = Clock::now(); allocation_probe::enabled.store(true);
        rollback.correct_and_resimulate(9U,
            {.velocity = std::span<const VelocityMutation>(&corrected[8], 1U)}, 16U);
        allocation_probe::enabled.store(false); const auto end = Clock::now();
        if (!rollback.equivalent_to(clean)) throw std::runtime_error("v0.16 rollback diverged from clean execution");
        if (trial >= warmup) samples.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
        max_cpp = std::max(max_cpp, allocation_probe::cpp_allocations.load());
        max_cpp_bytes = std::max(max_cpp_bytes, allocation_probe::cpp_bytes.load());
        max_c = std::max(max_c, allocation_probe::c_allocations.load());
        max_c_bytes = std::max(max_c_bytes, allocation_probe::c_bytes.load());
        const AtomicTemporalPhysicsStats after = rollback.stats();
        builds = after.broadphase_builds - before.broadphase_builds;
        reuses = after.broadphase_reuses - before.broadphase_reuses;
        narrow = after.narrowphase_pair_tests - before.narrowphase_pair_tests;
        contacts = after.narrowphase_contacts - before.narrowphase_contacts;
        hash = rollback.hash(); physical_hash = rollback.physical_hash();
        pairs = rollback.candidate_pair_count(); reserved = rollback.reserved_bytes();
    }
    return Row{force_rebuild ? "rebuild_each_frame" : "temporal_cache",
        percentile(samples, .50), percentile(samples, .95), max_cpp, max_cpp_bytes,
        max_c, max_c_bytes, builds, reuses, narrow, contacts, hash, physical_hash, pairs, reserved};
}

void write_outputs(const std::filesystem::path& directory, const std::array<Row, 2>& rows) {
    std::filesystem::create_directories(directory);
    std::ofstream csv(directory / "atomic_temporal_rollback.csv");
    csv << "mode,p50_ms,p95_ms,cpp_allocations,cpp_bytes,c_allocations,c_bytes,broadphase_builds,broadphase_reuses,narrowphase_tests,contacts,pairs,reserved_bytes,hash,physical_hash\n";
    for (const Row& row : rows) {
        csv << row.mode << ',' << std::fixed << std::setprecision(6) << row.p50 << ',' << row.p95 << ','
            << row.cpp_allocations << ',' << row.cpp_bytes << ',' << row.c_allocations << ',' << row.c_bytes << ','
            << row.builds << ',' << row.reuses << ',' << row.narrow_tests << ',' << row.contacts << ','
            << row.pairs << ',' << row.reserved << ",0x" << std::hex << std::uppercase << row.hash
            << ",0x" << row.physical_hash << std::dec << '\n';
    }
    std::ofstream json(directory / "summary.json");
    json << "{\n  \"version\": \"0.16\",\n  \"gate_ms\": 2.0,\n  \"temporal_p95_ms\": "
         << std::fixed << std::setprecision(6) << rows[0].p95
         << ",\n  \"rebuild_p95_ms\": " << rows[1].p95
         << ",\n  \"temporal_cpp_allocations\": " << rows[0].cpp_allocations
         << ",\n  \"temporal_c_allocations\": " << rows[0].c_allocations
         << ",\n  \"hash\": \"0x" << std::hex << std::uppercase << rows[0].hash
         << "\",\n  \"physical_hash\": \"0x" << rows[0].physical_hash << std::dec << "\"\n}\n";
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path directory = argc > 1 ? argv[1] : "artifacts/v0.16-benchmark";
    const Dataset data = make_dataset(10'000U);
    const std::array<Row, 2> rows{measure(data, false), measure(data, true)};
    write_outputs(directory, rows);
    for (const Row& row : rows) {
        std::cout << row.mode << " p50=" << row.p50 << " ms p95=" << row.p95
                  << " ms builds=" << row.builds << " reuses=" << row.reuses
                  << " cpp_alloc=" << row.cpp_allocations << " c_alloc=" << row.c_allocations
                  << " pairs=" << row.pairs << " hash=0x" << std::hex << std::uppercase << row.hash
                  << " physical=0x" << row.physical_hash << std::dec << '\n';
    }
    return EXIT_SUCCESS;
}
