#include "neoeng/core/visual_correlation.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace neoeng::core {
namespace {

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

} // namespace

const char* to_string(VisualProducerKind producer) noexcept {
    switch (producer) {
    case VisualProducerKind::CpuReference: return "cpu_reference";
    case VisualProducerKind::GpuInstrumented: return "gpu_instrumented";
    }
    return "unknown";
}

std::string visual_correlation_json(
    std::span<const VisualCorrelationRecord> records,
    std::string_view environment_id) {
    if (environment_id.empty()) {
        throw std::invalid_argument("visual correlation environment id cannot be empty");
    }
    std::ostringstream stream;
    stream << "{\n  \"schema\": \"" << kVisualCorrelationSchema << "\",\n"
           << "  \"environment_id\": ";
    append_json_string(stream, environment_id);
    stream << ",\n  \"records\": [";
    for (std::size_t index = 0U; index < records.size(); ++index) {
        const VisualCorrelationRecord& record = records[index];
        if (!record.valid()) {
            throw std::invalid_argument("invalid visual correlation record");
        }
        stream << (index == 0U ? "\n" : ",\n")
               << "    {\"core_frame\":" << record.core_frame
               << ",\"render_frame\":" << record.render_frame
               << ",\"state_hash\":" << record.state_hash
               << ",\"visibility_hash\":" << record.visibility_hash
               << ",\"color_hash\":" << record.color_hash
               // Correlation IDs are uint64 values and must not be coerced to
               // an imprecise JavaScript Number by browser consumers.
               << ",\"correlation_id\":\"" << record.correlation_id << '"'
               << ",\"producer\":\"" << to_string(record.producer) << "\""
               << ",\"cpu_begin_ns\":" << record.cpu_begin_ns
               << ",\"cpu_end_ns\":" << record.cpu_end_ns;
        if (record.gpu.has_value()) {
            stream << ",\"gpu\":{\"submission_id\":" << record.gpu->submission_id
                   << ",\"gpu_begin_ns\":" << record.gpu->gpu_begin_ns
                   << ",\"gpu_end_ns\":" << record.gpu->gpu_end_ns
                   << ",\"host_sample_begin_ns\":" << record.gpu->host_sample_begin_ns
                   << ",\"host_sample_end_ns\":" << record.gpu->host_sample_end_ns
                   << '}';
        }
        stream << '}';
    }
    if (!records.empty()) {
        stream << '\n';
    }
    stream << "  ]\n}\n";
    return stream.str();
}

} // namespace neoeng::core
