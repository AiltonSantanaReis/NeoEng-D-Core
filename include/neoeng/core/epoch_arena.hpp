#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace neoeng::core {

struct EpochArenaStats final {
    std::uint64_t allocations{};
    std::uint64_t bytes_requested{};
    std::uint64_t bytes_committed{};
    std::uint64_t epochs_reclaimed{};
    std::uint64_t overflow_blocks{};
};

class PersistentEpochArena final {
public:
    PersistentEpochArena(std::size_t retained_epochs, std::size_t bytes_per_epoch);

    void begin_epoch(std::uint64_t frame);
    [[nodiscard]] void* allocate(std::size_t bytes, std::size_t alignment);

    template <typename T>
    [[nodiscard]] std::span<T> allocate_array(std::size_t count) {
        return {static_cast<T*>(allocate(sizeof(T) * count, alignof(T))), count};
    }

    [[nodiscard]] EpochArenaStats stats() const noexcept { return stats_; }
    [[nodiscard]] std::size_t retained_epochs() const noexcept { return epochs_.size(); }
    [[nodiscard]] std::size_t bytes_per_epoch() const noexcept { return bytes_per_epoch_; }

private:
    struct OverflowBlock final {
        std::unique_ptr<std::byte[]> data{};
        std::size_t size{};
    };

    struct Epoch final {
        std::uint64_t frame{};
        bool initialized{};
        std::unique_ptr<std::byte[]> storage{};
        std::size_t offset{};
        std::vector<OverflowBlock> overflow{};
    };

    [[nodiscard]] static std::size_t align_up(std::size_t value, std::size_t alignment);

    std::vector<Epoch> epochs_{};
    std::size_t bytes_per_epoch_{};
    Epoch* current_{};
    EpochArenaStats stats_{};
};

} // namespace neoeng::core
