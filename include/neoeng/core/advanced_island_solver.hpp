#pragma once

#include "neoeng/core/island_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

struct SpecializedIslandProjectionStats final {
    std::uint64_t islands_processed{};
    std::uint64_t matching_islands{};
    std::uint64_t chain_islands{};
    std::uint64_t star_tree_islands{};
    std::uint64_t reduced_cycle_islands{};
    std::uint64_t general_fallback_islands{};
    std::uint64_t contacts_processed{};
    std::uint64_t pair_adjustments{};
    std::uint64_t isotonic_blocks{};
    std::uint64_t violations_before{};
    std::uint64_t violations_after{};
    std::uint64_t warm_start_attempts{};
    std::uint64_t warm_start_accepts{};
    std::uint64_t warm_contacts_applied{};
    std::int64_t rounding_error_raw{};
};

class IslandSolverScratch final {
public:
    IslandSolverScratch(std::size_t maximum_bodies, std::size_t maximum_contacts);

    [[nodiscard]] std::size_t maximum_bodies() const noexcept { return maximum_bodies_; }
    [[nodiscard]] std::size_t maximum_contacts() const noexcept { return maximum_contacts_; }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

public:
    // Internal fixed-capacity buffers; exposed for allocation-free solver helpers.
    struct StarLeaf final {
        Fixed::rep value{};
        std::size_t body{};
        auto operator<=>(const StarLeaf&) const = default;
    };

    std::size_t maximum_bodies_{};
    std::size_t maximum_contacts_{};
    std::vector<Fixed::rep> ordered_values_{};
    std::vector<Fixed::rep> projected_values_{};
    std::vector<std::size_t> block_begin_{};
    std::vector<std::size_t> block_end_{};
    std::vector<WideInteger> block_sum_{};
    std::vector<std::uint32_t> degree_{};
    std::vector<StarLeaf> star_leaves_{};

    friend SpecializedIslandProjectionStats project_contact_islands_specialized(
        std::span<Fixed::rep>,
        std::span<const SweptContact>,
        const ContactIslandWorkspace&,
        std::size_t,
        IslandSolverScratch&,
        class GeneralImpulseWarmStart*);
};

class GeneralImpulseWarmStart final {
public:
    explicit GeneralImpulseWarmStart(std::size_t maximum_contacts);

    void clear() noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept { return maximum_contacts_; }
    [[nodiscard]] std::size_t size() const noexcept { return contact_count_; }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

private:
    std::size_t maximum_contacts_{};
    std::size_t contact_count_{};
    bool initialized_{};
    std::vector<std::size_t> first_{};
    std::vector<std::size_t> second_{};
    std::vector<ContactAxis> axis_{};
    std::vector<Fixed::rep> impulses_{};
    std::vector<Fixed::rep> next_impulses_{};

    friend SpecializedIslandProjectionStats project_contact_islands_specialized(
        std::span<Fixed::rep>,
        std::span<const SweptContact>,
        const ContactIslandWorkspace&,
        std::size_t,
        IslandSolverScratch&,
        GeneralImpulseWarmStart*);
};

// Uses exact specialized projections where the graph structure admits a proven reduction:
// - matching: pair projection;
// - canonical consecutive chain: PAV isotonic regression;
// - canonical simple cycle: redundant closing edge removed, then PAV;
// - outward canonical star: closed-form star isotonic projection.
// Other trees/cycles/general graphs use deterministic colored pair projections.
[[nodiscard]] SpecializedIslandProjectionStats project_contact_islands_specialized(
    std::span<Fixed::rep> values,
    std::span<const SweptContact> contacts,
    const ContactIslandWorkspace& workspace,
    std::size_t fallback_iterations,
    IslandSolverScratch& scratch,
    GeneralImpulseWarmStart* warm_start = nullptr);

} // namespace neoeng::core
