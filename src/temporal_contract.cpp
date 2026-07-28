#include "neoeng/core/temporal_contract.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <system_error>
#include <type_traits>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace neoeng::core {
namespace {

constexpr std::array<std::uint8_t, 8U> kRecordMagic{
    'N', 'E', 'O', 'D', 'T', 'R', '1', '2'};
constexpr std::size_t kEncodedDigestCount = 4U;
constexpr std::size_t kMinimumRecordBytes =
    kRecordMagic.size() + sizeof(std::uint16_t) * 2U
    + sizeof(std::uint64_t) * 6U
    + kEncodedDigestCount * kSha256DigestBytes;

constexpr std::array<CanonicalFieldDescriptor, 6U> kCanonicalFields{{
    {"state.frame", true, true, true},
    {"body.id", true, true, true},
    {"body.position.x", true, true, true},
    {"body.position.y", true, true, true},
    {"body.velocity.x", true, true, true},
    {"body.velocity.y", true, true, true},
}};

constexpr std::array<MandatoryPathDescriptor, 9U> kMandatoryPaths{{
    {MandatoryOperationalPath::InputIngest,
     BudgetId::InputIngest, TraceSubsystem::NetworkGateway, true},
    {MandatoryOperationalPath::StateAdvance,
     BudgetId::StateAdvance, TraceSubsystem::Simulation, true},
    {MandatoryOperationalPath::Rollback,
     BudgetId::Rollback, TraceSubsystem::Rollback, true},
    {MandatoryOperationalPath::EcsMaintenance,
     BudgetId::EcsMaintenance, TraceSubsystem::Simulation, true},
    {MandatoryOperationalPath::EvidenceCheckpoint,
     BudgetId::EvidenceCheckpoint, TraceSubsystem::Evidence, true},
    {MandatoryOperationalPath::SupportBundleExport,
     BudgetId::SupportBundleExport, TraceSubsystem::SupportBundle, true},
    {MandatoryOperationalPath::DivergenceLocalization,
     BudgetId::DivergenceLocalization, TraceSubsystem::Evidence, true},
    {MandatoryOperationalPath::DurableRecorder,
     BudgetId::DurableRecorder, TraceSubsystem::Evidence, true},
    {MandatoryOperationalPath::ExternalEffectCommit,
     BudgetId::ExternalEffectCommit, TraceSubsystem::Recovery, true},
}};

template <typename T>
void append_little_endian(std::vector<std::uint8_t>& output, T value) {
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits = static_cast<Unsigned>(value);
    for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
        output.push_back(static_cast<std::uint8_t>(bits & Unsigned{0xFFU}));
        bits >>= 8U;
    }
}

void append_digest(
    std::vector<std::uint8_t>& output,
    const Sha256Digest& digest) {
    output.insert(output.end(), digest.begin(), digest.end());
}

template <typename T>
[[nodiscard]] bool read_little_endian(
    std::span<const std::uint8_t> bytes,
    std::size_t& cursor,
    T& value) noexcept {
    using Unsigned = std::make_unsigned_t<T>;
    if (cursor > bytes.size() || bytes.size() - cursor < sizeof(Unsigned)) {
        return false;
    }
    Unsigned bits{};
    for (std::size_t index = 0U; index < sizeof(Unsigned); ++index) {
        bits |= static_cast<Unsigned>(bytes[cursor + index])
            << static_cast<unsigned int>(index * 8U);
    }
    cursor += sizeof(Unsigned);
    value = static_cast<T>(bits);
    return true;
}

[[nodiscard]] bool read_digest(
    std::span<const std::uint8_t> bytes,
    std::size_t& cursor,
    Sha256Digest& digest) noexcept {
    if (cursor > bytes.size() || bytes.size() - cursor < digest.size()) {
        return false;
    }
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
        digest.size(), digest.begin());
    cursor += digest.size();
    return true;
}

[[nodiscard]] Sha256Digest hash_text(std::string_view text) noexcept {
    return sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

[[nodiscard]] std::string record_filename(std::uint64_t sequence) {
    std::ostringstream stream;
    stream << "record-" << std::setw(20) << std::setfill('0')
           << sequence << ".ndtr";
    return stream.str();
}

[[nodiscard]] bool is_record_filename(std::string_view name) noexcept {
    constexpr std::string_view prefix{"record-"};
    constexpr std::string_view suffix{".ndtr"};
    if (name.size() != prefix.size() + 20U + suffix.size()
        || !name.starts_with(prefix) || !name.ends_with(suffix)) {
        return false;
    }
    return std::all_of(
        name.begin() + static_cast<std::ptrdiff_t>(prefix.size()),
        name.end() - static_cast<std::ptrdiff_t>(suffix.size()),
        [](char value) { return value >= '0' && value <= '9'; });
}

[[nodiscard]] bool stable_flush_file(
    const std::filesystem::path& path) noexcept {
#if defined(_WIN32)
    const HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    const bool accepted = FlushFileBuffers(handle) != 0;
    CloseHandle(handle);
    return accepted;
#else
    const int descriptor = ::open(path.c_str(), O_RDONLY);
    if (descriptor < 0) {
        return false;
    }
    const bool accepted = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return accepted;
#endif
}

[[nodiscard]] bool stable_flush_directory(
    const std::filesystem::path& path) noexcept {
#if defined(_WIN32)
    static_cast<void>(path);
    return true;
#else
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0) {
        return false;
    }
    const bool accepted = ::fsync(descriptor) == 0;
    ::close(descriptor);
    return accepted;
#endif
}

[[nodiscard]] std::vector<std::uint8_t> encode_record(
    const DurableTimelineRecord& record) {
    std::vector<std::uint8_t> output;
    output.reserve(kMinimumRecordBytes
        + record.timeline_json.size() + record.evidence_json.size());
    output.insert(output.end(), kRecordMagic.begin(), kRecordMagic.end());
    append_little_endian(output, kDurableTimelineFormatVersion);
    append_little_endian(output, std::uint16_t{0U});
    append_little_endian(output, record.sequence);
    append_little_endian(output, record.branch_id);
    append_little_endian(output, record.first_frame);
    append_little_endian(output, record.last_frame);
    append_little_endian(
        output, static_cast<std::uint64_t>(record.timeline_json.size()));
    append_little_endian(
        output, static_cast<std::uint64_t>(record.evidence_json.size()));
    append_digest(output, record.previous_record_hash);
    append_digest(output, record.timeline_sha256);
    append_digest(output, record.evidence_sha256);
    output.insert(
        output.end(), record.timeline_json.begin(), record.timeline_json.end());
    output.insert(
        output.end(), record.evidence_json.begin(), record.evidence_json.end());
    append_digest(output, sha256(output));
    return output;
}

[[nodiscard]] DurableRecorderReason decode_record(
    std::span<const std::uint8_t> bytes,
    std::uint64_t expected_sequence,
    const Sha256Digest& expected_previous,
    std::size_t maximum_record_bytes,
    DurableTimelineRecord& output) noexcept {
    if (bytes.size() < kMinimumRecordBytes
        || bytes.size() > maximum_record_bytes
        || !std::equal(kRecordMagic.begin(), kRecordMagic.end(), bytes.begin())) {
        return bytes.size() > maximum_record_bytes
            ? DurableRecorderReason::RecordTooLarge
            : DurableRecorderReason::CorruptRecord;
    }
    std::size_t cursor = kRecordMagic.size();
    std::uint16_t version{};
    std::uint16_t reserved{};
    std::uint64_t timeline_size{};
    std::uint64_t evidence_size{};
    if (!read_little_endian(bytes, cursor, version)
        || !read_little_endian(bytes, cursor, reserved)
        || version != kDurableTimelineFormatVersion
        || reserved != 0U
        || !read_little_endian(bytes, cursor, output.sequence)
        || !read_little_endian(bytes, cursor, output.branch_id)
        || !read_little_endian(bytes, cursor, output.first_frame)
        || !read_little_endian(bytes, cursor, output.last_frame)
        || !read_little_endian(bytes, cursor, timeline_size)
        || !read_little_endian(bytes, cursor, evidence_size)
        || !read_digest(bytes, cursor, output.previous_record_hash)
        || !read_digest(bytes, cursor, output.timeline_sha256)
        || !read_digest(bytes, cursor, output.evidence_sha256)) {
        return DurableRecorderReason::CorruptRecord;
    }
    if (output.sequence != expected_sequence) {
        return DurableRecorderReason::SequenceMismatch;
    }
    if (!sha256_equal(output.previous_record_hash, expected_previous)) {
        return DurableRecorderReason::ChainMismatch;
    }
    if (output.branch_id == 0U || output.first_frame > output.last_frame
        || timeline_size == 0U || evidence_size == 0U
        || timeline_size > std::numeric_limits<std::size_t>::max()
        || evidence_size > std::numeric_limits<std::size_t>::max()) {
        return DurableRecorderReason::CorruptRecord;
    }
    const std::size_t timeline_bytes = static_cast<std::size_t>(timeline_size);
    const std::size_t evidence_bytes = static_cast<std::size_t>(evidence_size);
    const std::size_t trailer_size = kSha256DigestBytes;
    if (cursor > bytes.size() || timeline_bytes > bytes.size() - cursor) {
        return DurableRecorderReason::CorruptRecord;
    }
    const std::size_t evidence_cursor = cursor + timeline_bytes;
    if (evidence_bytes > bytes.size() - evidence_cursor
        || bytes.size() - evidence_cursor - evidence_bytes != trailer_size) {
        return DurableRecorderReason::CorruptRecord;
    }
    output.timeline_json.assign(
        reinterpret_cast<const char*>(bytes.data() + cursor), timeline_bytes);
    output.evidence_json.assign(
        reinterpret_cast<const char*>(bytes.data() + evidence_cursor),
        evidence_bytes);
    std::size_t digest_cursor = evidence_cursor + evidence_bytes;
    if (!read_digest(bytes, digest_cursor, output.record_sha256)
        || !sha256_equal(output.timeline_sha256, hash_text(output.timeline_json))
        || !sha256_equal(output.evidence_sha256, hash_text(output.evidence_json))
        || !sha256_equal(
            output.record_sha256,
            sha256(bytes.first(bytes.size() - trailer_size)))) {
        return DurableRecorderReason::CorruptRecord;
    }
    return DurableRecorderReason::None;
}

void record_temporal_trace(
    TraceBuffer* traces,
    CorrelationId correlation_id,
    std::uint64_t frame,
    TraceOutcome outcome,
    TraceCode code,
    std::uint64_t subject,
    std::uint32_t detail) noexcept {
    if (traces == nullptr) {
        return;
    }
    traces->record({
        .correlation_id = correlation_id,
        .frame = frame,
        .category = TraceCategory::Evidence,
        .outcome = outcome,
        .code = code,
        .subsystem = TraceSubsystem::Evidence,
        .severity = outcome == TraceOutcome::Rejected
            ? TraceSeverity::Error : TraceSeverity::Info,
        .subject_token = subject,
        .detail_code = detail,
    });
}

void record_effect_trace(
    TraceBuffer* traces,
    CorrelationId correlation_id,
    const ExternalEffectIntent* intent,
    TraceOutcome outcome,
    TraceCode code,
    ExternalEffectReason reason) noexcept {
    if (traces == nullptr) {
        return;
    }
    traces->record({
        .correlation_id = correlation_id,
        .frame = intent == nullptr ? 0U : intent->frame,
        .category = TraceCategory::Recovery,
        .outcome = outcome,
        .code = code,
        .subsystem = TraceSubsystem::Recovery,
        .severity = outcome == TraceOutcome::Rejected
            ? TraceSeverity::Error : TraceSeverity::Info,
        .detail_code = static_cast<std::uint32_t>(reason),
    });
}

[[nodiscard]] bool same_intent(
    const ExternalEffectIntent& lhs,
    const ExternalEffectIntent& rhs) noexcept {
    return lhs.idempotency_key == rhs.idempotency_key
        && lhs.kind == rhs.kind
        && lhs.frame == rhs.frame
        && lhs.payload_sha256 == rhs.payload_sha256
        && lhs.compensation_supported == rhs.compensation_supported;
}

} // namespace

std::span<const CanonicalFieldDescriptor>
canonical_world_v1_fields() noexcept {
    return kCanonicalFields;
}

std::span<const MandatoryPathDescriptor>
mandatory_operational_paths_v1() noexcept {
    return kMandatoryPaths;
}

DurableTimelineRecorder::DurableTimelineRecorder(
    std::filesystem::path directory,
    std::size_t maximum_record_bytes)
    : directory_(std::move(directory)),
      maximum_record_bytes_(maximum_record_bytes) {
    if (directory_.empty() || maximum_record_bytes_ < kMinimumRecordBytes) {
        throw std::invalid_argument(
            "durable timeline directory or record limit is invalid");
    }
}

DurableRecorderResult DurableTimelineRecorder::recover() {
    records_.clear();
    recovered_ = false;
    try {
        std::filesystem::create_directories(directory_);
        std::vector<std::filesystem::path> paths;
        for (const auto& entry : std::filesystem::directory_iterator(directory_)) {
            if (entry.is_regular_file()
                && is_record_filename(entry.path().filename().string())) {
                paths.push_back(entry.path());
            }
        }
        std::sort(paths.begin(), paths.end());
        Sha256Digest previous{};
        for (std::size_t index = 0U; index < paths.size(); ++index) {
            const std::uintmax_t file_size = std::filesystem::file_size(paths[index]);
            if (file_size > maximum_record_bytes_
                || file_size > std::numeric_limits<std::size_t>::max()) {
                return {.reason = DurableRecorderReason::RecordTooLarge};
            }
            std::ifstream input(paths[index], std::ios::binary);
            if (!input) {
                return {.reason = DurableRecorderReason::IoFailure};
            }
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_size));
            input.read(reinterpret_cast<char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            if (!input) {
                return {.reason = DurableRecorderReason::IoFailure};
            }
            DurableTimelineRecord record;
            const DurableRecorderReason reason = decode_record(
                bytes, static_cast<std::uint64_t>(index), previous,
                maximum_record_bytes_, record);
            if (reason != DurableRecorderReason::None) {
                return {.reason = reason};
            }
            if (paths[index].filename() != record_filename(record.sequence)) {
                return {.reason = DurableRecorderReason::SequenceMismatch};
            }
            previous = record.record_sha256;
            records_.push_back(std::move(record));
        }
        recovered_ = true;
        return {
            .sequence = records_.empty() ? 0U : records_.back().sequence,
            .record_sha256 =
                records_.empty() ? Sha256Digest{} : records_.back().record_sha256,
        };
    } catch (...) {
        records_.clear();
        return {.reason = DurableRecorderReason::IoFailure};
    }
}

DurableRecorderResult DurableTimelineRecorder::append(
    const DurableTimelineInput& input,
    CorrelationId correlation_id,
    TraceBuffer* traces) {
    BudgetMonitor monitor;
    ScopedBudgetMeasurement budget(
        traces == nullptr ? nullptr : &monitor,
        traces,
        {
            .id = BudgetId::DurableRecorder,
            .subsystem = TraceSubsystem::Evidence,
            .limit_ns = 0U,
            .exceed_severity = TraceSeverity::Warning,
        },
        correlation_id,
        input.last_frame);
    const auto reject = [&](DurableRecorderReason reason) {
        record_temporal_trace(
            traces, correlation_id, input.last_frame,
            TraceOutcome::Rejected, TraceCode::TemporalRecordRejected,
            input.branch_id, static_cast<std::uint32_t>(reason));
        return DurableRecorderResult{.reason = reason};
    };
    if (!recovered_) {
        return reject(DurableRecorderReason::NotRecovered);
    }
    if (input.branch_id == 0U || input.first_frame > input.last_frame
        || input.timeline_json.empty() || input.evidence_json.empty()) {
        return reject(DurableRecorderReason::InvalidInput);
    }
    DurableTimelineRecord record{
        .sequence = static_cast<std::uint64_t>(records_.size()),
        .branch_id = input.branch_id,
        .first_frame = input.first_frame,
        .last_frame = input.last_frame,
        .previous_record_hash =
            records_.empty() ? Sha256Digest{} : records_.back().record_sha256,
        .timeline_sha256 = hash_text(input.timeline_json),
        .evidence_sha256 = hash_text(input.evidence_json),
        .timeline_json = input.timeline_json,
        .evidence_json = input.evidence_json,
    };
    std::vector<std::uint8_t> bytes = encode_record(record);
    if (bytes.size() > maximum_record_bytes_) {
        return reject(DurableRecorderReason::RecordTooLarge);
    }
    std::copy_n(
        bytes.end() - static_cast<std::ptrdiff_t>(kSha256DigestBytes),
        kSha256DigestBytes, record.record_sha256.begin());
    const std::filesystem::path destination =
        directory_ / record_filename(record.sequence);
    const std::filesystem::path temporary =
        destination.string() + ".pending";
    try {
        if (std::filesystem::exists(destination)) {
            return reject(DurableRecorderReason::SequenceMismatch);
        }
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        {
            std::ofstream output(
                temporary, std::ios::binary | std::ios::trunc);
            if (!output) {
                return reject(DurableRecorderReason::IoFailure);
            }
            output.write(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
            output.flush();
            if (!output) {
                return reject(DurableRecorderReason::IoFailure);
            }
        }
        if (!stable_flush_file(temporary)) {
            std::filesystem::remove(temporary, cleanup_error);
            return reject(DurableRecorderReason::IoFailure);
        }
        std::filesystem::rename(temporary, destination);
        if (!stable_flush_file(destination)
            || !stable_flush_directory(directory_)) {
            return reject(DurableRecorderReason::IoFailure);
        }
        records_.push_back(record);
        record_temporal_trace(
            traces, correlation_id, input.last_frame,
            TraceOutcome::Applied, TraceCode::TemporalRecordCommitted,
            input.branch_id, 0U);
        return {
            .sequence = record.sequence,
            .record_sha256 = record.record_sha256,
        };
    } catch (...) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return reject(DurableRecorderReason::IoFailure);
    }
}

DurableRecorderResult verify_durable_timeline_directory(
    const std::filesystem::path& directory,
    std::size_t maximum_record_bytes) {
    try {
        DurableTimelineRecorder recorder(directory, maximum_record_bytes);
        return recorder.recover();
    } catch (...) {
        return {.reason = DurableRecorderReason::InvalidInput};
    }
}

std::size_t ExternalEffectLedger::find(
    std::string_view idempotency_key) const noexcept {
    for (std::size_t index = 0U; index < records_.size(); ++index) {
        if (records_[index].intent.idempotency_key == idempotency_key) {
            return index;
        }
    }
    return records_.size();
}

ExternalEffectDecision ExternalEffectLedger::prepare(
    ExternalEffectIntent intent,
    CorrelationId correlation_id,
    TraceBuffer* traces) {
    if (intent.idempotency_key.empty()
        || intent.idempotency_key.size() > kMaximumExternalEffectKeyBytes
        || intent.kind.empty()
        || intent.kind.size() > kMaximumExternalEffectKindBytes) {
        record_effect_trace(
            traces, correlation_id, &intent, TraceOutcome::Rejected,
            TraceCode::ExternalEffectRejected,
            ExternalEffectReason::InvalidIntent);
        return {.reason = ExternalEffectReason::InvalidIntent};
    }
    const std::size_t existing = find(intent.idempotency_key);
    if (existing != records_.size()) {
        if (!same_intent(records_[existing].intent, intent)) {
            record_effect_trace(
                traces, correlation_id, &intent, TraceOutcome::Rejected,
                TraceCode::ExternalEffectRejected,
                ExternalEffectReason::IdempotencyConflict);
            return {
                .reason = ExternalEffectReason::IdempotencyConflict,
                .state = records_[existing].state,
            };
        }
        return {.state = records_[existing].state};
    }
    records_.push_back({
        .intent = std::move(intent),
        .state = ExternalEffectState::Prepared,
    });
    record_effect_trace(
        traces, correlation_id, &records_.back().intent, TraceOutcome::Accepted,
        TraceCode::ExternalEffectPrepared, ExternalEffectReason::None);
    return {.state = ExternalEffectState::Prepared};
}

ExternalEffectDecision ExternalEffectLedger::commit(
    std::string_view idempotency_key,
    std::uint64_t confirmed_frame,
    ExternalEffectExecutor& executor,
    CorrelationId correlation_id,
    TraceBuffer* traces) {
    const std::size_t index = find(idempotency_key);
    if (index == records_.size()) {
        record_effect_trace(
            traces, correlation_id, nullptr, TraceOutcome::Rejected,
            TraceCode::ExternalEffectRejected,
            ExternalEffectReason::UnknownEffect);
        return {.reason = ExternalEffectReason::UnknownEffect};
    }
    ExternalEffectRecord& record = records_[index];
    BudgetMonitor monitor;
    ScopedBudgetMeasurement budget(
        traces == nullptr ? nullptr : &monitor,
        traces,
        {
            .id = BudgetId::ExternalEffectCommit,
            .subsystem = TraceSubsystem::Recovery,
            .limit_ns = 0U,
            .exceed_severity = TraceSeverity::Warning,
        },
        correlation_id,
        record.intent.frame);
    if (record.state == ExternalEffectState::Committed) {
        return {.state = record.state};
    }
    if (record.state != ExternalEffectState::Prepared) {
        return {
            .reason = ExternalEffectReason::InvalidState,
            .state = record.state,
        };
    }
    if (confirmed_frame < record.intent.frame) {
        record_effect_trace(
            traces, correlation_id, &record.intent, TraceOutcome::Rejected,
            TraceCode::ExternalEffectRejected,
            ExternalEffectReason::NotConfirmed);
        return {
            .reason = ExternalEffectReason::NotConfirmed,
            .state = record.state,
        };
    }
    const ExternalEffectApplyResult applied = executor.commit(record.intent);
    if (applied != ExternalEffectApplyResult::Applied
        && applied != ExternalEffectApplyResult::AlreadyApplied) {
        record_effect_trace(
            traces, correlation_id, &record.intent, TraceOutcome::Rejected,
            TraceCode::ExternalEffectRejected,
            ExternalEffectReason::ExecutionFailed);
        return {
            .reason = ExternalEffectReason::ExecutionFailed,
            .state = record.state,
            .executor_invoked = true,
        };
    }
    record.state = ExternalEffectState::Committed;
    record_effect_trace(
        traces, correlation_id, &record.intent,
        applied == ExternalEffectApplyResult::AlreadyApplied
            ? TraceOutcome::Skipped : TraceOutcome::Applied,
        TraceCode::ExternalEffectCommitted, ExternalEffectReason::None);
    return {
        .state = record.state,
        .executor_invoked = true,
    };
}

ExternalEffectDecision ExternalEffectLedger::compensate(
    std::string_view idempotency_key,
    ExternalEffectExecutor& executor,
    CorrelationId correlation_id,
    TraceBuffer* traces) {
    const std::size_t index = find(idempotency_key);
    if (index == records_.size()) {
        return {.reason = ExternalEffectReason::UnknownEffect};
    }
    ExternalEffectRecord& record = records_[index];
    if (!record.intent.compensation_supported) {
        return {
            .reason = ExternalEffectReason::CompensationUnsupported,
            .state = record.state,
        };
    }
    if (record.state == ExternalEffectState::Compensated) {
        return {.state = record.state};
    }
    if (record.state != ExternalEffectState::Committed) {
        return {
            .reason = ExternalEffectReason::InvalidState,
            .state = record.state,
        };
    }
    const ExternalEffectApplyResult applied = executor.compensate(record.intent);
    if (applied != ExternalEffectApplyResult::Applied
        && applied != ExternalEffectApplyResult::AlreadyApplied) {
        return {
            .reason = ExternalEffectReason::ExecutionFailed,
            .state = record.state,
            .executor_invoked = true,
        };
    }
    record.state = ExternalEffectState::Compensated;
    record_effect_trace(
        traces, correlation_id, &record.intent,
        applied == ExternalEffectApplyResult::AlreadyApplied
            ? TraceOutcome::Skipped : TraceOutcome::Recovered,
        TraceCode::ExternalEffectCompensated, ExternalEffectReason::None);
    return {
        .state = record.state,
        .executor_invoked = true,
    };
}

ExternalEffectRollbackDecision ExternalEffectLedger::reconcile_rollback(
    std::uint64_t restored_frame,
    CorrelationId correlation_id,
    TraceBuffer* traces) {
    ExternalEffectRollbackDecision decision;
    for (const ExternalEffectRecord& record : records_) {
        if (record.intent.frame > restored_frame
            && record.state == ExternalEffectState::Committed) {
            ++decision.committed_effects_after_frame;
        }
    }
    const auto removed = std::remove_if(
        records_.begin(), records_.end(),
        [restored_frame](const ExternalEffectRecord& record) {
            return record.intent.frame > restored_frame
                && record.state == ExternalEffectState::Prepared;
        });
    decision.prepared_effects_discarded =
        static_cast<std::size_t>(records_.end() - removed);
    records_.erase(removed, records_.end());
    if (decision.committed_effects_after_frame != 0U) {
        decision.reason =
            ExternalEffectReason::CommittedEffectCrossedRollback;
        record_effect_trace(
            traces, correlation_id, nullptr, TraceOutcome::Rejected,
            TraceCode::ExternalEffectRejected, decision.reason);
    }
    return decision;
}

const char* to_string(MandatoryOperationalPath path) noexcept {
    switch (path) {
    case MandatoryOperationalPath::InputIngest: return "input_ingest";
    case MandatoryOperationalPath::StateAdvance: return "state_advance";
    case MandatoryOperationalPath::Rollback: return "rollback";
    case MandatoryOperationalPath::EcsMaintenance: return "ecs_maintenance";
    case MandatoryOperationalPath::EvidenceCheckpoint:
        return "evidence_checkpoint";
    case MandatoryOperationalPath::SupportBundleExport:
        return "support_bundle_export";
    case MandatoryOperationalPath::DivergenceLocalization:
        return "divergence_localization";
    case MandatoryOperationalPath::DurableRecorder:
        return "durable_recorder";
    case MandatoryOperationalPath::ExternalEffectCommit:
        return "external_effect_commit";
    }
    return "unknown";
}

const char* to_string(DurableRecorderReason reason) noexcept {
    switch (reason) {
    case DurableRecorderReason::None: return "none";
    case DurableRecorderReason::InvalidInput: return "invalid_input";
    case DurableRecorderReason::NotRecovered: return "not_recovered";
    case DurableRecorderReason::IoFailure: return "io_failure";
    case DurableRecorderReason::RecordTooLarge: return "record_too_large";
    case DurableRecorderReason::CorruptRecord: return "corrupt_record";
    case DurableRecorderReason::SequenceMismatch: return "sequence_mismatch";
    case DurableRecorderReason::ChainMismatch: return "chain_mismatch";
    }
    return "unknown";
}

const char* to_string(ExternalEffectState state) noexcept {
    switch (state) {
    case ExternalEffectState::Prepared: return "prepared";
    case ExternalEffectState::Committed: return "committed";
    case ExternalEffectState::Compensated: return "compensated";
    }
    return "unknown";
}

const char* to_string(ExternalEffectReason reason) noexcept {
    switch (reason) {
    case ExternalEffectReason::None: return "none";
    case ExternalEffectReason::InvalidIntent: return "invalid_intent";
    case ExternalEffectReason::IdempotencyConflict:
        return "idempotency_conflict";
    case ExternalEffectReason::UnknownEffect: return "unknown_effect";
    case ExternalEffectReason::NotConfirmed: return "not_confirmed";
    case ExternalEffectReason::InvalidState: return "invalid_state";
    case ExternalEffectReason::ExecutionFailed: return "execution_failed";
    case ExternalEffectReason::CompensationUnsupported:
        return "compensation_unsupported";
    case ExternalEffectReason::CommittedEffectCrossedRollback:
        return "committed_effect_crossed_rollback";
    }
    return "unknown";
}

} // namespace neoeng::core
