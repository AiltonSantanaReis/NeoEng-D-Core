#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace neoeng::core {

enum class DirtyComponent : std::uint8_t {
    None = 0,
    Identity = 1U << 0U,
    PositionX = 1U << 1U,
    PositionY = 1U << 2U,
    VelocityX = 1U << 3U,
    VelocityY = 1U << 4U,
    All = 0x1FU,
};

[[nodiscard]] constexpr std::uint8_t component_mask(DirtyComponent component) noexcept {
    return static_cast<std::uint8_t>(component);
}

[[nodiscard]] constexpr DirtyComponent operator|(DirtyComponent lhs, DirtyComponent rhs) noexcept {
    return static_cast<DirtyComponent>(component_mask(lhs) | component_mask(rhs));
}

class DirtySet final {
public:
    explicit DirtySet(std::size_t entity_count = 0)
        : entity_count_(entity_count),
          entity_bits_((entity_count + 63U) / 64U, 0U),
          component_masks_(entity_count, 0U) {}

    [[nodiscard]] static DirtySet full(std::size_t entity_count) {
        DirtySet set(entity_count);
        for (std::size_t index = 0; index < entity_count; ++index) {
            set.mark(index, DirtyComponent::All);
        }
        return set;
    }

    void mark(std::size_t index, DirtyComponent components) {
        if (index >= entity_count_) {
            throw std::out_of_range("DirtySet index is outside the world");
        }
        const std::uint8_t incoming = component_mask(components);
        if (incoming == 0U) {
            return;
        }
        if (component_masks_[index] == 0U) {
            entity_bits_[index / 64U] |= std::uint64_t{1} << (index % 64U);
            ++changed_count_;
        }
        component_masks_[index] |= incoming;
    }

    [[nodiscard]] bool is_dirty(std::size_t index) const noexcept {
        return index < entity_count_
            && (entity_bits_[index / 64U] & (std::uint64_t{1} << (index % 64U))) != 0U;
    }

    [[nodiscard]] bool component_dirty(std::size_t index, DirtyComponent component) const noexcept {
        return index < entity_count_
            && (component_masks_[index] & component_mask(component)) != 0U;
    }

    [[nodiscard]] std::uint8_t mask(std::size_t index) const noexcept {
        return index < entity_count_ ? component_masks_[index] : 0U;
    }

    [[nodiscard]] std::size_t entity_count() const noexcept { return entity_count_; }
    [[nodiscard]] std::size_t changed_count() const noexcept { return changed_count_; }
    [[nodiscard]] std::span<const std::uint64_t> words() const noexcept { return entity_bits_; }

    template <typename Function>
    void for_each_dirty(Function&& function) const {
        for (std::size_t word_index = 0; word_index < entity_bits_.size(); ++word_index) {
            std::uint64_t word = entity_bits_[word_index];
            while (word != 0U) {
#if defined(__GNUC__) || defined(__clang__)
                const unsigned bit = static_cast<unsigned>(__builtin_ctzll(word));
#else
                unsigned bit = 0;
                while (((word >> bit) & 1U) == 0U) {
                    ++bit;
                }
#endif
                const std::size_t index = word_index * 64U + bit;
                if (index < entity_count_) {
                    function(index, component_masks_[index]);
                }
                word &= word - 1U;
            }
        }
    }

private:
    std::size_t entity_count_{};
    std::vector<std::uint64_t> entity_bits_{};
    std::vector<std::uint8_t> component_masks_{};
    std::size_t changed_count_{};
};

} // namespace neoeng::core
