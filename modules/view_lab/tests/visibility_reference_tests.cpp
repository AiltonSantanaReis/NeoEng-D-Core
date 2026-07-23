#include "neoeng/render/visibility_buffer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

using namespace neoeng::render;

namespace {

int failures = 0;

void check(bool condition, std::string_view expression, std::string_view test) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL [" << test << "]: " << expression << '\n';
    }
}

#define CHECK(test_name, expression) check((expression), #expression, (test_name))

ScreenVertex vertex(std::int32_t x, std::int32_t y, std::uint32_t depth, Rgba8 color) {
    return ScreenVertex{
        .x_subpixel = subpixel_from_pixel(x),
        .y_subpixel = subpixel_from_pixel(y),
        .depth = depth,
        .color = color,
    };
}

MeshTriangle triangle(
    std::uint32_t entity,
    std::uint32_t primitive,
    std::uint16_t material,
    ScreenVertex a,
    ScreenVertex b,
    ScreenVertex c) {
    return MeshTriangle{
        .vertices = {a, b, c},
        .semantic = VisibilitySemantic{
            .entity_id = entity,
            .primitive_id = primitive,
            .material_id = material,
            .representation = RepresentationKind::Mesh,
            .flags = 0U,
        },
    };
}

void test_semantic_round_trip() {
    constexpr std::string_view name = "semantic_round_trip";
    const std::array<VisibilitySemantic, 4U> values{
        VisibilitySemantic{},
        VisibilitySemantic{
            .entity_id = 1U,
            .primitive_id = 2U,
            .material_id = 3U,
            .representation = RepresentationKind::Mesh,
            .flags = 1U,
        },
        VisibilitySemantic{
            .entity_id = kVisibilityMaxEntityId,
            .primitive_id = kVisibilityMaxPrimitiveId,
            .material_id = kVisibilityMaxMaterialId,
            .representation = RepresentationKind::Voxel,
            .flags = kVisibilityMaxFlags,
        },
        VisibilitySemantic{
            .entity_id = 0x00ABCDU,
            .primitive_id = 0x005432U,
            .material_id = 0x0A5U,
            .representation = RepresentationKind::SignedDistanceField,
            .flags = 2U,
        },
    };
    for (const VisibilitySemantic& value : values) {
        CHECK(name, unpack_visibility_semantic(pack_visibility_semantic(value)) == value);
    }

    bool entity_threw = false;
    try {
        static_cast<void>(pack_visibility_semantic(VisibilitySemantic{
            .entity_id = kVisibilityMaxEntityId + 1U,
        }));
    } catch (const std::out_of_range&) {
        entity_threw = true;
    }
    CHECK(name, entity_threw);

    bool material_threw = false;
    try {
        static_cast<void>(pack_visibility_semantic(VisibilitySemantic{
            .material_id = static_cast<std::uint16_t>(kVisibilityMaxMaterialId + 1U),
        }));
    } catch (const std::out_of_range&) {
        material_threw = true;
    }
    CHECK(name, material_threw);
}

void test_quad_has_no_holes_and_matches_baseline() {
    constexpr std::string_view name = "quad_no_holes_and_baseline";
    const Rgba8 red{255U, 0U, 0U, 255U};
    const Rgba8 green{0U, 255U, 0U, 255U};
    const Rgba8 blue{0U, 0U, 255U, 255U};
    const Rgba8 white{255U, 255U, 255U, 255U};
    const std::array<MeshTriangle, 2U> triangles{
        triangle(1U, 1U, 1U,
            vertex(0, 0, 100U, red),
            vertex(16, 0, 100U, green),
            vertex(16, 16, 100U, blue)),
        triangle(1U, 2U, 1U,
            vertex(0, 0, 100U, red),
            vertex(16, 16, 100U, blue),
            vertex(0, 16, 100U, white)),
    };
    const VisibilityReferenceResult result = render_visibility_reference(16U, 16U, triangles);
    CHECK(name, result.covered_samples == 16U * 16U);
    CHECK(name, result.color_mismatches == 0U);
    CHECK(name, result.unresolved_semantics == 0U);
    CHECK(name, result.deferred_hash == result.conventional_hash);
}

void test_submission_order_is_deterministic() {
    constexpr std::string_view name = "submission_order_deterministic";
    const Rgba8 red{255U, 0U, 0U, 255U};
    const Rgba8 blue{0U, 0U, 255U, 255U};
    std::vector<MeshTriangle> forward{
        triangle(9U, 8U, 1U,
            vertex(1, 1, 200U, red),
            vertex(15, 1, 200U, red),
            vertex(8, 15, 200U, red)),
        triangle(3U, 2U, 1U,
            vertex(1, 1, 200U, blue),
            vertex(15, 1, 200U, blue),
            vertex(8, 15, 200U, blue)),
    };
    std::vector<MeshTriangle> reverse{forward.rbegin(), forward.rend()};
    const VisibilityReferenceResult a = render_visibility_reference(16U, 16U, forward);
    const VisibilityReferenceResult b = render_visibility_reference(16U, 16U, reverse);
    CHECK(name, a.visibility_hash == b.visibility_hash);
    CHECK(name, a.deferred_hash == b.deferred_hash);
    CHECK(name, a.conventional_hash == b.conventional_hash);
    CHECK(name, a.deterministic_tie_breaks > 0U || b.deterministic_tie_breaks > 0U);
    const VisibilitySemantic winner = unpack_visibility_semantic(a.visibility.at(8U, 8U).semantic);
    CHECK(name, winner.entity_id == 3U);
    CHECK(name, winner.primitive_id == 2U);
}

void test_nearer_geometry_wins() {
    constexpr std::string_view name = "nearer_geometry_wins";
    const Rgba8 near_color{20U, 220U, 50U, 255U};
    const Rgba8 far_color{220U, 20U, 50U, 255U};
    const std::array<MeshTriangle, 2U> triangles{
        triangle(1U, 1U, 1U,
            vertex(0, 0, 900U, far_color),
            vertex(16, 0, 900U, far_color),
            vertex(0, 16, 900U, far_color)),
        triangle(2U, 1U, 2U,
            vertex(0, 0, 100U, near_color),
            vertex(16, 0, 100U, near_color),
            vertex(0, 16, 100U, near_color)),
    };
    const VisibilityReferenceResult result = render_visibility_reference(16U, 16U, triangles);
    const VisibilitySemantic winner = unpack_visibility_semantic(result.visibility.at(2U, 2U).semantic);
    CHECK(name, winner.entity_id == 2U);
    CHECK(name, result.deferred_color.at(2U, 2U) == near_color);
}

void test_clockwise_triangle_preserves_attributes() {
    constexpr std::string_view name = "clockwise_triangle_preserves_attributes";
    const Rgba8 red{255U, 0U, 0U, 255U};
    const Rgba8 green{0U, 255U, 0U, 255U};
    const Rgba8 blue{0U, 0U, 255U, 255U};
    const std::array<MeshTriangle, 1U> triangles{
        triangle(7U, 4U, 2U,
            vertex(0, 0, 100U, red),
            vertex(0, 16, 100U, blue),
            vertex(16, 0, 100U, green)),
    };
    const VisibilityReferenceResult result = render_visibility_reference(16U, 16U, triangles);
    CHECK(name, result.covered_samples > 0U);
    CHECK(name, result.color_mismatches == 0U);
    CHECK(name, result.deferred_hash == result.conventional_hash);
}

void test_duplicate_semantics_are_rejected() {
    constexpr std::string_view name = "duplicate_semantics_rejected";
    const Rgba8 color{4U, 5U, 6U, 255U};
    const MeshTriangle first = triangle(1U, 1U, 1U,
        vertex(0, 0, 100U, color), vertex(4, 0, 100U, color), vertex(0, 4, 100U, color));
    const std::array<MeshTriangle, 2U> triangles{first, first};
    bool threw = false;
    try {
        static_cast<void>(render_visibility_reference(4U, 4U, triangles));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(name, threw);
}

void test_non_mesh_primitive_is_rejected_by_mesh_reference() {
    constexpr std::string_view name = "non_mesh_rejected";
    const Rgba8 color{7U, 8U, 9U, 255U};
    MeshTriangle item = triangle(1U, 1U, 1U,
        vertex(0, 0, 100U, color), vertex(4, 0, 100U, color), vertex(0, 4, 100U, color));
    item.semantic.representation = RepresentationKind::SignedDistanceField;
    const std::array<MeshTriangle, 1U> triangles{item};
    bool threw = false;
    try {
        static_cast<void>(render_visibility_reference(4U, 4U, triangles));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(name, threw);
}

void test_degenerate_triangle_is_rejected() {
    constexpr std::string_view name = "degenerate_triangle_rejected";
    const Rgba8 color{1U, 2U, 3U, 255U};
    const std::array<MeshTriangle, 1U> triangles{
        triangle(1U, 1U, 1U,
            vertex(1, 1, 100U, color),
            vertex(2, 2, 100U, color),
            vertex(3, 3, 100U, color)),
    };
    const VisibilityReferenceResult result = render_visibility_reference(8U, 8U, triangles);
    CHECK(name, result.degenerate_triangles == 1U);
    CHECK(name, result.covered_samples == 0U);
}

} // namespace

int main() {
    try {
        test_semantic_round_trip();
        test_quad_has_no_holes_and_matches_baseline();
        test_submission_order_is_deterministic();
        test_nearer_geometry_wins();
        test_clockwise_triangle_preserves_attributes();
        test_duplicate_semantics_are_rejected();
        test_non_mesh_primitive_is_rejected_by_mesh_reference();
        test_degenerate_triangle_is_rejected();
    } catch (const std::exception& exception) {
        std::cerr << "Unexpected exception: " << exception.what() << '\n';
        return 2;
    }

    if (failures != 0) {
        std::cerr << failures << " visibility test assertion(s) failed\n";
        return 1;
    }
    std::cout << "NeoEng D-Core View Lab imported visibility reference tests passed\n";
    return 0;
}
