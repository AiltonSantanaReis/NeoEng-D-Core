#include "neoeng/core/fixed_slot_allocator.hpp"

#include <bit>
#include <limits>
#include <stdexcept>

namespace neoeng::core {
namespace {
constexpr std::uint64_t all_bits = std::numeric_limits<std::uint64_t>::max();
}

FixedSlotAllocator::FixedSlotAllocator(std::size_t capacity, FixedSlotAllocatorMode mode) {
    configure(capacity, mode);
}

void FixedSlotAllocator::configure(std::size_t capacity, FixedSlotAllocatorMode mode) {
    if (capacity == 0U || capacity > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::invalid_argument("Fixed slot allocator capacity is invalid");
    }
    capacity_ = capacity;
    mode_ = mode;
    free_words_.assign((capacity + word_bits - 1U) / word_bits, all_bits);
    summary_words_.assign((free_words_.size() + word_bits - 1U) / word_bits, 0U);
    reset();
}

void FixedSlotAllocator::reset() noexcept {
    if (capacity_ == 0U) return;
    for (std::uint64_t& word : free_words_) word = all_bits;
    const std::size_t valid_last = capacity_ % word_bits;
    if (valid_last != 0U) free_words_.back() = (std::uint64_t{1} << valid_last) - 1U;
    rebuild_summary();
    next_cursor_ = 0U;
    stats_ = {};
}

void FixedSlotAllocator::rebuild_summary() noexcept {
    for (std::uint64_t& word : summary_words_) word = 0U;
    for (std::size_t word = 0U; word < free_words_.size(); ++word) {
        if (free_words_[word] != 0U) {
            summary_words_[word / word_bits] |= std::uint64_t{1} << (word % word_bits);
        }
    }
}

bool FixedSlotAllocator::is_free(std::uint32_t index) const noexcept {
    if (index >= capacity_) return false;
    const std::size_t word = static_cast<std::size_t>(index) / word_bits;
    const unsigned bit = static_cast<unsigned>(index % word_bits);
    return (free_words_[word] & (std::uint64_t{1} << bit)) != 0U;
}

void FixedSlotAllocator::mark_used(std::uint32_t index) noexcept {
    const std::size_t word = static_cast<std::size_t>(index) / word_bits;
    const unsigned bit = static_cast<unsigned>(index % word_bits);
    free_words_[word] &= ~(std::uint64_t{1} << bit);
    if (free_words_[word] == 0U) {
        summary_words_[word / word_bits] &= ~(std::uint64_t{1} << (word % word_bits));
    }
    ++stats_.used;
    if (stats_.used > stats_.peak_used) stats_.peak_used = stats_.used;
    ++stats_.acquisitions;
}

void FixedSlotAllocator::mark_free(std::uint32_t index) noexcept {
    const std::size_t word = static_cast<std::size_t>(index) / word_bits;
    const unsigned bit = static_cast<unsigned>(index % word_bits);
    free_words_[word] |= std::uint64_t{1} << bit;
    summary_words_[word / word_bits] |= std::uint64_t{1} << (word % word_bits);
    --stats_.used;
    ++stats_.releases;
}

std::uint32_t FixedSlotAllocator::acquire_next_fit() {
    for (std::size_t offset = 0U; offset < capacity_; ++offset) {
        const std::uint32_t index = static_cast<std::uint32_t>((static_cast<std::size_t>(next_cursor_) + offset) % capacity_);
        ++stats_.probes;
        if (!is_free(index)) continue;
        mark_used(index);
        next_cursor_ = static_cast<std::uint32_t>((static_cast<std::size_t>(index) + 1U) % capacity_);
        return index;
    }
    ++stats_.failed_acquisitions;
    throw std::length_error("Fixed next-fit allocator is exhausted");
}

std::uint32_t FixedSlotAllocator::acquire_bitmap() {
    for (std::size_t summary_index = 0U; summary_index < summary_words_.size(); ++summary_index) {
        ++stats_.probes;
        const std::uint64_t summary = summary_words_[summary_index];
        if (summary == 0U) continue;
        const unsigned word_bit = static_cast<unsigned>(std::countr_zero(summary));
        const std::size_t word_index = summary_index * word_bits + word_bit;
        if (word_index >= free_words_.size()) break;
        ++stats_.probes;
        const std::uint64_t free_word = free_words_[word_index];
        const unsigned slot_bit = static_cast<unsigned>(std::countr_zero(free_word));
        const std::size_t raw_index = word_index * word_bits + slot_bit;
        if (raw_index >= capacity_) break;
        const auto index = static_cast<std::uint32_t>(raw_index);
        mark_used(index);
        return index;
    }
    ++stats_.failed_acquisitions;
    throw std::length_error("Fixed hierarchical bitmap allocator is exhausted");
}

std::uint32_t FixedSlotAllocator::acquire_adaptive() {
    // Exact integer threshold: bitmap at two-thirds occupancy and above. The policy is
    // deterministic and depends only on allocator state, never on wall-clock data.
    if (stats_.used * 3U >= capacity_ * 2U) {
        ++stats_.bitmap_dispatches;
        return acquire_bitmap();
    }
    ++stats_.next_fit_dispatches;
    return acquire_next_fit();
}

std::uint32_t FixedSlotAllocator::acquire() {
    if (capacity_ == 0U) throw std::logic_error("Fixed slot allocator is not configured");
    switch (mode_) {
    case FixedSlotAllocatorMode::NextFit: return acquire_next_fit();
    case FixedSlotAllocatorMode::HierarchicalBitmap: return acquire_bitmap();
    case FixedSlotAllocatorMode::Adaptive: return acquire_adaptive();
    }
    throw std::logic_error("Unknown fixed slot allocator mode");
}

void FixedSlotAllocator::release(std::uint32_t index) {
    if (index >= capacity_) throw std::out_of_range("Fixed slot allocator release is outside capacity");
    if (is_free(index)) throw std::logic_error("Fixed slot allocator double release");
    mark_free(index);
}

std::size_t FixedSlotAllocator::reserved_bytes() const noexcept {
    return free_words_.capacity() * sizeof(std::uint64_t)
        + summary_words_.capacity() * sizeof(std::uint64_t);
}

} // namespace neoeng::core
