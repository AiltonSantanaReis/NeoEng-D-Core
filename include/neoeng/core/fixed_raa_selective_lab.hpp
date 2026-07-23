#pragma once

#include <cstddef>
#include "neoeng/core/fixed.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

enum class FixedRaaSelectiveProfile : std::uint8_t {
    Mixed = 0U,
    MostlySafe = 1U,
    BoundaryDense = 2U,
    Approaching = 3U,
    Separating = 4U,
};

struct FixedRaaTimingDistribution final {
    double p50_us{};
    double p95_us{};
    double p99_us{};
    double maximum_us{};
};

struct FixedRaaSelectiveConfig final {
    std::size_t contacts{1'000U};
    std::size_t steps{8U};
    std::size_t maximum_terms{8U};
    std::size_t timing_repetitions{24U};
    FixedRaaSelectiveProfile profile{FixedRaaSelectiveProfile::Mixed};
    std::uint64_t seed{0x2804000000000000ULL};
};


struct FixedRaaSelectiveContactInput final {
    Fixed x1_center{};
    Fixed x2_center{};
    Fixed v1_center{};
    Fixed v2_center{};
    Fixed x1_radius{};
    Fixed x2_radius{};
    Fixed v1_radius{};
    Fixed v2_radius{};
};

struct FixedRaaSelectiveBatchResult final {
    double classifier_us{};
    double full_raa_us{};
    double selective_kernel_us{};
    double selective_total_us{};
    double diagnostic_total_us{};
    std::vector<std::uint8_t> oracle_mask{};
    std::vector<std::uint8_t> classifier_mask{};
    std::size_t oracle_vulnerable{};
    std::size_t selected{};
    std::size_t true_positives{};
    std::size_t false_positives{};
    std::size_t false_negatives{};
    std::size_t true_negatives{};
    std::size_t center_mismatches{};
    std::size_t selected_state_mismatches{};
    std::uint64_t corpus_hash{};
    std::uint64_t oracle_mask_hash{};
    std::uint64_t classifier_mask_hash{};
    std::uint64_t full_center_hash{};
    std::uint64_t selective_center_hash{};
    std::uint64_t full_state_hash{};
    std::uint64_t selective_state_hash{};
};

struct FixedRaaIntervalArithmeticAuditResult final {
    std::size_t cases{};
    std::size_t multiplication_corner_checks{};
    std::size_t violations{};
    std::uint64_t hash{};
};

struct FixedRaaSelectiveResult final {
    FixedRaaTimingDistribution nominal{};
    FixedRaaTimingDistribution classifier{};
    FixedRaaTimingDistribution full_raa{};
    FixedRaaTimingDistribution selective_kernel{};
    FixedRaaTimingDistribution selective_total{};

    std::size_t contacts{};
    std::size_t oracle_vulnerable{};
    std::size_t selected{};
    std::size_t true_positives{};
    std::size_t false_positives{};
    std::size_t false_negatives{};
    std::size_t true_negatives{};
    std::size_t center_mismatches{};
    std::size_t selected_state_mismatches{};

    double full_final_width{};
    double oracle_vulnerable_final_width{};
    double selected_final_width{};
    double missed_vulnerable_final_width{};

    std::uint64_t full_compressions{};
    std::uint64_t selective_compressions{};
    std::uint64_t full_rounding_guard_raw{};
    std::uint64_t selective_rounding_guard_raw{};

    std::uint64_t corpus_hash{};
    std::uint64_t oracle_mask_hash{};
    std::uint64_t classifier_mask_hash{};
    std::uint64_t nominal_center_hash{};
    std::uint64_t full_center_hash{};
    std::uint64_t selective_center_hash{};
    std::uint64_t full_state_hash{};
    std::uint64_t selective_state_hash{};
};

// Laboratory-only experiment. The oracle labels a contact as vulnerable when
// the full fixed-capacity RAA enclosure of its signed gap straddles zero at the
// initial state or after any configured step. The classifier is an independent
// Q32.32 interval propagation with outward rounding. No result from this API
// modifies or approves the authoritative physics pipeline.
[[nodiscard]] FixedRaaSelectiveResult run_fixed_raa_selective_experiment(
    const FixedRaaSelectiveConfig& config = {});


// Evaluates caller-supplied one-dimensional contact states with the same
// laboratory oracle and interval classifier used by the synthetic experiment.
// This API is intended for shadow diagnostics only and never mutates the
// authoritative physics state supplied by the caller.
[[nodiscard]] FixedRaaSelectiveBatchResult evaluate_fixed_raa_selective_contacts(
    std::span<const FixedRaaSelectiveContactInput> contacts,
    std::size_t steps = 8U,
    std::size_t maximum_terms = 8U);

// Verifies the raw Q32.32 interval primitives against exact signed 128-bit
// inequalities. This is an implementation audit, not a global proof of the
// composed dynamics.
[[nodiscard]] FixedRaaIntervalArithmeticAuditResult
run_fixed_raa_interval_arithmetic_audit(
    std::size_t cases = 1'000'000U,
    std::uint64_t seed = 0x2804A11D17A11ULL);

[[nodiscard]] const char* fixed_raa_selective_profile_name(
    FixedRaaSelectiveProfile profile) noexcept;

} // namespace neoeng::core
