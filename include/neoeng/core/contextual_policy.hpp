#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>

namespace neoeng::core {

enum class RuntimeRepresentation : std::uint8_t {
    FullScanAoS = 0,
    ActiveChunkedAoS = 1,
    ActiveComponentSoA = 2,
};

inline constexpr std::size_t kRuntimeRepresentationCount = 3U;

struct PolicyFeatures final {
    std::size_t body_count{};
    std::size_t changed_bodies{};
    std::size_t touched_chunks{};
    std::size_t chunk_size{};
};

struct ContextualPolicyConfig final {
    std::array<std::array<std::uint64_t, kRuntimeRepresentationCount>, kRuntimeRepresentationCount>
        transition_cost{};
    std::uint64_t prior_weight{2U};
    std::size_t planning_horizon_frames{8U};
};

struct ContextualPolicyDecision final {
    RuntimeRepresentation representation{RuntimeRepresentation::ActiveChunkedAoS};
    std::array<std::uint64_t, kRuntimeRepresentationCount> estimates{};
    std::uint32_t density_bucket{};
    std::uint32_t dispersion_bucket{};
};

class ContextualEncodingPolicy final {
public:
    explicit ContextualEncodingPolicy(ContextualPolicyConfig config = {});

    [[nodiscard]] ContextualPolicyDecision choose(
        const PolicyFeatures& features,
        const std::array<bool, kRuntimeRepresentationCount>& allowed);

    void observe(
        const PolicyFeatures& features,
        const std::array<std::uint64_t, kRuntimeRepresentationCount>& observed_costs);

    void reset_sequence() noexcept;

private:
    struct ContextKey final {
        std::uint32_t density{};
        std::uint32_t dispersion{};
        auto operator<=>(const ContextKey&) const = default;
    };

    struct ContextStats final {
        std::array<std::uint64_t, kRuntimeRepresentationCount> cost_sum{};
        std::array<std::uint64_t, kRuntimeRepresentationCount> observations{};
    };

    [[nodiscard]] static ContextKey classify(const PolicyFeatures& features) noexcept;
    [[nodiscard]] static std::array<std::uint64_t, kRuntimeRepresentationCount> analytic_priors(
        const PolicyFeatures& features) noexcept;

    ContextualPolicyConfig config_{};
    std::map<ContextKey, ContextStats> contexts_{};
    RuntimeRepresentation previous_{RuntimeRepresentation::ActiveChunkedAoS};
    bool has_previous_{};
};

} // namespace neoeng::core
