#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

#include "neoeng/core/segmented_authoritative_paged_temporal_physics.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <malloc.h>
#include <windows.h>
#endif

namespace allocation_probe {
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> cpp_allocations{0U};
std::atomic<std::uint64_t> c_allocations{0U};
void cpp() noexcept {
    if (enabled.load(std::memory_order_relaxed)) cpp_allocations.fetch_add(1U, std::memory_order_relaxed);
}
void c() noexcept {
    if (enabled.load(std::memory_order_relaxed)) c_allocations.fetch_add(1U, std::memory_order_relaxed);
}
void reset() noexcept {
    cpp_allocations.store(0U, std::memory_order_relaxed);
    c_allocations.store(0U, std::memory_order_relaxed);
}
} // namespace allocation_probe

#if defined(NEOENG_C_ALLOCATION_PROBE_SUPPORTED)
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void __real_free(void*);
extern "C" void* __real_aligned_alloc(std::size_t, std::size_t);
extern "C" int __real_posix_memalign(void**, std::size_t, std::size_t);
extern "C" void* __wrap_malloc(std::size_t size) { allocation_probe::c(); return __real_malloc(size == 0U ? 1U : size); }
extern "C" void* __wrap_calloc(std::size_t count, std::size_t size) { allocation_probe::c(); return __real_calloc(count, size); }
extern "C" void* __wrap_realloc(void* pointer, std::size_t size) { allocation_probe::c(); return __real_realloc(pointer, size); }
extern "C" void __wrap_free(void* pointer) { __real_free(pointer); }
extern "C" void* __wrap_aligned_alloc(std::size_t alignment, std::size_t size) {
    allocation_probe::c();
    return __real_aligned_alloc(alignment, size);
}
extern "C" int __wrap_posix_memalign(void** pointer, std::size_t alignment, std::size_t size) {
    allocation_probe::c();
    return __real_posix_memalign(pointer, alignment, size);
}
void* operator new(std::size_t size) {
    if (void* pointer = __real_malloc(size == 0U ? 1U : size)) { allocation_probe::cpp(); return pointer; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { __real_free(pointer); }
void operator delete[](void* pointer) noexcept { __real_free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { __real_free(pointer); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { __real_free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    void* pointer = nullptr;
    if (__real_posix_memalign(&pointer, static_cast<std::size_t>(alignment), size == 0U ? 1U : size) != 0) throw std::bad_alloc();
    allocation_probe::cpp();
    return pointer;
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new(size, alignment); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size, alignment); } catch (...) { return nullptr; }
}
void operator delete(void* pointer, std::align_val_t) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { __real_free(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { __real_free(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { __real_free(pointer); }
#else
void* operator new(std::size_t size) {
    if (void* pointer = std::malloc(size == 0U ? 1U : size)) { allocation_probe::cpp(); return pointer; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    const std::size_t alignment_value = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
    void* pointer = _aligned_malloc(size == 0U ? 1U : size, alignment_value);
#else
    const std::size_t requested = size == 0U ? 1U : size;
    const std::size_t rounded = ((requested + alignment_value - 1U) / alignment_value) * alignment_value;
    void* pointer = std::aligned_alloc(alignment_value, rounded);
#endif
    if (pointer == nullptr) throw std::bad_alloc();
    allocation_probe::cpp();
    return pointer;
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new(size, alignment); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size, alignment); } catch (...) { return nullptr; }
}
void operator delete(void* pointer, std::align_val_t) noexcept {
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}
void operator delete[](void* pointer, std::align_val_t alignment) noexcept { ::operator delete(pointer, alignment); }
void operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(pointer, alignment); }
void operator delete[](void* pointer, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(pointer, alignment); }
void operator delete(void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept { ::operator delete(pointer, alignment); }
void operator delete[](void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept { ::operator delete(pointer, alignment); }
#endif

namespace {
struct AllocationCalibration final {
    std::uint64_t cpp_allocations{};
    std::uint64_t c_allocations{};
    bool passed{};
};

volatile std::uintptr_t calibration_sink{};

[[nodiscard]] AllocationCalibration calibrate_allocation_probe() {
    allocation_probe::reset();
    allocation_probe::enabled.store(true, std::memory_order_release);
    bool allocations_succeeded = true;
    try {
        auto* cpp_memory = new std::byte[32U];
        cpp_memory[0] = std::byte{0x3C};
        calibration_sink = reinterpret_cast<std::uintptr_t>(cpp_memory);
        delete[] cpp_memory;
        void* aligned_cpp_memory = ::operator new(64U, std::align_val_t{64U});
        calibration_sink = reinterpret_cast<std::uintptr_t>(aligned_cpp_memory);
        ::operator delete(aligned_cpp_memory, std::align_val_t{64U});
#if defined(NEOENG_C_ALLOCATION_PROBE_SUPPORTED)
        void* malloc_memory = std::malloc(32U);
        calibration_sink = reinterpret_cast<std::uintptr_t>(malloc_memory);
        void* calloc_memory = std::calloc(2U, 16U);
        calibration_sink = reinterpret_cast<std::uintptr_t>(calloc_memory);
        void* realloc_memory = std::malloc(16U);
        calibration_sink = reinterpret_cast<std::uintptr_t>(realloc_memory);
        void* grown_memory = std::realloc(realloc_memory, 48U);
        if (grown_memory != nullptr) realloc_memory = grown_memory;
        calibration_sink = reinterpret_cast<std::uintptr_t>(realloc_memory);
        void* aligned_memory = std::aligned_alloc(64U, 64U);
        calibration_sink = reinterpret_cast<std::uintptr_t>(aligned_memory);
        void* posix_memory = nullptr;
        const int posix_result = ::posix_memalign(&posix_memory, 64U, 64U);
        calibration_sink = reinterpret_cast<std::uintptr_t>(posix_memory);
        allocations_succeeded = malloc_memory != nullptr && calloc_memory != nullptr
            && grown_memory != nullptr && aligned_memory != nullptr
            && posix_result == 0 && posix_memory != nullptr;
        std::free(malloc_memory);
        std::free(calloc_memory);
        std::free(realloc_memory);
        std::free(aligned_memory);
        std::free(posix_memory);
#endif
    } catch (...) {
        allocation_probe::enabled.store(false, std::memory_order_release);
        throw;
    }
    allocation_probe::enabled.store(false, std::memory_order_release);
    const std::uint64_t cpp_count = allocation_probe::cpp_allocations.load(std::memory_order_relaxed);
    const std::uint64_t c_count = allocation_probe::c_allocations.load(std::memory_order_relaxed);
#if defined(NEOENG_C_ALLOCATION_PROBE_SUPPORTED)
    const bool passed = allocations_succeeded && cpp_count >= 2U && c_count >= 6U;
#else
    const bool passed = allocations_succeeded && cpp_count >= 2U;
#endif
    allocation_probe::reset();
    return {.cpp_allocations = cpp_count, .c_allocations = c_count, .passed = passed};
}

using namespace neoeng::core;
using Clock = std::chrono::steady_clock;
#if defined(__clang__)
constexpr std::string_view kCompilerId = "Clang";
constexpr std::string_view kCompilerVersion = __clang_version__;
#elif defined(__GNUC__)
constexpr std::string_view kCompilerId = "GCC";
constexpr std::string_view kCompilerVersion = __VERSION__;
#elif defined(_MSC_VER)
constexpr std::string_view kCompilerId = "MSVC";
#define NEOENG_STRINGIZE_DETAIL(value) #value
#define NEOENG_STRINGIZE(value) NEOENG_STRINGIZE_DETAIL(value)
constexpr std::string_view kCompilerVersion = NEOENG_STRINGIZE(_MSC_VER);
#else
constexpr std::string_view kCompilerId = "Unknown";
constexpr std::string_view kCompilerVersion = "unknown";
#endif
#if defined(__x86_64__) || defined(_M_X64)
constexpr std::string_view kArchitecture = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
constexpr std::string_view kArchitecture = "aarch64";
#else
constexpr std::string_view kArchitecture = "unknown";
#endif
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 4U> kNormals{{
    {kOne, 0}, {0, kOne}, {759250125, 759250125}, {644245094, 858993459},
}};

struct Dataset final {
    std::vector<Fixed::rep> position_x, position_y, velocity_x, velocity_y;
    std::vector<std::uint32_t> masses;
    std::vector<NormalContact> contacts;
};

struct Sample final {
    std::size_t index{};
    double milliseconds{};
    std::uint64_t cpp_allocations{};
    std::uint64_t c_allocations{};
    int cpu_before{-1};
    int cpu_after{-1};
};

[[nodiscard]] std::size_t parse_size(const char* text, const char* field, bool allow_zero = false) {
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    if (end == text || *end != '\0' || (!allow_zero && value == 0ULL)
        || value > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(field);
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] Fixed::rep mul_q30(std::int32_t normal, Fixed::rep magnitude) {
    return static_cast<Fixed::rep>(static_cast<WideInteger>(normal) * magnitude / (WideInteger{1} << 30U));
}

[[nodiscard]] Dataset make_matching_dataset() {
    constexpr std::size_t bodies = 10'000U;
    constexpr std::size_t pairs = 5'000U;
    constexpr std::size_t columns = 100U;
    Dataset data;
    data.position_x.resize(bodies);
    data.position_y.resize(bodies);
    data.velocity_x.resize(bodies);
    data.velocity_y.resize(bodies);
    data.masses.resize(bodies);
    data.contacts.reserve(pairs);
    const Fixed::rep separation = Fixed::from_ratio(3, 4).raw();
    const Fixed::rep speed = Fixed::from_ratio(1, 16).raw();
    for (std::size_t body = 0U; body < bodies; ++body) {
        data.position_x[body] = Fixed::from_integer(static_cast<Fixed::rep>((body % columns) * 4U)).raw();
        data.position_y[body] = Fixed::from_integer(static_cast<Fixed::rep>((body / columns) * 4U)).raw();
        data.masses[body] = 1U + static_cast<std::uint32_t>((body * 17U) % 64U);
    }
    for (std::size_t pair = 0U; pair < pairs; ++pair) {
        const NormalQ30 normal = kNormals[pair % kNormals.size()];
        const std::size_t first = pair * 2U;
        const std::size_t second = first + 1U;
        const Fixed::rep center_x = Fixed::from_integer(static_cast<Fixed::rep>((pair % columns) * 4U)).raw();
        const Fixed::rep center_y = Fixed::from_integer(static_cast<Fixed::rep>((pair / columns) * 4U)).raw();
        const Fixed::rep delta_x = mul_q30(normal.x, separation);
        const Fixed::rep delta_y = mul_q30(normal.y, separation);
        data.position_x[first] = center_x - delta_x / 2;
        data.position_y[first] = center_y - delta_y / 2;
        data.position_x[second] = center_x + delta_x / 2;
        data.position_y[second] = center_y + delta_y / 2;
        data.velocity_x[first] = mul_q30(normal.x, speed);
        data.velocity_y[first] = mul_q30(normal.y, speed);
        data.velocity_x[second] = -data.velocity_x[first];
        data.velocity_y[second] = -data.velocity_y[first];
        data.contacts.push_back({first, second, normal});
    }
    return data;
}

[[nodiscard]] SegmentedAuthoritativeTemporalConfig make_config(const Dataset& data) {
    const AtomicTemporalPhysicsConfig physics{
        .bodies = data.velocity_x.size(),
        .contacts = data.contacts.size(),
        .maximum_candidate_pairs = data.contacts.size() * 2U + 512U,
        .history_capacity = 32U,
        .horizon_frames = 16U,
        .maximum_velocity_mutations = 2U,
        .maximum_mass_mutations = 1U,
        .maximum_contact_mutations = 1U,
        .half_extent = Fixed::from_ratio(1, 2),
        .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
    };
    const PagedAtomicTemporalConfig paged{
        .physics = physics,
        .history = {
            .bodies = physics.bodies,
            .contacts = physics.contacts,
            .maximum_candidate_pairs = physics.maximum_candidate_pairs,
            .history_capacity = 32U,
            .page_elements = 256U,
            .maximum_position_dirty_pages_per_frame = 0U,
            .maximum_velocity_dirty_pages_per_frame = 4U,
            .maximum_mass_dirty_pages_per_frame = 2U,
            .maximum_contact_dirty_pages_per_frame = 4U,
            .full_position_generations = 2U,
            .full_velocity_generations = 4U,
            .full_contact_generations = 4U,
            .maximum_cache_generations = 6U,
        },
    };
    const std::size_t pages = (physics.bodies + 255U) / 256U;
    return {
        .physics = paged,
        .pair_history = {
            .bodies = physics.bodies,
            .maximum_contacts = physics.contacts,
            .maximum_pairs = physics.maximum_candidate_pairs,
            .maximum_pairs_per_segment = 1U,
            .history_capacity = 32U,
            .table_page_elements = 256U,
            .segment_generations = physics.contacts + 96U,
            .spill_generations = 6U,
            .table_generations = 34U,
            .body_key_page_generations = pages + 8U,
            .segment_map_page_generations = pages + 40U,
        },
    };
}

[[nodiscard]] bool apply_affinity(std::size_t cpu) {
#if defined(__linux__)
    if (cpu >= static_cast<std::size_t>(CPU_SETSIZE)) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    return sched_setaffinity(0, sizeof(set), &set) == 0;
#elif defined(_WIN32)
    if (cpu >= sizeof(DWORD_PTR) * 8U) return false;
    const DWORD_PTR mask = DWORD_PTR{1} << cpu;
    return SetProcessAffinityMask(GetCurrentProcess(), mask) != 0;
#else
    (void)cpu;
    return false;
#endif
}

[[nodiscard]] int current_cpu() noexcept {
#if defined(__linux__)
    return sched_getcpu();
#elif defined(_WIN32)
    return static_cast<int>(GetCurrentProcessorNumber());
#else
    return -1;
#endif
}

[[nodiscard]] double percentile(const std::vector<double>& sorted, double fraction) {
    return sorted[static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1U))];
}

void write_json_bool(std::ostream& stream, bool value) { stream << (value ? "true" : "false"); }
} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1 ? argv[1] : "artifacts/v0.28-bare-metal-smoke";
        const std::size_t trials = argc > 2 ? parse_size(argv[2], "invalid trial count") : 200U;
        const bool affinity_requested = argc > 3;
        const std::size_t requested_cpu = affinity_requested ? parse_size(argv[3], "invalid CPU index", true) : 0U;
        std::filesystem::create_directories(output);
        const AllocationCalibration allocation_calibration = calibrate_allocation_probe();
        if (!allocation_calibration.passed) throw std::runtime_error("allocation probe calibration failed");
        const bool affinity_applied = !affinity_requested || apply_affinity(requested_cpu);
        if (!affinity_applied) throw std::runtime_error("failed to apply requested CPU affinity");

        const Dataset data = make_matching_dataset();
        const SegmentedAuthoritativeTemporalConfig config = make_config(data);
        std::array<VelocityMutation, 16U> original{}, corrected{};
        for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
            const std::size_t body = (frame * 17U) % data.position_x.size();
            original[frame - 1U] = {body, static_cast<Fixed::rep>(frame * 101U), -static_cast<Fixed::rep>(frame * 37U)};
            corrected[frame - 1U] = original[frame - 1U];
        }
        corrected[8].delta_x += 777;
        corrected[8].delta_y -= 313;

        SegmentedAuthoritativePagedTemporalPhysicsEngine clean(config);
        clean.initialize(data.position_x, data.position_y, data.velocity_x, data.velocity_y, data.masses, data.contacts);
        for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
            clean.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
        }
        clean.simulate_to(16U);
        const std::uint64_t expected_hash = clean.physical_hash();
        const std::uint64_t expected_pair_hash = clean.authoritative_pair_hash();

        constexpr std::size_t warmup = 16U;
        std::vector<Sample> samples;
        samples.reserve(trials);
        std::uint64_t maximum_cpp_allocations = 0U;
        std::uint64_t maximum_c_allocations = 0U;
        bool cpu_migration_detected = false;

        for (std::size_t index = 0U; index < warmup + trials; ++index) {
            SegmentedAuthoritativePagedTemporalPhysicsEngine engine(config);
            engine.initialize(data.position_x, data.position_y, data.velocity_x, data.velocity_y, data.masses, data.contacts);
            for (std::uint64_t frame = 1U; frame <= 16U; ++frame) {
                engine.set_input(frame, {.velocity = std::span<const VelocityMutation>(&original[frame - 1U], 1U)});
            }
            engine.simulate_to(16U);
            allocation_probe::reset();
            const int cpu_before = current_cpu();
            allocation_probe::enabled.store(true, std::memory_order_release);
            const auto begin = Clock::now();
            engine.correct_and_resimulate(9U,
                {.velocity = std::span<const VelocityMutation>(&corrected[8], 1U)}, 16U);
            const auto end = Clock::now();
            allocation_probe::enabled.store(false, std::memory_order_release);
            const int cpu_after = current_cpu();
            if (engine.physical_hash() != expected_hash || engine.authoritative_pair_hash() != expected_pair_hash) {
                throw std::runtime_error("v0.28 rollback semantic mismatch");
            }
            const std::uint64_t cpp_allocations = allocation_probe::cpp_allocations.load(std::memory_order_relaxed);
            const std::uint64_t c_allocations = allocation_probe::c_allocations.load(std::memory_order_relaxed);
            maximum_cpp_allocations = std::max(maximum_cpp_allocations, cpp_allocations);
            maximum_c_allocations = std::max(maximum_c_allocations, c_allocations);
            if (cpu_before >= 0 && cpu_after >= 0 && cpu_before != cpu_after) cpu_migration_detected = true;
            if (index >= warmup) {
                samples.push_back({
                    .index = index - warmup,
                    .milliseconds = std::chrono::duration<double, std::milli>(end - begin).count(),
                    .cpp_allocations = cpp_allocations,
                    .c_allocations = c_allocations,
                    .cpu_before = cpu_before,
                    .cpu_after = cpu_after,
                });
            }
        }

        std::vector<double> durations;
        durations.reserve(samples.size());
        for (const Sample& sample : samples) durations.push_back(sample.milliseconds);
        if (durations.empty()) throw std::logic_error("rollback benchmark produced no samples");
        std::sort(durations.begin(), durations.end());
        const double p50 = percentile(durations, 0.50);
        const double p95 = percentile(durations, 0.95);
        const double p99 = percentile(durations, 0.99);
        const double maximum = durations.back();
#if defined(NEOENG_C_ALLOCATION_PROBE_SUPPORTED)
        constexpr bool c_probe_supported = true;
#else
        constexpr bool c_probe_supported = false;
#endif
        const bool semantic_gate = maximum_cpp_allocations == 0U
            && (!c_probe_supported || maximum_c_allocations == 0U)
            && !cpu_migration_detected;
        const bool timing_gate = p95 <= 2.0 && maximum <= 2.0;

        std::ofstream csv(output / "rollback_samples.csv");
        csv << "sample,duration_ms,cpp_allocations,c_allocations,cpu_before,cpu_after\n";
        for (const Sample& sample : samples) {
            csv << sample.index << ',' << std::fixed << std::setprecision(9) << sample.milliseconds << ','
                << sample.cpp_allocations << ',' << sample.c_allocations << ','
                << sample.cpu_before << ',' << sample.cpu_after << '\n';
        }
        std::ofstream json(output / "summary.json");
        json << "{\n"
             << "  \"version\": \"0.28.0-development\",\n"
             << "  \"compiler_id\": \"" << kCompilerId << "\",\n"
             << "  \"compiler_version\": \"" << kCompilerVersion << "\",\n"
             << "  \"architecture\": \"" << kArchitecture << "\",\n"
             << "  \"workload\": \"canonical_10000_bodies_5000_contacts_rollback_8_frames\",\n"
             << "  \"warmup_samples\": " << warmup << ",\n"
             << "  \"measured_samples\": " << trials << ",\n"
             << "  \"affinity_requested\": "; write_json_bool(json, affinity_requested); json << ",\n"
             << "  \"affinity_applied\": "; write_json_bool(json, affinity_applied); json << ",\n"
             << "  \"requested_cpu\": " << requested_cpu << ",\n"
             << "  \"cpu_migration_detected\": "; write_json_bool(json, cpu_migration_detected); json << ",\n"
             << "  \"p50_ms\": " << std::fixed << std::setprecision(9) << p50 << ",\n"
             << "  \"p95_ms\": " << p95 << ",\n"
             << "  \"p99_ms\": " << p99 << ",\n"
             << "  \"maximum_ms\": " << maximum << ",\n"
             << "  \"physical_hash\": \"0x" << std::hex << std::uppercase << expected_hash << std::dec << "\",\n"
             << "  \"pair_hash\": \"0x" << std::hex << std::uppercase << expected_pair_hash << std::dec << "\",\n"
             << "  \"maximum_cpp_allocations\": " << maximum_cpp_allocations << ",\n"
             << "  \"allocation_probe_calibrated\": "; write_json_bool(json, allocation_calibration.passed); json << ",\n"
             << "  \"calibration_cpp_allocations\": " << allocation_calibration.cpp_allocations << ",\n"
             << "  \"calibration_c_allocations\": " << allocation_calibration.c_allocations << ",\n"
             << "  \"c_allocation_probe_supported\": "; write_json_bool(json, c_probe_supported); json << ",\n"
             << "  \"maximum_c_allocations\": " << maximum_c_allocations << ",\n"
             << "  \"semantic_gate_passed\": "; write_json_bool(json, semantic_gate); json << ",\n"
             << "  \"timing_gate_passed_for_this_run\": "; write_json_bool(json, timing_gate); json << "\n"
             << "}\n";

        std::cout << "v0.28 rollback p50=" << p50 << " ms p95=" << p95 << " ms p99=" << p99
                  << " ms max=" << maximum << " ms hash=0x" << std::hex << std::uppercase << expected_hash
                  << std::dec << " alloc=" << maximum_cpp_allocations << '/' << maximum_c_allocations
                  << " migration=" << (cpu_migration_detected ? "yes" : "no") << '\n';
        return semantic_gate ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        allocation_probe::enabled.store(false, std::memory_order_release);
        std::cerr << "v0.28 bare-metal rollback benchmark failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
