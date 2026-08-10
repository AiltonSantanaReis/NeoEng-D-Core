#include "neoeng/core/support_bundle.hpp"

#include "neoeng/core/diagnostics.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace neoeng::core {
namespace {

[[nodiscard]] bool valid_utf8_sequence(
    std::string_view value,
    std::size_t index,
    std::size_t& length) noexcept {
    const auto lead = static_cast<unsigned char>(value[index]);
    if (lead < 0x80U) {
        length = 1U;
        return true;
    }
    std::uint32_t code_point{};
    if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2U;
        code_point = lead & 0x1FU;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3U;
        code_point = lead & 0x0FU;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4U;
        code_point = lead & 0x07U;
    } else {
        return false;
    }
    if (index + length > value.size()) return false;
    for (std::size_t offset = 1U; offset < length; ++offset) {
        const auto continuation = static_cast<unsigned char>(value[index + offset]);
        if ((continuation & 0xC0U) != 0x80U) return false;
        code_point = (code_point << 6U) | (continuation & 0x3FU);
    }
    return (length != 2U || code_point >= 0x80U)
        && (length != 3U || code_point >= 0x800U)
        && (length != 4U || code_point >= 0x10000U)
        && code_point <= 0x10FFFFU
        && !(code_point >= 0xD800U && code_point <= 0xDFFFU);
}

[[nodiscard]] bool valid_utf8(std::string_view value) noexcept {
    for (std::size_t index = 0U; index < value.size();) {
        std::size_t length{};
        if (!valid_utf8_sequence(value, index, length)) return false;
        index += length;
    }
    return true;
}

[[nodiscard]] char hex_digit(unsigned int value) noexcept {
    return value < 10U ? static_cast<char>('0' + value)
        : static_cast<char>('A' + value - 10U);
}

void append_json_uint64(std::ostringstream& stream, std::uint64_t value) {
    stream << '"' << value << '"';
}

void append_json_int64(std::ostringstream& stream, std::int64_t value) {
    stream << '"' << value << '"';
}

void append_json_string(std::ostringstream& stream, std::string_view value) {
    stream << '"';
    for (std::size_t index = 0U; index < value.size(); ++index) {
        const char character = value[index];
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
            } else if (static_cast<unsigned char>(character) >= 0x80U) {
                std::size_t length{};
                if (valid_utf8_sequence(value, index, length)) {
                    stream.write(value.data() + static_cast<std::ptrdiff_t>(index),
                        static_cast<std::streamsize>(length));
                    index += length - 1U;
                } else {
                    const auto byte = static_cast<unsigned int>(
                        static_cast<unsigned char>(character));
                    stream << "\\u00" << hex_digit((byte >> 4U) & 0x0FU)
                           << hex_digit(byte & 0x0FU);
                }
            } else {
                stream << character;
            }
        }
    }
    stream << '"';
}

[[nodiscard]] bool exceeds_limit(
    std::size_t total,
    std::size_t addition,
    std::size_t limit) noexcept {
    return addition > limit || total > limit - addition;
}

[[nodiscard]] Sha256Digest hash_text(std::string_view text) {
    return sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

[[nodiscard]] bool safe_relative_path(std::string_view path) noexcept {
    if (path.empty() || path.front() == '/' || path.find('\\') != std::string_view::npos
        || path.find('\0') != std::string_view::npos) {
        return false;
    }
    for (const char character : path) {
        const bool allowed = (character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9')
            || character == '/' || character == '.' || character == '_' || character == '-';
        if (!allowed) return false;
    }
    std::size_t begin = 0U;
    while (begin <= path.size()) {
        const std::size_t end = path.find('/', begin);
        const std::string_view segment = path.substr(
            begin, end == std::string_view::npos ? path.size() - begin : end - begin);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return true;
}

[[nodiscard]] std::string pseudonymize_subject(
    std::uint64_t subject,
    std::string_view salt) {
    if (subject == 0U) return "";
    std::vector<std::uint8_t> bytes(salt.begin(), salt.end());
    constexpr std::string_view domain{"NEOENG-DCORE-SUPPORT-SUBJECT-V1"};
    bytes.insert(bytes.end(), domain.begin(), domain.end());
    for (std::size_t index = 0U; index < sizeof(subject); ++index) {
        bytes.push_back(static_cast<std::uint8_t>(subject & 0xFFU));
        subject >>= 8U;
    }
    const Sha256Digest digest = sha256(bytes);
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (std::size_t index = 0U; index < 12U; ++index) {
        stream << std::setw(2) << static_cast<unsigned int>(digest[index]);
    }
    return stream.str();
}

[[nodiscard]] std::string traces_json(
    std::span<const TraceEvent> source,
    const SupportBundlePolicy& policy) {
    const std::size_t offset = source.size() > policy.maximum_trace_events
        ? source.size() - policy.maximum_trace_events : 0U;
    std::ostringstream stream;
    stream << "{\n  \"schema\": \"neoeng.dcore.trace-export.v2\",\n"
           << "  \"source_event_count\": " << source.size() << ",\n"
           << "  \"exported_event_count\": " << (source.size() - offset) << ",\n"
           << "  \"events\": [";
    for (std::size_t index = offset; index < source.size(); ++index) {
        const TraceEvent& event = source[index];
        stream << (index == offset ? "\n" : ",\n")
               << "    {\"correlation_id\":"; append_json_uint64(stream, event.correlation_id); stream
               << ",\"sequence\":"; append_json_uint64(stream, event.sequence); stream
               << ",\"frame\":"; append_json_uint64(stream, event.frame);
        if (policy.include_monotonic_timestamps) {
            stream << ",\"monotonic_time_ns\":"; append_json_uint64(stream, event.monotonic_time_ns);
        }
        stream << ",\"category\":\"" << to_string(event.category)
               << "\",\"subsystem\":\"" << to_string(event.subsystem)
               << "\",\"severity\":\"" << to_string(event.severity)
               << "\",\"outcome\":\"" << to_string(event.outcome)
               << "\",\"code\":\"" << to_string(event.code)
               << "\",\"entity\":" << event.entity
               << ",\"component\":" << event.component
               << ",\"measured_value\":"; append_json_int64(stream, event.measured_value); stream
               << ",\"budget_limit\":"; append_json_int64(stream, event.budget_limit); stream
               << ",\"related_hash\":"; append_json_uint64(stream, event.related_hash); stream
               << ",\"detail_code\":" << event.detail_code
               << ",\"subject_pseudonym\":"
               << '"' << pseudonymize_subject(event.subject_token, policy.pseudonymization_salt)
               << "\"}";
    }
    if (source.size() != offset) stream << '\n';
    stream << "  ]\n}\n";
    return stream.str();
}

[[nodiscard]] std::string metadata_json(const SupportBundleContext& context) {
    std::ostringstream stream;
    stream << "{\n  \"schema\": \"neoeng.dcore.support-metadata.v1\",\n"
           << "  \"project\": \"NeoEng D-Core\",\n  \"project_version\": ";
    append_json_string(stream, context.project_version);
    stream << ",\n  \"environment_id\": ";
    append_json_string(stream, context.environment_id);
    stream << ",\n  \"hardware_profile\": ";
    append_json_string(stream, context.hardware_profile);
    stream << ",\n  \"seed\": "; append_json_uint64(stream, context.seed); stream
           << ",\n  \"authority\": \"canonical-state-remains-in-dcore\"\n}\n";
    return stream.str();
}

[[nodiscard]] std::string redaction_json(const SupportBundlePolicy& policy) {
    std::ostringstream stream;
    stream << "{\n  \"schema\": \"neoeng.dcore.support-redaction.v1\",\n"
           << "  \"session_keys_included\": false,\n"
           << "  \"authentication_secrets_included\": false,\n"
           << "  \"private_signing_material_included\": false,\n"
           << "  \"raw_subject_ids_included\": false,\n"
           << "  \"subject_pseudonymization\": \"sha256-domain-separated-truncated-96-bit\",\n"
           << "  \"monotonic_timestamps_included\": "
           << (policy.include_monotonic_timestamps ? "true" : "false") << "\n}\n";
    return stream.str();
}

void add_entry(
    SupportBundleArtifact& bundle,
    std::string path,
    std::string content,
    const SupportBundlePolicy& policy,
    std::size_t& total) {
    if (!safe_relative_path(path)) {
        throw std::invalid_argument("support bundle path is unsafe");
    }
    if (!valid_utf8(content)) {
        throw std::invalid_argument("support bundle entry is not valid UTF-8");
    }
    if (exceeds_limit(total, content.size(), policy.maximum_total_bytes)
        || content.size() > policy.maximum_entry_bytes) {
        throw std::length_error("support bundle size limit exceeded");
    }
    total += content.size();
    bundle.entries.push_back({
        .path = std::move(path),
        .content = std::move(content),
    });
    bundle.entries.back().sha256 = hash_text(bundle.entries.back().content);
}

[[nodiscard]] std::string manifest_json(
    std::span<const SupportBundleEntry> entries,
    const SupportBundleContext* context = nullptr) {
    std::ostringstream stream;
    stream << "{\n  \"schema\": \"" << kSupportBundleSchema << "\",\n"
           << "  \"entry_count\": " << entries.size();
    if (context != nullptr) {
        stream << ",\n  \"project_version\": ";
        append_json_string(stream, context->project_version);
        stream << ",\n  \"environment_id\": ";
        append_json_string(stream, context->environment_id);
    }
    stream << ",\n  \"entries\": [";
    for (std::size_t index = 0U; index < entries.size(); ++index) {
        const SupportBundleEntry& entry = entries[index];
        stream << (index == 0U ? "\n" : ",\n") << "    {\"path\":";
        append_json_string(stream, entry.path);
        stream << ",\"size\":" << entry.content.size()
               << ",\"sha256\":\"" << sha256_hex(entry.sha256) << "\"}";
    }
    if (!entries.empty()) stream << '\n';
    stream << "  ]\n}\n";
    return stream.str();
}

[[nodiscard]] bool gate_valid(const DeferredValidationGate& gate) noexcept {
    const bool valid_category = [&]() noexcept {
        switch (gate.category) {
        case ValidationGateCategory::ImplementationGap:
        case ValidationGateCategory::NativeValidationPending:
        case ValidationGateCategory::ExternalAssurancePending:
        case ValidationGateCategory::FutureInfrastructure:
            return true;
        }
        return false;
    }();
    const bool valid_execution_status = [&]() noexcept {
        switch (gate.execution_status) {
        case ValidationExecutionStatus::NotExecuted:
        case ValidationExecutionStatus::Passed:
        case ValidationExecutionStatus::Failed:
        case ValidationExecutionStatus::NotApplicable:
            return true;
        }
        return false;
    }();
    return !gate.gate_id.empty() && !gate.target.empty() && !gate.reason.empty()
        && !gate.implementation_status.empty() && !gate.required_profile.empty()
        && valid_category && valid_execution_status;
}

struct ManifestTuple final {
    std::string path{};
    std::size_t size{};
    std::string sha256{};
};

class ManifestParser final {
public:
    explicit ManifestParser(std::string_view input) : input_(input) {}

    [[nodiscard]] bool parse(
        std::size_t expected_entry_count,
        std::vector<ManifestTuple>& tuples) {
        if (!parse_manifest_object(expected_entry_count, tuples)) return false;
        skip_whitespace();
        return position_ == input_.size();
    }

private:
    [[nodiscard]] bool parse_manifest_object(
        std::size_t expected_entry_count,
        std::vector<ManifestTuple>& tuples) {
        if (!consume('{')) return false;
        bool schema_seen{};
        bool count_seen{};
        bool project_version_seen{};
        bool environment_id_seen{};
        bool entries_seen{};
        std::size_t declared_count{};
        skip_whitespace();
        if (consume('}')) return false;
        while (true) {
            std::string key;
            if (!parse_string(key) || !consume(':')) return false;
            bool recognized{};
            if (key == "schema") {
                if (schema_seen) return false;
                schema_seen = true;
                recognized = parse_string(key) && key == kSupportBundleSchema;
            } else if (key == "entry_count") {
                if (count_seen) return false;
                count_seen = true;
                recognized = parse_unsigned(declared_count);
            } else if (key == "project_version") {
                if (project_version_seen) return false;
                project_version_seen = true;
                recognized = parse_string(key);
            } else if (key == "environment_id") {
                if (environment_id_seen) return false;
                recognized = parse_string(key);
                environment_id_seen = true;
            } else if (key == "entries") {
                if (entries_seen) return false;
                entries_seen = true;
                recognized = parse_entries(tuples);
            } else {
                return false;
            }
            if (!recognized) return false;
            skip_whitespace();
            if (consume('}')) break;
            if (!consume(',')) return false;
        }
        return schema_seen && count_seen && entries_seen
            && declared_count == expected_entry_count
            && tuples.size() == expected_entry_count;
    }

    [[nodiscard]] bool parse_entries(std::vector<ManifestTuple>& tuples) {
        if (!consume('[')) return false;
        skip_whitespace();
        if (consume(']')) return true;
        while (true) {
            if (!consume('{')) return false;
            ManifestTuple tuple;
            bool path_seen{};
            bool size_seen{};
            bool hash_seen{};
            skip_whitespace();
            if (consume('}')) return false;
            while (true) {
                std::string key;
                if (!parse_string(key) || !consume(':')) return false;
                if (key == "path") {
                    if (path_seen || !parse_string(tuple.path)) return false;
                    path_seen = true;
                } else if (key == "size") {
                    if (size_seen || !parse_unsigned(tuple.size)) return false;
                    size_seen = true;
                } else if (key == "sha256") {
                    if (hash_seen || !parse_string(tuple.sha256)) return false;
                    hash_seen = true;
                } else {
                    return false;
                }
                skip_whitespace();
                if (consume('}')) break;
                if (!consume(',')) return false;
            }
            if (!path_seen || !size_seen || !hash_seen || tuple.sha256.size() != 64U) {
                return false;
            }
            for (const char character : tuple.sha256) {
                const bool hex = (character >= '0' && character <= '9')
                    || (character >= 'a' && character <= 'f')
                    || (character >= 'A' && character <= 'F');
                if (!hex) return false;
            }
            tuples.push_back(std::move(tuple));
            skip_whitespace();
            if (consume(']')) break;
            if (!consume(',')) return false;
        }
        return true;
    }

    [[nodiscard]] bool parse_unsigned(std::size_t& result) {
        skip_whitespace();
        if (position_ == input_.size()
            || input_[position_] < '0' || input_[position_] > '9') {
            return false;
        }
        if (input_[position_] == '0') {
            ++position_;
            result = 0U;
            return position_ == input_.size()
                || (input_[position_] < '0' || input_[position_] > '9');
        }
        result = 0U;
        while (position_ < input_.size()
            && input_[position_] >= '0' && input_[position_] <= '9') {
            const std::size_t digit = static_cast<std::size_t>(input_[position_] - '0');
            if (result > (std::numeric_limits<std::size_t>::max() - digit) / 10U) {
                return false;
            }
            result = result * 10U + digit;
            ++position_;
        }
        return true;
    }

    [[nodiscard]] bool parse_string(std::string& result) {
        skip_whitespace();
        if (!consume('"')) return false;
        result.clear();
        while (position_ < input_.size()) {
            const unsigned char character = static_cast<unsigned char>(input_[position_++]);
            if (character == '"') return valid_utf8(result);
            if (character < 0x20U) return false;
            if (character != '\\') {
                result.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ == input_.size()) return false;
            const char escaped = input_[position_++];
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                unsigned int code_point{};
                for (std::size_t index = 0U; index < 4U; ++index) {
                    if (position_ == input_.size()) return false;
                    const char hex = input_[position_++];
                    unsigned int value{};
                    if (hex >= '0' && hex <= '9') value = static_cast<unsigned int>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') value = static_cast<unsigned int>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') value = static_cast<unsigned int>(hex - 'A' + 10);
                    else return false;
                    code_point = (code_point << 4U) | value;
                }
                if (code_point <= 0x7FU) {
                    result.push_back(static_cast<char>(code_point));
                } else if (code_point <= 0x7FFU) {
                    result.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
                    result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
                } else if (code_point >= 0xD800U && code_point <= 0xDFFFU) {
                    return false;
                } else {
                    result.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
                    result.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
                    result.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
                }
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    void skip_whitespace() noexcept {
        while (position_ < input_.size()) {
            const char character = input_[position_];
            if (character != ' ' && character != '\n' && character != '\r' && character != '\t') {
                break;
            }
            ++position_;
        }
    }

    [[nodiscard]] bool consume(char expected) noexcept {
        skip_whitespace();
        if (position_ >= input_.size() || input_[position_] != expected) return false;
        ++position_;
        return true;
    }

    std::string_view input_;
    std::size_t position_{};
};

} // namespace

std::string export_deferred_validation_gates_json(
    std::span<const DeferredValidationGate> gates) {
    std::ostringstream stream;
    stream << "{\n  \"schema\": \"" << kDeferredValidationSchema << "\",\n"
           << "  \"gates\": [";
    for (std::size_t index = 0U; index < gates.size(); ++index) {
        const DeferredValidationGate& gate = gates[index];
        if (!gate_valid(gate)) {
            throw std::invalid_argument("deferred validation gate is incomplete");
        }
        stream << (index == 0U ? "\n" : ",\n") << "    {\"gate_id\":";
        append_json_string(stream, gate.gate_id);
        stream << ",\"category\":\"" << to_string(gate.category) << "\",\"target\":";
        append_json_string(stream, gate.target);
        stream << ",\"reason\":";
        append_json_string(stream, gate.reason);
        stream << ",\"implementation_status\":";
        append_json_string(stream, gate.implementation_status);
        stream << ",\"execution_status\":\"" << to_string(gate.execution_status)
               << "\",\"required_profile\":";
        append_json_string(stream, gate.required_profile);
        stream << ",\"required_artifacts\":[";
        for (std::size_t artifact_index = 0U;
             artifact_index < gate.required_artifacts.size(); ++artifact_index) {
            if (artifact_index != 0U) stream << ',';
            append_json_string(stream, gate.required_artifacts[artifact_index]);
        }
        stream << "],\"blocking_for_current_changeset\":"
               << (gate.blocking_for_current_changeset ? "true" : "false")
               << ",\"blocking_for_profile_qualification\":"
               << (gate.blocking_for_profile_qualification ? "true" : "false") << '}';
    }
    if (!gates.empty()) stream << '\n';
    stream << "  ]\n}\n";
    return stream.str();
}

SupportBundleArtifact build_support_bundle(
    const SupportBundleContext& context,
    const SupportBundlePolicy& policy,
    TraceBuffer* audit_traces,
    CorrelationId correlation_id) {
    const BudgetMonitor budget_monitor;
    ScopedBudgetMeasurement budget_scope(
        audit_traces != nullptr ? &budget_monitor : nullptr,
        audit_traces,
        {
            .id = BudgetId::SupportBundleExport,
            .subsystem = TraceSubsystem::SupportBundle,
            .limit_ns = 0U,
            .exceed_severity = TraceSeverity::Warning,
        },
        correlation_id,
        0U);
    if (context.project_version.empty() || context.environment_id.empty()
        || context.hardware_profile.empty() || policy.maximum_trace_events == 0U
        || policy.maximum_entry_bytes == 0U || policy.maximum_total_bytes == 0U
        || policy.pseudonymization_salt.empty()) {
        throw std::invalid_argument("support bundle context or policy is incomplete");
    }
    SupportBundleArtifact bundle;
    std::size_t total{};
    add_entry(bundle, "metadata.json", metadata_json(context), policy, total);
    add_entry(bundle, "traces.json", traces_json(context.traces, policy), policy, total);
    add_entry(bundle, "evidence-chain.json",
        export_evidence_chain_json(context.evidence_records), policy, total);
    add_entry(bundle, "deferred-validation-gates.json",
        export_deferred_validation_gates_json(context.deferred_gates), policy, total);
    add_entry(bundle, "redaction-report.json", redaction_json(policy), policy, total);
    if (policy.include_time_travel && !context.time_travel_json.empty()) {
        if (!policy.time_travel_payload_authorized) {
            throw std::invalid_argument("time-travel export requires explicit payload authorization");
        }
        add_entry(bundle, "time-travel.json", context.time_travel_json, policy, total);
    }
    if (policy.include_visual_correlation && !context.visual_records.empty()) {
        add_entry(bundle, "visual-correlation.json",
            visual_correlation_json(context.visual_records, context.environment_id), policy, total);
    }
    std::sort(bundle.entries.begin(), bundle.entries.end(),
        [](const SupportBundleEntry& lhs, const SupportBundleEntry& rhs) {
            return lhs.path < rhs.path;
        });
    bundle.manifest_json = manifest_json(bundle.entries, &context);
    bundle.manifest_sha256 = hash_text(bundle.manifest_json);
    if (bundle.manifest_json.size() > policy.maximum_entry_bytes
        || exceeds_limit(total, bundle.manifest_json.size(), policy.maximum_total_bytes)) {
        throw std::length_error("support bundle manifest exceeds size limits");
    }
    if (audit_traces != nullptr) {
        for (std::size_t index = 0U; index < context.deferred_gates.size(); ++index) {
            const DeferredValidationGate& gate = context.deferred_gates[index];
            audit_traces->record({
                .correlation_id = correlation_id,
                .category = TraceCategory::Tooling,
                .outcome = TraceOutcome::Skipped,
                .code = TraceCode::ValidationGateDeferred,
                .measured_value = static_cast<std::int64_t>(index),
                .subsystem = TraceSubsystem::Qualification,
                .severity = gate.blocking_for_current_changeset
                    ? TraceSeverity::Error : TraceSeverity::Info,
                .detail_code = static_cast<std::uint32_t>(gate.category),
            });
        }
        audit_traces->record({
            .correlation_id = correlation_id,
            .category = TraceCategory::Tooling,
            .outcome = TraceOutcome::Applied,
            .code = TraceCode::SupportBundleCreated,
            .measured_value = static_cast<std::int64_t>(bundle.entries.size()),
            .budget_limit = static_cast<std::int64_t>(total + bundle.manifest_json.size()),
            .subsystem = TraceSubsystem::SupportBundle,
            .severity = TraceSeverity::Info,
            .related_hash = static_cast<std::uint64_t>(bundle.manifest_sha256[0]) << 56U,
        });
    }
    return bundle;
}

SupportBundleVerifyResult verify_support_bundle(
    const SupportBundleArtifact& bundle,
    const SupportBundlePolicy& policy) noexcept {
    try {
        std::set<std::string> paths;
        if (bundle.manifest_json.size() > policy.maximum_entry_bytes) {
            return {SupportBundleVerifyReason::EntryTooLarge, 0U};
        }
        if (bundle.manifest_json.size() > policy.maximum_total_bytes) {
            return {SupportBundleVerifyReason::TotalTooLarge, 0U};
        }
        std::size_t total = bundle.manifest_json.size();
        bool has_metadata{};
        bool has_traces{};
        bool has_evidence{};
        bool has_gates{};
        bool has_redaction{};
        for (std::size_t index = 0U; index < bundle.entries.size(); ++index) {
            const SupportBundleEntry& entry = bundle.entries[index];
            if (!safe_relative_path(entry.path)) return {SupportBundleVerifyReason::InvalidPath, index};
            if (!paths.insert(entry.path).second) return {SupportBundleVerifyReason::DuplicatePath, index};
            if (entry.content.size() > policy.maximum_entry_bytes) {
                return {SupportBundleVerifyReason::EntryTooLarge, index};
            }
            if (exceeds_limit(total, entry.content.size(), policy.maximum_total_bytes)) {
                return {SupportBundleVerifyReason::TotalTooLarge, index};
            }
            total += entry.content.size();
            if (!sha256_equal(hash_text(entry.content), entry.sha256)) {
                return {SupportBundleVerifyReason::HashMismatch, index};
            }
            has_metadata = has_metadata || entry.path == "metadata.json";
            has_traces = has_traces || entry.path == "traces.json";
            has_evidence = has_evidence || entry.path == "evidence-chain.json";
            has_gates = has_gates || entry.path == "deferred-validation-gates.json";
            has_redaction = has_redaction || entry.path == "redaction-report.json";
        }
        if (!has_metadata || !has_traces || !has_evidence || !has_gates || !has_redaction) {
            return {SupportBundleVerifyReason::MissingRequiredEntry, 0U};
        }
        if (!sha256_equal(hash_text(bundle.manifest_json), bundle.manifest_sha256)) {
            return {SupportBundleVerifyReason::ManifestMismatch, 0U};
        }
        std::vector<ManifestTuple> manifest_entries;
        ManifestParser parser(bundle.manifest_json);
        if (!parser.parse(bundle.entries.size(), manifest_entries)) {
            return {SupportBundleVerifyReason::ManifestMismatch, 0U};
        }
        std::set<std::string> manifest_paths;
        for (const ManifestTuple& tuple : manifest_entries) {
            if (!manifest_paths.insert(tuple.path).second) {
                return {SupportBundleVerifyReason::ManifestMismatch, 0U};
            }
            const auto entry = std::find_if(bundle.entries.begin(), bundle.entries.end(),
                [&](const SupportBundleEntry& candidate) { return candidate.path == tuple.path; });
            if (entry == bundle.entries.end() || entry->content.size() != tuple.size
                || sha256_hex(entry->sha256) != tuple.sha256) {
                return {SupportBundleVerifyReason::ManifestMismatch, 0U};
            }
        }
        return {};
    } catch (...) {
        return {SupportBundleVerifyReason::ManifestMismatch, 0U};
    }
}

void write_support_bundle_directory(
    const SupportBundleArtifact& bundle,
    const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    for (const SupportBundleEntry& entry : bundle.entries) {
        if (!safe_relative_path(entry.path)) {
            throw std::invalid_argument("support bundle contains unsafe path");
        }
        const std::filesystem::path destination = directory / entry.path;
        std::filesystem::create_directories(destination.parent_path());
        std::ofstream output(destination, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot create support bundle entry");
        output.write(entry.content.data(), static_cast<std::streamsize>(entry.content.size()));
        if (!output) throw std::runtime_error("cannot write support bundle entry");
    }
    {
        std::ofstream output(directory / "manifest.json", std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot create support bundle manifest");
        output.write(bundle.manifest_json.data(),
            static_cast<std::streamsize>(bundle.manifest_json.size()));
    }
    {
        std::ofstream output(directory / "manifest.sha256", std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("cannot create support bundle manifest digest");
        output << sha256_hex(bundle.manifest_sha256) << "  manifest.json\n";
    }
}

const char* to_string(ValidationGateCategory category) noexcept {
    switch (category) {
    case ValidationGateCategory::ImplementationGap: return "implementation_gap";
    case ValidationGateCategory::NativeValidationPending: return "native_validation_pending";
    case ValidationGateCategory::ExternalAssurancePending: return "external_assurance_pending";
    case ValidationGateCategory::FutureInfrastructure: return "future_infrastructure";
    }
    return "unknown";
}

const char* to_string(ValidationExecutionStatus status) noexcept {
    switch (status) {
    case ValidationExecutionStatus::NotExecuted: return "not_executed";
    case ValidationExecutionStatus::Passed: return "passed";
    case ValidationExecutionStatus::Failed: return "failed";
    case ValidationExecutionStatus::NotApplicable: return "not_applicable";
    }
    return "unknown";
}

const char* to_string(SupportBundleVerifyReason reason) noexcept {
    switch (reason) {
    case SupportBundleVerifyReason::None: return "none";
    case SupportBundleVerifyReason::InvalidPath: return "invalid_path";
    case SupportBundleVerifyReason::DuplicatePath: return "duplicate_path";
    case SupportBundleVerifyReason::EntryTooLarge: return "entry_too_large";
    case SupportBundleVerifyReason::TotalTooLarge: return "total_too_large";
    case SupportBundleVerifyReason::HashMismatch: return "hash_mismatch";
    case SupportBundleVerifyReason::MissingRequiredEntry: return "missing_required_entry";
    case SupportBundleVerifyReason::ManifestMismatch: return "manifest_mismatch";
    }
    return "unknown";
}

} // namespace neoeng::core
