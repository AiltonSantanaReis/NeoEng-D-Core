#include "neoeng/core/contextual_policy.hpp"

#include "neoeng/core/types.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace neoeng::core {
namespace {

[[nodiscard]] std::size_t representation_index(RuntimeRepresentation representation) noexcept {
    return static_cast<std::size_t>(representation);
}

[[nodiscard]] std::uint64_t saturating_add(
    std::uint64_t lhs, std::uint64_t rhs) noexcept {
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    return lhs > maximum - rhs ? maximum : lhs + rhs;
}

[[nodiscard]] std::uint64_t saturating_multiply(
    std::uint64_t lhs, std::uint64_t rhs) noexcept {
    if (lhs == 0U || rhs == 0U) return 0U;
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    return lhs > maximum / rhs ? maximum : lhs * rhs;
}


} // namespace

ContextualEncodingPolicy::ContextualEncodingPolicy(ContextualPolicyConfig config)
    : config_(config) {
    if (config_.planning_horizon_frames == 0U) {
        throw std::invalid_argument("Contextual policy planning horizon must be positive");
    }
}

ContextualEncodingPolicy::ContextKey ContextualEncodingPolicy::classify(
    const PolicyFeatures& features) noexcept {
    const std::uint64_t density_ppm = features.body_count == 0U
        ? 0U
        : static_cast<std::uint64_t>(features.changed_bodies) * 1'000'000ULL
            / static_cast<std::uint64_t>(features.body_count);
    std::uint32_t density = 0U;
    if (density_ppm > 1'000U) density = 1U;
    if (density_ppm > 10'000U) density = 2U;
    if (density_ppm > 100'000U) density = 3U;
    if (density_ppm > 500'000U) density = 4U;

    const std::uint64_t covered = static_cast<std::uint64_t>(features.touched_chunks)
        * static_cast<std::uint64_t>(features.chunk_size);
    const std::uint64_t amplification_ppm = features.changed_bodies == 0U
        ? 0U
        : covered * 1'000'000ULL / static_cast<std::uint64_t>(features.changed_bodies);
    std::uint32_t dispersion = 0U;
    if (amplification_ppm > 1'500'000U) dispersion = 1U;
    if (amplification_ppm > 4'000'000U) dispersion = 2U;
    if (amplification_ppm > 16'000'000U) dispersion = 3U;
    return ContextKey{.density = density, .dispersion = dispersion};
}

std::array<std::uint64_t, kRuntimeRepresentationCount> ContextualEncodingPolicy::analytic_priors(
    const PolicyFeatures& features) noexcept {
    constexpr std::uint64_t body_bytes = sizeof(Body);
    const std::uint64_t full = static_cast<std::uint64_t>(features.body_count) * body_bytes;
    const std::uint64_t delta = static_cast<std::uint64_t>(features.changed_bodies)
        * (body_bytes + sizeof(std::size_t)) + full / 16U;
    const std::uint64_t persistent = static_cast<std::uint64_t>(features.touched_chunks)
        * static_cast<std::uint64_t>(features.chunk_size) * body_bytes
        + static_cast<std::uint64_t>(features.touched_chunks) * 256U;
    return {full, delta, persistent};
}

ContextualPolicyDecision ContextualEncodingPolicy::choose(
    const PolicyFeatures& features,
    const std::array<bool, kRuntimeRepresentationCount>& allowed) {
    const ContextKey key = classify(features);
    const auto priors = analytic_priors(features);
    const auto found = contexts_.find(key);
    std::array<std::uint64_t, kRuntimeRepresentationCount> estimates{};
    for (std::size_t encoding = 0U; encoding < kRuntimeRepresentationCount; ++encoding) {
        const std::uint64_t observations = found == contexts_.end()
            ? 0U : found->second.observations[encoding];
        const std::uint64_t weighted_prior = saturating_multiply(
            priors[encoding], config_.prior_weight);
        const std::uint64_t observed_sum = found == contexts_.end()
            ? 0U : found->second.cost_sum[encoding];
        const std::uint64_t denominator = config_.prior_weight + observations;
        estimates[encoding] = denominator == 0U
            ? priors[encoding]
            : saturating_add(weighted_prior, observed_sum) / denominator;
    }

    std::uint64_t best_cost = std::numeric_limits<std::uint64_t>::max();
    RuntimeRepresentation best = RuntimeRepresentation::ActiveChunkedAoS;
    bool selected = false;
    for (std::size_t encoding = 0U; encoding < kRuntimeRepresentationCount; ++encoding) {
        if (!allowed[encoding]) continue;
        const RuntimeRepresentation candidate = static_cast<RuntimeRepresentation>(encoding);
        std::uint64_t score = saturating_multiply(
            estimates[encoding], static_cast<std::uint64_t>(config_.planning_horizon_frames));
        if (has_previous_) {
            const std::uint64_t transition = config_.transition_cost[representation_index(previous_)][encoding];
            if (score > std::numeric_limits<std::uint64_t>::max() - transition) {
                score = std::numeric_limits<std::uint64_t>::max();
            } else {
                score += transition;
            }
        }
        if (!selected || score < best_cost || (score == best_cost && encoding < representation_index(best))) {
            selected = true;
            best_cost = score;
            best = candidate;
        }
    }
    if (!selected) throw std::runtime_error("Contextual policy has no permitted encoding");
    previous_ = best;
    has_previous_ = true;
    return ContextualPolicyDecision{
        .representation = best,
        .estimates = estimates,
        .density_bucket = key.density,
        .dispersion_bucket = key.dispersion,
    };
}

void ContextualEncodingPolicy::observe(
    const PolicyFeatures& features,
    const std::array<std::uint64_t, kRuntimeRepresentationCount>& observed_costs) {
    ContextStats& stats = contexts_[classify(features)];
    for (std::size_t encoding = 0U; encoding < kRuntimeRepresentationCount; ++encoding) {
        stats.cost_sum[encoding] = saturating_add(stats.cost_sum[encoding], observed_costs[encoding]);
        ++stats.observations[encoding];
    }
}

void ContextualEncodingPolicy::reset_sequence() noexcept {
    previous_ = RuntimeRepresentation::ActiveChunkedAoS;
    has_previous_ = false;
}

} // namespace neoeng::core
