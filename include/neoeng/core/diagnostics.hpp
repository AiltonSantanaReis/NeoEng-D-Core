#pragma once

#include "neoeng/core/observability.hpp"
#include "neoeng/core/state_evidence.hpp"
#include "neoeng/core/types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace neoeng::core {

enum class BudgetId : std::uint16_t {
    InputIngest = 1U,
    StateAdvance = 2U,
    Rollback = 3U,
    EcsMaintenance = 4U,
    EvidenceCheckpoint = 5U,
    SupportBundleExport = 6U,
    ViewLabFrame = 7U,
    DurableRecorder = 8U,
    ExternalEffectCommit = 9U,
    DivergenceLocalization = 10U,
};

struct BudgetDefinition final {
    BudgetId id{BudgetId::InputIngest};
    TraceSubsystem subsystem{TraceSubsystem::Unknown};
    std::uint64_t limit_ns{};
    TraceSeverity exceed_severity{TraceSeverity::Warning};
};

struct BudgetSample final {
    BudgetDefinition definition{};
    CorrelationId correlation_id{};
    std::uint64_t frame{};
    EntityId entity{};
    std::uint64_t monotonic_time_ns{};
    std::uint64_t measured_ns{};
};

struct BudgetEvaluation final {
    bool exceeded{};
    std::uint64_t measured_ns{};
    std::uint64_t limit_ns{};
};

class BudgetMonitor final {
public:
    [[nodiscard]] BudgetEvaluation record(
        const BudgetSample& sample,
        TraceBuffer& traces) const noexcept;
};

class ScopedBudgetMeasurement final {
public:
    ScopedBudgetMeasurement(
        const BudgetMonitor* monitor,
        TraceBuffer* traces,
        BudgetDefinition definition,
        CorrelationId correlation_id,
        std::uint64_t frame,
        EntityId entity = 0U) noexcept;
    ~ScopedBudgetMeasurement();

    ScopedBudgetMeasurement(const ScopedBudgetMeasurement&) = delete;
    ScopedBudgetMeasurement& operator=(const ScopedBudgetMeasurement&) = delete;
    ScopedBudgetMeasurement(ScopedBudgetMeasurement&&) = delete;
    ScopedBudgetMeasurement& operator=(ScopedBudgetMeasurement&&) = delete;

private:
    const BudgetMonitor* monitor_{};
    TraceBuffer* traces_{};
    BudgetDefinition definition_{};
    CorrelationId correlation_id_{};
    std::uint64_t frame_{};
    EntityId entity_{};
    std::chrono::steady_clock::time_point start_{};
};

struct StateDivergenceReport final {
    bool divergent{};
    std::uint64_t expected_stable_hash{};
    std::uint64_t actual_stable_hash{};
    Sha256Digest expected_canonical_sha256{};
    Sha256Digest actual_canonical_sha256{};
    Sha256Digest expected_merkle_root{};
    Sha256Digest actual_merkle_root{};
    std::optional<std::size_t> first_divergent_chunk{};
    std::optional<EntityId> first_divergent_entity{};
    std::string_view first_divergent_component{};
};

[[nodiscard]] StateDivergenceReport diagnose_state_divergence(
    const WorldState& expected,
    const WorldState& actual,
    CorrelationId correlation_id,
    TraceBuffer* traces = nullptr,
    std::uint64_t monotonic_time_ns = 0U,
    std::size_t merkle_chunk_size = kDefaultStateMerkleChunkBodies);

[[nodiscard]] const char* to_string(BudgetId id) noexcept;

} // namespace neoeng::core
