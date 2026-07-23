#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace neoeng::core {

template <typename T>
class IndexedFrameRing final {
public:
    explicit IndexedFrameRing(std::size_t capacity)
        : slots_(capacity) {
        if (capacity == 0U) throw std::invalid_argument("Indexed ring capacity must be positive");
    }

    void capture(std::uint64_t frame, T value) {
        if (latest_.has_value()) {
            if (*latest_ == std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("Indexed ring frame counter would wrap");
            }
            if (frame != *latest_ + 1U) {
                throw std::invalid_argument("Indexed ring frames must be consecutive");
            }
        }
        auto& slot = slots_[slot_index(frame)];
        slot = Slot{.frame = frame, .value = std::move(value)};
        latest_ = frame;
        if (!oldest_.has_value()) oldest_ = frame;
        const std::uint64_t retained = *latest_ - *oldest_ + 1U;
        if (retained > slots_.size()) oldest_ = *latest_ - slots_.size() + 1U;
    }

    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept {
        if (!oldest_.has_value() || frame < *oldest_ || frame > *latest_) return false;
        const auto& slot = slots_[slot_index(frame)];
        return slot.has_value() && slot->frame == frame;
    }

    [[nodiscard]] const T& at(std::uint64_t frame) const {
        if (!contains(frame)) throw std::out_of_range("Indexed ring frame is not retained");
        return slots_[slot_index(frame)]->value;
    }

    [[nodiscard]] T& at(std::uint64_t frame) {
        if (!contains(frame)) throw std::out_of_range("Indexed ring frame is not retained");
        return slots_[slot_index(frame)]->value;
    }

    void truncate_after(std::uint64_t frame) {
        if (!latest_.has_value()) return;
        if (!contains(frame)) throw std::out_of_range("Indexed ring truncation frame is not retained");
        for (std::uint64_t current = frame + 1U; current <= *latest_; ++current) {
            auto& slot = slots_[slot_index(current)];
            if (slot.has_value() && slot->frame == current) slot.reset();
        }
        latest_ = frame;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }
    [[nodiscard]] std::size_t size() const noexcept {
        if (!oldest_.has_value()) return 0U;
        return static_cast<std::size_t>(*latest_ - *oldest_ + 1U);
    }
    [[nodiscard]] std::optional<std::uint64_t> oldest_frame() const noexcept { return oldest_; }
    [[nodiscard]] std::optional<std::uint64_t> latest_frame() const noexcept { return latest_; }

private:
    struct Slot final {
        std::uint64_t frame{};
        T value{};
    };

    [[nodiscard]] std::size_t slot_index(std::uint64_t frame) const noexcept {
        return static_cast<std::size_t>(frame % slots_.size());
    }

    std::vector<std::optional<Slot>> slots_{};
    std::optional<std::uint64_t> oldest_{};
    std::optional<std::uint64_t> latest_{};
};

} // namespace neoeng::core
