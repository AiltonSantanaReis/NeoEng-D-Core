#include "neoeng/core/observability.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace neoeng::core {
namespace {

[[nodiscard]] std::int64_t saturating_i64(std::uint64_t value) noexcept {
    return value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(value);
}

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

void append_component_diffs(
    std::vector<ComponentDiff>& output,
    const Body& left,
    const Body& right) {
    if (left.id != right.id) {
        output.push_back({left.id, "identity", left.id, right.id});
    }
    if (left.position.x != right.position.x) {
        output.push_back({left.id, "position.x", left.position.x.raw(), right.position.x.raw()});
    }
    if (left.position.y != right.position.y) {
        output.push_back({left.id, "position.y", left.position.y.raw(), right.position.y.raw()});
    }
    if (left.velocity.x != right.velocity.x) {
        output.push_back({left.id, "velocity.x", left.velocity.x.raw(), right.velocity.x.raw()});
    }
    if (left.velocity.y != right.velocity.y) {
        output.push_back({left.id, "velocity.y", left.velocity.y.raw(), right.velocity.y.raw()});
    }
}

} // namespace

TraceBuffer::TraceBuffer(std::size_t capacity) : events_(capacity) {
    if (capacity == 0U) {
        throw std::invalid_argument("TraceBuffer capacity must be greater than zero");
    }
}

void TraceBuffer::record(TraceEvent event) noexcept {
    event.sequence = next_sequence_++;
    events_[next_index_] = event;
    next_index_ = (next_index_ + 1U) % events_.size();
    if (size_ < events_.size()) {
        ++size_;
    } else {
        ++overwritten_events_;
    }
}

std::vector<TraceEvent> TraceBuffer::snapshot() const {
    std::vector<TraceEvent> output;
    output.reserve(size_);
    const std::size_t start = size_ == events_.size() ? next_index_ : 0U;
    for (std::size_t position = 0; position < size_; ++position) {
        output.push_back(events_[(start + position) % events_.size()]);
    }
    return output;
}

std::vector<TraceEvent> TraceBuffer::by_frame(std::uint64_t frame) const {
    std::vector<TraceEvent> output;
    for (const TraceEvent& event : snapshot()) {
        if (event.frame == frame) {
            output.push_back(event);
        }
    }
    return output;
}

std::vector<TraceEvent> TraceBuffer::by_correlation(CorrelationId correlation_id) const {
    std::vector<TraceEvent> output;
    for (const TraceEvent& event : snapshot()) {
        if (event.correlation_id == correlation_id) {
            output.push_back(event);
        }
    }
    return output;
}

TimeTravelDebugger::TimeTravelDebugger(std::size_t frame_capacity)
    : records_(frame_capacity) {
    if (frame_capacity == 0U) {
        throw std::invalid_argument("TimeTravelDebugger capacity must be greater than zero");
    }
}

void TimeTravelDebugger::record_frame(
    const WorldState& state,
    std::span<const InputCommand> inputs,
    std::span<const TraceEvent> events) {
    const std::optional<std::uint64_t> newest = newest_frame();
    if (newest.has_value() && state.frame <= *newest) {
        throw std::invalid_argument("Time-travel frames must be recorded in strictly increasing order");
    }
    FrameRecord record{
        .state = state,
        .inputs = std::vector<InputCommand>(inputs.begin(), inputs.end()),
        .events = std::vector<TraceEvent>(events.begin(), events.end()),
        .state_hash = stable_hash(state),
    };

    if (size_ < records_.size()) {
        records_[logical_index(size_)] = std::move(record);
        ++size_;
        return;
    }
    records_[oldest_index_] = std::move(record);
    oldest_index_ = (oldest_index_ + 1U) % records_.size();
}

std::size_t TimeTravelDebugger::logical_index(std::size_t position) const noexcept {
    return (oldest_index_ + position) % records_.size();
}

const FrameRecord* TimeTravelDebugger::frame(std::uint64_t frame_number) const noexcept {
    for (std::size_t position = 0; position < size_; ++position) {
        const auto& slot = records_[logical_index(position)];
        if (slot.has_value() && slot->state.frame == frame_number) {
            return &*slot;
        }
    }
    return nullptr;
}

const Body* TimeTravelDebugger::entity(
    std::uint64_t frame_number,
    EntityId entity_id) const noexcept {
    const FrameRecord* record = frame(frame_number);
    if (record == nullptr) {
        return nullptr;
    }
    const auto iterator = std::lower_bound(record->state.bodies.begin(), record->state.bodies.end(),
        entity_id, [](const Body& body, EntityId id) { return body.id < id; });
    return iterator != record->state.bodies.end() && iterator->id == entity_id
        ? &*iterator
        : nullptr;
}

FrameDiff TimeTravelDebugger::compare(
    std::uint64_t left_frame,
    std::uint64_t right_frame) const {
    const FrameRecord* left = frame(left_frame);
    const FrameRecord* right = frame(right_frame);
    if (left == nullptr || right == nullptr) {
        throw std::out_of_range("Requested time-travel frame is not retained");
    }

    FrameDiff diff{
        .left_frame = left_frame,
        .right_frame = right_frame,
        .left_hash = left->state_hash,
        .right_hash = right->state_hash,
    };
    if (left->state.frame != right->state.frame) {
        diff.changes.push_back({
            0U,
            "state.frame",
            saturating_i64(left->state.frame),
            saturating_i64(right->state.frame),
        });
    }
    std::size_t left_index{};
    std::size_t right_index{};
    while (left_index < left->state.bodies.size() || right_index < right->state.bodies.size()) {
        if (right_index >= right->state.bodies.size()
            || (left_index < left->state.bodies.size()
                && left->state.bodies[left_index].id < right->state.bodies[right_index].id)) {
            const Body& body = left->state.bodies[left_index++];
            diff.changes.push_back({body.id, "entity.removed", 1, 0});
            continue;
        }
        if (left_index >= left->state.bodies.size()
            || right->state.bodies[right_index].id < left->state.bodies[left_index].id) {
            const Body& body = right->state.bodies[right_index++];
            diff.changes.push_back({body.id, "entity.added", 0, 1});
            continue;
        }
        append_component_diffs(diff.changes,
            left->state.bodies[left_index], right->state.bodies[right_index]);
        ++left_index;
        ++right_index;
    }
    return diff;
}

std::vector<TraceEvent> TimeTravelDebugger::correlate(CorrelationId correlation_id) const {
    std::vector<TraceEvent> output;
    for (std::size_t position = 0; position < size_; ++position) {
        const auto& slot = records_[logical_index(position)];
        if (!slot.has_value()) {
            continue;
        }
        for (const TraceEvent& event : slot->events) {
            if (event.correlation_id == correlation_id) {
                output.push_back(event);
            }
        }
    }
    std::sort(output.begin(), output.end(), [](const TraceEvent& lhs, const TraceEvent& rhs) {
        if (lhs.frame != rhs.frame) {
            return lhs.frame < rhs.frame;
        }
        return lhs.sequence < rhs.sequence;
    });
    return output;
}

std::string TimeTravelDebugger::export_reproducible_json(
    std::uint64_t seed,
    std::string_view environment_id) const {
    std::ostringstream stream;
    stream << "{\n  \"schema\": \"neoeng.dcore.time-travel.v1\",\n"
           << "  \"seed\": " << seed << ",\n  \"environment_id\": ";
    append_json_string(stream, environment_id);
    stream << ",\n  \"frames\": [";

    for (std::size_t position = 0; position < size_; ++position) {
        const FrameRecord& record = *records_[logical_index(position)];
        stream << (position == 0U ? "\n" : ",\n")
               << "    {\"frame\": " << record.state.frame
               << ", \"hash\": \"" << hash_hex(record.state_hash) << "\", \"bodies\": [";
        for (std::size_t index = 0; index < record.state.bodies.size(); ++index) {
            const Body& body = record.state.bodies[index];
            stream << (index == 0U ? "" : ",")
                   << "{\"id\":" << body.id
                   << ",\"px\":" << body.position.x.raw()
                   << ",\"py\":" << body.position.y.raw()
                   << ",\"vx\":" << body.velocity.x.raw()
                   << ",\"vy\":" << body.velocity.y.raw() << "}";
        }
        stream << "], \"inputs\": [";
        for (std::size_t index = 0; index < record.inputs.size(); ++index) {
            const InputCommand& input = record.inputs[index];
            stream << (index == 0U ? "" : ",")
                   << "{\"entity\":" << input.entity
                   << ",\"ax\":" << input.acceleration.x.raw()
                   << ",\"ay\":" << input.acceleration.y.raw() << "}";
        }
        stream << "], \"events\": [";
        for (std::size_t index = 0; index < record.events.size(); ++index) {
            const TraceEvent& event = record.events[index];
            stream << (index == 0U ? "" : ",")
                   << "{\"correlation\":" << event.correlation_id
                   << ",\"sequence\":" << event.sequence
                   << ",\"category\":\"" << to_string(event.category)
                   << "\",\"outcome\":\"" << to_string(event.outcome)
                   << "\",\"code\":\"" << to_string(event.code) << "\"}";
        }
        stream << "]}";
    }
    if (size_ != 0U) {
        stream << '\n';
    }
    stream << "  ]\n}\n";
    return stream.str();
}

void TimeTravelDebugger::truncate_after(std::uint64_t frame_number) noexcept {
    while (size_ != 0U) {
        const std::size_t newest_index = logical_index(size_ - 1U);
        const auto& slot = records_[newest_index];
        if (slot.has_value() && slot->state.frame <= frame_number) {
            break;
        }
        records_[newest_index].reset();
        --size_;
    }
    if (size_ == 0U) {
        oldest_index_ = 0U;
    }
}

std::optional<std::uint64_t> TimeTravelDebugger::oldest_frame() const noexcept {
    if (size_ == 0U) {
        return std::nullopt;
    }
    return records_[oldest_index_]->state.frame;
}

std::optional<std::uint64_t> TimeTravelDebugger::newest_frame() const noexcept {
    if (size_ == 0U) {
        return std::nullopt;
    }
    return records_[logical_index(size_ - 1U)]->state.frame;
}

const char* to_string(TraceCategory category) noexcept {
    switch (category) {
    case TraceCategory::Input: return "input";
    case TraceCategory::Network: return "network";
    case TraceCategory::Simulation: return "simulation";
    case TraceCategory::Rollback: return "rollback";
    case TraceCategory::Budget: return "budget";
    case TraceCategory::Recovery: return "recovery";
    case TraceCategory::Tooling: return "tooling";
    case TraceCategory::Evidence: return "evidence";
    }
    return "unknown";
}

const char* to_string(TraceSubsystem subsystem) noexcept {
    switch (subsystem) {
    case TraceSubsystem::Unknown: return "unknown";
    case TraceSubsystem::InputParser: return "input_parser";
    case TraceSubsystem::NetworkGateway: return "network_gateway";
    case TraceSubsystem::Session: return "session";
    case TraceSubsystem::Simulation: return "simulation";
    case TraceSubsystem::Rollback: return "rollback";
    case TraceSubsystem::Recovery: return "recovery";
    case TraceSubsystem::Evidence: return "evidence";
    case TraceSubsystem::ViewLab: return "view_lab";
    case TraceSubsystem::SupportBundle: return "support_bundle";
    case TraceSubsystem::Qualification: return "qualification";
    }
    return "unknown";
}

const char* to_string(TraceSeverity severity) noexcept {
    switch (severity) {
    case TraceSeverity::Debug: return "debug";
    case TraceSeverity::Info: return "info";
    case TraceSeverity::Warning: return "warning";
    case TraceSeverity::Error: return "error";
    case TraceSeverity::Critical: return "critical";
    }
    return "unknown";
}

const char* to_string(TraceOutcome outcome) noexcept {
    switch (outcome) {
    case TraceOutcome::Accepted: return "accepted";
    case TraceOutcome::Rejected: return "rejected";
    case TraceOutcome::Applied: return "applied";
    case TraceOutcome::Skipped: return "skipped";
    case TraceOutcome::Degraded: return "degraded";
    case TraceOutcome::Recovered: return "recovered";
    case TraceOutcome::Failed: return "failed";
    }
    return "unknown";
}

const char* to_string(TraceCode code) noexcept {
    switch (code) {
    case TraceCode::None: return "none";
    case TraceCode::InputAuthenticated: return "input_authenticated";
    case TraceCode::InputMalformed: return "input_malformed";
    case TraceCode::InputReplayRejected: return "input_replay_rejected";
    case TraceCode::InputRateLimited: return "input_rate_limited";
    case TraceCode::StateAdvanced: return "state_advanced";
    case TraceCode::StateDivergence: return "state_divergence";
    case TraceCode::RollbackStarted: return "rollback_started";
    case TraceCode::RollbackCompleted: return "rollback_completed";
    case TraceCode::BudgetSampled: return "budget_sampled";
    case TraceCode::BudgetExceeded: return "budget_exceeded";
    case TraceCode::DeviceLost: return "device_lost";
    case TraceCode::IoStall: return "io_stall";
    case TraceCode::OutOfMemory: return "out_of_memory";
    case TraceCode::SafeWaitEntered: return "safe_wait_entered";
    case TraceCode::SafeRollbackEntered: return "safe_rollback_entered";
    case TraceCode::HeadlessModeEntered: return "headless_mode_entered";
    case TraceCode::RecoveryAcknowledged: return "recovery_acknowledged";
    case TraceCode::RecoveryAcknowledgementRejected: return "recovery_acknowledgement_rejected";
    case TraceCode::SessionEstablished: return "session_established";
    case TraceCode::SessionRejected: return "session_rejected";
    case TraceCode::EvidenceCreated: return "evidence_created";
    case TraceCode::EvidenceVerificationFailed: return "evidence_verification_failed";
    case TraceCode::EvidenceChainBroken: return "evidence_chain_broken";
    case TraceCode::EvidenceSignatureRejected: return "evidence_signature_rejected";
    case TraceCode::MerkleProofRejected: return "merkle_proof_rejected";
    case TraceCode::DivergenceLocalized: return "divergence_localized";
    case TraceCode::SupportBundleCreated: return "support_bundle_created";
    case TraceCode::SupportBundleVerificationFailed: return "support_bundle_verification_failed";
    case TraceCode::ValidationGateDeferred: return "validation_gate_deferred";
    case TraceCode::TemporalRecordCommitted: return "temporal_record_committed";
    case TraceCode::TemporalRecordRejected: return "temporal_record_rejected";
    case TraceCode::ExternalEffectPrepared: return "external_effect_prepared";
    case TraceCode::ExternalEffectCommitted: return "external_effect_committed";
    case TraceCode::ExternalEffectCompensated: return "external_effect_compensated";
    case TraceCode::ExternalEffectRejected: return "external_effect_rejected";
    }
    return "unknown";
}

} // namespace neoeng::core
