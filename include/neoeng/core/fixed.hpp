#pragma once

#include <compare>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace neoeng::core {

#if defined(__SIZEOF_INT128__)
__extension__ typedef __int128 WideInteger;
#else
#error "NeoEng Core Lab requires a compiler with signed 128-bit integer support"
#endif

class Fixed final {
public:
    using rep = std::int64_t;
    static constexpr int fractional_bits = 32;
    static constexpr rep scale = rep{1} << fractional_bits;

    constexpr Fixed() noexcept = default;

    [[nodiscard]] static constexpr Fixed from_raw(rep value) noexcept {
        return Fixed(value, RawTag{});
    }

    [[nodiscard]] static Fixed from_integer(rep value) {
        const auto wide = static_cast<WideInteger>(value) * static_cast<WideInteger>(scale);
        return from_raw(narrow(wide));
    }

    [[nodiscard]] static Fixed from_ratio(rep numerator, rep denominator) {
        if (denominator == 0) {
            throw std::domain_error("Fixed division by zero");
        }
        const auto wide = static_cast<WideInteger>(numerator) * static_cast<WideInteger>(scale)
                        / static_cast<WideInteger>(denominator);
        return from_raw(narrow(wide));
    }

    [[nodiscard]] constexpr rep raw() const noexcept { return raw_; }
    [[nodiscard]] double to_double() const noexcept {
        return static_cast<double>(raw_) / static_cast<double>(scale);
    }

    friend Fixed operator+(Fixed lhs, Fixed rhs) {
        return from_raw(narrow(static_cast<WideInteger>(lhs.raw_) + rhs.raw_));
    }

    friend Fixed operator-(Fixed lhs, Fixed rhs) {
        return from_raw(narrow(static_cast<WideInteger>(lhs.raw_) - rhs.raw_));
    }

    friend Fixed operator-(Fixed value) {
        return from_raw(narrow(-static_cast<WideInteger>(value.raw_)));
    }

    friend Fixed operator*(Fixed lhs, Fixed rhs) {
        const auto product = static_cast<WideInteger>(lhs.raw_) * rhs.raw_;
        return from_raw(narrow(product / static_cast<WideInteger>(scale)));
    }

    friend Fixed operator/(Fixed lhs, Fixed rhs) {
        if (rhs.raw_ == 0) {
            throw std::domain_error("Fixed division by zero");
        }
        const auto quotient = static_cast<WideInteger>(lhs.raw_) * static_cast<WideInteger>(scale)
                            / static_cast<WideInteger>(rhs.raw_);
        return from_raw(narrow(quotient));
    }

    Fixed& operator+=(Fixed rhs) { return *this = *this + rhs; }
    Fixed& operator-=(Fixed rhs) { return *this = *this - rhs; }
    Fixed& operator*=(Fixed rhs) { return *this = *this * rhs; }
    Fixed& operator/=(Fixed rhs) { return *this = *this / rhs; }

    auto operator<=>(const Fixed&) const = default;

private:
    struct RawTag {};
    constexpr Fixed(rep value, RawTag) noexcept : raw_(value) {}

    [[nodiscard]] static rep narrow(WideInteger value) {
        constexpr auto min = static_cast<WideInteger>(std::numeric_limits<rep>::min());
        constexpr auto max = static_cast<WideInteger>(std::numeric_limits<rep>::max());
        if (value < min || value > max) {
            throw std::overflow_error("Fixed-point overflow");
        }
        return static_cast<rep>(value);
    }

    rep raw_{0};
};

inline constexpr Fixed kSimulationDelta = Fixed::from_raw(71'582'788); // round(2^32 / 60)

} // namespace neoeng::core
