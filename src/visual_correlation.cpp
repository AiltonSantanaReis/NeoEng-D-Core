#include "neoeng/core/visual_correlation.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace neoeng::core {
namespace {

void append_json_string(std::ostringstream& stream, std::string_view value) {
    stream << '"';
    for (const char character : value) {
        switch (character) {
        case '\\': stream << "\\\\"; break;
        case '"': stream << "\\\""; break;
        case '\n': stream << "\\n"; break;
        case '\r': stream << "\\r"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(static_cast<unsigned char>(character))
                       << std::dec;
            } else {
                stream << character;
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
               << ",\"correlation_id\":" << record.correlation_id
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
