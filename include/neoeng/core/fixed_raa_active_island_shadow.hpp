#pragma once

#include "neoeng/core/fixed_raa_selective_lab.hpp"
#include "neoeng/core/island_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace neoeng::core {

struct FixedRaaActiveIslandShadowConfig final {
    std::size_t groups_per_topology{8U};
    std::size_t frames{8U};
    std::size_t raa_steps{8U};
    std::size_t maximum_terms{8U};
    std::size_t page_size{64U};
    std::uint64_t seed{0x2805000000000000ULL};
};

struct FixedRaaActiveIslandRecord final {
    std::size_t frame{};
    std::size_t island_index{};
    IslandTopology topology{IslandTopology::General};
    std::size_t bodies{};
    std::size_t contacts{};
    std::size_t oracle_vulnerable{};
    std::size_t selected{};
    std::size_t false_positives{};
    std::size_t false_negatives{};
    std::uint64_t contact_hash{};
    std::uint64_t oracle_mask_hash{};
    std::uint64_t classifier_mask_hash{};
};

struct FixedRaaActiveIslandShadowResult final {
    FixedRaaTimingDistribution classifier_timing{};
    FixedRaaTimingDistribution full_raa_timing{};
    FixedRaaTimingDistribution selective_kernel_timing{};
    FixedRaaTimingDistribution selective_total_timing{};
    FixedRaaTimingDistribution shadow_timing{};
    std::size_t bodies{};
    std::size_t frames_processed{};
    std::size_t zero_contact_frames{};
    std::size_t islands_observed{};
    std::size_t contacts_observed{};
    std::size_t vulnerable_islands{};
    std::size_t selected_islands{};
    std::size_t oracle_vulnerable{};
    std::size_t selected{};
    std::size_t true_positives{};
    std::size_t false_positives{};
    std::size_t false_negatives{};
    std::size_t true_negatives{};
    std::size_t center_mismatches{};
    std::size_t selected_state_mismatches{};
    std::size_t authoritative_state_mismatches{};
    std::size_t maximum_island_bodies{};
    std::size_t maximum_island_contacts{};
    std::array<std::size_t, 5U> topology_counts{};
    std::uint64_t initial_world_hash{};
    std::uint64_t final_world_hash{};
    std::uint64_t island_layout_hash{};
    std::uint64_t aggregate_hash{};
    std::vector<FixedRaaActiveIslandRecord> islands{};
};

// Builds actual swept contacts and ContactIslandWorkspace components from the
// authoritative component runtime. The RAA path is observation-only: a direct
// authoritative solve is compared bit-for-bit with a solve preceded by the
// shadow diagnostic on every frame.
[[nodiscard]] FixedRaaActiveIslandShadowResult
run_fixed_raa_active_island_shadow(
    const FixedRaaActiveIslandShadowConfig& config = {});

} // namespace neoeng::core
