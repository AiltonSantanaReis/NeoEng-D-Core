#include "neoeng/core/epoch_arena.hpp"

#include <limits>
#include <new>
#include <stdexcept>

namespace neoeng::core {

PersistentEpochArena::PersistentEpochArena(
    std::size_t retained_epochs,
    std::size_t bytes_per_epoch)
    : epochs_(retained_epochs), bytes_per_epoch_(bytes_per_epoch) {
    if (retained_epochs == 0U || bytes_per_epoch == 0U) {
        throw std::invalid_argument("Epoch arena dimensions must be positive");
    }
    for (Epoch& epoch : epochs_) epoch.storage = std::make_unique<std::byte[]>(bytes_per_epoch_);
    stats_.bytes_committed = retained_epochs * bytes_per_epoch_;
}

std::size_t PersistentEpochArena::align_up(std::size_t value, std::size_t alignment) {
    if (alignment == 0U || (alignment & (alignment - 1U)) != 0U) {
        throw std::invalid_argument("Arena alignment must be a non-zero power of two");
    }
    if (value > std::numeric_limits<std::size_t>::max() - (alignment - 1U)) {
        throw std::overflow_error("Arena alignment overflow");
    }
    return (value + alignment - 1U) & ~(alignment - 1U);
}

void PersistentEpochArena::begin_epoch(std::uint64_t frame) {
    Epoch& epoch = epochs_[static_cast<std::size_t>(frame % epochs_.size())];
    if (epoch.initialized && epoch.frame != frame) ++stats_.epochs_reclaimed;
    epoch.frame = frame;
    epoch.initialized = true;
    epoch.offset = 0U;
    epoch.overflow.clear();
    current_ = &epoch;
}

void* PersistentEpochArena::allocate(std::size_t bytes, std::size_t alignment) {
    if (current_ == nullptr) throw std::logic_error("Arena epoch must be begun before allocation");
    if (bytes == 0U) bytes = 1U;
    const std::size_t aligned = align_up(current_->offset, alignment);
    ++stats_.allocations;
    stats_.bytes_requested += bytes;
    if (aligned <= bytes_per_epoch_ && bytes <= bytes_per_epoch_ - aligned) {
        void* result = current_->storage.get() + aligned;
        current_->offset = aligned + bytes;
        return result;
    }

    const std::size_t block_size = bytes + alignment - 1U;
    OverflowBlock block{
        .data = std::make_unique<std::byte[]>(block_size),
        .size = block_size,
    };
    const auto address = reinterpret_cast<std::uintptr_t>(block.data.get());
    const auto aligned_address = (address + alignment - 1U) & ~(alignment - 1U);
    void* result = reinterpret_cast<void*>(aligned_address);
    stats_.bytes_committed += block_size;
    ++stats_.overflow_blocks;
    current_->overflow.push_back(std::move(block));
    return result;
}

} // namespace neoeng::core
