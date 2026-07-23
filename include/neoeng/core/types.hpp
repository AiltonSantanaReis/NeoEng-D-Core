#pragma once

#include "neoeng/core/fixed.hpp"

#include <cstdint>
#include <vector>

namespace neoeng::core {

using EntityId = std::uint32_t;

struct Vec2 final {
    Fixed x{};
    Fixed y{};

    auto operator<=>(const Vec2&) const = default;
};

struct Body final {
    EntityId id{};
    Vec2 position{};
    Vec2 velocity{};

    auto operator<=>(const Body&) const = default;
};

struct InputCommand final {
    EntityId entity{};
    Vec2 acceleration{};

    auto operator<=>(const InputCommand&) const = default;
};

struct WorldState final {
    std::uint64_t frame{};
    std::vector<Body> bodies{};

    auto operator<=>(const WorldState&) const = default;
};

} // namespace neoeng::core
