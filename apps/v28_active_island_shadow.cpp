#include "neoeng/core/fixed_raa_active_island_shadow.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::size_t parse_size(const char* text, const char* name) {
    try {
        const auto value = static_cast<std::size_t>(std::stoull(text));
        if (value == 0U) throw std::invalid_argument("zero");
        return value;
    } catch (...) {
        throw std::invalid_argument(std::string("invalid ") + name);
    }
}

std::uint64_t parse_seed(const char* text) {
    try {
        return std::stoull(text, nullptr, 0);
    } catch (...) {
        throw std::invalid_argument("invalid seed");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path output = argc > 1
            ? std::filesystem::path(argv[1])
            : std::filesystem::path("artifacts/v0.28-stage5-active-island-shadow");
        const std::size_t groups = argc > 2 ? parse_size(argv[2], "group count") : 8U;
        const std::size_t frames = argc > 3 ? parse_size(argv[3], "frame count") : 8U;
        const std::size_t maximum_terms = argc > 4 ? parse_size(argv[4], "maximum terms") : 8U;
        const std::uint64_t seed = argc > 5 ? parse_seed(argv[5]) : 0x2805000000000000ULL;
        std::filesystem::create_directories(output);

        const auto result = neoeng::core::run_fixed_raa_active_island_shadow({
            .groups_per_topology = groups,
            .frames = frames,
            .raa_steps = 8U,
            .maximum_terms = maximum_terms,
            .page_size = 64U,
            .seed = seed,
        });

        std::ofstream csv(output / "active_island_shadow.csv");
        csv << "frame,island_index,topology,bodies,contacts,oracle_vulnerable,selected,"
               "false_positives,false_negatives,contact_hash,oracle_mask_hash,classifier_mask_hash\n";
        for (const auto& island : result.islands) {
            csv << island.frame << ',' << island.island_index << ','
                << neoeng::core::to_string(island.topology) << ','
                << island.bodies << ',' << island.contacts << ','
                << island.oracle_vulnerable << ',' << island.selected << ','
                << island.false_positives << ',' << island.false_negatives << ",0x"
                << std::hex << std::uppercase << island.contact_hash << ",0x"
                << island.oracle_mask_hash << ",0x" << island.classifier_mask_hash
                << std::dec << '\n';
        }

        std::ofstream summary(output / "active_island_shadow_summary.json");
        summary << std::fixed << std::setprecision(6)
            << "{\n"
            << "  \"groups_per_topology\": " << groups << ",\n"
            << "  \"frames\": " << result.frames_processed << ",\n"
            << "  \"maximum_terms\": " << maximum_terms << ",\n"
            << "  \"seed\": \"0x" << std::hex << std::uppercase << seed << std::dec << "\",\n"
            << "  \"bodies\": " << result.bodies << ",\n"
            << "  \"islands_observed\": " << result.islands_observed << ",\n"
            << "  \"contacts_observed\": " << result.contacts_observed << ",\n"
            << "  \"oracle_vulnerable\": " << result.oracle_vulnerable << ",\n"
            << "  \"selected\": " << result.selected << ",\n"
            << "  \"false_positives\": " << result.false_positives << ",\n"
            << "  \"false_negatives\": " << result.false_negatives << ",\n"
            << "  \"center_mismatches\": " << result.center_mismatches << ",\n"
            << "  \"selected_state_mismatches\": " << result.selected_state_mismatches << ",\n"
            << "  \"authoritative_state_mismatches\": "
            << result.authoritative_state_mismatches << ",\n"
            << "  \"maximum_island_bodies\": " << result.maximum_island_bodies << ",\n"
            << "  \"maximum_island_contacts\": " << result.maximum_island_contacts << ",\n"
            << "  \"topology_matching\": " << result.topology_counts[0] << ",\n"
            << "  \"topology_chain\": " << result.topology_counts[1] << ",\n"
            << "  \"topology_tree\": " << result.topology_counts[2] << ",\n"
            << "  \"topology_cycle\": " << result.topology_counts[3] << ",\n"
            << "  \"topology_general\": " << result.topology_counts[4] << ",\n"
            << "  \"classifier_p50_us\": " << result.classifier_timing.p50_us << ",\n"
            << "  \"classifier_p95_us\": " << result.classifier_timing.p95_us << ",\n"
            << "  \"full_raa_p50_us\": " << result.full_raa_timing.p50_us << ",\n"
            << "  \"full_raa_p95_us\": " << result.full_raa_timing.p95_us << ",\n"
            << "  \"selective_kernel_p50_us\": " << result.selective_kernel_timing.p50_us << ",\n"
            << "  \"selective_kernel_p95_us\": " << result.selective_kernel_timing.p95_us << ",\n"
            << "  \"selective_total_p50_us\": " << result.selective_total_timing.p50_us << ",\n"
            << "  \"selective_total_p95_us\": " << result.selective_total_timing.p95_us << ",\n"
            << "  \"shadow_p50_us\": " << result.shadow_timing.p50_us << ",\n"
            << "  \"shadow_p95_us\": " << result.shadow_timing.p95_us << ",\n"
            << "  \"shadow_p99_us\": " << result.shadow_timing.p99_us << ",\n"
            << "  \"shadow_maximum_us\": " << result.shadow_timing.maximum_us << ",\n"
            << "  \"initial_world_hash\": \"0x" << std::hex << std::uppercase
            << result.initial_world_hash << "\",\n"
            << "  \"final_world_hash\": \"0x" << result.final_world_hash << "\",\n"
            << "  \"island_layout_hash\": \"0x" << result.island_layout_hash << "\",\n"
            << "  \"aggregate_hash\": \"0x" << result.aggregate_hash << "\"\n"
            << std::dec << "}\n";

        const bool passed = result.false_negatives == 0U
            && result.center_mismatches == 0U
            && result.selected_state_mismatches == 0U
            && result.authoritative_state_mismatches == 0U
            && result.islands_observed != 0U
            && result.contacts_observed != 0U;
        std::cout << "v0.28 active-island shadow bodies=" << result.bodies
                  << " frames=" << result.frames_processed
                  << " islands=" << result.islands_observed
                  << " contacts=" << result.contacts_observed
                  << " vulnerable=" << result.oracle_vulnerable
                  << " selected=" << result.selected
                  << " fp=" << result.false_positives
                  << " fn=" << result.false_negatives
                  << " state_mismatch=" << result.authoritative_state_mismatches
                  << " p95_us=" << result.shadow_timing.p95_us
                  << " aggregate=0x" << std::hex << std::uppercase
                  << result.aggregate_hash << std::dec
                  << " passed=" << (passed ? "true" : "false") << '\n';
        return passed ? 0 : 1;
    } catch (const std::exception& exception) {
        std::cerr << "v0.28 active-island shadow failed: " << exception.what() << '\n';
        return 2;
    }
}
