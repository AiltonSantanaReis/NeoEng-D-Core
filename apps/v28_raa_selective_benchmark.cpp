#include "neoeng/core/fixed_raa_selective_lab.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::size_t parse_positive(const char* text, const char* error) {
    const std::string value(text);
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0U) throw std::invalid_argument(error);
    return static_cast<std::size_t>(parsed);
}

void write_timing(std::ostream& output, const neoeng::core::FixedRaaTimingDistribution& timing) {
    output << timing.p50_us << ',' << timing.p95_us << ','
           << timing.p99_us << ',' << timing.maximum_us;
}

struct Row final {
    neoeng::core::FixedRaaSelectiveProfile profile{};
    std::size_t maximum_terms{};
    neoeng::core::FixedRaaSelectiveResult result{};
};

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("artifacts/v0.28-stage4-selective-raa");
        const std::size_t contacts = argc > 2
            ? parse_positive(argv[2], "invalid contact count") : 1'000U;
        const std::size_t timing_repetitions = argc > 3
            ? parse_positive(argv[3], "invalid timing repetition count") : 24U;
        std::filesystem::create_directories(output);

        constexpr std::array profiles{
            neoeng::core::FixedRaaSelectiveProfile::Mixed,
            neoeng::core::FixedRaaSelectiveProfile::MostlySafe,
            neoeng::core::FixedRaaSelectiveProfile::BoundaryDense,
            neoeng::core::FixedRaaSelectiveProfile::Approaching,
            neoeng::core::FixedRaaSelectiveProfile::Separating,
        };
        constexpr std::array<std::size_t, 3U> term_counts{8U, 12U, 16U};

        std::vector<Row> rows;
        rows.reserve(profiles.size() * term_counts.size());
        bool safety_failed = false;
        bool equivalence_failed = false;

        for (const auto profile : profiles) {
            for (const std::size_t maximum_terms : term_counts) {
                auto result = neoeng::core::run_fixed_raa_selective_experiment({
                    .contacts = contacts,
                    .steps = 8U,
                    .maximum_terms = maximum_terms,
                    .timing_repetitions = timing_repetitions,
                    .profile = profile,
                    .seed = 0x2804000000000000ULL
                        ^ (static_cast<std::uint64_t>(profile) << 40U),
                });
                safety_failed = safety_failed || result.false_negatives != 0U;
                equivalence_failed = equivalence_failed
                    || result.center_mismatches != 0U
                    || result.selected_state_mismatches != 0U
                    || result.nominal_center_hash != result.full_center_hash
                    || result.full_center_hash != result.selective_center_hash;
                rows.push_back({
                    .profile = profile,
                    .maximum_terms = maximum_terms,
                    .result = result,
                });
                std::cout << neoeng::core::fixed_raa_selective_profile_name(profile)
                          << " terms=" << maximum_terms
                          << " vulnerable=" << result.oracle_vulnerable
                          << " selected=" << result.selected
                          << " fn=" << result.false_negatives
                          << " full_p95_us=" << result.full_raa.p95_us
                          << " selective_total_p95_us=" << result.selective_total.p95_us
                          << '\n';
            }
        }

        std::ofstream csv(output / "selective_raa_results.csv");
        csv << "profile,maximum_terms,contacts,oracle_vulnerable,selected,true_positives,"
               "false_positives,false_negatives,true_negatives,selection_percent,recall_percent,"
               "precision_percent,center_mismatches,selected_state_mismatches,"
               "nominal_p50_us,nominal_p95_us,nominal_p99_us,nominal_max_us,"
               "classifier_p50_us,classifier_p95_us,classifier_p99_us,classifier_max_us,"
               "full_p50_us,full_p95_us,full_p99_us,full_max_us,"
               "selective_kernel_p50_us,selective_kernel_p95_us,selective_kernel_p99_us,selective_kernel_max_us,"
               "selective_total_p50_us,selective_total_p95_us,selective_total_p99_us,selective_total_max_us,"
               "selective_total_over_full_p95,full_final_width,oracle_vulnerable_final_width,"
               "selected_final_width,missed_vulnerable_final_width,full_compressions,selective_compressions,"
               "full_rounding_guard_raw,selective_rounding_guard_raw,corpus_hash,oracle_mask_hash,"
               "classifier_mask_hash,nominal_center_hash,full_center_hash,selective_center_hash,"
               "full_state_hash,selective_state_hash\n";
        csv << std::fixed << std::setprecision(12);
        for (const Row& row : rows) {
            const auto& result = row.result;
            const double selection_percent = 100.0 * static_cast<double>(result.selected)
                / static_cast<double>(result.contacts);
            const double recall_percent = result.oracle_vulnerable == 0U ? 100.0
                : 100.0 * static_cast<double>(result.true_positives)
                    / static_cast<double>(result.oracle_vulnerable);
            const double precision_percent = result.selected == 0U ? 100.0
                : 100.0 * static_cast<double>(result.true_positives)
                    / static_cast<double>(result.selected);
            const double cost_ratio = result.selective_total.p95_us / result.full_raa.p95_us;

            csv << neoeng::core::fixed_raa_selective_profile_name(row.profile) << ','
                << row.maximum_terms << ',' << result.contacts << ','
                << result.oracle_vulnerable << ',' << result.selected << ','
                << result.true_positives << ',' << result.false_positives << ','
                << result.false_negatives << ',' << result.true_negatives << ','
                << selection_percent << ',' << recall_percent << ',' << precision_percent << ','
                << result.center_mismatches << ',' << result.selected_state_mismatches << ',';
            write_timing(csv, result.nominal); csv << ',';
            write_timing(csv, result.classifier); csv << ',';
            write_timing(csv, result.full_raa); csv << ',';
            write_timing(csv, result.selective_kernel); csv << ',';
            write_timing(csv, result.selective_total); csv << ',';
            csv << cost_ratio << ',' << result.full_final_width << ','
                << result.oracle_vulnerable_final_width << ',' << result.selected_final_width << ','
                << result.missed_vulnerable_final_width << ',' << result.full_compressions << ','
                << result.selective_compressions << ',' << result.full_rounding_guard_raw << ','
                << result.selective_rounding_guard_raw << ",0x" << std::hex << std::uppercase
                << result.corpus_hash << ",0x" << result.oracle_mask_hash
                << ",0x" << result.classifier_mask_hash << ",0x" << result.nominal_center_hash
                << ",0x" << result.full_center_hash << ",0x" << result.selective_center_hash
                << ",0x" << result.full_state_hash << ",0x" << result.selective_state_hash
                << std::dec << '\n';
        }

        std::ofstream json(output / "summary.json");
        json << "{\n"
             << "  \"version\": \"0.28.0-development-stage4\",\n"
             << "  \"scope\": \"laboratory_selective_raa_contact_topology_ambiguity\",\n"
             << "  \"authoritative_pipeline_modified\": false,\n"
             << "  \"oracle\": \"full_raa_gap_enclosure_straddles_zero_at_any_checkpoint\",\n"
             << "  \"classifier\": \"outward_rounded_q32_32_interval_propagation\",\n"
             << "  \"profiles\": " << profiles.size() << ",\n"
             << "  \"term_capacities\": " << term_counts.size() << ",\n"
             << "  \"rows\": " << rows.size() << ",\n"
             << "  \"contacts_per_row\": " << contacts << ",\n"
             << "  \"timing_repetitions\": " << timing_repetitions << ",\n"
             << "  \"safety_failed\": " << (safety_failed ? "true" : "false") << ",\n"
             << "  \"equivalence_failed\": " << (equivalence_failed ? "true" : "false") << "\n"
             << "}\n";

        if (equivalence_failed) {
            std::cerr << "Selective RAA experiment failed center/state equivalence\n";
            return 3;
        }
        if (safety_failed) {
            std::cerr << "Selective RAA interval classifier produced false negatives\n";
            return 2;
        }
        std::cout << "v0.28 selective RAA rows=" << rows.size()
                  << " safety=passed equivalence=passed output=" << output.string() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "v0.28 selective RAA benchmark failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
