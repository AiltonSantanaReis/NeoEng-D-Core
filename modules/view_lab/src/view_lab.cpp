#include "neoeng/view_lab/view_lab.hpp"

#include "neoeng/core/hash.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace neoeng::view_lab {
namespace {

using neoeng::core::Body;
using neoeng::core::FrameRecord;
using neoeng::render::MeshTriangle;
using neoeng::render::RepresentationKind;
using neoeng::render::Rgba8;
using neoeng::render::ScreenVertex;
using neoeng::render::VisibilitySemantic;

[[nodiscard]] std::uint8_t channel(std::uint32_t entity, std::uint32_t salt) noexcept {
    std::uint32_t value = entity * 0x9E3779B9U + salt;
    value ^= value >> 16U;
    return static_cast<std::uint8_t>(64U + (value % 192U));
}

[[nodiscard]] Rgba8 entity_color(std::uint32_t entity) noexcept {
    return {
        channel(entity, 0xA341316CU),
        channel(entity, 0xC8013EA4U),
        channel(entity, 0xAD90777DU),
        255U,
    };
}

[[nodiscard]] std::int32_t checked_pixel(double value) {
    if (!std::isfinite(value)
        || value < static_cast<double>(neoeng::render::kVisibilityMinPixelCoordinate)
        || value > static_cast<double>(neoeng::render::kVisibilityMaxPixelCoordinate)) {
        throw std::out_of_range("view-lab pixel coordinate outside validated range");
    }
    return static_cast<std::int32_t>(std::llround(value));
}

[[nodiscard]] ScreenVertex vertex(
    std::int32_t x,
    std::int32_t y,
    std::uint32_t depth,
    Rgba8 color) {
    return {
        .x_subpixel = neoeng::render::subpixel_from_pixel(x),
        .y_subpixel = neoeng::render::subpixel_from_pixel(y),
        .depth = depth,
        .color = color,
    };
}

void append_body_triangles(
    std::vector<MeshTriangle>& output,
    const Body& body,
    const ViewLabConfig& config) {
    if (body.id > neoeng::render::kVisibilityMaxEntityId) {
        throw std::out_of_range("entity id exceeds visibility semantic range");
    }
    const double center_x = static_cast<double>(config.width) / 2.0
        + body.position.x.to_double() * config.pixels_per_world_unit;
    const double center_y = static_cast<double>(config.height) / 2.0
        - body.position.y.to_double() * config.pixels_per_world_unit;
    const std::int32_t x = checked_pixel(center_x);
    const std::int32_t y = checked_pixel(center_y);
    const std::int32_t half = config.body_half_extent_pixels;
    const Rgba8 color = entity_color(body.id);
    const std::uint32_t depth = 1'000U + (body.id % 10'000U);
    const std::uint16_t material = static_cast<std::uint16_t>(
        body.id % (neoeng::render::kVisibilityMaxMaterialId + 1U));

    const ScreenVertex top_left = vertex(x - half, y - half, depth, color);
    const ScreenVertex top_right = vertex(x + half, y - half, depth, color);
    const ScreenVertex bottom_right = vertex(x + half, y + half, depth, color);
    const ScreenVertex bottom_left = vertex(x - half, y + half, depth, color);

    const auto semantic = [body, material](std::uint32_t primitive) {
        return VisibilitySemantic{
            .entity_id = body.id,
            .primitive_id = primitive,
            .material_id = material,
            .representation = RepresentationKind::Mesh,
            .flags = 0U,
        };
    };
    output.push_back({
        .vertices = {top_left, top_right, bottom_right},
        .semantic = semantic(0U),
    });
    output.push_back({
        .vertices = {top_left, bottom_right, bottom_left},
        .semantic = semantic(1U),
    });
}

void write_u16(std::ostream& stream, std::uint16_t value) {
    const std::array<char, 2U> bytes{
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
    };
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_u32(std::ostream& stream, std::uint32_t value) {
    const std::array<char, 4U> bytes{
        static_cast<char>(value & 0xFFU),
        static_cast<char>((value >> 8U) & 0xFFU),
        static_cast<char>((value >> 16U) & 0xFFU),
        static_cast<char>((value >> 24U) & 0xFFU),
    };
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

[[nodiscard]] std::size_t utf8_sequence_length(std::string_view value, std::size_t index) noexcept {
    const auto byte = [&value](std::size_t offset) {
        return static_cast<unsigned char>(value[offset]);
    };
    const unsigned char lead = byte(index);
    std::size_t length{};
    if (lead < 0x80U) return 1U;
    if (lead >= 0xC2U && lead <= 0xDFU) length = 2U;
    else if (lead >= 0xE0U && lead <= 0xEFU) length = 3U;
    else if (lead >= 0xF0U && lead <= 0xF4U) length = 4U;
    else return 0U;
    if (index + length > value.size()) return 0U;
    for (std::size_t offset = 1U; offset < length; ++offset) {
        if ((byte(index + offset) & 0xC0U) != 0x80U) return 0U;
    }
    const unsigned char second = byte(index + 1U);
    if ((length == 3U && ((lead == 0xE0U && second < 0xA0U)
                          || (lead == 0xEDU && second > 0x9FU)))
        || (length == 4U && ((lead == 0xF0U && second < 0x90U)
                             || (lead == 0xF4U && second > 0x8FU)))) return 0U;
    return length;
}

void append_json_string(std::ostringstream& stream, std::string_view value) {
    stream << '"';
    for (std::size_t index = 0U; index < value.size();) {
        const char character = value[index];
        switch (character) {
        case '\\': stream << "\\\\"; ++index; break;
        case '"': stream << "\\\""; ++index; break;
        case '<': stream << "\\u003c"; ++index; break;
        case '>': stream << "\\u003e"; ++index; break;
        case '&': stream << "\\u0026"; ++index; break;
        case '\n': stream << "\\n"; ++index; break;
        case '\r': stream << "\\r"; ++index; break;
        case '\t': stream << "\\t"; ++index; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(static_cast<unsigned char>(character))
                       << std::dec;
                ++index;
            } else if (static_cast<unsigned char>(character) < 0x80U) {
                stream << character;
                ++index;
            } else {
                const std::size_t length = utf8_sequence_length(value, index);
                if (length == 0U) {
                    stream << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<unsigned int>(static_cast<unsigned char>(character))
                           << std::dec;
                    ++index;
                } else {
                    stream.write(value.data() + index, static_cast<std::streamsize>(length));
                    index += length;
                }
            }
        }
    }
    stream << '"';
}

[[nodiscard]] std::string frame_filename(std::uint64_t frame, std::string_view extension) {
    std::ostringstream stream;
    stream << "frame-" << std::setw(8) << std::setfill('0') << frame << extension;
    return stream.str();
}

[[nodiscard]] bool is_generated_frame_filename(std::string_view filename) noexcept {
    constexpr std::string_view prefix = "frame-";
    constexpr std::string_view bmp_suffix = ".bmp";
    constexpr std::string_view ppm_suffix = ".ppm";
    const std::size_t suffix_size = filename.ends_with(bmp_suffix) ? bmp_suffix.size()
        : filename.ends_with(ppm_suffix) ? ppm_suffix.size() : 0U;
    if (suffix_size == 0U || filename.size() != prefix.size() + 8U + suffix_size
        || !filename.starts_with(prefix)) {
        return false;
    }
    for (std::size_t index = prefix.size(); index < prefix.size() + 8U; ++index) {
        if (filename[index] < '0' || filename[index] > '9') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string viewer_html(std::string_view data_json) {
    std::ostringstream stream;
    stream << R"HTML(<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>NeoEng D-Core View Lab</title>
<style>
:root{color-scheme:dark}body{font-family:system-ui,sans-serif;margin:0;background:#111;color:#eee}
header{padding:14px 18px;border-bottom:1px solid #333}main{display:grid;grid-template-columns:minmax(320px,2fr) minmax(280px,1fr);gap:16px;padding:16px}
.panel{background:#1a1a1a;border:1px solid #333;border-radius:8px;padding:12px}img{width:100%;image-rendering:pixelated;background:#000;border:1px solid #444}
input[type=range]{width:100%}table{width:100%;border-collapse:collapse;font-size:13px}th,td{text-align:left;padding:5px;border-bottom:1px solid #333;vertical-align:top}
code{font-size:12px;word-break:break-all}.status{display:flex;gap:18px;flex-wrap:wrap}.muted{color:#aaa}
@media(max-width:800px){main{grid-template-columns:1fr}}
</style>
</head>
<body>
<header><h1>NeoEng D-Core View Lab</h1><div class="muted">Viewer estático e determinístico; somente leitura do estado canônico.</div></header>
<main>
<section class="panel"><img id="frameImage" alt="frame visual"><input id="frameSlider" type="range" min="0" value="0" step="1"><div class="status"><span id="frameLabel"></span><span id="hashLabel"></span></div></section>
<section class="panel"><h2>Entidades</h2><table><thead><tr><th>ID</th><th>Posição</th><th>Velocidade</th></tr></thead><tbody id="entities"></tbody></table><h2>Trace</h2><table><thead><tr><th>Correlação</th><th>Categoria</th><th>Código</th><th>Resultado</th></tr></thead><tbody id="events"></tbody></table></section>
</main>
<script>
const data=)HTML" << data_json << R"HTML(;
const slider=document.getElementById('frameSlider');
slider.max=Math.max(0,data.frames.length-1);
function cell(value){const td=document.createElement('td');td.textContent=value;return td}
function show(index){const frame=data.frames[index];if(!frame)return;document.getElementById('frameImage').src=frame.image;document.getElementById('frameLabel').textContent=`Core frame ${frame.core_frame} / render frame ${frame.render_frame}`;document.getElementById('hashLabel').textContent=`state ${frame.state_hash_hex} · visual ${frame.color_hash_hex}`;const entities=document.getElementById('entities');entities.replaceChildren();for(const entity of frame.entities){const tr=document.createElement('tr');tr.append(cell(entity.id),cell(`${entity.position_x}, ${entity.position_y}`),cell(`${entity.velocity_x}, ${entity.velocity_y}`));entities.append(tr)}const events=document.getElementById('events');events.replaceChildren();for(const event of frame.events){const tr=document.createElement('tr');tr.append(cell(event.correlation_id),cell(event.category),cell(event.code),cell(event.outcome));events.append(tr)}}
slider.addEventListener('input',()=>show(Number(slider.value)));show(0);
</script>
</body></html>
)HTML";
    return stream.str();
}

} // namespace

ViewLabFrame render_frame(
    const FrameRecord& frame,
    std::uint64_t render_frame_number,
    const ViewLabConfig& config,
    neoeng::core::CorrelationId correlation_id) {
    if (config.width == 0U || config.height == 0U
        || config.pixels_per_world_unit <= 0
        || config.body_half_extent_pixels <= 0) {
        throw std::invalid_argument("invalid view-lab configuration");
    }
    std::vector<MeshTriangle> triangles;
    triangles.reserve(frame.state.bodies.size() * 2U);
    for (const Body& body : frame.state.bodies) {
        append_body_triangles(triangles, body, config);
    }
    neoeng::render::VisibilityReferenceResult reference =
        neoeng::render::render_visibility_reference(config.width, config.height, triangles);
    if (correlation_id == 0U) {
        correlation_id = 0x5649455700000000ULL ^ frame.state.frame;
    }
    neoeng::core::VisualCorrelationRecord correlation{
        .core_frame = frame.state.frame,
        .render_frame = render_frame_number,
        .state_hash = frame.state_hash,
        .visibility_hash = reference.visibility_hash,
        .color_hash = reference.deferred_hash,
        .correlation_id = correlation_id,
        .producer = neoeng::core::VisualProducerKind::CpuReference,
        .cpu_begin_ns = 0U,
        .cpu_end_ns = 0U,
    };
    return {
        .core_frame = frame.state.frame,
        .render_frame = render_frame_number,
        .correlation = correlation,
        .reference = std::move(reference),
    };
}

void write_bmp(
    const neoeng::render::ColorBuffer& buffer,
    const std::filesystem::path& path) {
    if (buffer.width == 0U || buffer.height == 0U
        || buffer.pixels.size() != buffer.width * buffer.height
        || buffer.width > std::numeric_limits<std::uint32_t>::max()
        || buffer.height > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("invalid color buffer for BMP export");
    }
    const std::size_t row_bytes = buffer.width * 3U;
    const std::size_t padded_row_bytes = (row_bytes + 3U) & ~std::size_t{3U};
    const std::size_t image_bytes = padded_row_bytes * buffer.height;
    if (image_bytes > std::numeric_limits<std::uint32_t>::max() - 54U) {
        throw std::overflow_error("BMP output exceeds format limits");
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot open BMP output");
    }
    stream.put('B');
    stream.put('M');
    write_u32(stream, static_cast<std::uint32_t>(54U + image_bytes));
    write_u16(stream, 0U);
    write_u16(stream, 0U);
    write_u32(stream, 54U);
    write_u32(stream, 40U);
    write_u32(stream, static_cast<std::uint32_t>(buffer.width));
    write_u32(stream, static_cast<std::uint32_t>(buffer.height));
    write_u16(stream, 1U);
    write_u16(stream, 24U);
    write_u32(stream, 0U);
    write_u32(stream, static_cast<std::uint32_t>(image_bytes));
    write_u32(stream, 2'835U);
    write_u32(stream, 2'835U);
    write_u32(stream, 0U);
    write_u32(stream, 0U);
    const std::array<char, 3U> zeroes{};
    for (std::size_t row = 0U; row < buffer.height; ++row) {
        const std::size_t y = buffer.height - 1U - row;
        for (std::size_t x = 0U; x < buffer.width; ++x) {
            const neoeng::render::Rgba8& pixel = buffer.at(x, y);
            const std::array<char, 3U> bgr{
                static_cast<char>(pixel.b),
                static_cast<char>(pixel.g),
                static_cast<char>(pixel.r),
            };
            stream.write(bgr.data(), static_cast<std::streamsize>(bgr.size()));
        }
        const std::size_t padding = padded_row_bytes - row_bytes;
        stream.write(zeroes.data(), static_cast<std::streamsize>(padding));
    }
    if (!stream) {
        throw std::runtime_error("failed while writing BMP output");
    }
}

void write_ppm(
    const neoeng::render::ColorBuffer& buffer,
    const std::filesystem::path& path) {
    if (buffer.width == 0U || buffer.height == 0U
        || buffer.pixels.size() != buffer.width * buffer.height) {
        throw std::invalid_argument("invalid color buffer for PPM export");
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("cannot open PPM output");
    }
    stream << "P6\n" << buffer.width << ' ' << buffer.height << "\n255\n";
    for (const neoeng::render::Rgba8& pixel : buffer.pixels) {
        const std::array<char, 3U> rgb{
            static_cast<char>(pixel.r),
            static_cast<char>(pixel.g),
            static_cast<char>(pixel.b),
        };
        stream.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
    }
    if (!stream) {
        throw std::runtime_error("failed while writing PPM output");
    }
}

ViewerExportResult export_static_viewer(
    const neoeng::core::TimeTravelDebugger& debugger,
    const std::filesystem::path& output_directory,
    std::string_view environment_id,
    const ViewLabConfig& config) {
    if (environment_id.empty()) {
        throw std::invalid_argument("view-lab environment id cannot be empty");
    }
    const auto oldest = debugger.oldest_frame();
    const auto newest = debugger.newest_frame();
    if (!oldest.has_value() || !newest.has_value()) {
        throw std::invalid_argument("time-travel debugger contains no frames");
    }
    std::filesystem::create_directories(output_directory);
    for (const std::filesystem::directory_entry& entry
         : std::filesystem::directory_iterator(output_directory)) {
        const std::string filename = entry.path().filename().string();
        const bool generated_frame = is_generated_frame_filename(filename);
        if (generated_frame && entry.is_regular_file()) {
            std::error_code error;
            if (!std::filesystem::remove(entry.path(), error) || error) {
                throw std::runtime_error("failed to remove stale viewer artifact");
            }
        }
    }

    std::vector<neoeng::core::VisualCorrelationRecord> correlations;
    std::ostringstream data;
    data << "{\"schema\":\"neoeng.dcore.view-lab.v1\",\"environment_id\":";
    append_json_string(data, environment_id);
    data << ",\"frames\":[";
    std::size_t count = 0U;
    const std::vector<std::uint64_t> retained_frames = debugger.retained_frame_numbers();
    for (const std::uint64_t core_frame : retained_frames) {
        const FrameRecord* record = debugger.frame(core_frame);
        if (record == nullptr) {
            continue;
        }
        ViewLabFrame visual = render_frame(*record, count, config);
        const std::string image = frame_filename(core_frame, ".bmp");
        write_bmp(visual.reference.deferred_color, output_directory / image);
        if (config.write_ppm_reference) {
            write_ppm(visual.reference.deferred_color,
                output_directory / frame_filename(core_frame, ".ppm"));
        }
        correlations.push_back(visual.correlation);
        data << (count == 0U ? "" : ",") << "{\"core_frame\":" << core_frame
             << ",\"render_frame\":" << visual.render_frame
             << ",\"image\":";
        append_json_string(data, image);
        std::ostringstream state_hash_hex;
        state_hash_hex << "0x" << std::hex << std::uppercase << visual.correlation.state_hash;
        std::ostringstream color_hash_hex;
        color_hash_hex << "0x" << std::hex << std::uppercase << visual.correlation.color_hash;
        data << ",\"state_hash\":" << visual.correlation.state_hash
             << ",\"state_hash_hex\":";
        append_json_string(data, state_hash_hex.str());
        data << ",\"color_hash\":" << visual.correlation.color_hash
             << ",\"color_hash_hex\":";
        append_json_string(data, color_hash_hex.str());
        data << ",\"entities\":[";
        for (std::size_t index = 0U; index < record->state.bodies.size(); ++index) {
            const Body& body = record->state.bodies[index];
            data << (index == 0U ? "" : ",") << "{\"id\":" << body.id
                 << ",\"position_x\":" << body.position.x.raw()
                 << ",\"position_y\":" << body.position.y.raw()
                 << ",\"velocity_x\":" << body.velocity.x.raw()
                 << ",\"velocity_y\":" << body.velocity.y.raw() << '}';
        }
        data << "],\"events\":[";
        for (std::size_t index = 0U; index < record->events.size(); ++index) {
            const neoeng::core::TraceEvent& event = record->events[index];
            data << (index == 0U ? "" : ",") << "{\"correlation_id\":\""
                 << event.correlation_id << "\",\"category\":\""
                 << neoeng::core::to_string(event.category) << "\",\"code\":\""
                 << neoeng::core::to_string(event.code) << "\",\"outcome\":\""
                 << neoeng::core::to_string(event.outcome) << "\"}";
        }
        data << "]}";
        ++count;
        if (core_frame == std::numeric_limits<std::uint64_t>::max()) {
            break;
        }
    }
    data << "]}";
    if (count == 0U) {
        throw std::runtime_error("time-travel range contains no materialized frames");
    }

    const std::filesystem::path correlation_path =
        output_directory / "visual-correlation.json";
    {
        std::ofstream stream(correlation_path, std::ios::binary | std::ios::trunc);
        stream << neoeng::core::visual_correlation_json(correlations, environment_id);
        if (!stream) {
            throw std::runtime_error("failed while writing visual correlation output");
        }
    }
    const std::filesystem::path viewer_path = output_directory / "index.html";
    {
        std::ofstream stream(viewer_path, std::ios::binary | std::ios::trunc);
        stream << viewer_html(data.str());
        if (!stream) {
            throw std::runtime_error("failed while writing static viewer");
        }
    }
    return {
        .viewer_path = viewer_path,
        .correlation_path = correlation_path,
        .frames_written = count,
        .first_frame = *oldest,
        .last_frame = *newest,
    };
}

} // namespace neoeng::view_lab
