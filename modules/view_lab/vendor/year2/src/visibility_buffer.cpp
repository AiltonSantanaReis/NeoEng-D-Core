#include "neoeng/render/visibility_buffer.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace neoeng::render {
namespace {

#if defined(__SIZEOF_INT128__)
__extension__ using WideInteger = __int128;
#else
#error "NeoEng visibility reference requires signed 128-bit integer support"
#endif

constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;
constexpr std::uint64_t kEntityMask = (std::uint64_t{1} << 24U) - 1U;
constexpr std::uint64_t kPrimitiveMask = (std::uint64_t{1} << 24U) - 1U;
constexpr std::uint64_t kMaterialMask = (std::uint64_t{1} << 12U) - 1U;
constexpr std::uint16_t kAttributeScale = std::numeric_limits<std::uint16_t>::max();

struct Point final {
    std::int64_t x{};
    std::int64_t y{};
};

struct BaselineSample final {
    Rgba8 color{};
};

[[nodiscard]] std::int64_t edge(const Point& a, const Point& b, const Point& p) noexcept {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

[[nodiscard]] bool is_top_left_edge(const Point& a, const Point& b) noexcept {
    const std::int64_t dx = b.x - a.x;
    const std::int64_t dy = b.y - a.y;
    return dy > 0 || (dy == 0 && dx < 0);
}

[[nodiscard]] bool edge_accepts(
    std::int64_t value,
    const Point& a,
    const Point& b,
    std::int64_t orientation) noexcept {
    if (value > 0) {
        return true;
    }
    if (value < 0) {
        return false;
    }
    return orientation > 0 ? is_top_left_edge(a, b) : is_top_left_edge(b, a);
}

[[nodiscard]] std::int64_t floor_div(std::int64_t numerator, std::int64_t denominator) {
    if (denominator <= 0) {
        throw std::invalid_argument("floor_div denominator must be positive");
    }
    const std::int64_t quotient = numerator / denominator;
    const std::int64_t remainder = numerator % denominator;
    return quotient - ((remainder < 0) ? 1 : 0);
}

[[nodiscard]] std::int64_t ceil_div(std::int64_t numerator, std::int64_t denominator) {
    if (denominator <= 0) {
        throw std::invalid_argument("ceil_div denominator must be positive");
    }
    const std::int64_t quotient = numerator / denominator;
    const std::int64_t remainder = numerator % denominator;
    return quotient + ((remainder > 0) ? 1 : 0);
}

[[nodiscard]] std::uint32_t interpolate_depth(
    std::int64_t w0,
    std::int64_t w1,
    std::int64_t w2,
    std::int64_t area,
    const std::array<ScreenVertex, 3U>& vertices) {
    const auto numerator = static_cast<WideInteger>(w0) * vertices[0].depth
        + static_cast<WideInteger>(w1) * vertices[1].depth
        + static_cast<WideInteger>(w2) * vertices[2].depth;
    const auto value = numerator / static_cast<WideInteger>(area);
    if (value < 0 || value >= static_cast<WideInteger>(kVisibilityClearDepth)) {
        throw std::overflow_error("visibility depth outside representable range");
    }
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint16_t quantize_weight(std::int64_t weight, std::int64_t area) {
    const auto numerator = static_cast<WideInteger>(weight)
        * static_cast<WideInteger>(kAttributeScale);
    const auto value = numerator / static_cast<WideInteger>(area);
    if (value < 0 || value > static_cast<WideInteger>(kAttributeScale)) {
        throw std::overflow_error("visibility barycentric outside representable range");
    }
    return static_cast<std::uint16_t>(value);
}

[[nodiscard]] Rgba8 interpolate_color(
    const std::array<ScreenVertex, 3U>& vertices,
    std::uint16_t attribute_u,
    std::uint16_t attribute_v) noexcept {
    const std::uint32_t u = attribute_u;
    const std::uint32_t v = attribute_v;
    const std::uint32_t w = static_cast<std::uint32_t>(kAttributeScale) - u - v;

    const auto channel = [w, u, v](std::uint8_t c0, std::uint8_t c1, std::uint8_t c2) {
        const std::uint64_t numerator = static_cast<std::uint64_t>(w) * c0
            + static_cast<std::uint64_t>(u) * c1
            + static_cast<std::uint64_t>(v) * c2
            + static_cast<std::uint64_t>(kAttributeScale / 2U);
        return static_cast<std::uint8_t>(numerator / kAttributeScale);
    };

    return Rgba8{
        .r = channel(vertices[0].color.r, vertices[1].color.r, vertices[2].color.r),
        .g = channel(vertices[0].color.g, vertices[1].color.g, vertices[2].color.g),
        .b = channel(vertices[0].color.b, vertices[1].color.b, vertices[2].color.b),
        .a = channel(vertices[0].color.a, vertices[1].color.a, vertices[2].color.a),
    };
}

[[nodiscard]] bool candidate_wins(
    const VisibilitySample& current,
    std::uint32_t depth,
    std::uint64_t semantic,
    std::uint16_t attribute_u,
    std::uint16_t attribute_v) noexcept {
    if (!current.valid()) {
        return true;
    }
    return std::tie(depth, semantic, attribute_u, attribute_v)
        < std::tie(current.depth, current.semantic, current.attribute_u, current.attribute_v);
}

void append_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

template <typename T>
void append_little_endian(std::uint64_t& hash, T value) noexcept {
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(U); ++index) {
        append_byte(hash, static_cast<std::uint8_t>(bits & U{0xFF}));
        bits >>= 8U;
    }
}

[[nodiscard]] const MeshTriangle* find_triangle(
    std::span<const MeshTriangle> triangles,
    std::uint64_t semantic) {
    for (const MeshTriangle& triangle : triangles) {
        if (pack_visibility_semantic(triangle.semantic) == semantic) {
            return &triangle;
        }
    }
    return nullptr;
}

} // namespace

std::uint64_t pack_visibility_semantic(const VisibilitySemantic& semantic) {
    if (semantic.entity_id > kVisibilityMaxEntityId) {
        throw std::out_of_range("visibility entity id exceeds 24-bit contract");
    }
    if (semantic.primitive_id > kVisibilityMaxPrimitiveId) {
        throw std::out_of_range("visibility primitive id exceeds 24-bit contract");
    }
    if (semantic.material_id > kVisibilityMaxMaterialId) {
        throw std::out_of_range("visibility material id exceeds 12-bit contract");
    }
    const auto representation = static_cast<std::uint8_t>(semantic.representation);
    if (representation > static_cast<std::uint8_t>(RepresentationKind::Reserved)) {
        throw std::out_of_range("visibility representation exceeds 2-bit contract");
    }
    if (semantic.flags > kVisibilityMaxFlags) {
        throw std::out_of_range("visibility flags exceed 2-bit contract");
    }

    return static_cast<std::uint64_t>(semantic.entity_id)
        | (static_cast<std::uint64_t>(semantic.primitive_id) << 24U)
        | (static_cast<std::uint64_t>(semantic.material_id) << 48U)
        | (static_cast<std::uint64_t>(representation) << 60U)
        | (static_cast<std::uint64_t>(semantic.flags) << 62U);
}

VisibilitySemantic unpack_visibility_semantic(std::uint64_t packed) noexcept {
    return VisibilitySemantic{
        .entity_id = static_cast<std::uint32_t>(packed & kEntityMask),
        .primitive_id = static_cast<std::uint32_t>((packed >> 24U) & kPrimitiveMask),
        .material_id = static_cast<std::uint16_t>((packed >> 48U) & kMaterialMask),
        .representation = static_cast<RepresentationKind>((packed >> 60U) & 0x3U),
        .flags = static_cast<std::uint8_t>((packed >> 62U) & 0x3U),
    };
}

const VisibilitySample& VisibilityBuffer::at(std::size_t x, std::size_t y) const {
    if (x >= width || y >= height) {
        throw std::out_of_range("visibility buffer coordinate outside extent");
    }
    return samples[y * width + x];
}

VisibilitySample& VisibilityBuffer::at(std::size_t x, std::size_t y) {
    if (x >= width || y >= height) {
        throw std::out_of_range("visibility buffer coordinate outside extent");
    }
    return samples[y * width + x];
}

const Rgba8& ColorBuffer::at(std::size_t x, std::size_t y) const {
    if (x >= width || y >= height) {
        throw std::out_of_range("color buffer coordinate outside extent");
    }
    return pixels[y * width + x];
}

Rgba8& ColorBuffer::at(std::size_t x, std::size_t y) {
    if (x >= width || y >= height) {
        throw std::out_of_range("color buffer coordinate outside extent");
    }
    return pixels[y * width + x];
}

VisibilityReferenceResult render_visibility_reference(
    std::size_t width,
    std::size_t height,
    std::span<const MeshTriangle> triangles) {
    if (width == 0U || height == 0U) {
        throw std::invalid_argument("visibility framebuffer extent must be non-zero");
    }
    if (width > static_cast<std::size_t>(kVisibilityMaxPixelCoordinate)
        || height > static_cast<std::size_t>(kVisibilityMaxPixelCoordinate)) {
        throw std::out_of_range("visibility framebuffer extent exceeds 24.8 reference limits");
    }
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::overflow_error("visibility framebuffer size overflows size_t");
    }

    for (std::size_t first = 0; first < triangles.size(); ++first) {
        if (triangles[first].semantic.representation != RepresentationKind::Mesh) {
            throw std::invalid_argument("mesh visibility reference received a non-mesh primitive");
        }
        const std::uint64_t first_semantic = pack_visibility_semantic(triangles[first].semantic);
        for (std::size_t second = first + 1U; second < triangles.size(); ++second) {
            if (first_semantic == pack_visibility_semantic(triangles[second].semantic)) {
                throw std::invalid_argument("visibility semantics must uniquely identify a primitive");
            }
        }
    }

    VisibilityReferenceResult result{};
    result.visibility.width = width;
    result.visibility.height = height;
    result.visibility.samples.assign(width * height, VisibilitySample{});
    result.conventional_color.width = width;
    result.conventional_color.height = height;
    result.conventional_color.pixels.assign(width * height, Rgba8{});
    result.deferred_color.width = width;
    result.deferred_color.height = height;
    result.deferred_color.pixels.assign(width * height, Rgba8{});
    result.submitted_triangles = triangles.size();

    std::vector<BaselineSample> baseline(width * height);

    for (const MeshTriangle& source_triangle : triangles) {
        const MeshTriangle& triangle = source_triangle;
        const std::uint64_t packed_semantic = pack_visibility_semantic(triangle.semantic);
        const Point p0{triangle.vertices[0].x_subpixel, triangle.vertices[0].y_subpixel};
        const Point p1{triangle.vertices[1].x_subpixel, triangle.vertices[1].y_subpixel};
        const Point p2{triangle.vertices[2].x_subpixel, triangle.vertices[2].y_subpixel};
        const std::int64_t signed_area = edge(p0, p1, p2);
        if (signed_area == 0) {
            ++result.degenerate_triangles;
            continue;
        }
        const std::int64_t orientation = signed_area < 0 ? -1 : 1;
        const std::int64_t area = signed_area * orientation;

        const std::int64_t minimum_x = std::min({p0.x, p1.x, p2.x});
        const std::int64_t maximum_x = std::max({p0.x, p1.x, p2.x});
        const std::int64_t minimum_y = std::min({p0.y, p1.y, p2.y});
        const std::int64_t maximum_y = std::max({p0.y, p1.y, p2.y});
        constexpr std::int64_t half = kVisibilitySubpixelScale / 2;
        const std::int64_t x0 = std::max<std::int64_t>(0, ceil_div(minimum_x - half, kVisibilitySubpixelScale));
        const std::int64_t y0 = std::max<std::int64_t>(0, ceil_div(minimum_y - half, kVisibilitySubpixelScale));
        const std::int64_t x1 = std::min<std::int64_t>(
            static_cast<std::int64_t>(width) - 1,
            floor_div(maximum_x - half, kVisibilitySubpixelScale));
        const std::int64_t y1 = std::min<std::int64_t>(
            static_cast<std::int64_t>(height) - 1,
            floor_div(maximum_y - half, kVisibilitySubpixelScale));

        if (x0 > x1 || y0 > y1) {
            continue;
        }

        for (std::int64_t y = y0; y <= y1; ++y) {
            for (std::int64_t x = x0; x <= x1; ++x) {
                const Point sample{
                    x * kVisibilitySubpixelScale + half,
                    y * kVisibilitySubpixelScale + half,
                };
                const std::int64_t w0 = edge(p1, p2, sample) * orientation;
                const std::int64_t w1 = edge(p2, p0, sample) * orientation;
                const std::int64_t w2 = edge(p0, p1, sample) * orientation;
                if (!edge_accepts(w0, p1, p2, orientation)
                    || !edge_accepts(w1, p2, p0, orientation)
                    || !edge_accepts(w2, p0, p1, orientation)) {
                    continue;
                }

                const std::uint32_t depth = interpolate_depth(w0, w1, w2, area, triangle.vertices);
                const std::uint16_t attribute_u = quantize_weight(w1, area);
                const std::uint16_t attribute_v = quantize_weight(w2, area);
                const std::size_t index = static_cast<std::size_t>(y) * width
                    + static_cast<std::size_t>(x);
                VisibilitySample& current = result.visibility.samples[index];
                const bool equal_depth = current.valid() && current.depth == depth;
                if (equal_depth) {
                    ++result.deterministic_tie_breaks;
                }
                if (!candidate_wins(current, depth, packed_semantic, attribute_u, attribute_v)) {
                    ++result.depth_rejections;
                    continue;
                }

                current = VisibilitySample{
                    .semantic = packed_semantic,
                    .depth = depth,
                    .attribute_u = attribute_u,
                    .attribute_v = attribute_v,
                };
                baseline[index] = BaselineSample{
                    .color = interpolate_color(triangle.vertices, attribute_u, attribute_v),
                };
            }
        }
    }

    for (std::size_t index = 0; index < result.visibility.samples.size(); ++index) {
        const VisibilitySample& sample = result.visibility.samples[index];
        if (!sample.valid()) {
            continue;
        }
        ++result.covered_samples;
        const MeshTriangle* triangle = find_triangle(triangles, sample.semantic);
        if (triangle == nullptr) {
            ++result.unresolved_semantics;
            continue;
        }
        result.deferred_color.pixels[index] = interpolate_color(
            triangle->vertices, sample.attribute_u, sample.attribute_v);
        result.conventional_color.pixels[index] = baseline[index].color;
        if (result.deferred_color.pixels[index] != result.conventional_color.pixels[index]) {
            ++result.color_mismatches;
        }
    }

    result.visibility_hash = hash_visibility_buffer(result.visibility);
    result.deferred_hash = hash_color_buffer(result.deferred_color);
    result.conventional_hash = hash_color_buffer(result.conventional_color);
    return result;
}

std::uint64_t hash_visibility_buffer(const VisibilityBuffer& buffer) noexcept {
    std::uint64_t hash = kFnvOffset;
    append_little_endian(hash, static_cast<std::uint64_t>(buffer.width));
    append_little_endian(hash, static_cast<std::uint64_t>(buffer.height));
    for (const VisibilitySample& sample : buffer.samples) {
        append_little_endian(hash, sample.semantic);
        append_little_endian(hash, sample.depth);
        append_little_endian(hash, sample.attribute_u);
        append_little_endian(hash, sample.attribute_v);
    }
    return hash;
}

std::uint64_t hash_color_buffer(const ColorBuffer& buffer) noexcept {
    std::uint64_t hash = kFnvOffset;
    append_little_endian(hash, static_cast<std::uint64_t>(buffer.width));
    append_little_endian(hash, static_cast<std::uint64_t>(buffer.height));
    for (const Rgba8& pixel : buffer.pixels) {
        append_byte(hash, pixel.r);
        append_byte(hash, pixel.g);
        append_byte(hash, pixel.b);
        append_byte(hash, pixel.a);
    }
    return hash;
}

} // namespace neoeng::render
