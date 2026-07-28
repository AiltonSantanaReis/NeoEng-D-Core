#include "neoeng/core/diagnostics.hpp"

#include "neoeng/core/hash.hpp"

#include <algorithm>
#include <limits>

namespace neoeng::core {
namespace {

[[nodiscard]] std::int64_t saturating_i64(std::uint64_t value) noexcept {
    return value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())
        ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(value);
}

[[nodiscard]] std::string_view first_component_difference(
    const Body& expected,
    const Body& actual) noexcept {
    if (expected.id != actual.id) return "identity";
    if (expected.position.x != actual.position.x) return "position.x";
    if (expected.position.y != actual.position.y) return "position.y";
    if (expected.velocity.x != actual.velocity.x) return "velocity.x";
    if (expected.velocity.y != actual.velocity.y) return "velocity.y";
    return {};
}

} // namespace

BudgetEvaluation BudgetMonitor::record(
    const BudgetSample& sample,
    TraceBuffer& traces) const noexcept {
    const bool exceeded = sample.definition.limit_ns != 0U
        && sample.measured_ns > sample.definition.limit_ns;
    traces.record({
        .correlation_id = sample.correlation_id,
        .frame = sample.frame,
        .monotonic_time_ns = sample.monotonic_time_ns,
        .category = TraceCategory::Budget,
        .outcome = exceeded ? TraceOutcome::Failed : TraceOutcome::Accepted,
        .code = exceeded ? TraceCode::BudgetExceeded : TraceCode::BudgetSampled,
        .entity = sample.entity,
        .measured_value = saturating_i64(sample.measured_ns),
        .budget_limit = saturating_i64(sample.definition.limit_ns),
        .subsystem = sample.definition.subsystem,
        .severity = exceeded ? sample.definition.exceed_severity : TraceSeverity::Debug,
        .detail_code = static_cast<std::uint32_t>(sample.definition.id),
    });
    return {
        .exceeded = exceeded,
        .measured_ns = sample.measured_ns,
        .limit_ns = sample.definition.limit_ns,
    };
}

ScopedBudgetMeasurement::ScopedBudgetMeasurement(
    const BudgetMonitor* monitor,
    TraceBuffer* traces,
    BudgetDefinition definition,
    CorrelationId correlation_id,
    std::uint64_t frame,
    EntityId entity) noexcept
    : monitor_(monitor),
      traces_(traces),
      definition_(definition),
      correlation_id_(correlation_id),
      frame_(frame),
      entity_(entity),
      start_(std::chrono::steady_clock::now()) {}

ScopedBudgetMeasurement::~ScopedBudgetMeasurement() {
    if (monitor_ == nullptr || traces_ == nullptr) {
        return;
    }
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start_).count();
    const std::uint64_t measured = elapsed < 0 ? 0U : static_cast<std::uint64_t>(elapsed);
    (void)monitor_->record({
        .definition = definition_,
        .correlation_id = correlation_id_,
        .frame = frame_,
        .entity = entity_,
        .monotonic_time_ns = 0U,
        .measured_ns = measured,
    }, *traces_);
}

StateDivergenceReport diagnose_state_divergence(
    const WorldState& expected,
    const WorldState& actual,
    CorrelationId correlation_id,
    TraceBuffer* traces,
    std::uint64_t monotonic_time_ns,
    std::size_t merkle_chunk_size) {
    const BudgetMonitor budget_monitor;
    ScopedBudgetMeasurement budget_scope(
        traces != nullptr ? &budget_monitor : nullptr,
        traces,
        {
            .id = BudgetId::DivergenceLocalization,
            .subsystem = TraceSubsystem::Evidence,
            .limit_ns = 0U,
            .exceed_severity = TraceSeverity::Warning,
        },
        correlation_id,
        actual.frame);
    StateDivergenceReport report{
        .expected_stable_hash = stable_hash(expected),
        .actual_stable_hash = stable_hash(actual),
        .expected_canonical_sha256 = canonical_state_sha256(expected),
        .actual_canonical_sha256 = canonical_state_sha256(actual),
        .expected_merkle_root = state_merkle_sha256(expected, merkle_chunk_size).root,
        .actual_merkle_root = state_merkle_sha256(actual, merkle_chunk_size).root,
    };
    report.divergent = report.expected_stable_hash != report.actual_stable_hash
        || !sha256_equal(report.expected_canonical_sha256, report.actual_canonical_sha256)
        || !sha256_equal(report.expected_merkle_root, report.actual_merkle_root);
    if (!report.divergent) {
        return report;
    }

    if (expected.frame != actual.frame) {
        report.first_divergent_entity = 0U;
        report.first_divergent_component = "state.frame";
    }

    const std::size_t max_bodies = std::max(expected.bodies.size(), actual.bodies.size());
    for (std::size_t index = 0U;
         report.first_divergent_component.empty() && index < max_bodies;
         ++index) {
        if (index >= expected.bodies.size()) {
            report.first_divergent_entity = actual.bodies[index].id;
            report.first_divergent_component = "entity.added";
            report.first_divergent_chunk = index / merkle_chunk_size;
            break;
        }
        if (index >= actual.bodies.size()) {
            report.first_divergent_entity = expected.bodies[index].id;
            report.first_divergent_component = "entity.removed";
            report.first_divergent_chunk = index / merkle_chunk_size;
            break;
        }
        const std::string_view component = first_component_difference(
            expected.bodies[index], actual.bodies[index]);
        if (!component.empty()) {
            report.first_divergent_entity = expected.bodies[index].id;
            report.first_divergent_component = component;
            report.first_divergent_chunk = index / merkle_chunk_size;
            break;
        }
    }

    if (traces != nullptr) {
        traces->record({
            .correlation_id = correlation_id,
            .frame = actual.frame,
            .monotonic_time_ns = monotonic_time_ns,
            .category = TraceCategory::Simulation,
            .outcome = TraceOutcome::Failed,
            .code = TraceCode::StateDivergence,
            .entity = report.first_divergent_entity.value_or(0U),
            .measured_value = saturating_i64(report.actual_stable_hash),
            .budget_limit = saturating_i64(report.expected_stable_hash),
            .subsystem = TraceSubsystem::Simulation,
            .severity = TraceSeverity::Error,
            .related_hash = report.actual_stable_hash,
            .detail_code = report.first_divergent_chunk.has_value()
                ? static_cast<std::uint32_t>(*report.first_divergent_chunk)
                : 0U,
        });
        if (report.first_divergent_chunk.has_value()) {
            traces->record({
                .correlation_id = correlation_id,
                .frame = actual.frame,
                .monotonic_time_ns = monotonic_time_ns,
                .category = TraceCategory::Evidence,
                .outcome = TraceOutcome::Failed,
                .code = TraceCode::DivergenceLocalized,
                .entity = report.first_divergent_entity.value_or(0U),
                .measured_value = static_cast<std::int64_t>(*report.first_divergent_chunk),
                .subsystem = TraceSubsystem::Evidence,
                .severity = TraceSeverity::Error,
                .related_hash = report.actual_stable_hash,
            });
        }
    }
    return report;
}

const char* to_string(BudgetId id) noexcept {
    switch (id) {
    case BudgetId::InputIngest: return "input_ingest";
    case BudgetId::StateAdvance: return "state_advance";
    case BudgetId::Rollback: return "rollback";
    case BudgetId::EcsMaintenance: return "ecs_maintenance";
    case BudgetId::EvidenceCheckpoint: return "evidence_checkpoint";
    case BudgetId::SupportBundleExport: return "support_bundle_export";
    case BudgetId::ViewLabFrame: return "view_lab_frame";
    case BudgetId::DurableRecorder: return "durable_recorder";
    case BudgetId::ExternalEffectCommit: return "external_effect_commit";
    case BudgetId::DivergenceLocalization: return "divergence_localization";
    }
    return "unknown";
}

} // namespace neoeng::core
