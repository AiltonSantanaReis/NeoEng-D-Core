#pragma once

#include "neoeng/core/contact_solver.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace neoeng::core {

struct FatAabb final {
    Fixed minimum_x{};
    Fixed maximum_x{};
    Fixed minimum_y{};
    Fixed maximum_y{};

    auto operator<=>(const FatAabb&) const = default;
};

struct TemporalBroadphaseConfig final {
    std::size_t horizon_frames{8U};
    std::size_t incremental_body_limit{512U};
};

struct PersistentManifoldState;
struct TemporalContactStepResult;

struct TemporalBroadphaseStats final {
    std::uint64_t pair_cache_builds{};
    std::uint64_t pair_cache_incremental_updates{};
    std::uint64_t pair_cache_reuses{};
    std::uint64_t escaped_bodies{};
    std::uint64_t fat_bounds_tested{};
    std::uint64_t fat_pair_tests{};
    std::uint64_t cached_pairs{};
    std::uint64_t manifold_pairs_reused{};
};

class TemporalBroadphaseState final {
public:
    TemporalBroadphaseState() = default;
    TemporalBroadphaseState(
        std::uint64_t frame,
        std::size_t body_count,
        std::uint64_t valid_until_frame,
        std::shared_ptr<const std::vector<FatAabb>> bounds,
        std::shared_ptr<const std::vector<BroadphasePair>> pairs)
        : frame_(frame), body_count_(body_count), valid_until_frame_(valid_until_frame),
          bounds_(std::move(bounds)), pairs_(std::move(pairs)) {}

    [[nodiscard]] std::uint64_t frame() const noexcept { return frame_; }
    [[nodiscard]] std::size_t body_count() const noexcept { return body_count_; }
    [[nodiscard]] std::uint64_t valid_until_frame() const noexcept { return valid_until_frame_; }
    [[nodiscard]] std::span<const FatAabb> bounds() const noexcept {
        return bounds_ ? std::span<const FatAabb>(*bounds_) : std::span<const FatAabb>{};
    }
    [[nodiscard]] std::span<const BroadphasePair> pairs() const noexcept {
        return pairs_ ? std::span<const BroadphasePair>(*pairs_) : std::span<const BroadphasePair>{};
    }
    [[nodiscard]] const std::shared_ptr<const std::vector<FatAabb>>& shared_bounds() const noexcept {
        return bounds_;
    }
    [[nodiscard]] const std::shared_ptr<const std::vector<BroadphasePair>>& shared_pairs() const noexcept {
        return pairs_;
    }

private:
    friend TemporalBroadphaseState make_temporal_broadphase(
        const ComponentWorldState&, ContactSolverConfig, TemporalBroadphaseConfig,
        TemporalBroadphaseStats*);
    friend struct TemporalContactStepResult;
    friend TemporalContactStepResult step_component_contacts_temporal(
        const ComponentWorldState&, const DeterministicActiveSet&,
        const TemporalBroadphaseState&, const PersistentManifoldState&,
        std::span<const InputCommand>,
        ContactSolverConfig, TemporalBroadphaseConfig, ComponentStepOptions);

    std::uint64_t frame_{};
    std::size_t body_count_{};
    std::uint64_t valid_until_frame_{};
    std::shared_ptr<const std::vector<FatAabb>> bounds_{};
    std::shared_ptr<const std::vector<BroadphasePair>> pairs_{};
};

struct TemporalContactStepResult final {
    ContactStepResult contact{};
    TemporalBroadphaseState broadphase{};
    PersistentManifoldState manifold{};
    TemporalBroadphaseStats temporal_stats{};
};

[[nodiscard]] TemporalBroadphaseState make_temporal_broadphase(
    const ComponentWorldState& state,
    ContactSolverConfig contacts = {},
    TemporalBroadphaseConfig temporal = {},
    TemporalBroadphaseStats* stats = nullptr);

[[nodiscard]] TemporalContactStepResult step_component_contacts_temporal(
    const ComponentWorldState& current,
    const DeterministicActiveSet& active,
    const TemporalBroadphaseState& broadphase,
    const PersistentManifoldState& manifold,
    std::span<const InputCommand> inputs,
    ContactSolverConfig contacts = {},
    TemporalBroadphaseConfig temporal = {},
    ComponentStepOptions options = {});

[[nodiscard]] bool temporal_cache_is_conservative(
    const ComponentWorldState& state,
    const TemporalBroadphaseState& broadphase,
    ContactSolverConfig contacts = {});

} // namespace neoeng::core
