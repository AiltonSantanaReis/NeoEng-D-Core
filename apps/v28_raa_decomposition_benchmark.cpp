#include "neoeng/core/fixed_raa_decomposition.hpp"
#include "neoeng/core/fixed_raa_microkernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using neoeng::core::FixedRaaDecompositionResult;
using neoeng::core::FixedRaaMicrokernelConfig;
using neoeng::core::FixedRaaMicrokernelResult;
using neoeng::core::FixedRaaWidthBreakdown;
using neoeng::core::run_fixed_raa_microkernel;
using neoeng::core::run_fixed_raa_width_decomposition;

struct Row final {
    std::size_t active_bodies{};
    std::size_t maximum_terms{};
    FixedRaaMicrokernelResult timed{};
    FixedRaaDecompositionResult diagnostic{};
};

[[nodiscard]] std::size_t parse_positive(const char* text, const char* field) {
    const unsigned long long value = std::strtoull(text, nullptr, 10);
    if (value == 0ULL || value > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(field);
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] bool approximately_equal(double first, double second, double tolerance = 1.0e-8) {
    return std::abs(first - second) <= tolerance * std::max({1.0, std::abs(first), std::abs(second)});
}

void validate_row(const Row& row) {
    if (row.timed.hash != row.diagnostic.hash) {
        throw std::runtime_error("Diagnostic RAA hash does not match canonical microkernel");
    }
    if (row.timed.compressions != row.diagnostic.compressions
        || row.timed.rounding_guard_raw != row.diagnostic.rounding_guard_raw
        || row.timed.maximum_terms != row.diagnostic.maximum_terms
        || row.timed.empirical_violations != row.diagnostic.empirical_violations) {
        throw std::runtime_error("Diagnostic RAA counters do not match canonical microkernel");
    }
    if (!approximately_equal(row.timed.final_total_width, row.diagnostic.final_width.total())
        || !approximately_equal(row.timed.average_total_width, row.diagnostic.average_width.total())) {
        throw std::runtime_error("Diagnostic RAA width decomposition does not close");
    }
}

void write_breakdown_csv(std::ostream& output, const FixedRaaWidthBreakdown& value) {
    output << value.retained_input_width << ','
           << value.retained_nonlinear_width << ','
           << value.condensation_width << ','
           << value.rounding_width << ','
           << value.total();
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("artifacts/v0.28-raa-decomposition");
        const std::size_t timing_repetitions = argc > 2
            ? parse_positive(argv[2], "invalid timing repetition count") : 8U;
        const std::size_t monte_carlo_samples = argc > 3
            ? parse_positive(argv[3], "invalid Monte Carlo sample count") : 2'048U;
        std::filesystem::create_directories(output);

        constexpr std::array<std::size_t, 6U> active_body_counts{2U, 8U, 32U, 128U, 512U, 2'000U};
        constexpr std::array<std::size_t, 3U> term_counts{8U, 12U, 16U};
        std::vector<Row> rows;
        rows.reserve(active_body_counts.size() * term_counts.size());

        for (const std::size_t active_bodies : active_body_counts) {
            for (const std::size_t maximum_terms : term_counts) {
                const FixedRaaMicrokernelConfig config{
                    .bodies = active_bodies,
                    .steps = 8U,
                    .maximum_terms = maximum_terms,
                    .monte_carlo_samples = monte_carlo_samples,
                    .timing_repetitions = timing_repetitions,
                    .seed = 0x2803000000000000ULL
                        + static_cast<std::uint64_t>(active_bodies * 32U + maximum_terms),
                };
                Row row{
                    .active_bodies = active_bodies,
                    .maximum_terms = maximum_terms,
                    .timed = run_fixed_raa_microkernel(config),
                    .diagnostic = run_fixed_raa_width_decomposition(config),
                };
                validate_row(row);
                rows.push_back(row);
            }
        }

        std::ofstream csv(output / "raa_active_subset_decomposition.csv");
        csv << "active_bodies,independent_contacts,maximum_terms,p50_us,p95_us,"
               "ns_per_body_step,ns_per_contact_step,final_total_width,average_total_width,"
               "final_retained_input_width,final_retained_nonlinear_width,"
               "final_condensation_width,final_rounding_width,final_component_sum_width,"
               "average_retained_input_width,average_retained_nonlinear_width,"
               "average_condensation_width,average_rounding_width,average_component_sum_width,"
               "compressions,rounding_guard_raw,empirical_violations,canonical_hash,diagnostic_hash\n";
        csv << std::fixed << std::setprecision(12);
        for (const Row& row : rows) {
            csv << row.active_bodies << ',' << row.active_bodies / 2U << ',' << row.maximum_terms << ','
                << row.timed.p50_us << ',' << row.timed.p95_us << ','
                << row.timed.ns_per_body_step << ',' << row.timed.ns_per_contact_step << ','
                << row.timed.final_total_width << ',' << row.timed.average_total_width << ',';
            write_breakdown_csv(csv, row.diagnostic.final_width);
            csv << ',';
            write_breakdown_csv(csv, row.diagnostic.average_width);
            csv << ',' << row.timed.compressions << ',' << row.timed.rounding_guard_raw << ','
                << row.timed.empirical_violations << ",0x" << std::hex << std::uppercase
                << row.timed.hash << ",0x" << row.diagnostic.hash << std::dec << '\n';
        }

        std::ofstream json(output / "summary.json");
        json << "{\n"
             << "  \"version\": \"0.28.0-development-stage3\",\n"
             << "  \"scope\": \"synthetic_independent_contact_active_subset_scaling\",\n"
             << "  \"authoritative_pipeline_modified\": false,\n"
             << "  \"timing_uses_uninstrumented_microkernel\": true,\n"
             << "  \"diagnostic_shadow_hash_checked\": true,\n"
             << "  \"rows\": " << rows.size() << ",\n"
             << "  \"timing_repetitions\": " << timing_repetitions << ",\n"
             << "  \"monte_carlo_samples_per_row\": " << monte_carlo_samples << "\n"
             << "}\n";

        std::cout << "v0.28 RAA decomposition rows=" << rows.size()
                  << " hashes=matched widths=closed output=" << output.string() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "v0.28 RAA decomposition benchmark failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
