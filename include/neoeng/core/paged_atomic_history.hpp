#pragma once

#include "neoeng/core/atomic_temporal_physics.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace neoeng::core {

struct PagedAtomicHistoryConfig final {
    std::size_t bodies{};
    std::size_t contacts{};
    std::size_t maximum_candidate_pairs{};
    std::size_t history_capacity{32U};
    std::size_t page_elements{256U};
    // Zero preserves the v0.17 worst-case reservation (every position page per frame).
    // A non-zero value enables a bounded sparse-position pool.
    std::size_t maximum_position_dirty_pages_per_frame{};
    std::size_t maximum_velocity_dirty_pages_per_frame{4U};
    std::size_t maximum_mass_dirty_pages_per_frame{2U};
    std::size_t maximum_contact_dirty_pages_per_frame{4U};
    std::size_t full_position_generations{2U};
    std::size_t full_velocity_generations{3U};
    std::size_t full_contact_generations{3U};
    std::size_t maximum_cache_generations{4U};
};

struct PagedAtomicHistoryStats final {
    std::uint64_t snapshots_captured{};
    std::uint64_t snapshots_evicted{};
    std::uint64_t pages_copied{};
    std::uint64_t pages_shared{};
    std::uint64_t pages_released{};
    std::uint64_t zero_copy_promotions{};
    std::uint64_t page_pool_exhaustions{};
    std::size_t live_pages{};
    std::size_t peak_live_pages{};
};

class PagedAtomicHistory final {
public:
    explicit PagedAtomicHistory(PagedAtomicHistoryConfig config);

    void capture(const AtomicTemporalExternalState& state);
    void capture(AtomicTemporalStateView state, AtomicTemporalCaptureHints hints);
    void restore(std::uint64_t frame, AtomicTemporalExternalState& output) const;
    [[nodiscard]] AtomicTemporalRestoreMetadata restore_direct(
        std::uint64_t frame, AtomicTemporalMutableStateView output) const;
    [[nodiscard]] AtomicTemporalRestoreMetadata restore_direct_from_current(
        std::uint64_t frame, std::uint64_t current_frame,
        AtomicTemporalMutableStateView output,
        bool restore_pairs = true) const;
    void truncate_after(std::uint64_t frame);
    void clear();

    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;
    [[nodiscard]] std::size_t live_payload_bytes() const noexcept;
    [[nodiscard]] const PagedAtomicHistoryStats& stats() const noexcept { return stats_; }

private:
    using PageId = std::uint32_t;
    static constexpr PageId invalid_page = std::numeric_limits<PageId>::max();
    static constexpr std::uint64_t empty_frame = ~std::uint64_t{0};

    template <typename T>
    class PagePool final {
    public:
        PagePool() = default;
        PagePool(std::size_t page_elements, std::size_t page_capacity)
            : page_elements_(page_elements), data_(page_elements * page_capacity),
              refcount_(page_capacity), free_(page_capacity) {
            if (page_elements == 0U || page_capacity == 0U
                || page_capacity >= static_cast<std::size_t>(invalid_page)) {
                throw std::invalid_argument("Paged history page pool configuration is invalid");
            }
            for (std::size_t index = 0U; index < page_capacity; ++index) {
                free_[index] = static_cast<PageId>(page_capacity - 1U - index);
            }
            free_count_ = page_capacity;
        }

        [[nodiscard]] PageId acquire() {
            if (free_count_ == 0U) throw std::length_error("Paged history page pool exhausted");
            const PageId id = free_[--free_count_];
            if (refcount_[id] != 0U) throw std::logic_error("Paged history free list corruption");
            refcount_[id] = 1U;
            ++live_pages_;
            peak_live_pages_ = std::max(peak_live_pages_, live_pages_);
            return id;
        }

        void retain(PageId id) {
            validate_live(id);
            if (refcount_[id] == std::numeric_limits<std::uint32_t>::max()) {
                throw std::overflow_error("Paged history reference count overflow");
            }
            ++refcount_[id];
        }

        [[nodiscard]] bool release(PageId id) {
            validate_live(id);
            const bool survives = refcount_[id] > 1U;
            if (--refcount_[id] == 0U) {
                free_[free_count_++] = id;
                --live_pages_;
            }
            return survives;
        }

        [[nodiscard]] std::span<T> page(PageId id) {
            validate_live(id);
            return std::span<T>(data_.data() + static_cast<std::size_t>(id) * page_elements_, page_elements_);
        }
        [[nodiscard]] std::span<const T> page(PageId id) const {
            validate_live(id);
            return std::span<const T>(data_.data() + static_cast<std::size_t>(id) * page_elements_, page_elements_);
        }
        [[nodiscard]] std::size_t refcount(PageId id) const {
            validate_live(id);
            return refcount_[id];
        }
        [[nodiscard]] std::size_t reserved_bytes() const noexcept {
            return data_.capacity() * sizeof(T) + refcount_.capacity() * sizeof(std::uint32_t)
                + free_.capacity() * sizeof(PageId);
        }
        [[nodiscard]] std::size_t live_payload_bytes() const noexcept {
            return live_pages_ * page_elements_ * sizeof(T);
        }
        [[nodiscard]] std::size_t live_pages() const noexcept { return live_pages_; }
        [[nodiscard]] std::size_t peak_live_pages() const noexcept { return peak_live_pages_; }
        [[nodiscard]] std::size_t page_elements() const noexcept { return page_elements_; }

    private:
        void validate_live(PageId id) const {
            if (id == invalid_page || id >= refcount_.size() || refcount_[id] == 0U) {
                throw std::out_of_range("Paged history page id is not live");
            }
        }

        std::size_t page_elements_{};
        std::vector<T> data_{};
        std::vector<std::uint32_t> refcount_{};
        std::vector<PageId> free_{};
        std::size_t free_count_{};
        std::size_t live_pages_{};
        std::size_t peak_live_pages_{};
    };

    struct Snapshot final {
        std::uint64_t frame{empty_frame};
        std::uint64_t valid_until_frame{};
        std::uint64_t topology_signature{};
        std::size_t pair_count{};
        std::vector<PageId> position_x{}, position_y{}, velocity_x{}, velocity_y{};
        std::vector<PageId> masses{}, dual{}, manifold{}, contact_stable{}, contact_candidate{};
        std::vector<PageId> fat_bounds{}, pairs{};
    };

    template <typename T>
    void capture_array(
        std::span<const T> values,
        const std::vector<PageId>* previous,
        std::size_t previous_size,
        std::span<const std::uint8_t> dirty_pages,
        bool force_copy,
        std::vector<PageId>& output,
        PagePool<T>& pool);

    template <typename T>
    void restore_array(
        const std::vector<PageId>& refs,
        std::size_t logical_size,
        std::span<T> output,
        const PagePool<T>& pool) const;

    template <typename T>
    void restore_array_delta(
        const std::vector<PageId>& refs,
        const std::vector<PageId>* current_refs,
        std::size_t logical_size,
        std::span<T> output,
        const PagePool<T>& pool) const;

    template <typename T>
    void release_refs(std::vector<PageId>& refs, PagePool<T>& pool);

    void capture_internal(AtomicTemporalStateView state, AtomicTemporalCaptureHints hints, bool force_all);
    void release_snapshot(Snapshot& snapshot);
    [[nodiscard]] Snapshot& slot(std::uint64_t frame) noexcept;
    [[nodiscard]] const Snapshot& slot(std::uint64_t frame) const noexcept;
    [[nodiscard]] const Snapshot* previous_snapshot(std::uint64_t frame) const noexcept;
    void refresh_page_stats() noexcept;

    PagedAtomicHistoryConfig config_{};
    std::size_t body_pages_{};
    std::size_t contact_pages_{};
    std::size_t pair_pages_{};
    std::size_t size_{};
    std::vector<Snapshot> snapshots_{};
    Snapshot staging_{};
    std::vector<std::uint8_t> position_dirty_pages_{};
    std::vector<std::uint8_t> body_dirty_pages_{};
    std::vector<std::uint8_t> contact_dirty_pages_{};
    std::vector<std::uint8_t> all_body_pages_{};
    std::vector<std::uint8_t> all_contact_pages_{};
    std::vector<std::uint8_t> all_pair_pages_{};
    std::vector<std::uint8_t> no_body_pages_{};
    std::vector<std::uint8_t> no_contact_pages_{};
    std::vector<std::uint8_t> no_pair_pages_{};

    PagePool<Fixed::rep> position_x_pool_{}, position_y_pool_{};
    PagePool<Fixed::rep> velocity_x_pool_{}, velocity_y_pool_{};
    PagePool<std::uint32_t> masses_pool_{};
    PagePool<Fixed::rep> dual_pool_{};
    PagePool<NormalContact> manifold_pool_{};
    PagePool<std::uint8_t> stable_pool_{};
    PagePool<std::uint8_t> candidate_pool_{};
    PagePool<FatAabb> bounds_pool_{};
    PagePool<BroadphasePair> pair_pool_{};
    PagedAtomicHistoryStats stats_{};
};

} // namespace neoeng::core
