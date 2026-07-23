#include "neoeng/core/snapshot_store.hpp"

#include "neoeng/core/hash.hpp"
#include "neoeng/core/simulation.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace neoeng::core {
namespace {

class MemoryLedger final {
public:
    void allocate_payload(std::size_t bytes) noexcept {
        payload_requested_ += bytes;
        live_payload_ += bytes;
        peak_payload_ = std::max(peak_payload_, live_payload_);
        ++allocations_;
    }
    void release_payload(std::size_t bytes) noexcept { live_payload_ -= bytes; }
    void allocate_metadata(std::size_t bytes) noexcept {
        metadata_requested_ += bytes;
        live_metadata_ += bytes;
        peak_metadata_ = std::max(peak_metadata_, live_metadata_);
        ++allocations_;
    }
    void release_metadata(std::size_t bytes) noexcept { live_metadata_ -= bytes; }
    void consume_dirty(std::size_t count) noexcept { dirty_entities_consumed_ += count; }
    void scan_entities(std::size_t count) noexcept { comparison_entities_scanned_ += count; }
    void record(SnapshotEncoding encoding, std::uint64_t regret = 0U) noexcept {
        switch (encoding) {
        case SnapshotEncoding::Full: ++full_frames_; break;
        case SnapshotEncoding::Delta: ++delta_frames_; break;
        case SnapshotEncoding::Pages: ++page_frames_; break;
        case SnapshotEncoding::ComponentSoA: ++soa_frames_; break;
        }
        cumulative_regret_bytes_ += regret;
    }
    [[nodiscard]] SnapshotStoreStats snapshot(std::size_t retained_frames) const noexcept {
        return SnapshotStoreStats{
            .payload_bytes_requested = payload_requested_,
            .metadata_bytes_requested = metadata_requested_,
            .allocation_count = allocations_,
            .live_payload_bytes = live_payload_,
            .live_metadata_bytes = live_metadata_,
            .peak_live_payload_bytes = peak_payload_,
            .peak_live_metadata_bytes = peak_metadata_,
            .retained_frames = retained_frames,
            .dirty_entities_consumed = dirty_entities_consumed_,
            .comparison_entities_scanned = comparison_entities_scanned_,
            .full_frames = full_frames_,
            .delta_frames = delta_frames_,
            .page_frames = page_frames_,
            .soa_frames = soa_frames_,
            .cumulative_regret_bytes = cumulative_regret_bytes_,
        };
    }

private:
    std::uint64_t payload_requested_{};
    std::uint64_t metadata_requested_{};
    std::uint64_t allocations_{};
    std::size_t live_payload_{};
    std::size_t live_metadata_{};
    std::size_t peak_payload_{};
    std::size_t peak_metadata_{};
    std::uint64_t dirty_entities_consumed_{};
    std::uint64_t comparison_entities_scanned_{};
    std::uint64_t full_frames_{};
    std::uint64_t delta_frames_{};
    std::uint64_t page_frames_{};
    std::uint64_t soa_frames_{};
    std::uint64_t cumulative_regret_bytes_{};
};

void validate_config(const SnapshotStoreConfig& config) {
    if (config.capacity == 0U) {
        throw std::invalid_argument("Snapshot capacity must be greater than zero");
    }
    if (config.page_bodies == 0U || config.persistent_leaf_bodies == 0U) {
        throw std::invalid_argument("Snapshot page and leaf sizes must be greater than zero");
    }
    if (config.checkpoint_interval == 0U) {
        throw std::invalid_argument("Checkpoint interval must be greater than zero");
    }
}

void require_next_frame(std::optional<std::uint64_t> latest, std::uint64_t incoming) {
    if (latest.has_value() && incoming != *latest + 1U) {
        throw std::invalid_argument("Snapshots must be captured for consecutive frames");
    }
}

[[nodiscard]] const Body* find_body(const std::vector<Body>& bodies, EntityId entity) noexcept {
    const auto iterator = std::lower_bound(bodies.begin(), bodies.end(), entity,
        [](const Body& body, EntityId id) { return body.id < id; });
    return iterator != bodies.end() && iterator->id == entity ? &*iterator : nullptr;
}

[[nodiscard]] std::size_t world_payload_bytes(const WorldState& state) noexcept {
    return state.bodies.capacity() * sizeof(Body);
}

[[nodiscard]] std::size_t touched_page_count(
    const std::vector<std::size_t>& indices, std::size_t page_size) noexcept {
    if (indices.empty()) return 0U;
    std::size_t count = 1U;
    std::size_t previous = indices.front() / page_size;
    for (std::size_t position = 1; position < indices.size(); ++position) {
        const std::size_t page = indices[position] / page_size;
        if (page != previous) {
            ++count;
            previous = page;
        }
    }
    return count;
}

[[nodiscard]] SnapshotFeatureVector make_features(
    const std::vector<std::size_t>& indices, std::size_t entity_count) noexcept {
    SnapshotFeatureVector features;
    if (entity_count != 0U) {
        features.density_ppm = static_cast<std::uint32_t>(
            indices.size() * 1'000'000ULL / entity_count);
    }
    features.touched_pages_16 = touched_page_count(indices, 16U);
    features.touched_pages_32 = touched_page_count(indices, 32U);
    features.touched_pages_64 = touched_page_count(indices, 64U);
    features.touched_pages_128 = touched_page_count(indices, 128U);
    features.touched_pages_256 = touched_page_count(indices, 256U);
    if (!indices.empty()) {
        features.contiguous_runs = 1U;
        features.longest_run = 1U;
        std::size_t current_run = 1U;
        for (std::size_t position = 1; position < indices.size(); ++position) {
            if (indices[position] == indices[position - 1U] + 1U) {
                ++current_run;
                features.longest_run = std::max(features.longest_run, current_run);
            } else {
                ++features.contiguous_runs;
                current_run = 1U;
            }
        }
        features.index_span = indices.back() - indices.front() + 1U;
    }
    return features;
}

[[nodiscard]] std::vector<std::size_t> changed_indices(
    const WorldState& previous,
    const WorldState& state,
    const DirtySet* dirty,
    bool audit,
    MemoryLedger& ledger) {
    if (previous.bodies.size() != state.bodies.size()) {
        throw std::invalid_argument("Versioned stores currently require a stable body count");
    }
    std::vector<std::size_t> indices;
    if (dirty != nullptr) {
        if (dirty->entity_count() != state.bodies.size()) {
            throw std::invalid_argument("DirtySet entity count does not match the world");
        }
        if (audit && !dirty_set_describes_transition(previous, state, *dirty)) {
            throw std::invalid_argument("DirtySet under-reports or misclassifies a transition");
        }
        indices.reserve(dirty->changed_count());
        dirty->for_each_dirty([&indices](std::size_t index, std::uint8_t) {
            indices.push_back(index);
        });
        ledger.consume_dirty(indices.size());
        return indices;
    }

    ledger.scan_entities(state.bodies.size());
    for (std::size_t index = 0; index < state.bodies.size(); ++index) {
        if (state.bodies[index] != previous.bodies[index]) {
            indices.push_back(index);
        }
    }
    return indices;
}

class FullCopyStore final : public ISnapshotStore {
public:
    explicit FullCopyStore(SnapshotStoreConfig config) : config_(config) { validate_config(config_); }

    void capture(const WorldState& state, const DirtySet*) override {
        validate_world(state);
        require_next_frame(latest_frame(), state.frame);
        WorldState copy = state;
        copy.bodies.shrink_to_fit();
        ledger_.allocate_payload(world_payload_bytes(copy));
        ledger_.allocate_metadata(sizeof(WorldState));
        ledger_.record(SnapshotEncoding::Full);
        snapshots_.push_back(std::move(copy));
        while (snapshots_.size() > config_.capacity) {
            release(snapshots_.front());
            snapshots_.pop_front();
        }
    }

    [[nodiscard]] WorldState restore(std::uint64_t frame) const override {
        const WorldState* state = find(frame);
        if (state == nullptr) throw std::out_of_range("Snapshot frame is not retained");
        return *state;
    }
    [[nodiscard]] std::optional<Body> lookup(std::uint64_t frame, EntityId entity) const override {
        const WorldState* state = find(frame);
        if (state == nullptr) return std::nullopt;
        const Body* body = find_body(state->bodies, entity);
        return body == nullptr ? std::nullopt : std::optional<Body>{*body};
    }
    [[nodiscard]] std::uint64_t scan_hash(std::uint64_t frame) const override {
        return stable_hash(restore(frame));
    }
    void truncate_after(std::uint64_t frame) override {
        while (!snapshots_.empty() && snapshots_.back().frame > frame) {
            release(snapshots_.back());
            snapshots_.pop_back();
        }
    }
    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept override { return find(frame) != nullptr; }
    [[nodiscard]] std::size_t size() const noexcept override { return snapshots_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept override { return config_.capacity; }
    [[nodiscard]] SnapshotStoreStats stats() const noexcept override { return ledger_.snapshot(size()); }
    [[nodiscard]] SnapshotStrategy strategy() const noexcept override { return SnapshotStrategy::FullCopy; }

private:
    [[nodiscard]] std::optional<std::uint64_t> latest_frame() const noexcept {
        return snapshots_.empty() ? std::nullopt : std::optional<std::uint64_t>{snapshots_.back().frame};
    }
    [[nodiscard]] const WorldState* find(std::uint64_t frame) const noexcept {
        for (auto iterator = snapshots_.rbegin(); iterator != snapshots_.rend(); ++iterator) {
            if (iterator->frame == frame) return &*iterator;
            if (iterator->frame < frame) break;
        }
        return nullptr;
    }
    void release(const WorldState& state) noexcept {
        ledger_.release_payload(world_payload_bytes(state));
        ledger_.release_metadata(sizeof(WorldState));
    }
    SnapshotStoreConfig config_;
    std::deque<WorldState> snapshots_;
    MemoryLedger ledger_;
};

struct BodyPatch final { std::uint32_t index{}; Body body{}; };

struct DeltaEntry final {
    std::uint64_t frame{};
    bool checkpoint{};
    WorldState state{};
    std::vector<BodyPatch> patches{};
    std::size_t payload_bytes{};
    std::size_t metadata_bytes{};
    SnapshotDecisionRecord decision{};
};

class DeltaCheckpointStore final : public ISnapshotStore {
public:
    explicit DeltaCheckpointStore(SnapshotStoreConfig config) : config_(config) { validate_config(config_); }

    void capture(const WorldState& state, const DirtySet* dirty) override {
        validate_world(state);
        require_next_frame(latest_frame(), state.frame);
        if (entries_.empty()) {
            latest_ = state;
            add_checkpoint(state, SnapshotDecisionRecord{
                .frame = state.frame,
                .encoding = SnapshotEncoding::Full,
                .changed_bodies = state.bodies.size(),
                .selected_cost_bytes = state.bodies.size() * sizeof(Body),
                .oracle_cost_bytes = state.bodies.size() * sizeof(Body),
            });
            return;
        }

        const std::vector<std::size_t> indices = changed_indices(
            latest_, state, dirty, config_.audit_dirty_contract, ledger_);
        const bool checkpoint = state.frame % config_.checkpoint_interval == 0U;
        if (checkpoint) {
            add_checkpoint(state, SnapshotDecisionRecord{
                .frame = state.frame,
                .encoding = SnapshotEncoding::Full,
                .changed_bodies = indices.size(),
                .selected_cost_bytes = state.bodies.size() * sizeof(Body),
                .oracle_cost_bytes = std::min<std::uint64_t>(
                    state.bodies.size() * sizeof(Body), indices.size() * sizeof(BodyPatch)),
            });
        } else {
            DeltaEntry entry;
            entry.frame = state.frame;
            entry.checkpoint = false;
            entry.patches.reserve(indices.size());
            for (const std::size_t index : indices) {
                if (index > std::numeric_limits<std::uint32_t>::max()) {
                    throw std::overflow_error("Body index exceeds patch representation");
                }
                entry.patches.push_back(BodyPatch{
                    .index = static_cast<std::uint32_t>(index), .body = state.bodies[index]});
            }
            entry.patches.shrink_to_fit();
            entry.payload_bytes = entry.patches.capacity() * sizeof(BodyPatch);
            entry.metadata_bytes = sizeof(DeltaEntry);
            entry.decision = SnapshotDecisionRecord{
                .frame = state.frame,
                .encoding = SnapshotEncoding::Delta,
                .changed_bodies = indices.size(),
                .selected_cost_bytes = entry.payload_bytes,
                .oracle_cost_bytes = std::min<std::uint64_t>(
                    state.bodies.size() * sizeof(Body), entry.payload_bytes),
            };
            ledger_.allocate_payload(entry.payload_bytes);
            ledger_.allocate_metadata(entry.metadata_bytes);
            ledger_.record(SnapshotEncoding::Delta);
            entries_.push_back(std::move(entry));
        }
        latest_ = state;
        enforce_capacity();
    }

    [[nodiscard]] WorldState restore(std::uint64_t frame) const override {
        if (!contains(frame)) throw std::out_of_range("Snapshot frame is not retained");
        std::size_t checkpoint_index = 0U;
        bool found = false;
        for (std::size_t index = 0; index < entries_.size(); ++index) {
            if (entries_[index].frame > frame) break;
            if (entries_[index].checkpoint) {
                checkpoint_index = index;
                found = true;
            }
        }
        if (!found) throw std::logic_error("Delta store has no retained checkpoint");
        WorldState restored = entries_[checkpoint_index].state;
        for (std::size_t index = checkpoint_index + 1U; index < entries_.size(); ++index) {
            const DeltaEntry& entry = entries_[index];
            if (entry.frame > frame) break;
            if (entry.checkpoint) {
                restored = entry.state;
            } else {
                for (const BodyPatch& patch : entry.patches) {
                    restored.bodies[patch.index] = patch.body;
                }
                restored.frame = entry.frame;
            }
        }
        return restored;
    }
    [[nodiscard]] std::optional<Body> lookup(std::uint64_t frame, EntityId entity) const override {
        if (!contains(frame)) return std::nullopt;
        const WorldState state = restore(frame);
        const Body* body = find_body(state.bodies, entity);
        return body == nullptr ? std::nullopt : std::optional<Body>{*body};
    }
    [[nodiscard]] std::uint64_t scan_hash(std::uint64_t frame) const override { return stable_hash(restore(frame)); }
    void truncate_after(std::uint64_t frame) override {
        while (!entries_.empty() && entries_.back().frame > frame) {
            release(entries_.back());
            entries_.pop_back();
        }
        if (!entries_.empty()) latest_ = restore(entries_.back().frame);
    }
    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept override {
        return !entries_.empty() && frame >= entries_.front().frame && frame <= entries_.back().frame;
    }
    [[nodiscard]] std::size_t size() const noexcept override { return entries_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept override { return config_.capacity; }
    [[nodiscard]] SnapshotStoreStats stats() const noexcept override { return ledger_.snapshot(size()); }
    [[nodiscard]] SnapshotStrategy strategy() const noexcept override { return SnapshotStrategy::DeltaLog; }
    [[nodiscard]] std::optional<SnapshotDecisionRecord> decision_for(std::uint64_t frame) const noexcept override {
        for (const DeltaEntry& entry : entries_) if (entry.frame == frame) return entry.decision;
        return std::nullopt;
    }

private:
    void add_checkpoint(const WorldState& state, SnapshotDecisionRecord decision) {
        DeltaEntry entry;
        entry.frame = state.frame;
        entry.checkpoint = true;
        entry.state = state;
        entry.state.bodies.shrink_to_fit();
        entry.payload_bytes = world_payload_bytes(entry.state);
        entry.metadata_bytes = sizeof(DeltaEntry);
        entry.decision = decision;
        ledger_.allocate_payload(entry.payload_bytes);
        ledger_.allocate_metadata(entry.metadata_bytes);
        ledger_.record(SnapshotEncoding::Full);
        entries_.push_back(std::move(entry));
        enforce_capacity();
    }
    void enforce_capacity() {
        while (entries_.size() > config_.capacity) {
            if (entries_.size() >= 2U && !entries_[1].checkpoint) {
                const WorldState promoted = restore(entries_[1].frame);
                const SnapshotDecisionRecord decision = entries_[1].decision;
                release(entries_[1]);
                entries_[1] = DeltaEntry{};
                entries_[1].frame = promoted.frame;
                entries_[1].checkpoint = true;
                entries_[1].state = promoted;
                entries_[1].state.bodies.shrink_to_fit();
                entries_[1].payload_bytes = world_payload_bytes(entries_[1].state);
                entries_[1].metadata_bytes = sizeof(DeltaEntry);
                entries_[1].decision = decision;
                ledger_.allocate_payload(entries_[1].payload_bytes);
                ledger_.allocate_metadata(entries_[1].metadata_bytes);
            }
            release(entries_.front());
            entries_.pop_front();
        }
    }
    void release(const DeltaEntry& entry) noexcept {
        ledger_.release_payload(entry.payload_bytes);
        ledger_.release_metadata(entry.metadata_bytes);
    }
    [[nodiscard]] std::optional<std::uint64_t> latest_frame() const noexcept {
        return entries_.empty() ? std::nullopt : std::optional<std::uint64_t>{entries_.back().frame};
    }
    SnapshotStoreConfig config_;
    std::deque<DeltaEntry> entries_;
    WorldState latest_;
    MemoryLedger ledger_;
};

template <typename T>
struct TrackedVector final {
    TrackedVector(std::shared_ptr<MemoryLedger> ledger, std::vector<T> values)
        : ledger_(std::move(ledger)), values_(std::move(values)) {
        values_.shrink_to_fit();
        payload_bytes_ = values_.capacity() * sizeof(T);
        metadata_bytes_ = sizeof(TrackedVector<T>);
        ledger_->allocate_payload(payload_bytes_);
        ledger_->allocate_metadata(metadata_bytes_);
    }
    ~TrackedVector() {
        ledger_->release_payload(payload_bytes_);
        ledger_->release_metadata(metadata_bytes_);
    }
    std::shared_ptr<MemoryLedger> ledger_;
    std::vector<T> values_;
    std::size_t payload_bytes_{};
    std::size_t metadata_bytes_{};
};

using BodyPage = TrackedVector<Body>;

struct PageSnapshot final {
    std::uint64_t frame{};
    std::size_t body_count{};
    std::vector<std::shared_ptr<const BodyPage>> pages{};
    std::size_t metadata_bytes{};
    SnapshotDecisionRecord decision{};
};

class PagedStore final : public ISnapshotStore {
public:
    PagedStore(SnapshotStoreConfig config, SnapshotStrategy strategy, std::size_t page_size)
        : config_(config), strategy_(strategy), page_size_(page_size),
          ledger_(std::make_shared<MemoryLedger>()) { validate_config(config_); }

    void capture(const WorldState& state, const DirtySet* dirty) override {
        validate_world(state);
        require_next_frame(latest_frame(), state.frame);
        if (!snapshots_.empty() && snapshots_.back().body_count != state.bodies.size()) {
            throw std::invalid_argument("Paged stores currently require a stable body count");
        }
        if (dirty != nullptr && config_.audit_dirty_contract && !latest_.bodies.empty()
            && !dirty_set_describes_transition(latest_, state, *dirty)) {
            throw std::invalid_argument("DirtySet under-reports or misclassifies a transition");
        }

        const std::size_t page_count = (state.bodies.size() + page_size_ - 1U) / page_size_;
        std::vector<bool> touched(page_count, snapshots_.empty());
        std::size_t changed_count = state.bodies.size();
        if (!snapshots_.empty()) {
            if (dirty != nullptr) {
                if (dirty->entity_count() != state.bodies.size()) {
                    throw std::invalid_argument("DirtySet entity count does not match the world");
                }
                changed_count = dirty->changed_count();
                dirty->for_each_dirty([&](std::size_t index, std::uint8_t) {
                    touched[index / page_size_] = true;
                });
                ledger_->consume_dirty(changed_count);
            } else {
                changed_count = 0U;
                ledger_->scan_entities(state.bodies.size());
                for (std::size_t index = 0; index < state.bodies.size(); ++index) {
                    if (state.bodies[index] != latest_.bodies[index]) {
                        touched[index / page_size_] = true;
                        ++changed_count;
                    }
                }
            }
        }

        PageSnapshot snapshot;
        snapshot.frame = state.frame;
        snapshot.body_count = state.bodies.size();
        snapshot.pages.reserve(page_count);
        std::size_t touched_count = 0U;
        for (std::size_t page = 0; page < page_count; ++page) {
            if (!touched[page]) {
                snapshot.pages.push_back(snapshots_.back().pages[page]);
                continue;
            }
            ++touched_count;
            const std::size_t begin = page * page_size_;
            const std::size_t end = std::min(begin + page_size_, state.bodies.size());
            std::vector<Body> values(state.bodies.begin() + static_cast<std::ptrdiff_t>(begin),
                                     state.bodies.begin() + static_cast<std::ptrdiff_t>(end));
            snapshot.pages.push_back(std::make_shared<const BodyPage>(ledger_, std::move(values)));
        }
        snapshot.metadata_bytes = sizeof(PageSnapshot)
            + snapshot.pages.capacity() * sizeof(std::shared_ptr<const BodyPage>);
        ledger_->allocate_metadata(snapshot.metadata_bytes);
        const SnapshotEncoding encoding = snapshots_.empty() ? SnapshotEncoding::Full : SnapshotEncoding::Pages;
        snapshot.decision = SnapshotDecisionRecord{
            .frame = state.frame,
            .encoding = encoding,
            .changed_bodies = changed_count,
            .touched_pages = touched_count,
            .selected_cost_bytes = touched_count * page_size_ * sizeof(Body),
            .oracle_cost_bytes = std::min<std::uint64_t>(
                state.bodies.size() * sizeof(Body), changed_count * sizeof(BodyPatch)),
        };
        ledger_->record(encoding);
        snapshots_.push_back(std::move(snapshot));
        latest_ = state;
        while (snapshots_.size() > config_.capacity) {
            release(snapshots_.front());
            snapshots_.pop_front();
        }
    }

    [[nodiscard]] WorldState restore(std::uint64_t frame) const override {
        const PageSnapshot* snapshot = find(frame);
        if (snapshot == nullptr) throw std::out_of_range("Snapshot frame is not retained");
        WorldState state;
        state.frame = frame;
        state.bodies.reserve(snapshot->body_count);
        for (const auto& page : snapshot->pages) {
            state.bodies.insert(state.bodies.end(), page->values_.begin(), page->values_.end());
        }
        return state;
    }
    [[nodiscard]] std::optional<Body> lookup(std::uint64_t frame, EntityId entity) const override {
        const PageSnapshot* snapshot = find(frame);
        if (snapshot == nullptr) return std::nullopt;
        for (const auto& page : snapshot->pages) {
            if (page->values_.empty() || page->values_.back().id < entity) continue;
            const Body* body = find_body(page->values_, entity);
            return body == nullptr ? std::nullopt : std::optional<Body>{*body};
        }
        return std::nullopt;
    }
    [[nodiscard]] std::uint64_t scan_hash(std::uint64_t frame) const override {
        const PageSnapshot* snapshot = find(frame);
        if (snapshot == nullptr) throw std::out_of_range("Snapshot frame is not retained");
        WorldHashBuilder builder(frame, snapshot->body_count);
        for (const auto& page : snapshot->pages) for (const Body& body : page->values_) builder.append(body);
        return builder.value();
    }
    void truncate_after(std::uint64_t frame) override {
        while (!snapshots_.empty() && snapshots_.back().frame > frame) {
            release(snapshots_.back());
            snapshots_.pop_back();
        }
        if (!snapshots_.empty()) latest_ = restore(snapshots_.back().frame);
    }
    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept override { return find(frame) != nullptr; }
    [[nodiscard]] std::size_t size() const noexcept override { return snapshots_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept override { return config_.capacity; }
    [[nodiscard]] SnapshotStoreStats stats() const noexcept override { return ledger_->snapshot(size()); }
    [[nodiscard]] SnapshotStrategy strategy() const noexcept override { return strategy_; }
    [[nodiscard]] std::optional<SnapshotDecisionRecord> decision_for(std::uint64_t frame) const noexcept override {
        const PageSnapshot* snapshot = find(frame);
        return snapshot == nullptr ? std::nullopt : std::optional<SnapshotDecisionRecord>{snapshot->decision};
    }

private:
    [[nodiscard]] std::optional<std::uint64_t> latest_frame() const noexcept {
        return snapshots_.empty() ? std::nullopt : std::optional<std::uint64_t>{snapshots_.back().frame};
    }
    [[nodiscard]] const PageSnapshot* find(std::uint64_t frame) const noexcept {
        for (auto iterator = snapshots_.rbegin(); iterator != snapshots_.rend(); ++iterator) {
            if (iterator->frame == frame) return &*iterator;
            if (iterator->frame < frame) break;
        }
        return nullptr;
    }
    void release(const PageSnapshot& snapshot) noexcept { ledger_->release_metadata(snapshot.metadata_bytes); }
    SnapshotStoreConfig config_;
    SnapshotStrategy strategy_;
    std::size_t page_size_;
    std::deque<PageSnapshot> snapshots_;
    WorldState latest_;
    std::shared_ptr<MemoryLedger> ledger_;
};

template <typename T>
using TrackedArrayPtr = std::shared_ptr<const TrackedVector<T>>;

struct SoASnapshot final {
    std::uint64_t frame{};
    std::size_t count{};
    TrackedArrayPtr<EntityId> ids{};
    TrackedArrayPtr<Fixed> px{};
    TrackedArrayPtr<Fixed> py{};
    TrackedArrayPtr<Fixed> vx{};
    TrackedArrayPtr<Fixed> vy{};
    std::size_t metadata_bytes{};
    SnapshotDecisionRecord decision{};
};

class ComponentSoAStore final : public ISnapshotStore {
public:
    explicit ComponentSoAStore(SnapshotStoreConfig config)
        : config_(config), ledger_(std::make_shared<MemoryLedger>()) { validate_config(config_); }

    void capture(const WorldState& state, const DirtySet* dirty) override {
        validate_world(state);
        require_next_frame(latest_frame(), state.frame);
        if (!snapshots_.empty() && snapshots_.back().count != state.bodies.size()) {
            throw std::invalid_argument("ComponentSoAStore currently requires a stable body count");
        }
        if (dirty != nullptr && config_.audit_dirty_contract && !latest_.bodies.empty()
            && !dirty_set_describes_transition(latest_, state, *dirty)) {
            throw std::invalid_argument("DirtySet under-reports or misclassifies a transition");
        }

        SoASnapshot snapshot;
        snapshot.frame = state.frame;
        snapshot.count = state.bodies.size();
        std::uint8_t aggregate = component_mask(DirtyComponent::All);
        std::size_t changed_count = state.bodies.size();
        if (!snapshots_.empty()) {
            aggregate = 0U;
            if (dirty != nullptr) {
                if (dirty->entity_count() != state.bodies.size()) {
                    throw std::invalid_argument("DirtySet entity count does not match the world");
                }
                changed_count = dirty->changed_count();
                dirty->for_each_dirty([&](std::size_t, std::uint8_t mask) { aggregate |= mask; });
                ledger_->consume_dirty(changed_count);
            } else {
                changed_count = 0U;
                ledger_->scan_entities(state.bodies.size());
                for (std::size_t index = 0; index < state.bodies.size(); ++index) {
                    const Body& before = latest_.bodies[index];
                    const Body& after = state.bodies[index];
                    if (before == after) continue;
                    ++changed_count;
                    if (before.id != after.id) aggregate |= component_mask(DirtyComponent::Identity);
                    if (before.position.x != after.position.x) aggregate |= component_mask(DirtyComponent::PositionX);
                    if (before.position.y != after.position.y) aggregate |= component_mask(DirtyComponent::PositionY);
                    if (before.velocity.x != after.velocity.x) aggregate |= component_mask(DirtyComponent::VelocityX);
                    if (before.velocity.y != after.velocity.y) aggregate |= component_mask(DirtyComponent::VelocityY);
                }
            }
        }

        const SoASnapshot* previous = snapshots_.empty() ? nullptr : &snapshots_.back();
        snapshot.ids = make_component<EntityId>(state, previous == nullptr ? nullptr : previous->ids,
            aggregate, DirtyComponent::Identity, [](const Body& body) { return body.id; });
        snapshot.px = make_component<Fixed>(state, previous == nullptr ? nullptr : previous->px,
            aggregate, DirtyComponent::PositionX, [](const Body& body) { return body.position.x; });
        snapshot.py = make_component<Fixed>(state, previous == nullptr ? nullptr : previous->py,
            aggregate, DirtyComponent::PositionY, [](const Body& body) { return body.position.y; });
        snapshot.vx = make_component<Fixed>(state, previous == nullptr ? nullptr : previous->vx,
            aggregate, DirtyComponent::VelocityX, [](const Body& body) { return body.velocity.x; });
        snapshot.vy = make_component<Fixed>(state, previous == nullptr ? nullptr : previous->vy,
            aggregate, DirtyComponent::VelocityY, [](const Body& body) { return body.velocity.y; });
        snapshot.metadata_bytes = sizeof(SoASnapshot);
        ledger_->allocate_metadata(snapshot.metadata_bytes);
        snapshot.decision = SnapshotDecisionRecord{
            .frame = state.frame,
            .encoding = SnapshotEncoding::ComponentSoA,
            .changed_bodies = changed_count,
            .selected_cost_bytes = component_cost(state.bodies.size(), aggregate),
            .oracle_cost_bytes = std::min<std::uint64_t>(
                state.bodies.size() * sizeof(Body), changed_count * sizeof(BodyPatch)),
        };
        ledger_->record(SnapshotEncoding::ComponentSoA);
        snapshots_.push_back(std::move(snapshot));
        latest_ = state;
        while (snapshots_.size() > config_.capacity) {
            release(snapshots_.front());
            snapshots_.pop_front();
        }
    }

    [[nodiscard]] WorldState restore(std::uint64_t frame) const override {
        const SoASnapshot* snapshot = find(frame);
        if (snapshot == nullptr) throw std::out_of_range("Snapshot frame is not retained");
        WorldState state;
        state.frame = frame;
        state.bodies.resize(snapshot->count);
        for (std::size_t index = 0; index < snapshot->count; ++index) {
            state.bodies[index] = Body{
                .id = snapshot->ids->values_[index],
                .position = {snapshot->px->values_[index], snapshot->py->values_[index]},
                .velocity = {snapshot->vx->values_[index], snapshot->vy->values_[index]},
            };
        }
        return state;
    }
    [[nodiscard]] std::optional<Body> lookup(std::uint64_t frame, EntityId entity) const override {
        const SoASnapshot* snapshot = find(frame);
        if (snapshot == nullptr) return std::nullopt;
        const auto iterator = std::lower_bound(snapshot->ids->values_.begin(), snapshot->ids->values_.end(), entity);
        if (iterator == snapshot->ids->values_.end() || *iterator != entity) return std::nullopt;
        const std::size_t index = static_cast<std::size_t>(iterator - snapshot->ids->values_.begin());
        return Body{.id = entity,
            .position = {snapshot->px->values_[index], snapshot->py->values_[index]},
            .velocity = {snapshot->vx->values_[index], snapshot->vy->values_[index]}};
    }
    [[nodiscard]] std::uint64_t scan_hash(std::uint64_t frame) const override {
        const SoASnapshot* snapshot = find(frame);
        if (snapshot == nullptr) throw std::out_of_range("Snapshot frame is not retained");
        WorldHashBuilder builder(frame, snapshot->count);
        for (std::size_t index = 0; index < snapshot->count; ++index) {
            builder.append(Body{.id = snapshot->ids->values_[index],
                .position = {snapshot->px->values_[index], snapshot->py->values_[index]},
                .velocity = {snapshot->vx->values_[index], snapshot->vy->values_[index]}});
        }
        return builder.value();
    }
    void truncate_after(std::uint64_t frame) override {
        while (!snapshots_.empty() && snapshots_.back().frame > frame) {
            release(snapshots_.back());
            snapshots_.pop_back();
        }
        if (!snapshots_.empty()) latest_ = restore(snapshots_.back().frame);
    }
    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept override { return find(frame) != nullptr; }
    [[nodiscard]] std::size_t size() const noexcept override { return snapshots_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept override { return config_.capacity; }
    [[nodiscard]] SnapshotStoreStats stats() const noexcept override { return ledger_->snapshot(size()); }
    [[nodiscard]] SnapshotStrategy strategy() const noexcept override { return SnapshotStrategy::ComponentSoA; }
    [[nodiscard]] std::optional<SnapshotDecisionRecord> decision_for(std::uint64_t frame) const noexcept override {
        const SoASnapshot* snapshot = find(frame);
        return snapshot == nullptr ? std::nullopt : std::optional<SnapshotDecisionRecord>{snapshot->decision};
    }

private:
    template <typename T, typename Getter>
    [[nodiscard]] TrackedArrayPtr<T> make_component(
        const WorldState& state,
        const TrackedArrayPtr<T>& previous,
        std::uint8_t aggregate,
        DirtyComponent component,
        Getter getter) {
        if (previous != nullptr && (aggregate & component_mask(component)) == 0U) return previous;
        std::vector<T> values;
        values.reserve(state.bodies.size());
        for (const Body& body : state.bodies) values.push_back(getter(body));
        return std::make_shared<const TrackedVector<T>>(ledger_, std::move(values));
    }
    [[nodiscard]] static std::uint64_t component_cost(std::size_t count, std::uint8_t aggregate) noexcept {
        std::uint64_t cost = 0U;
        if ((aggregate & component_mask(DirtyComponent::Identity)) != 0U) cost += count * sizeof(EntityId);
        if ((aggregate & component_mask(DirtyComponent::PositionX)) != 0U) cost += count * sizeof(Fixed);
        if ((aggregate & component_mask(DirtyComponent::PositionY)) != 0U) cost += count * sizeof(Fixed);
        if ((aggregate & component_mask(DirtyComponent::VelocityX)) != 0U) cost += count * sizeof(Fixed);
        if ((aggregate & component_mask(DirtyComponent::VelocityY)) != 0U) cost += count * sizeof(Fixed);
        return cost;
    }
    [[nodiscard]] std::optional<std::uint64_t> latest_frame() const noexcept {
        return snapshots_.empty() ? std::nullopt : std::optional<std::uint64_t>{snapshots_.back().frame};
    }
    [[nodiscard]] const SoASnapshot* find(std::uint64_t frame) const noexcept {
        for (auto iterator = snapshots_.rbegin(); iterator != snapshots_.rend(); ++iterator) {
            if (iterator->frame == frame) return &*iterator;
            if (iterator->frame < frame) break;
        }
        return nullptr;
    }
    void release(const SoASnapshot& snapshot) noexcept { ledger_->release_metadata(snapshot.metadata_bytes); }
    SnapshotStoreConfig config_;
    std::deque<SoASnapshot> snapshots_;
    WorldState latest_;
    std::shared_ptr<MemoryLedger> ledger_;
};

struct PagePatch final { std::uint32_t page{}; std::vector<Body> bodies{}; };
struct HybridEntry final {
    std::uint64_t frame{};
    SnapshotEncoding storage{SnapshotEncoding::Full};
    WorldState full{};
    std::vector<BodyPatch> deltas{};
    std::vector<PagePatch> pages{};
    std::size_t payload_bytes{};
    std::size_t metadata_bytes{};
    SnapshotDecisionRecord decision{};
};

class HybridAdaptiveStore final : public ISnapshotStore {
public:
    explicit HybridAdaptiveStore(SnapshotStoreConfig config) : config_(config) { validate_config(config_); }

    void capture(const WorldState& state, const DirtySet* dirty) override {
        validate_world(state);
        require_next_frame(latest_frame(), state.frame);
        if (entries_.empty()) {
            latest_ = state;
            HybridEntry entry = full_entry(state, SnapshotDecisionRecord{
                .frame = state.frame, .encoding = SnapshotEncoding::Full,
                .changed_bodies = state.bodies.size(),
                .touched_pages = (state.bodies.size() + config_.page_bodies - 1U) / config_.page_bodies,
                .selected_cost_bytes = state.bodies.size() * sizeof(Body),
                .oracle_cost_bytes = state.bodies.size() * sizeof(Body)});
            account_add(entry);
            ledger_.record(SnapshotEncoding::Full);
            entries_.push_back(std::move(entry));
            return;
        }

        const std::vector<std::size_t> indices = changed_indices(
            latest_, state, dirty, config_.audit_dirty_contract, ledger_);
        std::vector<bool> touched((state.bodies.size() + config_.page_bodies - 1U) / config_.page_bodies, false);
        for (const std::size_t index : indices) touched[index / config_.page_bodies] = true;
        const std::size_t touched_count = static_cast<std::size_t>(
            std::count(touched.begin(), touched.end(), true));
        const std::uint64_t full_cost = state.bodies.size() * sizeof(Body);
        const std::uint64_t delta_cost = indices.size() * sizeof(BodyPatch);
        std::uint64_t page_cost = touched_count * sizeof(std::uint32_t);
        for (std::size_t page = 0; page < touched.size(); ++page) {
            if (!touched[page]) continue;
            const std::size_t begin = page * config_.page_bodies;
            const std::size_t end = std::min(begin + config_.page_bodies, state.bodies.size());
            page_cost += (end - begin) * sizeof(Body);
        }
        const std::uint64_t oracle = std::min({full_cost, delta_cost, page_cost});
        const bool forced_checkpoint = state.frame % config_.checkpoint_interval == 0U;
        SnapshotEncoding selected = SnapshotEncoding::Full;
        if (!forced_checkpoint) {
            struct Candidate { SnapshotEncoding encoding; std::uint64_t raw; std::uint64_t adjusted; };
            const std::uint64_t switching_penalty = std::min<std::uint64_t>(4'096U, full_cost / 20U);
            const auto adjusted = [&](SnapshotEncoding encoding, std::uint64_t raw) {
                return raw + (encoding == last_decision_ ? 0U : switching_penalty);
            };
            const std::array<Candidate, 3> candidates{{
                {SnapshotEncoding::Full, full_cost, adjusted(SnapshotEncoding::Full, full_cost)},
                {SnapshotEncoding::Delta, delta_cost, adjusted(SnapshotEncoding::Delta, delta_cost)},
                {SnapshotEncoding::Pages, page_cost, adjusted(SnapshotEncoding::Pages, page_cost)},
            }};
            selected = std::min_element(candidates.begin(), candidates.end(),
                [](const Candidate& lhs, const Candidate& rhs) { return lhs.adjusted < rhs.adjusted; })->encoding;
        }
        const std::uint64_t selected_cost = selected == SnapshotEncoding::Full ? full_cost
            : selected == SnapshotEncoding::Delta ? delta_cost : page_cost;
        const SnapshotDecisionRecord decision{
            .frame = state.frame,
            .encoding = selected,
            .changed_bodies = indices.size(),
            .touched_pages = touched_count,
            .features = make_features(indices, state.bodies.size()),
            .selected_cost_bytes = selected_cost,
            .oracle_cost_bytes = oracle,
            .regret_bytes = selected_cost - oracle,
        };

        HybridEntry entry;
        if (selected == SnapshotEncoding::Full) {
            entry = full_entry(state, decision);
        } else if (selected == SnapshotEncoding::Delta) {
            entry.frame = state.frame;
            entry.storage = SnapshotEncoding::Delta;
            entry.deltas.reserve(indices.size());
            for (const std::size_t index : indices) {
                if (index > std::numeric_limits<std::uint32_t>::max()) throw std::overflow_error("Body index exceeds patch representation");
                entry.deltas.push_back(BodyPatch{.index = static_cast<std::uint32_t>(index), .body = state.bodies[index]});
            }
            entry.deltas.shrink_to_fit();
            entry.payload_bytes = entry.deltas.capacity() * sizeof(BodyPatch);
            entry.metadata_bytes = sizeof(HybridEntry);
            entry.decision = decision;
        } else {
            entry.frame = state.frame;
            entry.storage = SnapshotEncoding::Pages;
            entry.pages.reserve(touched_count);
            for (std::size_t page = 0; page < touched.size(); ++page) {
                if (!touched[page]) continue;
                const std::size_t begin = page * config_.page_bodies;
                const std::size_t end = std::min(begin + config_.page_bodies, state.bodies.size());
                PagePatch patch;
                patch.page = static_cast<std::uint32_t>(page);
                patch.bodies.assign(state.bodies.begin() + static_cast<std::ptrdiff_t>(begin),
                                    state.bodies.begin() + static_cast<std::ptrdiff_t>(end));
                patch.bodies.shrink_to_fit();
                entry.payload_bytes += patch.bodies.capacity() * sizeof(Body);
                entry.pages.push_back(std::move(patch));
            }
            entry.pages.shrink_to_fit();
            entry.metadata_bytes = sizeof(HybridEntry) + entry.pages.capacity() * sizeof(PagePatch);
            entry.decision = decision;
        }
        account_add(entry);
        ledger_.record(selected, decision.regret_bytes);
        entries_.push_back(std::move(entry));
        latest_ = state;
        last_decision_ = selected;
        enforce_capacity();
    }

    [[nodiscard]] WorldState restore(std::uint64_t frame) const override {
        if (!contains(frame)) throw std::out_of_range("Snapshot frame is not retained");
        std::size_t checkpoint_index = 0U;
        bool found = false;
        for (std::size_t index = 0; index < entries_.size(); ++index) {
            if (entries_[index].frame > frame) break;
            if (entries_[index].storage == SnapshotEncoding::Full) {
                checkpoint_index = index;
                found = true;
            }
        }
        if (!found) throw std::logic_error("Hybrid store has no retained checkpoint");
        WorldState state = entries_[checkpoint_index].full;
        for (std::size_t index = checkpoint_index + 1U; index < entries_.size(); ++index) {
            const HybridEntry& entry = entries_[index];
            if (entry.frame > frame) break;
            if (entry.storage == SnapshotEncoding::Full) {
                state = entry.full;
            } else if (entry.storage == SnapshotEncoding::Delta) {
                for (const BodyPatch& patch : entry.deltas) state.bodies[patch.index] = patch.body;
                state.frame = entry.frame;
            } else {
                for (const PagePatch& patch : entry.pages) {
                    const std::size_t begin = static_cast<std::size_t>(patch.page) * config_.page_bodies;
                    std::copy(patch.bodies.begin(), patch.bodies.end(),
                              state.bodies.begin() + static_cast<std::ptrdiff_t>(begin));
                }
                state.frame = entry.frame;
            }
        }
        return state;
    }
    [[nodiscard]] std::optional<Body> lookup(std::uint64_t frame, EntityId entity) const override {
        if (!contains(frame)) return std::nullopt;
        const WorldState state = restore(frame);
        const Body* body = find_body(state.bodies, entity);
        return body == nullptr ? std::nullopt : std::optional<Body>{*body};
    }
    [[nodiscard]] std::uint64_t scan_hash(std::uint64_t frame) const override { return stable_hash(restore(frame)); }
    void truncate_after(std::uint64_t frame) override {
        while (!entries_.empty() && entries_.back().frame > frame) {
            account_release(entries_.back());
            entries_.pop_back();
        }
        if (!entries_.empty()) {
            latest_ = restore(entries_.back().frame);
            last_decision_ = entries_.back().decision.encoding;
        }
    }
    [[nodiscard]] bool contains(std::uint64_t frame) const noexcept override {
        return !entries_.empty() && frame >= entries_.front().frame && frame <= entries_.back().frame;
    }
    [[nodiscard]] std::size_t size() const noexcept override { return entries_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept override { return config_.capacity; }
    [[nodiscard]] SnapshotStoreStats stats() const noexcept override { return ledger_.snapshot(size()); }
    [[nodiscard]] SnapshotStrategy strategy() const noexcept override { return SnapshotStrategy::HybridAdaptive; }
    [[nodiscard]] std::optional<SnapshotDecisionRecord> decision_for(std::uint64_t frame) const noexcept override {
        for (const HybridEntry& entry : entries_) if (entry.frame == frame) return entry.decision;
        return std::nullopt;
    }

private:
    [[nodiscard]] HybridEntry full_entry(const WorldState& state, SnapshotDecisionRecord decision) const {
        HybridEntry entry;
        entry.frame = state.frame;
        entry.storage = SnapshotEncoding::Full;
        entry.full = state;
        entry.full.bodies.shrink_to_fit();
        entry.payload_bytes = world_payload_bytes(entry.full);
        entry.metadata_bytes = sizeof(HybridEntry);
        entry.decision = decision;
        return entry;
    }
    void enforce_capacity() {
        while (entries_.size() > config_.capacity) {
            if (entries_.size() >= 2U && entries_[1].storage != SnapshotEncoding::Full) {
                const WorldState promoted = restore(entries_[1].frame);
                const SnapshotDecisionRecord decision = entries_[1].decision;
                account_release(entries_[1]);
                entries_[1] = full_entry(promoted, decision);
                account_add(entries_[1]);
            }
            account_release(entries_.front());
            entries_.pop_front();
        }
    }
    void account_add(const HybridEntry& entry) {
        ledger_.allocate_payload(entry.payload_bytes);
        ledger_.allocate_metadata(entry.metadata_bytes);
    }
    void account_release(const HybridEntry& entry) noexcept {
        ledger_.release_payload(entry.payload_bytes);
        ledger_.release_metadata(entry.metadata_bytes);
    }
    [[nodiscard]] std::optional<std::uint64_t> latest_frame() const noexcept {
        return entries_.empty() ? std::nullopt : std::optional<std::uint64_t>{entries_.back().frame};
    }
    SnapshotStoreConfig config_;
    std::deque<HybridEntry> entries_;
    WorldState latest_;
    SnapshotEncoding last_decision_{SnapshotEncoding::Full};
    MemoryLedger ledger_;
};

} // namespace

std::string_view to_string(SnapshotStrategy strategy) noexcept {
    switch (strategy) {
    case SnapshotStrategy::FullCopy: return "full_copy";
    case SnapshotStrategy::DeltaLog: return "delta_checkpoint";
    case SnapshotStrategy::PagedCopyOnWrite: return "paged_cow";
    case SnapshotStrategy::PersistentChunkTree: return "persistent_chunk_tree";
    case SnapshotStrategy::ComponentSoA: return "component_soa";
    case SnapshotStrategy::HybridAdaptive: return "hybrid_adaptive";
    }
    return "unknown";
}

std::string_view to_string(SnapshotEncoding encoding) noexcept {
    switch (encoding) {
    case SnapshotEncoding::Full: return "full";
    case SnapshotEncoding::Delta: return "delta";
    case SnapshotEncoding::Pages: return "pages";
    case SnapshotEncoding::ComponentSoA: return "component_soa";
    }
    return "unknown";
}

std::unique_ptr<ISnapshotStore> make_snapshot_store(const SnapshotStoreConfig& config) {
    validate_config(config);
    switch (config.strategy) {
    case SnapshotStrategy::FullCopy:
        return std::make_unique<FullCopyStore>(config);
    case SnapshotStrategy::DeltaLog:
        return std::make_unique<DeltaCheckpointStore>(config);
    case SnapshotStrategy::PagedCopyOnWrite:
        return std::make_unique<PagedStore>(config, config.strategy, config.page_bodies);
    case SnapshotStrategy::PersistentChunkTree:
        return std::make_unique<PagedStore>(config, config.strategy, config.persistent_leaf_bodies);
    case SnapshotStrategy::ComponentSoA:
        return std::make_unique<ComponentSoAStore>(config);
    case SnapshotStrategy::HybridAdaptive:
        return std::make_unique<HybridAdaptiveStore>(config);
    }
    throw std::invalid_argument("Unknown snapshot strategy");
}

std::unique_ptr<ISnapshotStore> make_snapshot_store(SnapshotStrategy strategy, std::size_t capacity) {
    return make_snapshot_store(SnapshotStoreConfig{.strategy = strategy, .capacity = capacity});
}

} // namespace neoeng::core
