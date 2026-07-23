#include "neoeng/core/active_world.hpp"
#include "neoeng/core/component_world.hpp"
#include "neoeng/core/hash.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace neoeng::core;
using Clock = std::chrono::steady_clock;

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
        ComponentWorldState state = make_component_world(make_world(body_count, active_count), 64U);
        DeterministicActiveSet active = DeterministicActiveSet::from_world(state.materialize());
        if (active.size() != active_count) throw std::logic_error("active-set initialization mismatch");

        std::vector<std::uint64_t> durations;
        durations.reserve(measured_samples);
        std::vector<ComponentAllocationStats> allocations;
        allocations.reserve(measured_samples);
        for (std::size_t index = 0U; index < warmup_samples + measured_samples; ++index) {
            const auto begin = Clock::now();
            ComponentStepResult result = step_component_active(state, active, {});
            const auto end = Clock::now();
            state = std::move(result.state);
            active = std::move(result.active);
            if (index >= warmup_samples) {
                durations.push_back(static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count()));
                allocations.push_back(result.allocation);
            }
        }

        std::vector<std::uint64_t> sorted = durations;
        std::sort(sorted.begin(), sorted.end());
        const std::uint64_t p50 = percentile_ns(sorted, 50U, 100U);
        const std::uint64_t p95 = percentile_ns(sorted, 95U, 100U);
        const std::uint64_t p99 = percentile_ns(sorted, 99U, 100U);
        const std::uint64_t maximum = sorted.back();
        const std::uint64_t final_hash = stable_hash(state.materialize());

        std::ofstream csv(output / "ecs_maintenance_samples.csv", std::ios::binary);
        if (!csv) throw std::runtime_error("cannot create ECS sample CSV");
        csv << "sample,duration_ns,component_pages_allocated,directories_allocated,"
               "component_values_copied,directory_entries_copied,candidate_bodies_scanned,"
               "changed_bodies\n";
        for (std::size_t index = 0U; index < durations.size(); ++index) {
            const ComponentAllocationStats& allocation = allocations[index];
            csv << index << ',' << durations[index] << ','
                << allocation.component_pages_allocated << ','
                << allocation.directories_allocated << ','
                << allocation.component_values_copied << ','
                << allocation.directory_entries_copied << ','
                << allocation.candidate_bodies_scanned << ','
                << allocation.changed_bodies << '\n';
        }

        std::ofstream summary(output / "summary.json", std::ios::binary);
        if (!summary) throw std::runtime_error("cannot create ECS summary JSON");
        summary << "{\n"
                << "  \"schema\": \"neoeng.dcore.ecs-maintenance-benchmark.v1\",\n"
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
                << "  \"qualification_note\": \"Timing is diagnostic until accepted by the native profile harness\"\n"
                << "}\n";

        std::cout << "ecs_maintenance_p99_ns=" << p99 << '\n'
                  << "ecs_maintenance_samples=" << measured_samples << '\n'
                  << "final_hash=" << hash_hex(final_hash) << '\n';
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ECS maintenance benchmark failed: " << exception.what() << '\n';
        return 1;
    }
}
