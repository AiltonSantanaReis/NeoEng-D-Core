#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace neoeng::core {

enum class FixedSlotAllocatorMode : std::uint8_t {
    NextFit,
    HierarchicalBitmap,
    Adaptive,
};

struct FixedSlotAllocatorStats final {
    std::uint64_t acquisitions{};
    std::uint64_t releases{};
    std::uint64_t probes{};
    std::uint64_t failed_acquisitions{};
    std::size_t used{};
    std::size_t peak_used{};
    std::uint64_t next_fit_dispatches{};
    std::uint64_t bitmap_dispatches{};
};

// Fixed-capacity, allocation-free-after-construction slot allocator.
//
// NextFit preserves the v0.25 strategy and scans from a deterministic cursor.
// HierarchicalBitmap keeps a two-level summary of free words and selects the
// lowest free slot deterministically. Both modes share the same exact free-bit
// representation so they can be cross-checked under fuzzing.
class FixedSlotAllocator final {
public:
    FixedSlotAllocator() = default;
    FixedSlotAllocator(std::size_t capacity, FixedSlotAllocatorMode mode);

    void configure(std::size_t capacity, FixedSlotAllocatorMode mode);
    [[nodiscard]] std::uint32_t acquire();
    void release(std::uint32_t index);
    void reset() noexcept;

    [[nodiscard]] bool is_free(std::uint32_t index) const noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t free_count() const noexcept { return capacity_ - stats_.used; }
    [[nodiscard]] FixedSlotAllocatorMode mode() const noexcept { return mode_; }
    [[nodiscard]] const FixedSlotAllocatorStats& stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

private:
    static constexpr std::size_t word_bits = 64U;

    [[nodiscard]] std::uint32_t acquire_next_fit();
    [[nodiscard]] std::uint32_t acquire_bitmap();
    [[nodiscard]] std::uint32_t acquire_adaptive();
    void mark_used(std::uint32_t index) noexcept;
    void mark_free(std::uint32_t index) noexcept;
    void rebuild_summary() noexcept;

    std::size_t capacity_{};
    FixedSlotAllocatorMode mode_{FixedSlotAllocatorMode::NextFit};
    std::vector<std::uint64_t> free_words_{};
    std::vector<std::uint64_t> summary_words_{};
    std::uint32_t next_cursor_{};
    FixedSlotAllocatorStats stats_{};
};

} // namespace neoeng::core
