#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <span>
#include <limits>
#include <stdexcept>
#include <vector>

namespace neoeng::render {

enum class RepresentationKind : std::uint8_t {
    Mesh = 0U,
    Voxel = 1U,
    SignedDistanceField = 2U,
    Reserved = 3U,
};

struct VisibilitySemantic final {
    std::uint32_t entity_id{};
    std::uint32_t primitive_id{};
    std::uint16_t material_id{};
    RepresentationKind representation{RepresentationKind::Mesh};
    std::uint8_t flags{};

    auto operator<=>(const VisibilitySemantic&) const = default;
};

inline constexpr std::uint32_t kVisibilityMaxEntityId = (1U << 24U) - 1U;
inline constexpr std::uint32_t kVisibilityMaxPrimitiveId = (1U << 24U) - 1U;
inline constexpr std::uint16_t kVisibilityMaxMaterialId = (1U << 12U) - 1U;
inline constexpr std::uint8_t kVisibilityMaxFlags = 3U;
inline constexpr std::uint32_t kVisibilityClearDepth = 0xFFFF'FFFFU;
inline constexpr std::int32_t kVisibilitySubpixelBits = 8;
inline constexpr std::int32_t kVisibilitySubpixelScale = 1 << kVisibilitySubpixelBits;
inline constexpr std::int32_t kVisibilityMaxPixelCoordinate =
    std::numeric_limits<std::int32_t>::max() / kVisibilitySubpixelScale;
inline constexpr std::int32_t kVisibilityMinPixelCoordinate =
    std::numeric_limits<std::int32_t>::min() / kVisibilitySubpixelScale;

[[nodiscard]] std::uint64_t pack_visibility_semantic(const VisibilitySemantic& semantic);
[[nodiscard]] VisibilitySemantic unpack_visibility_semantic(std::uint64_t packed) noexcept;

struct Rgba8 final {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255U};

    auto operator<=>(const Rgba8&) const = default;
};

struct ScreenVertex final {
    std::int32_t x_subpixel{};
    std::int32_t y_subpixel{};
    std::uint32_t depth{};
    Rgba8 color{};
};

struct MeshTriangle final {
    std::array<ScreenVertex, 3U> vertices{};
    VisibilitySemantic semantic{};
};

struct VisibilitySample final {
    std::uint64_t semantic{};
    std::uint32_t depth{kVisibilityClearDepth};
    std::uint16_t attribute_u{};
    std::uint16_t attribute_v{};

    [[nodiscard]] bool valid() const noexcept { return depth != kVisibilityClearDepth; }
    auto operator<=>(const VisibilitySample&) const = default;
};

struct VisibilityBuffer final {
    std::size_t width{};
    std::size_t height{};
    std::vector<VisibilitySample> samples{};

    [[nodiscard]] const VisibilitySample& at(std::size_t x, std::size_t y) const;
    [[nodiscard]] VisibilitySample& at(std::size_t x, std::size_t y);
};

struct ColorBuffer final {
    std::size_t width{};
    std::size_t height{};
    std::vector<Rgba8> pixels{};

    [[nodiscard]] const Rgba8& at(std::size_t x, std::size_t y) const;
    [[nodiscard]] Rgba8& at(std::size_t x, std::size_t y);
};

struct VisibilityReferenceResult final {
    VisibilityBuffer visibility{};
    ColorBuffer deferred_color{};
    ColorBuffer conventional_color{};
    std::size_t submitted_triangles{};
    std::size_t degenerate_triangles{};
    std::size_t covered_samples{};
    std::size_t depth_rejections{};
    std::size_t deterministic_tie_breaks{};
    std::size_t unresolved_semantics{};
    std::size_t color_mismatches{};
    std::uint64_t visibility_hash{};
    std::uint64_t deferred_hash{};
    std::uint64_t conventional_hash{};
};

[[nodiscard]] inline std::int32_t subpixel_from_pixel(std::int32_t pixel) {
    if (pixel < kVisibilityMinPixelCoordinate || pixel > kVisibilityMaxPixelCoordinate) {
        throw std::out_of_range("pixel coordinate exceeds 24.8 reference range");
    }
    return static_cast<std::int32_t>(
        static_cast<std::int64_t>(pixel) * kVisibilitySubpixelScale);
}

[[nodiscard]] VisibilityReferenceResult render_visibility_reference(
    std::size_t width,
    std::size_t height,
    std::span<const MeshTriangle> triangles);

[[nodiscard]] std::uint64_t hash_visibility_buffer(const VisibilityBuffer& buffer) noexcept;
[[nodiscard]] std::uint64_t hash_color_buffer(const ColorBuffer& buffer) noexcept;

} // namespace neoeng::render
