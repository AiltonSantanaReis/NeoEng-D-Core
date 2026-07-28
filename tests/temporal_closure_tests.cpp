#include "neoeng/core/component_world.hpp"
#include "neoeng/core/temporal_contract.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace neoeng::core;

void check(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] Sha256Digest digest(std::string_view value) {
    return sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}

[[nodiscard]] WorldState world(std::uint64_t frame = 7U) {
    return {
        .frame = frame,
        .bodies = {{
            .id = 11U,
            .position = {Fixed::from_raw(101), Fixed::from_raw(202)},
            .velocity = {Fixed::from_raw(303), Fixed::from_raw(404)},
        }},
    };
}

class FakeExecutor final : public ExternalEffectExecutor {
public:
    ExternalEffectApplyResult next_commit{ExternalEffectApplyResult::Applied};
    ExternalEffectApplyResult next_compensation{ExternalEffectApplyResult::Applied};
    std::size_t commit_calls{};
    std::size_t compensation_calls{};

    ExternalEffectApplyResult commit(const ExternalEffectIntent&) override {
        ++commit_calls;
        return next_commit;
    }

    ExternalEffectApplyResult compensate(const ExternalEffectIntent&) override {
        ++compensation_calls;
        return next_compensation;
    }
};

struct TemporaryDirectory final {
    std::filesystem::path path;

    TemporaryDirectory()
        : path(std::filesystem::temp_directory_path()
            / ("neoeng-cs012-" + std::to_string(
                std::chrono::high_resolution_clock::now()
                    .time_since_epoch().count()))) {
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

void test_normative_coverage() {
    const auto fields = canonical_world_v1_fields();
    check(fields.size() == 6U, "WorldState v1 must declare all six canonical fields");
    for (const CanonicalFieldDescriptor& field : fields) {
        check(!field.path.empty(), "canonical field path must be named");
        check(field.covered_by_diff, "every canonical field must be diff-covered");
        check(field.covered_by_canonical_sha256,
            "every canonical field must be canonical-SHA-covered");
        check(field.covered_by_merkle, "every canonical field must be Merkle-covered");
    }

    const auto paths = mandatory_operational_paths_v1();
    check(paths.size() == 9U, "all nine mandatory operational paths must be declared");
    std::set<BudgetId> budgets;
    for (const MandatoryPathDescriptor& path : paths) {
        check(path.automatic_when_tracing_enabled,
            "mandatory path instrumentation must be automatic");
        budgets.insert(path.budget);
    }
    check(budgets.size() == paths.size(), "mandatory paths require distinct budget IDs");
}

void test_all_canonical_fields_localize() {
    const WorldState expected = world();
    const auto assert_component = [&](WorldState actual, std::string_view component) {
        const StateDivergenceReport report =
            diagnose_state_divergence(expected, actual, 91U, nullptr, 0U, 1U);
        check(report.divergent, "changed canonical field must diverge");
        check(report.first_divergent_component == component,
            "changed canonical field must be localized");
    };

    WorldState changed = expected;
    changed.frame += 1U;
    assert_component(changed, "state.frame");
    changed = expected;
    changed.bodies[0].id += 1U;
    assert_component(changed, "identity");
    changed = expected;
    changed.bodies[0].position.x = Fixed::from_raw(102);
    assert_component(changed, "position.x");
    changed = expected;
    changed.bodies[0].position.y = Fixed::from_raw(203);
    assert_component(changed, "position.y");
    changed = expected;
    changed.bodies[0].velocity.x = Fixed::from_raw(304);
    assert_component(changed, "velocity.x");
    changed = expected;
    changed.bodies[0].velocity.y = Fixed::from_raw(405);
    assert_component(changed, "velocity.y");

    TraceBuffer traces(8U);
    (void)diagnose_state_divergence(
        expected, changed, 92U, &traces, 0U, 1U);
    const auto events = traces.by_correlation(92U);
    check(std::any_of(events.begin(), events.end(), [](const TraceEvent& event) {
        return event.code == TraceCode::BudgetSampled
            && event.detail_code
                == static_cast<std::uint32_t>(
                    BudgetId::DivergenceLocalization);
    }), "divergence localization must emit its automatic budget sample");
}

void test_durable_recorder_and_tamper_detection() {
    TemporaryDirectory temporary;
    TraceBuffer traces(32U);
    DurableTimelineRecorder recorder(temporary.path);
    check(recorder.append({
        .branch_id = 1U,
        .first_frame = 1U,
        .last_frame = 1U,
        .timeline_json = "{}",
        .evidence_json = "{}",
    }).reason == DurableRecorderReason::NotRecovered,
        "append before recovery must fail closed");
    check(recorder.recover().accepted(), "empty durable directory must recover");

    const DurableRecorderResult first = recorder.append({
        .branch_id = 1U,
        .first_frame = 1U,
        .last_frame = 7U,
        .timeline_json = R"({"frames":[1,7]})",
        .evidence_json = R"({"head":"a"})",
    }, 101U, &traces);
    const DurableRecorderResult second = recorder.append({
        .branch_id = 2U,
        .first_frame = 7U,
        .last_frame = 9U,
        .timeline_json = R"({"frames":[7,9]})",
        .evidence_json = R"({"head":"b"})",
    }, 102U, &traces);
    check(first.accepted() && second.accepted(),
        std::string("valid durable records must append: first=")
            + to_string(first.reason) + ", second=" + to_string(second.reason));
    check(second.sequence == 1U, "durable sequence must be monotonic");
    check(!sha256_is_zero(second.record_sha256), "durable record must be hashed");
    check(recorder.records()[1].previous_record_hash == first.record_sha256,
        "durable records must form a hash chain");
    check(verify_durable_timeline_directory(temporary.path).accepted(),
        "independent durable verifier must accept intact records");

    DurableTimelineRecorder reopened(temporary.path);
    check(reopened.recover().accepted(), "durable records must recover after restart");
    check(reopened.records().size() == 2U, "recovery must retain every record");
    check(reopened.records()[0].timeline_json == R"({"frames":[1,7]})",
        "recovery must retain exact timeline bytes");

    const auto events = traces.by_correlation(102U);
    check(std::any_of(events.begin(), events.end(), [](const TraceEvent& event) {
        return event.code == TraceCode::TemporalRecordCommitted;
    }), "durable append must be traced");
    check(std::any_of(events.begin(), events.end(), [](const TraceEvent& event) {
        return event.code == TraceCode::BudgetSampled
            && event.detail_code
                == static_cast<std::uint32_t>(BudgetId::DurableRecorder);
    }), "durable append must emit an automatic budget sample");

    const std::filesystem::path first_record =
        temporary.path / "record-00000000000000000000.ndtr";
    std::fstream tamper(first_record, std::ios::binary | std::ios::in | std::ios::out);
    check(static_cast<bool>(tamper), "durable record must be writable for tamper test");
    tamper.seekg(24);
    char byte{};
    tamper.get(byte);
    tamper.seekp(24);
    byte = static_cast<char>(static_cast<unsigned char>(byte) ^ 0x5AU);
    tamper.put(byte);
    tamper.close();
    check(!verify_durable_timeline_directory(temporary.path).accepted(),
        "independent verifier must reject one-byte tampering");
}

void test_external_effect_contract() {
    ExternalEffectLedger ledger;
    FakeExecutor executor;
    TraceBuffer traces(64U);
    const ExternalEffectIntent reversible{
        .idempotency_key = "invoice:42",
        .kind = "invoice.issue",
        .frame = 10U,
        .payload_sha256 = digest("invoice-42"),
        .compensation_supported = true,
    };
    check(ledger.prepare(reversible, 201U, &traces).accepted(),
        "valid effect must prepare");
    check(ledger.prepare(reversible, 201U, &traces).accepted(),
        "identical preparation must be idempotent");
    ExternalEffectIntent conflict = reversible;
    conflict.payload_sha256 = digest("different");
    check(ledger.prepare(conflict).reason
        == ExternalEffectReason::IdempotencyConflict,
        "same key with different intent must fail closed");

    check(ledger.commit("invoice:42", 9U, executor).reason
        == ExternalEffectReason::NotConfirmed,
        "effect cannot commit before its frame is confirmed");
    check(executor.commit_calls == 0U,
        "unconfirmed commit must not invoke the host executor");
    const ExternalEffectDecision committed =
        ledger.commit("invoice:42", 10U, executor, 202U, &traces);
    check(committed.accepted() && committed.executor_invoked,
        "confirmed effect must commit through the host");
    check(executor.commit_calls == 1U, "host commit must execute exactly once");
    check(ledger.commit("invoice:42", 10U, executor).accepted(),
        "repeated commit must be idempotent");
    check(executor.commit_calls == 1U,
        "repeated commit must not invoke the host again");
    check(ledger.compensate("invoice:42", executor).accepted(),
        "declared compensation must execute");
    check(executor.compensation_calls == 1U,
        "compensation must execute exactly once");

    const ExternalEffectIntent irreversible{
        .idempotency_key = "email:43",
        .kind = "email.send",
        .frame = 20U,
        .payload_sha256 = digest("email-43"),
        .compensation_supported = false,
    };
    const ExternalEffectIntent future{
        .idempotency_key = "draft:44",
        .kind = "draft.save",
        .frame = 21U,
        .payload_sha256 = digest("draft-44"),
        .compensation_supported = true,
    };
    check(ledger.prepare(irreversible).accepted(), "irreversible effect must prepare");
    check(ledger.commit("email:43", 20U, executor).accepted(),
        "irreversible effect may commit only after confirmation");
    check(ledger.prepare(future).accepted(), "future effect must prepare");
    const ExternalEffectRollbackDecision rollback =
        ledger.reconcile_rollback(15U, 203U, &traces);
    check(!rollback.safe(), "rollback across committed external effect must be explicit");
    check(rollback.reason
        == ExternalEffectReason::CommittedEffectCrossedRollback,
        "rollback must report irreversible boundary");
    check(rollback.committed_effects_after_frame == 1U,
        "rollback must count committed effects beyond restored frame");
    check(rollback.prepared_effects_discarded == 1U,
        "rollback must discard only uncommitted future intents");

    const auto commit_events = traces.by_correlation(202U);
    check(std::any_of(commit_events.begin(), commit_events.end(),
        [](const TraceEvent& event) {
            return event.code == TraceCode::BudgetSampled
                && event.detail_code
                    == static_cast<std::uint32_t>(
                        BudgetId::ExternalEffectCommit);
        }), "external commit must emit an automatic budget sample");
}

void test_ecs_automatic_budget() {
    const WorldState initial = world();
    const ComponentWorldState component = make_component_world(initial);
    const DeterministicActiveSet active({0U});
    BudgetMonitor monitor;
    TraceBuffer traces(8U);
    const ComponentStepResult result = step_component_active(
        component, active, {},
        {
            .kernel_mode = FixedKernelMode::Scalar,
            .budget_monitor = &monitor,
            .budget_traces = &traces,
            .correlation_id = 301U,
            .budget_limit_ns = 0U,
        });
    check(result.state.frame() == initial.frame + 1U, "ECS step must advance");
    const auto events = traces.by_correlation(301U);
    check(events.size() == 1U, "ECS instrumentation must emit one budget sample");
    check(events.front().detail_code
        == static_cast<std::uint32_t>(BudgetId::EcsMaintenance),
        "ECS instrumentation must use its declared budget ID");
}

} // namespace

int main() {
    try {
        test_normative_coverage();
        test_all_canonical_fields_localize();
        test_durable_recorder_and_tamper_detection();
        test_external_effect_contract();
        test_ecs_automatic_budget();
        std::cout << "temporal_closure_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "temporal_closure_tests_error=" << error.what() << '\n';
        return 1;
    }
}
