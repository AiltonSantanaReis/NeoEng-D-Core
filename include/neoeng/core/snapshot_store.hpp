#pragma once

#include "neoeng/core/dirty.hpp"
#include "neoeng/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace neoeng::core {

enum class SnapshotStrategy : std::uint8_t {
    FullCopy,
    DeltaLog,
    PagedCopyOnWrite,
    PersistentChunkTree,
    ComponentSoA,
    HybridAdaptive,
};

[[nodiscard]] std::string_view to_string(SnapshotStrategy strategy) noexcept;

enum class SnapshotEncoding : std::uint8_t {
    Full,
    Delta,
    Pages,
    ComponentSoA,
};

[[nodiscard]] std::string_view to_string(SnapshotEncoding encoding) noexcept;

struct SnapshotStoreConfig final {
    SnapshotStrategy strategy{SnapshotStrategy::FullCopy};
    std::size_t capacity{300};
    std::size_t page_bodies{256};
    std::size_t checkpoint_interval{16};
    std::size_t persistent_leaf_bodies{32};
    bool audit_dirty_contract{false};
};

struct SnapshotFeatureVector final {
    std::uint32_t density_ppm{};
    std::size_t touched_pages_16{};
    std::size_t touched_pages_32{};
    std::size_t touched_pages_64{};
    std::size_t touched_pages_128{};
    std::size_t touched_pages_256{};
    std::size_t contiguous_runs{};
    std::size_t longest_run{};
    std::size_t index_span{};
};

struct SnapshotDecisionRecord final {
    std::uint64_t frame{};
    SnapshotEncoding encoding{SnapshotEncoding::Full};
    std::size_t changed_bodies{};
    std::size_t touched_pages{};
    SnapshotFeatureVector features{};
    std::uint64_t selected_cost_bytes{};
    std::uint64_t oracle_cost_bytes{};
    std::uint64_t regret_bytes{};
};

struct SnapshotStoreStats final {
    std::uint64_t payload_bytes_requested{};
    std::uint64_t metadata_bytes_requested{};
    std::uint64_t allocation_count{};
    std::size_t live_payload_bytes{};
    std::size_t live_metadata_bytes{};
    std::size_t peak_live_payload_bytes{};
    std::size_t peak_live_metadata_bytes{};
    std::size_t retained_frames{};
    std::uint64_t dirty_entities_consumed{};
    std::uint64_t comparison_entities_scanned{};
    std::uint64_t full_frames{};
    std::uint64_t delta_frames{};
    std::uint64_t page_frames{};
    std::uint64_t soa_frames{};
    std::uint64_t cumulative_regret_bytes{};
};

class ISnapshotStore {
public:
    virtual ~ISnapshotStore() = default;

    virtual void capture(const WorldState& state, const DirtySet* dirty = nullptr) = 0;
    [[nodiscard]] virtual WorldState restore(std::uint64_t frame) const = 0;
    [[nodiscard]] virtual std::optional<Body> lookup(
        std::uint64_t frame, EntityId entity) const = 0;
    [[nodiscard]] virtual std::uint64_t scan_hash(std::uint64_t frame) const = 0;
    virtual void truncate_after(std::uint64_t frame) = 0;

    [[nodiscard]] virtual bool contains(std::uint64_t frame) const noexcept = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
    [[nodiscard]] virtual std::size_t capacity() const noexcept = 0;
    [[nodiscard]] virtual SnapshotStoreStats stats() const noexcept = 0;
    [[nodiscard]] virtual SnapshotStrategy strategy() const noexcept = 0;
    [[nodiscard]] virtual std::optional<SnapshotDecisionRecord> decision_for(
        std::uint64_t) const noexcept { return std::nullopt; }
};

[[nodiscard]] std::unique_ptr<ISnapshotStore> make_snapshot_store(
    const SnapshotStoreConfig& config);
[[nodiscard]] std::unique_ptr<ISnapshotStore> make_snapshot_store(
    SnapshotStrategy strategy, std::size_t capacity);

} // namespace neoeng::core
