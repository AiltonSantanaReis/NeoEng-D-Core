#pragma once

#include "neoeng/core/contact_solver.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace neoeng::core {

enum class IslandTopology : std::uint8_t {
    Matching,
    Chain,
    Tree,
    Cycle,
    General,
};

[[nodiscard]] const char* to_string(IslandTopology topology) noexcept;

struct ContactIslandDescriptor final {
    std::size_t minimum_body{};
    std::size_t body_begin{};
    std::size_t body_count{};
    std::size_t contact_begin{};
    std::size_t contact_count{};
    std::size_t color_count{};
    IslandTopology topology{IslandTopology::General};
};

struct IslandClassificationStats final {
    std::uint64_t classifications{};
    std::uint64_t bodies_scanned{};
    std::uint64_t contacts_scanned{};
    std::uint64_t union_operations{};
    std::uint64_t colors_assigned{};
    std::uint64_t capacity_failures{};
};

class ContactIslandWorkspace final {
public:
    ContactIslandWorkspace(
        std::size_t maximum_bodies,
        std::size_t maximum_contacts);

    void classify(
        std::size_t body_count,
        std::span<const SweptContact> contacts);

    [[nodiscard]] std::span<const ContactIslandDescriptor> islands() const noexcept;
    [[nodiscard]] std::span<const std::size_t> body_order() const noexcept;
    [[nodiscard]] std::span<const std::size_t> contact_order() const noexcept;
    [[nodiscard]] std::span<const std::uint16_t> contact_colors() const noexcept;
    [[nodiscard]] std::size_t body_count() const noexcept { return body_count_; }
    [[nodiscard]] std::size_t contact_count() const noexcept { return contact_count_; }
    [[nodiscard]] std::size_t maximum_bodies() const noexcept { return maximum_bodies_; }
    [[nodiscard]] std::size_t maximum_contacts() const noexcept { return maximum_contacts_; }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;
    [[nodiscard]] IslandClassificationStats stats() const noexcept { return stats_; }

private:
    [[nodiscard]] std::size_t find(std::size_t body) noexcept;
    void unite(std::size_t first, std::size_t second) noexcept;

    std::size_t maximum_bodies_{};
    std::size_t maximum_contacts_{};
    std::size_t body_count_{};
    std::size_t contact_count_{};
    std::size_t island_count_{};
    std::size_t involved_body_count_{};

    std::vector<std::size_t> parent_{};
    std::vector<std::uint32_t> degree_{};
    std::vector<std::uint8_t> involved_{};
    std::vector<std::size_t> root_to_island_{};
    std::vector<std::size_t> body_write_cursor_{};
    std::vector<std::size_t> contact_write_cursor_{};
    std::vector<std::uint64_t> used_color_mask_{};

    std::vector<ContactIslandDescriptor> descriptors_{};
    std::vector<std::size_t> body_order_{};
    std::vector<std::size_t> contact_order_{};
    std::vector<std::uint16_t> contact_colors_{};

    IslandClassificationStats stats_{};
};

struct IslandProjectionStats final {
    std::uint64_t islands_processed{};
    std::uint64_t contacts_processed{};
    std::uint64_t pair_adjustments{};
    std::uint64_t violations_before{};
    std::uint64_t violations_after{};
    std::size_t workers_used{};
};

// Projects scalar values toward the canonical constraints
// value[min(body)] <= value[max(body)] using deterministic edge coloring.
// Distinct islands write disjoint body sets, so static island partitioning is race-free.
[[nodiscard]] IslandProjectionStats project_contact_islands_monotone(
    std::span<Fixed::rep> values,
    std::span<const SweptContact> contacts,
    const ContactIslandWorkspace& workspace,
    std::size_t iterations,
    std::size_t worker_count = 1U);


class PersistentIslandWorkerPool final {
public:
    explicit PersistentIslandWorkerPool(std::size_t worker_count);
    ~PersistentIslandWorkerPool();

    PersistentIslandWorkerPool(const PersistentIslandWorkerPool&) = delete;
    PersistentIslandWorkerPool& operator=(const PersistentIslandWorkerPool&) = delete;
    PersistentIslandWorkerPool(PersistentIslandWorkerPool&&) = delete;
    PersistentIslandWorkerPool& operator=(PersistentIslandWorkerPool&&) = delete;

    [[nodiscard]] std::size_t worker_count() const noexcept;
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    friend IslandProjectionStats project_contact_islands_monotone_pooled(
        std::span<Fixed::rep>,
        std::span<const SweptContact>,
        const ContactIslandWorkspace&,
        std::size_t,
        PersistentIslandWorkerPool&,
        std::size_t);
};

[[nodiscard]] IslandProjectionStats project_contact_islands_monotone_pooled(
    std::span<Fixed::rep> values,
    std::span<const SweptContact> contacts,
    const ContactIslandWorkspace& workspace,
    std::size_t iterations,
    PersistentIslandWorkerPool& pool,
    std::size_t worker_count);

[[nodiscard]] std::uint64_t island_projection_hash(
    std::span<const Fixed::rep> values,
    const ContactIslandWorkspace& workspace) noexcept;

} // namespace neoeng::core
