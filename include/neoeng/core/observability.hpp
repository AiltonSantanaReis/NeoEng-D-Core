#pragma once

#include "neoeng/core/hash.hpp"
#include "neoeng/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neoeng::core {

using CorrelationId = std::uint64_t;

enum class TraceCategory : std::uint8_t {
    Input,
    Network,
    Simulation,
    Rollback,
    Budget,
    Recovery,
    Tooling,
};

enum class TraceOutcome : std::uint8_t {
    Accepted,
    Rejected,
    Applied,
    Skipped,
    Degraded,
    Recovered,
    Failed,
};

enum class TraceCode : std::uint16_t {
    None,
    InputAuthenticated,
    InputMalformed,
    InputReplayRejected,
    InputRateLimited,
    StateAdvanced,
    StateDivergence,
    RollbackStarted,
    RollbackCompleted,
    BudgetExceeded,
    DeviceLost,
    IoStall,
    OutOfMemory,
    SafeWaitEntered,
    SafeRollbackEntered,
    HeadlessModeEntered,
};

struct TraceEvent final {
    CorrelationId correlation_id{};
    std::uint64_t sequence{};
    std::uint64_t frame{};
    std::uint64_t monotonic_time_ns{};
    TraceCategory category{TraceCategory::Tooling};
    TraceOutcome outcome{TraceOutcome::Applied};
    TraceCode code{TraceCode::None};
    EntityId entity{};
    std::uint32_t component{};
    std::int64_t measured_value{};
    std::int64_t budget_limit{};

    auto operator<=>(const TraceEvent&) const = default;
};

class TraceBuffer final {
public:
    explicit TraceBuffer(std::size_t capacity);

    void record(TraceEvent event) noexcept;
    [[nodiscard]] std::vector<TraceEvent> snapshot() const;
    [[nodiscard]] std::vector<TraceEvent> by_frame(std::uint64_t frame) const;
    [[nodiscard]] std::vector<TraceEvent> by_correlation(CorrelationId correlation_id) const;
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return events_.size(); }
    [[nodiscard]] std::uint64_t overwritten_events() const noexcept { return overwritten_events_; }

private:
    std::vector<TraceEvent> events_{};
    std::size_t next_index_{};
    std::size_t size_{};
    std::uint64_t next_sequence_{};
    std::uint64_t overwritten_events_{};
};

struct ComponentDiff final {
    EntityId entity{};
    std::string_view component{};
    std::int64_t before_raw{};
    std::int64_t after_raw{};
};

struct FrameDiff final {
    std::uint64_t left_frame{};
    std::uint64_t right_frame{};
    std::uint64_t left_hash{};
    std::uint64_t right_hash{};
    std::vector<ComponentDiff> changes{};

    [[nodiscard]] bool identical() const noexcept {
        return left_hash == right_hash && changes.empty();
    }
};

struct FrameRecord final {
    WorldState state{};
    std::vector<InputCommand> inputs{};
    std::vector<TraceEvent> events{};
    std::uint64_t state_hash{};
};

class TimeTravelDebugger final {
public:
    explicit TimeTravelDebugger(std::size_t frame_capacity);

    void record_frame(
        const WorldState& state,
        std::span<const InputCommand> inputs,
        std::span<const TraceEvent> events = {});

    [[nodiscard]] const FrameRecord* frame(std::uint64_t frame_number) const noexcept;
    [[nodiscard]] const Body* entity(std::uint64_t frame_number, EntityId entity_id) const noexcept;
    [[nodiscard]] FrameDiff compare(std::uint64_t left_frame, std::uint64_t right_frame) const;
    [[nodiscard]] std::vector<TraceEvent> correlate(CorrelationId correlation_id) const;
    [[nodiscard]] std::string export_reproducible_json(
        std::uint64_t seed,
        std::string_view environment_id) const;

    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return records_.size(); }
    [[nodiscard]] std::optional<std::uint64_t> oldest_frame() const noexcept;
    [[nodiscard]] std::optional<std::uint64_t> newest_frame() const noexcept;

private:
    [[nodiscard]] std::size_t logical_index(std::size_t position) const noexcept;

    std::vector<std::optional<FrameRecord>> records_{};
    std::size_t oldest_index_{};
    std::size_t size_{};
};

[[nodiscard]] const char* to_string(TraceCategory category) noexcept;
[[nodiscard]] const char* to_string(TraceOutcome outcome) noexcept;
[[nodiscard]] const char* to_string(TraceCode code) noexcept;

} // namespace neoeng::core
