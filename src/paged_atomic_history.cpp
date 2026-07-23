#include "neoeng/core/paged_atomic_history.hpp"

#include <numeric>

namespace neoeng::core {
namespace {
std::size_t pages_for(std::size_t size, std::size_t page) {
    return size == 0U ? 0U : (size + page - 1U) / page;
}

std::size_t checked_capacity(std::size_t base, std::size_t extra, const char* label) {
    if (base > std::numeric_limits<std::size_t>::max() - extra) {
        throw std::overflow_error(label);
    }
    return std::max<std::size_t>(1U, base + extra);
}

std::size_t position_capacity(const PagedAtomicHistoryConfig& config, std::size_t body_pages) {
    if (config.maximum_position_dirty_pages_per_frame == 0U) {
        return checked_capacity(config.history_capacity * body_pages, body_pages,
            "Position page capacity overflow");
    }
    return checked_capacity(config.full_position_generations * body_pages,
        checked_capacity(config.history_capacity * config.maximum_position_dirty_pages_per_frame,
            body_pages, "Position staging capacity overflow"),
        "Position sparse page capacity overflow");
}
}

PagedAtomicHistory::PagedAtomicHistory(PagedAtomicHistoryConfig config)
    : config_(config),
      body_pages_(pages_for(config.bodies, config.page_elements)),
      contact_pages_(pages_for(config.contacts, config.page_elements)),
      pair_pages_(pages_for(config.maximum_candidate_pairs, config.page_elements)),
      snapshots_(config.history_capacity),
      position_dirty_pages_(body_pages_), body_dirty_pages_(body_pages_), contact_dirty_pages_(contact_pages_),
      all_body_pages_(body_pages_, 1U), all_contact_pages_(contact_pages_, 1U),
      all_pair_pages_(pair_pages_, 1U), no_body_pages_(body_pages_),
      no_contact_pages_(contact_pages_), no_pair_pages_(pair_pages_),
      position_x_pool_(config.page_elements, position_capacity(config, body_pages_)),
      position_y_pool_(config.page_elements, position_capacity(config, body_pages_)),
      velocity_x_pool_(config.page_elements, checked_capacity(
          config.full_velocity_generations * body_pages_,
          checked_capacity(config.history_capacity * config.maximum_velocity_dirty_pages_per_frame,
              body_pages_, "Velocity staging capacity overflow"),
          "Velocity page capacity overflow")),
      velocity_y_pool_(config.page_elements, checked_capacity(
          config.full_velocity_generations * body_pages_,
          checked_capacity(config.history_capacity * config.maximum_velocity_dirty_pages_per_frame,
              body_pages_, "Velocity staging capacity overflow"),
          "Velocity page capacity overflow")),
      masses_pool_(config.page_elements, checked_capacity(
          body_pages_, checked_capacity(config.history_capacity * config.maximum_mass_dirty_pages_per_frame,
              body_pages_, "Mass staging capacity overflow"),
          "Mass page capacity overflow")),
      dual_pool_(config.page_elements, checked_capacity(
          config.full_contact_generations * contact_pages_,
          checked_capacity(config.history_capacity * config.maximum_contact_dirty_pages_per_frame,
              contact_pages_, "Dual staging capacity overflow"),
          "Dual page capacity overflow")),
      manifold_pool_(config.page_elements, checked_capacity(
          contact_pages_, checked_capacity(config.history_capacity * config.maximum_contact_dirty_pages_per_frame,
              contact_pages_, "Manifold staging capacity overflow"),
          "Manifold page capacity overflow")),
      stable_pool_(config.page_elements, checked_capacity(
          config.full_contact_generations * contact_pages_,
          checked_capacity(config.history_capacity * config.maximum_contact_dirty_pages_per_frame,
              contact_pages_, "Stable staging capacity overflow"),
          "Stable page capacity overflow")),
      candidate_pool_(config.page_elements, checked_capacity(
          config.maximum_cache_generations * contact_pages_, contact_pages_ * 2U,
          "Candidate page capacity overflow")),
      bounds_pool_(config.page_elements, checked_capacity(
          config.maximum_cache_generations * body_pages_, body_pages_ * 2U,
          "Bounds page capacity overflow")),
      pair_pool_(config.page_elements, checked_capacity(
          config.maximum_cache_generations * pair_pages_, pair_pages_ * 2U,
          "Pair page capacity overflow")) {
    if (config.bodies == 0U || config.contacts == 0U || config.maximum_candidate_pairs == 0U
        || config.history_capacity < 2U || config.page_elements == 0U
        || config.maximum_cache_generations == 0U) {
        throw std::invalid_argument("Paged atomic history configuration is invalid");
    }
    for (Snapshot& snapshot : snapshots_) {
        snapshot.position_x.assign(body_pages_, invalid_page);
        snapshot.position_y.assign(body_pages_, invalid_page);
        snapshot.velocity_x.assign(body_pages_, invalid_page);
        snapshot.velocity_y.assign(body_pages_, invalid_page);
        snapshot.masses.assign(body_pages_, invalid_page);
        snapshot.dual.assign(contact_pages_, invalid_page);
        snapshot.manifold.assign(contact_pages_, invalid_page);
        snapshot.contact_stable.assign(contact_pages_, invalid_page);
        snapshot.contact_candidate.assign(contact_pages_, invalid_page);
        snapshot.fat_bounds.assign(body_pages_, invalid_page);
        snapshot.pairs.assign(pair_pages_, invalid_page);
    }
    staging_.position_x.assign(body_pages_, invalid_page);
    staging_.position_y.assign(body_pages_, invalid_page);
    staging_.velocity_x.assign(body_pages_, invalid_page);
    staging_.velocity_y.assign(body_pages_, invalid_page);
    staging_.masses.assign(body_pages_, invalid_page);
    staging_.dual.assign(contact_pages_, invalid_page);
    staging_.manifold.assign(contact_pages_, invalid_page);
    staging_.contact_stable.assign(contact_pages_, invalid_page);
    staging_.contact_candidate.assign(contact_pages_, invalid_page);
    staging_.fat_bounds.assign(body_pages_, invalid_page);
    staging_.pairs.assign(pair_pages_, invalid_page);
}

PagedAtomicHistory::Snapshot& PagedAtomicHistory::slot(std::uint64_t frame) noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}
const PagedAtomicHistory::Snapshot& PagedAtomicHistory::slot(std::uint64_t frame) const noexcept {
    return snapshots_[static_cast<std::size_t>(frame % snapshots_.size())];
}

const PagedAtomicHistory::Snapshot* PagedAtomicHistory::previous_snapshot(std::uint64_t frame) const noexcept {
    if (frame == 0U) return nullptr;
    const Snapshot& previous = slot(frame - 1U);
    return previous.frame == frame - 1U ? &previous : nullptr;
}

template <typename T>
void PagedAtomicHistory::release_refs(std::vector<PageId>& refs, PagePool<T>& pool) {
    for (PageId& id : refs) {
        if (id == invalid_page) continue;
        if (pool.refcount(id) > 1U) ++stats_.zero_copy_promotions;
        static_cast<void>(pool.release(id));
        ++stats_.pages_released;
        id = invalid_page;
    }
}

void PagedAtomicHistory::release_snapshot(Snapshot& snapshot) {
    if (snapshot.frame == empty_frame) return;
    release_refs(snapshot.position_x, position_x_pool_);
    release_refs(snapshot.position_y, position_y_pool_);
    release_refs(snapshot.velocity_x, velocity_x_pool_);
    release_refs(snapshot.velocity_y, velocity_y_pool_);
    release_refs(snapshot.masses, masses_pool_);
    release_refs(snapshot.dual, dual_pool_);
    release_refs(snapshot.manifold, manifold_pool_);
    release_refs(snapshot.contact_stable, stable_pool_);
    release_refs(snapshot.contact_candidate, candidate_pool_);
    release_refs(snapshot.fat_bounds, bounds_pool_);
    release_refs(snapshot.pairs, pair_pool_);
    snapshot.frame = empty_frame;
    snapshot.pair_count = 0U;
    if (size_ != 0U) --size_;
    ++stats_.snapshots_evicted;
}

template <typename T>
void PagedAtomicHistory::capture_array(
    std::span<const T> values,
    const std::vector<PageId>* previous,
    std::size_t previous_size,
    std::span<const std::uint8_t> dirty_pages,
    bool force_copy,
    std::vector<PageId>& output,
    PagePool<T>& pool) {
    const std::size_t page_size = pool.page_elements();
    const std::size_t used_pages = pages_for(values.size(), page_size);
    const std::size_t previous_pages = pages_for(previous_size, page_size);
    if (used_pages > output.size() || dirty_pages.size() < used_pages) {
        throw std::length_error("Paged history reference/mask vector is too small");
    }
    for (std::size_t page_index = 0U; page_index < used_pages; ++page_index) {
        const std::size_t offset = page_index * page_size;
        const std::size_t count = std::min(page_size, values.size() - offset);
        PageId selected = invalid_page;
        const bool previous_compatible = previous != nullptr && page_index < previous_pages
            && (*previous)[page_index] != invalid_page
            && std::min(page_size, previous_size - offset) == count;
        if (!force_copy && dirty_pages[page_index] == 0U && previous_compatible) {
            selected = (*previous)[page_index];
            pool.retain(selected);
            ++stats_.pages_shared;
        } else if (!force_copy && previous_compatible) {
            const PageId candidate = (*previous)[page_index];
            const auto old = pool.page(candidate).first(count);
            if (std::equal(values.begin() + static_cast<std::ptrdiff_t>(offset),
                           values.begin() + static_cast<std::ptrdiff_t>(offset + count), old.begin())) {
                pool.retain(candidate);
                selected = candidate;
                ++stats_.pages_shared;
            }
        }
        if (selected == invalid_page) {
            try {
                selected = pool.acquire();
            } catch (const std::length_error&) {
                ++stats_.page_pool_exhaustions;
                throw;
            }
            auto destination = pool.page(selected);
            std::copy(values.begin() + static_cast<std::ptrdiff_t>(offset),
                      values.begin() + static_cast<std::ptrdiff_t>(offset + count), destination.begin());
            std::fill(destination.begin() + static_cast<std::ptrdiff_t>(count), destination.end(), T{});
            ++stats_.pages_copied;
        }
        output[page_index] = selected;
    }
    for (std::size_t page_index = used_pages; page_index < output.size(); ++page_index) {
        output[page_index] = invalid_page;
    }
}

template <typename T>
void PagedAtomicHistory::restore_array(
    const std::vector<PageId>& refs,
    std::size_t logical_size,
    std::span<T> output,
    const PagePool<T>& pool) const {
    if (output.size() < logical_size) throw std::invalid_argument("Paged history restore target is too small");
    const std::size_t page_size = pool.page_elements();
    const std::size_t used_pages = pages_for(logical_size, page_size);
    for (std::size_t page_index = 0U; page_index < used_pages; ++page_index) {
        const std::size_t offset = page_index * page_size;
        const std::size_t count = std::min(page_size, logical_size - offset);
        if (refs[page_index] == invalid_page) throw std::logic_error("Paged snapshot contains a missing page");
        const auto source = pool.page(refs[page_index]).first(count);
        std::copy(source.begin(), source.end(), output.begin() + static_cast<std::ptrdiff_t>(offset));
    }
}

// The caller guarantees that output currently represents current_refs. Pages shared
// by the current and target snapshots are already correct and need not be copied.
template <typename T>
void PagedAtomicHistory::restore_array_delta(
    const std::vector<PageId>& refs,
    const std::vector<PageId>* current_refs,
    std::size_t logical_size,
    std::span<T> output,
    const PagePool<T>& pool) const {
    if (output.size() < logical_size) throw std::invalid_argument("Paged history restore target is too small");
    const std::size_t page_size = pool.page_elements();
    const std::size_t used_pages = pages_for(logical_size, page_size);
    for (std::size_t page_index = 0U; page_index < used_pages; ++page_index) {
        if (refs[page_index] == invalid_page) throw std::logic_error("Paged snapshot contains a missing page");
        if (current_refs != nullptr && page_index < current_refs->size()
            && (*current_refs)[page_index] == refs[page_index]) {
            continue;
        }
        const std::size_t offset = page_index * page_size;
        const std::size_t count = std::min(page_size, logical_size - offset);
        const auto source = pool.page(refs[page_index]).first(count);
        std::copy(source.begin(), source.end(), output.begin() + static_cast<std::ptrdiff_t>(offset));
    }
}

void PagedAtomicHistory::capture(const AtomicTemporalExternalState& state) {
    capture_internal(AtomicTemporalStateView{
        .frame = state.frame, .valid_until_frame = state.valid_until_frame,
        .position_x = state.position_x, .position_y = state.position_y,
        .velocity_x = state.velocity_x, .velocity_y = state.velocity_y,
        .masses = state.masses, .dual = state.dual, .manifold = state.manifold,
        .contact_stable = state.contact_stable, .contact_candidate = state.contact_candidate,
        .fat_bounds = state.fat_bounds,
        .pairs = std::span<const BroadphasePair>(state.pairs.data(), state.pair_count),
    }, {}, true);
}

void PagedAtomicHistory::capture(AtomicTemporalStateView state, AtomicTemporalCaptureHints hints) {
    capture_internal(state, hints, false);
}

void PagedAtomicHistory::capture_internal(
    AtomicTemporalStateView state, AtomicTemporalCaptureHints hints, bool force_all) {
    if (state.position_x.size() != config_.bodies || state.position_y.size() != config_.bodies
        || state.velocity_x.size() != config_.bodies || state.velocity_y.size() != config_.bodies
        || state.masses.size() != config_.bodies || state.dual.size() != config_.contacts
        || state.manifold.size() != config_.contacts || state.contact_stable.size() != config_.contacts
        || state.contact_candidate.size() != config_.contacts
        || state.fat_bounds.size() != config_.bodies
        || state.pairs.size() > config_.maximum_candidate_pairs) {
        throw std::invalid_argument("Paged atomic capture shape mismatch");
    }
    std::fill(position_dirty_pages_.begin(), position_dirty_pages_.end(), 0U);
    std::fill(body_dirty_pages_.begin(), body_dirty_pages_.end(), 0U);
    std::fill(contact_dirty_pages_.begin(), contact_dirty_pages_.end(), 0U);
    if (force_all) {
        std::fill(position_dirty_pages_.begin(), position_dirty_pages_.end(), 1U);
        std::fill(body_dirty_pages_.begin(), body_dirty_pages_.end(), 1U);
        std::fill(contact_dirty_pages_.begin(), contact_dirty_pages_.end(), 1U);
    } else {
        if (!hints.position_hints_complete) {
            std::fill(position_dirty_pages_.begin(), position_dirty_pages_.end(), 1U);
        }
        for (const std::size_t body : hints.changed_position_bodies) {
            if (body >= config_.bodies) throw std::out_of_range("Paged history dirty position body is invalid");
            position_dirty_pages_[body / config_.page_elements] = 1U;
        }
        for (const std::size_t body : hints.changed_bodies) {
            if (body >= config_.bodies) throw std::out_of_range("Paged history dirty body is invalid");
            body_dirty_pages_[body / config_.page_elements] = 1U;
        }
        if (!hints.changed_contacts.empty() && hints.changed_contacts.size() != config_.contacts) {
            throw std::invalid_argument("Paged history contact dirty mask has invalid size");
        }
        for (std::size_t contact = 0U; contact < hints.changed_contacts.size(); ++contact) {
            if (hints.changed_contacts[contact] != 0U) {
                contact_dirty_pages_[contact / config_.page_elements] = 1U;
            }
        }
    }

    const Snapshot* previous = previous_snapshot(state.frame);
    Snapshot& target = slot(state.frame);
    if (staging_.frame != empty_frame) {
        throw std::logic_error("Paged atomic staging snapshot is unexpectedly occupied");
    }
    staging_.frame = state.frame;
    staging_.valid_until_frame = state.valid_until_frame;
    staging_.topology_signature = state.topology_signature;
    staging_.pair_count = state.pairs.size();
    try {
        capture_array<Fixed::rep>(state.position_x, previous ? &previous->position_x : nullptr,
            config_.bodies, force_all ? all_body_pages_ : position_dirty_pages_, false,
            staging_.position_x, position_x_pool_);
        capture_array<Fixed::rep>(state.position_y, previous ? &previous->position_y : nullptr,
            config_.bodies, force_all ? all_body_pages_ : position_dirty_pages_, false,
            staging_.position_y, position_y_pool_);
        capture_array<Fixed::rep>(state.velocity_x, previous ? &previous->velocity_x : nullptr,
            config_.bodies, force_all ? all_body_pages_ : body_dirty_pages_, false,
            staging_.velocity_x, velocity_x_pool_);
        capture_array<Fixed::rep>(state.velocity_y, previous ? &previous->velocity_y : nullptr,
            config_.bodies, force_all ? all_body_pages_ : body_dirty_pages_, false,
            staging_.velocity_y, velocity_y_pool_);
        capture_array<std::uint32_t>(state.masses, previous ? &previous->masses : nullptr,
            config_.bodies, force_all ? all_body_pages_ : body_dirty_pages_, false,
            staging_.masses, masses_pool_);
        capture_array<Fixed::rep>(state.dual, previous ? &previous->dual : nullptr,
            config_.contacts, force_all ? all_contact_pages_ : contact_dirty_pages_, false,
            staging_.dual, dual_pool_);
        capture_array<NormalContact>(state.manifold, previous ? &previous->manifold : nullptr,
            config_.contacts,
            (force_all || hints.topology_changed) ? all_contact_pages_ : no_contact_pages_, false,
            staging_.manifold, manifold_pool_);
        capture_array<std::uint8_t>(state.contact_stable, previous ? &previous->contact_stable : nullptr,
            config_.contacts, force_all ? all_contact_pages_ : contact_dirty_pages_, false,
            staging_.contact_stable, stable_pool_);
        capture_array<std::uint8_t>(state.contact_candidate, previous ? &previous->contact_candidate : nullptr,
            config_.contacts, (force_all || hints.cache_rebuilt) ? all_contact_pages_ : no_contact_pages_, false,
            staging_.contact_candidate, candidate_pool_);
        capture_array<FatAabb>(state.fat_bounds, previous ? &previous->fat_bounds : nullptr,
            config_.bodies, (force_all || hints.cache_rebuilt) ? all_body_pages_ : no_body_pages_, false,
            staging_.fat_bounds, bounds_pool_);
        capture_array<BroadphasePair>(state.pairs, previous ? &previous->pairs : nullptr,
            previous ? previous->pair_count : 0U,
            (force_all || hints.cache_rebuilt) ? all_pair_pages_ : no_pair_pages_, false,
            staging_.pairs, pair_pool_);
    } catch (...) {
        release_refs(staging_.position_x, position_x_pool_);
        release_refs(staging_.position_y, position_y_pool_);
        release_refs(staging_.velocity_x, velocity_x_pool_);
        release_refs(staging_.velocity_y, velocity_y_pool_);
        release_refs(staging_.masses, masses_pool_);
        release_refs(staging_.dual, dual_pool_);
        release_refs(staging_.manifold, manifold_pool_);
        release_refs(staging_.contact_stable, stable_pool_);
        release_refs(staging_.contact_candidate, candidate_pool_);
        release_refs(staging_.fat_bounds, bounds_pool_);
        release_refs(staging_.pairs, pair_pool_);
        staging_.frame = empty_frame;
        staging_.pair_count = 0U;
        refresh_page_stats();
        throw;
    }

    if (target.frame != empty_frame) release_snapshot(target);
    using std::swap;
    swap(target.position_x, staging_.position_x);
    swap(target.position_y, staging_.position_y);
    swap(target.velocity_x, staging_.velocity_x);
    swap(target.velocity_y, staging_.velocity_y);
    swap(target.masses, staging_.masses);
    swap(target.dual, staging_.dual);
    swap(target.manifold, staging_.manifold);
    swap(target.contact_stable, staging_.contact_stable);
    swap(target.contact_candidate, staging_.contact_candidate);
    swap(target.fat_bounds, staging_.fat_bounds);
    swap(target.pairs, staging_.pairs);
    target.frame = staging_.frame;
    target.valid_until_frame = staging_.valid_until_frame;
    target.topology_signature = staging_.topology_signature;
    target.pair_count = staging_.pair_count;
    staging_.frame = empty_frame;
    staging_.pair_count = 0U;
    ++size_;
    ++stats_.snapshots_captured;
    refresh_page_stats();
}

void PagedAtomicHistory::restore(std::uint64_t frame, AtomicTemporalExternalState& output) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Paged atomic snapshot is not retained");
    if (output.position_x.size() != config_.bodies || output.pairs.size() < config_.maximum_candidate_pairs) {
        throw std::invalid_argument("Paged atomic restore target shape mismatch");
    }
    output.frame = frame;
    output.valid_until_frame = snapshot.valid_until_frame;
    output.topology_signature = snapshot.topology_signature;
    output.pair_count = snapshot.pair_count;
    restore_array(snapshot.position_x, config_.bodies, std::span<Fixed::rep>(output.position_x), position_x_pool_);
    restore_array(snapshot.position_y, config_.bodies, std::span<Fixed::rep>(output.position_y), position_y_pool_);
    restore_array(snapshot.velocity_x, config_.bodies, std::span<Fixed::rep>(output.velocity_x), velocity_x_pool_);
    restore_array(snapshot.velocity_y, config_.bodies, std::span<Fixed::rep>(output.velocity_y), velocity_y_pool_);
    restore_array(snapshot.masses, config_.bodies, std::span<std::uint32_t>(output.masses), masses_pool_);
    restore_array(snapshot.dual, config_.contacts, std::span<Fixed::rep>(output.dual), dual_pool_);
    restore_array(snapshot.manifold, config_.contacts, std::span<NormalContact>(output.manifold), manifold_pool_);
    restore_array(snapshot.contact_stable, config_.contacts, std::span<std::uint8_t>(output.contact_stable), stable_pool_);
    restore_array(snapshot.contact_candidate, config_.contacts, std::span<std::uint8_t>(output.contact_candidate), candidate_pool_);
    restore_array(snapshot.fat_bounds, config_.bodies, std::span<FatAabb>(output.fat_bounds), bounds_pool_);
    restore_array(snapshot.pairs, snapshot.pair_count,
        std::span<BroadphasePair>(output.pairs.data(), output.pairs.size()), pair_pool_);
}

AtomicTemporalRestoreMetadata PagedAtomicHistory::restore_direct(
    std::uint64_t frame, AtomicTemporalMutableStateView output) const {
    return restore_direct_from_current(frame, empty_frame, output);
}

AtomicTemporalRestoreMetadata PagedAtomicHistory::restore_direct_from_current(
    std::uint64_t frame, std::uint64_t current_frame,
    AtomicTemporalMutableStateView output, bool restore_pairs) const {
    const Snapshot& snapshot = slot(frame);
    if (snapshot.frame != frame) throw std::out_of_range("Paged atomic snapshot is not retained");
    if (output.position_x.size() != config_.bodies || output.position_y.size() != config_.bodies
        || output.velocity_x.size() != config_.bodies || output.velocity_y.size() != config_.bodies
        || output.masses.size() != config_.bodies || output.dual.size() != config_.contacts
        || output.manifold.size() != config_.contacts || output.contact_stable.size() != config_.contacts
        || output.contact_candidate.size() != config_.contacts
        || output.fat_bounds.size() != config_.bodies || output.pairs.size() < snapshot.pair_count) {
        throw std::invalid_argument("Paged atomic direct restore target shape mismatch");
    }
    const Snapshot* current = nullptr;
    if (current_frame != empty_frame) {
        const Snapshot& candidate = slot(current_frame);
        if (candidate.frame == current_frame) current = &candidate;
    }
    restore_array_delta(snapshot.position_x, current ? &current->position_x : nullptr,
        config_.bodies, output.position_x, position_x_pool_);
    restore_array_delta(snapshot.position_y, current ? &current->position_y : nullptr,
        config_.bodies, output.position_y, position_y_pool_);
    restore_array_delta(snapshot.velocity_x, current ? &current->velocity_x : nullptr,
        config_.bodies, output.velocity_x, velocity_x_pool_);
    restore_array_delta(snapshot.velocity_y, current ? &current->velocity_y : nullptr,
        config_.bodies, output.velocity_y, velocity_y_pool_);
    restore_array_delta(snapshot.masses, current ? &current->masses : nullptr,
        config_.bodies, output.masses, masses_pool_);
    restore_array_delta(snapshot.dual, current ? &current->dual : nullptr,
        config_.contacts, output.dual, dual_pool_);
    restore_array_delta(snapshot.manifold, current ? &current->manifold : nullptr,
        config_.contacts, output.manifold, manifold_pool_);
    restore_array_delta(snapshot.contact_stable, current ? &current->contact_stable : nullptr,
        config_.contacts, output.contact_stable, stable_pool_);
    restore_array_delta(snapshot.contact_candidate, current ? &current->contact_candidate : nullptr,
        config_.contacts, output.contact_candidate, candidate_pool_);
    restore_array_delta(snapshot.fat_bounds, current ? &current->fat_bounds : nullptr,
        config_.bodies, output.fat_bounds, bounds_pool_);
    if (restore_pairs) {
        restore_array_delta(snapshot.pairs, current ? &current->pairs : nullptr,
            snapshot.pair_count, output.pairs, pair_pool_);
    }
    return AtomicTemporalRestoreMetadata{
        .frame = frame,
        .valid_until_frame = snapshot.valid_until_frame,
        .topology_signature = snapshot.topology_signature,
        .pair_count = snapshot.pair_count,
    };
}

bool PagedAtomicHistory::contains(std::uint64_t frame) const noexcept {
    return slot(frame).frame == frame;
}

void PagedAtomicHistory::truncate_after(std::uint64_t frame) {
    for (Snapshot& snapshot : snapshots_) {
        if (snapshot.frame != empty_frame && snapshot.frame > frame) release_snapshot(snapshot);
    }
    refresh_page_stats();
}

void PagedAtomicHistory::clear() {
    for (Snapshot& snapshot : snapshots_) release_snapshot(snapshot);
    size_ = 0U;
    refresh_page_stats();
}

void PagedAtomicHistory::refresh_page_stats() noexcept {
    stats_.live_pages = position_x_pool_.live_pages() + position_y_pool_.live_pages()
        + velocity_x_pool_.live_pages() + velocity_y_pool_.live_pages()
        + masses_pool_.live_pages() + dual_pool_.live_pages() + manifold_pool_.live_pages()
        + stable_pool_.live_pages() + candidate_pool_.live_pages()
        + bounds_pool_.live_pages() + pair_pool_.live_pages();
    stats_.peak_live_pages = std::max(stats_.peak_live_pages, stats_.live_pages);
}

std::size_t PagedAtomicHistory::reserved_bytes() const noexcept {
    std::size_t bytes = snapshots_.capacity() * sizeof(Snapshot);
    for (const Snapshot& snapshot : snapshots_) {
        bytes += (snapshot.position_x.capacity() + snapshot.position_y.capacity()
            + snapshot.velocity_x.capacity() + snapshot.velocity_y.capacity()
            + snapshot.masses.capacity() + snapshot.dual.capacity() + snapshot.manifold.capacity()
            + snapshot.contact_stable.capacity() + snapshot.contact_candidate.capacity()
            + snapshot.fat_bounds.capacity()
            + snapshot.pairs.capacity()) * sizeof(PageId);
    }
    bytes += (staging_.position_x.capacity() + staging_.position_y.capacity()
        + staging_.velocity_x.capacity() + staging_.velocity_y.capacity()
        + staging_.masses.capacity() + staging_.dual.capacity() + staging_.manifold.capacity()
        + staging_.contact_stable.capacity() + staging_.contact_candidate.capacity()
        + staging_.fat_bounds.capacity()
        + staging_.pairs.capacity()) * sizeof(PageId);
    return bytes + position_x_pool_.reserved_bytes() + position_y_pool_.reserved_bytes()
        + velocity_x_pool_.reserved_bytes() + velocity_y_pool_.reserved_bytes()
        + masses_pool_.reserved_bytes() + dual_pool_.reserved_bytes()
        + manifold_pool_.reserved_bytes() + stable_pool_.reserved_bytes()
        + candidate_pool_.reserved_bytes() + bounds_pool_.reserved_bytes() + pair_pool_.reserved_bytes();
}

std::size_t PagedAtomicHistory::live_payload_bytes() const noexcept {
    return position_x_pool_.live_payload_bytes() + position_y_pool_.live_payload_bytes()
        + velocity_x_pool_.live_payload_bytes() + velocity_y_pool_.live_payload_bytes()
        + masses_pool_.live_payload_bytes() + dual_pool_.live_payload_bytes()
        + manifold_pool_.live_payload_bytes() + stable_pool_.live_payload_bytes()
        + candidate_pool_.live_payload_bytes() + bounds_pool_.live_payload_bytes()
        + pair_pool_.live_payload_bytes();
}


} // namespace neoeng::core
