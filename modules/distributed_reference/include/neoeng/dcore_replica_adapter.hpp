#pragma once

#include "neoeng/core/diagnostics.hpp"
#include "neoeng/core/rollback.hpp"
#include "neoeng/distributed_reference.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

namespace neoeng::distributed_reference {

inline constexpr std::uint64_t kDCoreBodyInputSchemaId = 0x4E454F44434F5245ULL;
inline constexpr std::uint16_t kDCoreInputCorrectionVersion = 1U;

class DCoreReplicaAdapter final : public ReplicaAdapter {
public:
    explicit DCoreReplicaAdapter(
        core::WorldState initial,
        std::size_t snapshot_capacity = 300U,
        core::SnapshotStrategy strategy = core::SnapshotStrategy::FullCopy);

    void advance(std::span<const core::InputCommand> inputs);

    [[nodiscard]] const core::WorldState& state() const noexcept {
        return engine_.state();
    }
    [[nodiscard]] core::StateDivergenceReport diagnose_against(
        const DCoreReplicaAdapter& other,
        core::CorrelationId correlation_id = 0U) const;
    [[nodiscard]] StateFingerprint fingerprint() const override;
    [[nodiscard]] std::vector<std::uint8_t> export_authoritative_correction(
        std::uint64_t input_frame) const override;
    [[nodiscard]] ReplicaStatus apply_authoritative_correction(
        std::uint64_t input_frame,
        std::span<const std::uint8_t> correction) noexcept override;

private:
    core::RollbackEngine engine_;
    std::size_t correction_retention_{};
    std::map<std::uint64_t, std::vector<core::InputCommand>> input_history_{};
};

} // namespace neoeng::distributed_reference
