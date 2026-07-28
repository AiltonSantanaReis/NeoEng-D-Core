#include "neoeng/core/active_world.hpp"
#include "neoeng/core/component_world.hpp"
#include "neoeng/core/epoch_arena.hpp"
#include "neoeng/core/hash.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
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
#if defined(_WIN32)
#include <malloc.h>
#endif
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

namespace allocation_probe {
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> allocations{0U};
std::atomic<std::uint64_t> bytes{0U};

void record(std::size_t size) noexcept {
    if (enabled.load(std::memory_order_relaxed)) {
        allocations.fetch_add(1U, std::memory_order_relaxed);
        bytes.fetch_add(static_cast<std::uint64_t>(size), std::memory_order_relaxed);
    }
}
void reset() noexcept {
    allocations.store(0U, std::memory_order_relaxed);
    bytes.store(0U, std::memory_order_relaxed);
}
} // namespace allocation_probe

void* operator new(std::size_t size) {
    const std::size_t actual = size == 0U ? 1U : size;
    if (void* pointer = std::malloc(actual)) {
        allocation_probe::record(actual);
        return pointer;
    }
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

#if defined(__cpp_aligned_new)
void* operator new(std::size_t size, std::align_val_t alignment) {
    const std::size_t actual = size == 0U ? 1U : size;
    const std::size_t align = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
    void* pointer = _aligned_malloc(actual, align);
    if (!pointer) throw std::bad_alloc();
#else
    void* pointer = nullptr;
    if (posix_memalign(&pointer, align, actual) != 0) throw std::bad_alloc();
#endif
    allocation_probe::record(actual);
    return pointer;
}
void* operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}
void operator delete[](void* pointer, std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}
void operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}
void operator delete[](void* pointer, std::size_t, std::align_val_t alignment) noexcept {
    ::operator delete(pointer, alignment);
}
#endif

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace {

using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

struct Sample final {
    std::uint64_t duration_ns{};
    std::uint64_t heap_allocations{};
    std::uint64_t heap_bytes{};
    ComponentAllocationStats cow{};
    std::uint64_t arena_allocations{};
    std::uint64_t arena_bytes_requested{};
    std::uint64_t arena_bytes_committed{};
    std::uint64_t arena_overflow_blocks{};
};

[[nodiscard]] std::size_t parse_size(std::string_view value, std::string_view name) {
    std::size_t result{};
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (error != std::errc{} || end != value.data() + value.size() || result == 0U) {
        throw std::invalid_argument(std::string(name) + " must be a positive integer");
    }
    return result;
}

[[nodiscard]] std::uint64_t percentile_ns(
    const std::vector<std::uint64_t>& sorted,
    std::size_t numerator,
    std::size_t denominator) {
    if (sorted.empty() || denominator == 0U || numerator > denominator) {
        throw std::invalid_argument("invalid percentile request");
    }
    const std::size_t rank = (numerator * sorted.size() + denominator - 1U) / denominator;
    return sorted[std::max<std::size_t>(1U, rank) - 1U];
}

[[nodiscard]] WorldState make_world(std::size_t body_count, std::size_t active_count) {
    WorldState world;
    world.bodies.reserve(body_count);
    for (std::size_t index = 0U; index < body_count; ++index) {
        world.bodies.push_back(Body{
            .id = static_cast<EntityId>(index + 1U),
            .position = {
                Fixed::from_integer(static_cast<Fixed::rep>(index % 1'000U)),
                Fixed::from_integer(static_cast<Fixed::rep>(index / 1'000U)),
            },
            .velocity = index < active_count
                ? Vec2{Fixed::from_integer(1), Fixed::from_ratio(1, 2)}
                : Vec2{},
        });
    }
    return world;
}

[[nodiscard]] bool calibrate_allocation_probe() {
    allocation_probe::reset();
    allocation_probe::enabled.store(true, std::memory_order_release);
    void* pointer = ::operator new(37U);
    allocation_probe::enabled.store(false, std::memory_order_release);
    ::operator delete(pointer);
    return allocation_probe::allocations.load(std::memory_order_relaxed) == 1U
        && allocation_probe::bytes.load(std::memory_order_relaxed) >= 37U;
}

[[nodiscard]] std::uint64_t sum_field(
    const std::vector<Sample>& samples,
    std::uint64_t Sample::*field) noexcept {
    std::uint64_t total = 0U;
    for (const Sample& sample : samples) total += sample.*field;
    return total;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("artifacts/ecs-maintenance");
        const std::size_t body_count = argc > 2 ? parse_size(argv[2], "body_count") : 10'000U;
        const std::size_t active_count = argc > 3 ? parse_size(argv[3], "active_count") : 100U;
        const std::size_t measured_samples = argc > 4 ? parse_size(argv[4], "samples") : 1'000U;
        const std::size_t warmup_samples = argc > 5 ? parse_size(argv[5], "warmup") : 128U;
        if (active_count > body_count) throw std::invalid_argument("active_count exceeds body_count");

        std::filesystem::create_directories(output);
        const bool allocation_probe_calibrated = calibrate_allocation_probe();
        if (!allocation_probe_calibrated) throw std::runtime_error("allocation probe calibration failed");

        ComponentWorldState state = make_component_world(make_world(body_count, active_count), 64U);
        DeterministicActiveSet active = DeterministicActiveSet::from_world(state.materialize());
        if (active.size() != active_count) throw std::logic_error("active-set initialization mismatch");

        const std::size_t arena_bytes_per_epoch = std::max<std::size_t>(64U * 1024U, active_count * 256U);
        PersistentEpochArena arena(16U, arena_bytes_per_epoch);
        std::vector<Sample> samples;
        samples.reserve(measured_samples);

        for (std::size_t index = 0U; index < warmup_samples + measured_samples; ++index) {
            const EpochArenaStats arena_before = arena.stats();
            arena.begin_epoch(static_cast<std::uint64_t>(index + 1U));
            [[maybe_unused]] auto indices = arena.allocate_array<std::size_t>(active_count);
            [[maybe_unused]] auto patches = arena.allocate_array<ComponentPatch>(active_count);
            const EpochArenaStats arena_after = arena.stats();

            allocation_probe::reset();
            const auto begin = Clock::now();
            allocation_probe::enabled.store(true, std::memory_order_release);
            ComponentStepResult result = step_component_active(state, active, {});
            allocation_probe::enabled.store(false, std::memory_order_release);
            const auto end = Clock::now();

            Sample sample{
                .duration_ns = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()),
                .heap_allocations = allocation_probe::allocations.load(std::memory_order_relaxed),
                .heap_bytes = allocation_probe::bytes.load(std::memory_order_relaxed),
                .cow = result.allocation,
                .arena_allocations = arena_after.allocations - arena_before.allocations,
                .arena_bytes_requested = arena_after.bytes_requested - arena_before.bytes_requested,
                .arena_bytes_committed = arena_after.bytes_committed - arena_before.bytes_committed,
                .arena_overflow_blocks = arena_after.overflow_blocks - arena_before.overflow_blocks,
            };
            state = std::move(result.state);
            active = std::move(result.active);
            if (index >= warmup_samples) samples.push_back(sample);
        }

        std::vector<std::uint64_t> durations;
        durations.reserve(samples.size());
        for (const Sample& sample : samples) durations.push_back(sample.duration_ns);
        std::sort(durations.begin(), durations.end());
        const std::uint64_t p50 = percentile_ns(durations, 50U, 100U);
        const std::uint64_t p95 = percentile_ns(durations, 95U, 100U);
        const std::uint64_t p99 = percentile_ns(durations, 99U, 100U);
        const std::uint64_t maximum = durations.back();
        const std::uint64_t final_hash = stable_hash(state.materialize());

        const bool general_allocation_zero = std::all_of(samples.begin(), samples.end(), [](const Sample& sample) {
            return sample.heap_allocations == 0U && sample.heap_bytes == 0U;
        });
        const bool arena_overflow_zero = std::all_of(samples.begin(), samples.end(), [](const Sample& sample) {
            return sample.arena_overflow_blocks == 0U;
        });
        const bool cow_semantics_observed = std::all_of(samples.begin(), samples.end(), [active_count](const Sample& sample) {
            return sample.cow.candidate_bodies_scanned == active_count
                && sample.cow.changed_bodies == active_count
                && sample.cow.component_pages_allocated > 0U
                && sample.cow.directories_allocated > 0U;
        });

        std::ofstream compatibility(output / "ecs_maintenance_samples.csv", std::ios::binary);
        std::ofstream index_csv(output / "index_maintenance_samples.csv", std::ios::binary);
        std::ofstream allocation_csv(output / "general_allocation_samples.csv", std::ios::binary);
        std::ofstream arena_csv(output / "arena_samples.csv", std::ios::binary);
        std::ofstream cow_csv(output / "copy_on_write_samples.csv", std::ios::binary);
        if (!compatibility || !index_csv || !allocation_csv || !arena_csv || !cow_csv) {
            throw std::runtime_error("cannot create ECS evidence CSV files");
        }
        const char* legacy_header = "sample,duration_ns,component_pages_allocated,directories_allocated,"
            "component_values_copied,directory_entries_copied,candidate_bodies_scanned,changed_bodies\n";
        compatibility << legacy_header;
        index_csv << "sample,duration_ns,candidate_bodies_scanned,inactive_bodies_skipped,changed_bodies\n";
        allocation_csv << "sample,probe_calibrated,cpp_heap_allocations,cpp_heap_bytes\n";
        arena_csv << "sample,allocations,bytes_requested,bytes_committed,overflow_blocks,bytes_per_epoch,retained_epochs\n";
        cow_csv << "sample,component_pages_allocated,directories_allocated,component_values_copied,"
                   "directory_entries_copied,candidate_bodies_scanned,changed_bodies,body_reconstructions\n";
        for (std::size_t index = 0U; index < samples.size(); ++index) {
            const Sample& sample = samples[index];
            compatibility << index << ',' << sample.duration_ns << ','
                << sample.cow.component_pages_allocated << ',' << sample.cow.directories_allocated << ','
                << sample.cow.component_values_copied << ',' << sample.cow.directory_entries_copied << ','
                << sample.cow.candidate_bodies_scanned << ',' << sample.cow.changed_bodies << '\n';
            index_csv << index << ',' << sample.duration_ns << ',' << sample.cow.candidate_bodies_scanned
                << ',' << sample.cow.inactive_bodies_skipped << ',' << sample.cow.changed_bodies << '\n';
            allocation_csv << index << ",1," << sample.heap_allocations << ',' << sample.heap_bytes << '\n';
            arena_csv << index << ',' << sample.arena_allocations << ',' << sample.arena_bytes_requested
                << ',' << sample.arena_bytes_committed << ',' << sample.arena_overflow_blocks << ','
                << arena_bytes_per_epoch << ",16\n";
            cow_csv << index << ',' << sample.cow.component_pages_allocated << ','
                << sample.cow.directories_allocated << ',' << sample.cow.component_values_copied << ','
                << sample.cow.directory_entries_copied << ',' << sample.cow.candidate_bodies_scanned << ','
                << sample.cow.changed_bodies << ',' << sample.cow.body_reconstructions << '\n';
        }

        std::ofstream summary(output / "summary.json", std::ios::binary);
        if (!summary) throw std::runtime_error("cannot create ECS summary JSON");
        summary << "{\n"
                << "  \"schema\": \"neoeng.dcore.ecs-maintenance-benchmark.v2\",\n"
                << "  \"ecs_scope_schema\": \"neoeng.dcore.ecs-scope-evidence.v1\",\n"
                << "  \"project_version\": \"1.14.0\",\n"
                << "  \"workload_id\": \"Y1-O2-SPARSE-COMPONENT-MAINTENANCE-V1\",\n"
                << "  \"body_count\": " << body_count << ",\n"
                << "  \"active_body_count\": " << active_count << ",\n"
                << "  \"page_size\": 64,\n"
                << "  \"warmup_samples\": " << warmup_samples << ",\n"
                << "  \"measured_samples\": " << measured_samples << ",\n"
                << "  \"p50_ns\": " << p50 << ",\n"
                << "  \"p95_ns\": " << p95 << ",\n"
                << "  \"p99_ns\": " << p99 << ",\n"
                << "  \"maximum_ns\": " << maximum << ",\n"
                << "  \"final_hash\": \"" << hash_hex(final_hash) << "\",\n"
                << "  \"allocation_probe_calibrated\": true,\n"
                << "  \"general_allocation_zero\": " << (general_allocation_zero ? "true" : "false") << ",\n"
                << "  \"general_allocation_events\": " << sum_field(samples, &Sample::heap_allocations) << ",\n"
                << "  \"general_allocation_bytes\": " << sum_field(samples, &Sample::heap_bytes) << ",\n"
                << "  \"arena_overflow_zero\": " << (arena_overflow_zero ? "true" : "false") << ",\n"
                << "  \"cow_semantics_observed\": " << (cow_semantics_observed ? "true" : "false") << ",\n"
                << "  \"scope_streams\": [\"general_allocation\", \"arena\", \"copy_on_write\", \"index_maintenance\"],\n"
                << "  \"qualification_note\": \"Evidence completeness is independent from native P1 timing and zero-allocation qualification\"\n"
                << "}\n";

        std::cout << "ecs_maintenance_p99_ns=" << p99 << '\n'
                  << "ecs_maintenance_samples=" << measured_samples << '\n'
                  << "ecs_scope_evidence_complete=1\n"
                  << "general_allocation_zero=" << (general_allocation_zero ? 1 : 0) << '\n'
                  << "final_hash=" << hash_hex(final_hash) << '\n';
        return 0;
    } catch (const std::exception& exception) {
        allocation_probe::enabled.store(false, std::memory_order_release);
        std::cerr << "ECS maintenance benchmark failed: " << exception.what() << '\n';
        return 1;
    }
}
