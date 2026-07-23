#include "neoeng/core/fixed_raa_selective_lab.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

[[nodiscard]] std::size_t parse_positive(const char* text, const char* error) {
    const std::string value(text);
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0U) throw std::invalid_argument(error);
    return static_cast<std::size_t>(parsed);
}

void mix_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("artifacts/v0.28-stage4-selective-fuzz");
        const std::size_t seeds = argc > 2
            ? parse_positive(argv[2], "invalid seed count") : 32U;
        const std::size_t contacts = argc > 3
            ? parse_positive(argv[3], "invalid contact count") : 128U;
        std::filesystem::create_directories(output);

        constexpr std::array profiles{
            neoeng::core::FixedRaaSelectiveProfile::Mixed,
            neoeng::core::FixedRaaSelectiveProfile::MostlySafe,
            neoeng::core::FixedRaaSelectiveProfile::BoundaryDense,
            neoeng::core::FixedRaaSelectiveProfile::Approaching,
            neoeng::core::FixedRaaSelectiveProfile::Separating,
        };
        constexpr std::array<std::size_t, 3U> term_counts{8U, 12U, 16U};

        std::ofstream csv(output / "selective_raa_fuzz.csv");
        csv << "seed_index,profile,maximum_terms,contacts,oracle_vulnerable,selected,"
               "true_positives,false_positives,false_negatives,true_negatives,"
               "center_mismatches,selected_state_mismatches,corpus_hash,oracle_mask_hash,"
               "classifier_mask_hash,nominal_center_hash,full_center_hash,selective_center_hash,"
               "full_state_hash,selective_state_hash\n";

        std::uint64_t aggregate = 0xCBF29CE484222325ULL;
        std::uint64_t total_contacts = 0U;
        std::uint64_t total_oracle = 0U;
        std::uint64_t total_selected = 0U;
        std::uint64_t total_true_positives = 0U;
        std::uint64_t total_false_positives = 0U;
        std::uint64_t total_false_negatives = 0U;
        std::uint64_t total_center_mismatches = 0U;
        std::uint64_t total_state_mismatches = 0U;

        for (std::size_t seed_index = 0U; seed_index < seeds; ++seed_index) {
            for (const auto profile : profiles) {
                for (const std::size_t maximum_terms : term_counts) {
                    const auto result = neoeng::core::run_fixed_raa_selective_experiment({
                        .contacts = contacts,
                        .steps = 8U,
                        .maximum_terms = maximum_terms,
                        .timing_repetitions = 2U,
                        .profile = profile,
                        .seed = 0x2804F00000000000ULL
                            ^ (static_cast<std::uint64_t>(seed_index) * 0x9E3779B97F4A7C15ULL)
                            ^ (static_cast<std::uint64_t>(profile) << 40U),
                    });

                    total_contacts += result.contacts;
                    total_oracle += result.oracle_vulnerable;
                    total_selected += result.selected;
                    total_true_positives += result.true_positives;
                    total_false_positives += result.false_positives;
                    total_false_negatives += result.false_negatives;
                    total_center_mismatches += result.center_mismatches;
                    total_state_mismatches += result.selected_state_mismatches;

                    for (const std::uint64_t value : {
                             result.corpus_hash, result.oracle_mask_hash,
                             result.classifier_mask_hash, result.nominal_center_hash,
                             result.full_center_hash, result.selective_center_hash,
                             result.full_state_hash, result.selective_state_hash,
                             static_cast<std::uint64_t>(result.oracle_vulnerable),
                             static_cast<std::uint64_t>(result.selected),
                             static_cast<std::uint64_t>(result.false_positives),
                             static_cast<std::uint64_t>(result.false_negatives)}) {
                        mix_u64(aggregate, value);
                    }

                    csv << seed_index << ','
                        << neoeng::core::fixed_raa_selective_profile_name(profile) << ','
                        << maximum_terms << ',' << result.contacts << ','
                        << result.oracle_vulnerable << ',' << result.selected << ','
                        << result.true_positives << ',' << result.false_positives << ','
                        << result.false_negatives << ',' << result.true_negatives << ','
                        << result.center_mismatches << ',' << result.selected_state_mismatches
                        << ",0x" << std::hex << std::uppercase << result.corpus_hash
                        << ",0x" << result.oracle_mask_hash
                        << ",0x" << result.classifier_mask_hash
                        << ",0x" << result.nominal_center_hash
                        << ",0x" << result.full_center_hash
                        << ",0x" << result.selective_center_hash
                        << ",0x" << result.full_state_hash
                        << ",0x" << result.selective_state_hash << std::dec << '\n';
                }
            }
        }

        const double recall = total_oracle == 0U ? 1.0
            : static_cast<double>(total_true_positives) / static_cast<double>(total_oracle);
        const double precision = total_selected == 0U ? 1.0
            : static_cast<double>(total_true_positives) / static_cast<double>(total_selected);

        std::ofstream json(output / "summary.json");
        json << std::fixed << std::setprecision(12)
             << "{\n"
             << "  \"version\": \"0.28.0-development-stage4\",\n"
             << "  \"seeds\": " << seeds << ",\n"
             << "  \"profiles\": " << profiles.size() << ",\n"
             << "  \"term_capacities\": " << term_counts.size() << ",\n"
             << "  \"contacts_per_configuration\": " << contacts << ",\n"
             << "  \"total_contact_evaluations\": " << total_contacts << ",\n"
             << "  \"oracle_vulnerable\": " << total_oracle << ",\n"
             << "  \"selected\": " << total_selected << ",\n"
             << "  \"true_positives\": " << total_true_positives << ",\n"
             << "  \"false_positives\": " << total_false_positives << ",\n"
             << "  \"false_negatives\": " << total_false_negatives << ",\n"
             << "  \"recall\": " << recall << ",\n"
             << "  \"precision\": " << precision << ",\n"
             << "  \"center_mismatches\": " << total_center_mismatches << ",\n"
             << "  \"selected_state_mismatches\": " << total_state_mismatches << ",\n"
             << "  \"aggregate_hash\": \"0x" << std::hex << std::uppercase
             << aggregate << std::dec << "\"\n"
             << "}\n";

        std::cout << "v0.28 selective RAA fuzz seeds=" << seeds
                  << " contact_evaluations=" << total_contacts
                  << " oracle=" << total_oracle
                  << " selected=" << total_selected
                  << " fp=" << total_false_positives
                  << " fn=" << total_false_negatives
                  << " center_mismatch=" << total_center_mismatches
                  << " state_mismatch=" << total_state_mismatches
                  << " aggregate=0x" << std::hex << std::uppercase << aggregate << std::dec
                  << '\n';

        if (total_false_negatives != 0U || total_center_mismatches != 0U
            || total_state_mismatches != 0U) return 2;
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "v0.28 selective RAA fuzz failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
