#include "neoeng/core/fixed_slot_allocator.hpp"
#include "neoeng/core/paged_segmented_pair_history.hpp"
#include "neoeng/core/uncertainty_lab.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace neoeng::core;
using Clock = std::chrono::steady_clock;
constexpr std::int32_t kOne = 1 << 30;

[[nodiscard]] double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1U))];
}
void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}
[[nodiscard]] std::string_view mode_name(FixedSlotAllocatorMode mode) noexcept {
    return mode == FixedSlotAllocatorMode::NextFit ? "next_fit" : "hierarchical_bitmap";
}

struct AllocatorBench final {
    FixedSlotAllocatorMode mode{};
    double p50_us{};
    double p95_us{};
    std::uint64_t probes{};
    std::uint64_t hash{};
    std::size_t peak_used{};
};

AllocatorBench allocator_bench(FixedSlotAllocatorMode mode) {
    constexpr std::size_t capacity = 65'536U;
    constexpr std::size_t initially_used = capacity - 32U;
    constexpr std::size_t batches = 256U;
    constexpr std::size_t operations_per_batch = 1'024U;
    FixedSlotAllocator allocator(capacity, mode);
    std::vector<std::uint32_t> active(initially_used);
    for (std::size_t index = 0U; index < initially_used; ++index) active[index] = allocator.acquire();
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    std::vector<double> samples; samples.reserve(batches);
    for (std::size_t batch = 0U; batch < batches; ++batch) {
        const auto begin = Clock::now();
        for (std::size_t operation = 0U; operation < operations_per_batch; ++operation) {
            state ^= state << 7U; state ^= state >> 9U; state ^= state << 8U;
            const std::size_t position = static_cast<std::size_t>(state % active.size());
            allocator.release(active[position]);
            active[position] = allocator.acquire();
            mix_u64(hash, active[position]);
        }
        const auto end = Clock::now();
        samples.push_back(std::chrono::duration<double, std::micro>(end - begin).count());
    }
    const auto stats = allocator.stats();
    if (stats.used != initially_used || allocator.free_count() != capacity - initially_used) {
        throw std::runtime_error("v0.26 allocator accounting mismatch");
    }
    return {.mode = mode, .p50_us = percentile(samples, .50), .p95_us = percentile(samples, .95),
        .probes = stats.probes, .hash = hash, .peak_used = stats.peak_used};
}

struct MultiDirty final {
    FixedSlotAllocatorMode mode{};
    std::size_t dirty_islands{};
    double capture_p95_us{};
    double restore_p95_us{};
    std::uint64_t hash{};
    std::uint64_t segments_written{};
    std::size_t reserved_bytes{};
};

MultiDirty multi_dirty_bench(FixedSlotAllocatorMode mode, std::size_t dirty_islands) {
    constexpr std::size_t bodies = 10'000U, base_pairs = 5'000U, frames = 12U;
    if (dirty_islands > 1'024U) throw std::invalid_argument("v0.26 dirty island count is outside benchmark range");
    const std::size_t maximum_pairs = base_pairs + dirty_islands;
    const std::size_t pages = (bodies + 255U) / 256U;
    std::vector<NormalContact> split_contacts(base_pairs), merged_contacts;
    std::vector<BroadphasePair> split_pairs(base_pairs), merged_pairs;
    for (std::size_t index = 0U; index < base_pairs; ++index) {
        split_contacts[index] = {index * 2U, index * 2U + 1U, {kOne, 0}};
        split_pairs[index] = {index * 2U, index * 2U + 1U};
    }
    merged_contacts = split_contacts; merged_pairs = split_pairs;
    std::vector<std::size_t> dirty; dirty.reserve(dirty_islands * 4U);
    for (std::size_t island = 0U; island < dirty_islands; ++island) {
        const std::size_t pair = island * 2U;
        const std::size_t first = pair * 2U + 1U;
        merged_contacts.push_back({first, first + 1U, {kOne, 0}});
        merged_pairs.push_back({first, first + 1U});
        for (std::size_t body = pair * 2U; body < pair * 2U + 4U; ++body) dirty.push_back(body);
    }
    std::sort(merged_pairs.begin(), merged_pairs.end());
    PagedSegmentedPairHistory history({
        .bodies = bodies,
        .maximum_contacts = maximum_pairs,
        .maximum_pairs = maximum_pairs,
        .maximum_pairs_per_segment = 3U,
        .history_capacity = 16U,
        .table_page_elements = 256U,
        .segment_generations = base_pairs + dirty_islands * frames * 3U + 2'048U,
        .spill_generations = 8U,
        .table_generations = 20U,
        .body_key_page_generations = pages * frames + 128U,
        .segment_map_page_generations = pages * frames + 256U,
        .allocation_mode = mode,
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
            throw std::runtime_error("v0.26 multi-dirty restore mismatch");
        }
    }
    const auto stats = history.stats();
    return {.mode = mode, .dirty_islands = dirty_islands,
        .capture_p95_us = percentile(capture_samples, .95),
        .restore_p95_us = percentile(restore_samples, .95),
        .hash = history.hash(frames), .segments_written = stats.segments_written,
        .reserved_bytes = history.reserved_bytes()};
}

void write_method(std::ofstream& csv, std::string_view name, const UncertaintyMethodMetrics& metrics,
                  std::uint64_t hash) {
    csv << name << ',' << std::fixed << std::setprecision(9)
        << metrics.runtime_us << ',' << metrics.final_width << ',' << metrics.average_width << ','
        << metrics.maximum_terms << ',' << metrics.empirical_violations << ",0x"
        << std::hex << std::uppercase << hash << std::dec << '\n';
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path output = argc > 1 ? argv[1] : "artifacts/v0.26-benchmark";
    std::filesystem::create_directories(output);

    const AllocatorBench next = allocator_bench(FixedSlotAllocatorMode::NextFit);
    const AllocatorBench bitmap = allocator_bench(FixedSlotAllocatorMode::HierarchicalBitmap);
    const std::array<std::size_t, 4> dirty_counts{{1U, 64U, 256U, 1'024U}};
    std::vector<MultiDirty> histories;
    for (const FixedSlotAllocatorMode mode : {FixedSlotAllocatorMode::NextFit, FixedSlotAllocatorMode::HierarchicalBitmap}) {
        for (const std::size_t dirty : dirty_counts) histories.push_back(multi_dirty_bench(mode, dirty));
    }
    for (std::size_t index = 0U; index < dirty_counts.size(); ++index) {
        if (histories[index].hash != histories[index + dirty_counts.size()].hash) {
            throw std::runtime_error("v0.26 allocator strategies changed history semantics");
        }
    }

    const UncertaintyLabResult uncertainty = run_uncertainty_lab({
        .steps = 120U, .monte_carlo_samples = 4'096U, .timing_repetitions = 24U,
        .full_affine_terms = 512U, .reduced_affine_terms = 12U,
    });
    if (uncertainty.interval.empirical_violations != 0U || uncertainty.affine.empirical_violations != 0U
        || uncertainty.reduced_affine.empirical_violations != 0U) {
        throw std::runtime_error("v0.26 uncertainty enclosure missed a Monte Carlo sample");
    }

    std::ofstream allocator_csv(output / "allocator.csv");
    allocator_csv << "mode,p50_batch_us,p95_batch_us,probes,peak_used,hash\n";
    for (const AllocatorBench& row : {next, bitmap}) {
        allocator_csv << mode_name(row.mode) << ',' << std::fixed << std::setprecision(6)
            << row.p50_us << ',' << row.p95_us << ',' << row.probes << ',' << row.peak_used
            << ",0x" << std::hex << std::uppercase << row.hash << std::dec << '\n';
    }

    std::ofstream history_csv(output / "multi_dirty.csv");
    history_csv << "mode,dirty_islands,capture_p95_us,restore_p95_us,reserved_bytes,segments_written,hash\n";
    for (const MultiDirty& row : histories) {
        history_csv << mode_name(row.mode) << ',' << row.dirty_islands << ',' << std::fixed << std::setprecision(6)
            << row.capture_p95_us << ',' << row.restore_p95_us << ',' << row.reserved_bytes << ','
            << row.segments_written << ",0x" << std::hex << std::uppercase << row.hash << std::dec << '\n';
    }

    std::ofstream uncertainty_csv(output / "uncertainty.csv");
    uncertainty_csv << "method,runtime_us,final_width,average_width,maximum_terms,empirical_violations,hash\n";
    uncertainty_csv << "double_center," << uncertainty.double_runtime_us << ",0,0,0,0,0x"
        << std::hex << std::uppercase << uncertainty.hash << std::dec << '\n';
    uncertainty_csv << "q32_32_center," << uncertainty.fixed_runtime_us << ",0,0,0,0,0x"
        << std::hex << std::uppercase << uncertainty.hash << std::dec << '\n';
    write_method(uncertainty_csv, "interval", uncertainty.interval, uncertainty.hash);
    write_method(uncertainty_csv, "affine", uncertainty.affine, uncertainty.hash);
    write_method(uncertainty_csv, "reduced_affine", uncertainty.reduced_affine, uncertainty.hash);

    std::ofstream summary(output / "summary.json");
    summary << "{\n"
        << "  \"next_fit_p95_batch_us\": " << next.p95_us << ",\n"
        << "  \"bitmap_p95_batch_us\": " << bitmap.p95_us << ",\n"
        << "  \"next_fit_probes\": " << next.probes << ",\n"
        << "  \"bitmap_probes\": " << bitmap.probes << ",\n"
        << "  \"dirty_1024_next_fit_p95_us\": " << histories[3].capture_p95_us << ",\n"
        << "  \"dirty_1024_bitmap_p95_us\": " << histories[7].capture_p95_us << ",\n"
        << "  \"double_runtime_us\": " << uncertainty.double_runtime_us << ",\n"
        << "  \"fixed_runtime_us\": " << uncertainty.fixed_runtime_us << ",\n"
        << "  \"interval_final_width\": " << uncertainty.interval.final_width << ",\n"
        << "  \"affine_final_width\": " << uncertainty.affine.final_width << ",\n"
        << "  \"reduced_affine_final_width\": " << uncertainty.reduced_affine.final_width << ",\n"
        << "  \"fixed_x_error\": " << uncertainty.fixed_x_error << ",\n"
        << "  \"fixed_v_error\": " << uncertainty.fixed_v_error << ",\n"
        << "  \"uncertainty_hash\": \"0x" << std::hex << std::uppercase << uncertainty.hash << std::dec << "\"\n"
        << "}\n";

    std::cout << "v0.26 allocator next=" << next.p95_us << " us bitmap=" << bitmap.p95_us
        << " us dirty1024=" << histories[3].capture_p95_us << '/' << histories[7].capture_p95_us
        << " us widths=" << uncertainty.interval.final_width << '/' << uncertainty.affine.final_width
        << '/' << uncertainty.reduced_affine.final_width << " hash=0x" << std::hex << std::uppercase
        << uncertainty.hash << std::dec << '\n';
    return EXIT_SUCCESS;
}
