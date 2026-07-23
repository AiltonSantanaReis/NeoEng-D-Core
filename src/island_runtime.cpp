#include "neoeng/core/island_runtime.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <condition_variable>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <tuple>

namespace neoeng::core {
namespace {

constexpr std::size_t kMissing = std::numeric_limits<std::size_t>::max();

[[nodiscard]] WideInteger floor_div_two(WideInteger value) noexcept {
    WideInteger quotient = value / 2;
    if (value < 0 && value % 2 != 0) --quotient;
    return quotient;
}

[[nodiscard]] Fixed::rep checked_rep(WideInteger value) {
    constexpr WideInteger minimum = static_cast<WideInteger>(
        std::numeric_limits<Fixed::rep>::min());
    constexpr WideInteger maximum = static_cast<WideInteger>(
        std::numeric_limits<Fixed::rep>::max());
    if (value < minimum || value > maximum) {
        throw std::overflow_error("Island projection fixed-point range overflow");
    }
    return static_cast<Fixed::rep>(value);
}

[[nodiscard]] std::pair<std::size_t, std::size_t> canonical_pair(
    const SweptContact& contact) noexcept {
    return contact.first < contact.second
        ? std::pair{contact.first, contact.second}
        : std::pair{contact.second, contact.first};
}

} // namespace

const char* to_string(IslandTopology topology) noexcept {
    switch (topology) {
    case IslandTopology::Matching: return "matching";
    case IslandTopology::Chain: return "chain";
    case IslandTopology::Tree: return "tree";
    case IslandTopology::Cycle: return "cycle";
    case IslandTopology::General: return "general";
    }
    return "unknown";
}

ContactIslandWorkspace::ContactIslandWorkspace(
    std::size_t maximum_bodies,
    std::size_t maximum_contacts)
    : maximum_bodies_(maximum_bodies), maximum_contacts_(maximum_contacts),
      parent_(maximum_bodies), degree_(maximum_bodies), involved_(maximum_bodies),
      root_to_island_(maximum_bodies), body_write_cursor_(maximum_contacts),
      contact_write_cursor_(maximum_contacts), used_color_mask_(maximum_bodies),
      descriptors_(maximum_contacts), body_order_(maximum_bodies),
      contact_order_(maximum_contacts), contact_colors_(maximum_contacts) {
    if (maximum_bodies == 0U) {
        throw std::invalid_argument("Island workspace requires body capacity");
    }
    if (maximum_contacts == 0U) {
        throw std::invalid_argument("Island workspace requires contact capacity");
    }
}

std::size_t ContactIslandWorkspace::find(std::size_t body) noexcept {
    std::size_t root = body;
    while (parent_[root] != root) root = parent_[root];
    while (parent_[body] != body) {
        const std::size_t next = parent_[body];
        parent_[body] = root;
        body = next;
    }
    return root;
}

void ContactIslandWorkspace::unite(std::size_t first, std::size_t second) noexcept {
    first = find(first);
    second = find(second);
    if (first == second) return;
    // The minimum body index is always the root. This makes island order canonical.
    if (second < first) std::swap(first, second);
    parent_[second] = first;
    ++stats_.union_operations;
}

void ContactIslandWorkspace::classify(
    std::size_t body_count,
    std::span<const SweptContact> contacts) {
    ++stats_.classifications;
    if (body_count > maximum_bodies_ || contacts.size() > maximum_contacts_) {
        ++stats_.capacity_failures;
        throw std::length_error("Island workspace capacity exceeded");
    }
    body_count_ = body_count;
    contact_count_ = contacts.size();
    island_count_ = 0U;
    involved_body_count_ = 0U;
    stats_.bodies_scanned += body_count;
    stats_.contacts_scanned += contacts.size();

    for (std::size_t body = 0U; body < body_count; ++body) {
        parent_[body] = body;
        degree_[body] = 0U;
        involved_[body] = 0U;
        root_to_island_[body] = kMissing;
        used_color_mask_[body] = 0U;
    }
    for (std::size_t index = 0U; index < contacts.size(); ++index) {
        contact_order_[index] = index;
        contact_colors_[index] = 0U;
    }
    std::sort(contact_order_.begin(),
              contact_order_.begin() + static_cast<std::ptrdiff_t>(contacts.size()),
              [&](std::size_t lhs_index, std::size_t rhs_index) {
        const SweptContact& lhs = contacts[lhs_index];
        const SweptContact& rhs = contacts[rhs_index];
        const auto [lhs_first, lhs_second] = canonical_pair(lhs);
        const auto [rhs_first, rhs_second] = canonical_pair(rhs);
        return std::tuple{lhs_first, lhs_second, lhs.axis, lhs_index}
             < std::tuple{rhs_first, rhs_second, rhs.axis, rhs_index};
    });

    for (std::size_t ordered = 0U; ordered < contacts.size(); ++ordered) {
        const SweptContact& contact = contacts[contact_order_[ordered]];
        if (contact.first >= body_count || contact.second >= body_count) {
            throw std::out_of_range("Contact endpoint exceeds body count");
        }
        if (contact.first == contact.second) {
            throw std::invalid_argument("Self contacts are not valid constraints");
        }
        involved_[contact.first] = 1U;
        involved_[contact.second] = 1U;
        ++degree_[contact.first];
        ++degree_[contact.second];
        unite(contact.first, contact.second);
    }

    for (std::size_t body = 0U; body < body_count; ++body) {
        if (involved_[body] == 0U) continue;
        const std::size_t root = find(body);
        if (root != body) continue;
        if (island_count_ >= maximum_contacts_) {
            ++stats_.capacity_failures;
            throw std::length_error("Too many contact islands for workspace");
        }
        root_to_island_[root] = island_count_;
        descriptors_[island_count_] = ContactIslandDescriptor{
            .minimum_body = root,
        };
        ++island_count_;
    }

    for (std::size_t body = 0U; body < body_count; ++body) {
        if (involved_[body] == 0U) continue;
        const std::size_t island = root_to_island_[find(body)];
        ++descriptors_[island].body_count;
        ++involved_body_count_;
    }
    for (std::size_t ordered = 0U; ordered < contacts.size(); ++ordered) {
        const SweptContact& contact = contacts[contact_order_[ordered]];
        const std::size_t island = root_to_island_[find(contact.first)];
        ++descriptors_[island].contact_count;
    }

    std::size_t body_offset = 0U;
    std::size_t contact_offset = 0U;
    for (std::size_t island = 0U; island < island_count_; ++island) {
        ContactIslandDescriptor& descriptor = descriptors_[island];
        descriptor.body_begin = body_offset;
        descriptor.contact_begin = contact_offset;
        body_write_cursor_[island] = body_offset;
        contact_write_cursor_[island] = contact_offset;
        body_offset += descriptor.body_count;
        contact_offset += descriptor.contact_count;
    }

    for (std::size_t body = 0U; body < body_count; ++body) {
        if (involved_[body] == 0U) continue;
        const std::size_t island = root_to_island_[find(body)];
        body_order_[body_write_cursor_[island]++] = body;
    }
    std::sort(contact_order_.begin(),
              contact_order_.begin() + static_cast<std::ptrdiff_t>(contacts.size()),
              [&](std::size_t lhs_index, std::size_t rhs_index) {
        const SweptContact& lhs = contacts[lhs_index];
        const SweptContact& rhs = contacts[rhs_index];
        const std::size_t lhs_island = root_to_island_[find(lhs.first)];
        const std::size_t rhs_island = root_to_island_[find(rhs.first)];
        const auto [lhs_first, lhs_second] = canonical_pair(lhs);
        const auto [rhs_first, rhs_second] = canonical_pair(rhs);
        return std::tuple{lhs_island, lhs_first, lhs_second, lhs.axis, lhs_index}
             < std::tuple{rhs_island, rhs_first, rhs_second, rhs.axis, rhs_index};
    });

    for (std::size_t island = 0U; island < island_count_; ++island) {
        ContactIslandDescriptor& descriptor = descriptors_[island];
        std::uint32_t maximum_degree = 0U;
        bool all_degree_two = true;
        for (std::size_t offset = 0U; offset < descriptor.body_count; ++offset) {
            const std::size_t body = body_order_[descriptor.body_begin + offset];
            maximum_degree = std::max(maximum_degree, degree_[body]);
            all_degree_two = all_degree_two && degree_[body] == 2U;
            used_color_mask_[body] = 0U;
        }
        if (descriptor.body_count == 2U && descriptor.contact_count == 1U) {
            descriptor.topology = IslandTopology::Matching;
        } else if (descriptor.contact_count + 1U == descriptor.body_count) {
            descriptor.topology = maximum_degree <= 2U
                ? IslandTopology::Chain : IslandTopology::Tree;
        } else if (descriptor.contact_count == descriptor.body_count && all_degree_two) {
            descriptor.topology = IslandTopology::Cycle;
        } else {
            descriptor.topology = IslandTopology::General;
        }

        std::size_t next_overflow_color = 64U;
        std::size_t maximum_color_plus_one = 0U;
        for (std::size_t offset = 0U; offset < descriptor.contact_count; ++offset) {
            const std::size_t ordered_index = descriptor.contact_begin + offset;
            const SweptContact& contact = contacts[contact_order_[ordered_index]];
            const std::uint64_t occupied = used_color_mask_[contact.first]
                | used_color_mask_[contact.second];
            std::size_t color = 0U;
            const std::uint64_t available = ~occupied;
            if (available != 0U) {
                color = static_cast<std::size_t>(std::countr_zero(available));
                const std::uint64_t bit = std::uint64_t{1} << color;
                used_color_mask_[contact.first] |= bit;
                used_color_mask_[contact.second] |= bit;
            } else {
                color = next_overflow_color++;
            }
            if (color > std::numeric_limits<std::uint16_t>::max()) {
                throw std::length_error("Contact coloring exceeds uint16 range");
            }
            contact_colors_[ordered_index] = static_cast<std::uint16_t>(color);
            maximum_color_plus_one = std::max(maximum_color_plus_one, color + 1U);
            ++stats_.colors_assigned;
        }
        descriptor.color_count = maximum_color_plus_one;
    }
}

std::span<const ContactIslandDescriptor> ContactIslandWorkspace::islands() const noexcept {
    return {descriptors_.data(), island_count_};
}

std::span<const std::size_t> ContactIslandWorkspace::body_order() const noexcept {
    return {body_order_.data(), involved_body_count_};
}

std::span<const std::size_t> ContactIslandWorkspace::contact_order() const noexcept {
    return {contact_order_.data(), contact_count_};
}

std::span<const std::uint16_t> ContactIslandWorkspace::contact_colors() const noexcept {
    return {contact_colors_.data(), contact_count_};
}

std::size_t ContactIslandWorkspace::reserved_bytes() const noexcept {
    return parent_.capacity() * sizeof(parent_[0])
        + degree_.capacity() * sizeof(degree_[0])
        + involved_.capacity() * sizeof(involved_[0])
        + root_to_island_.capacity() * sizeof(root_to_island_[0])
        + body_write_cursor_.capacity() * sizeof(body_write_cursor_[0])
        + contact_write_cursor_.capacity() * sizeof(contact_write_cursor_[0])
        + used_color_mask_.capacity() * sizeof(used_color_mask_[0])
        + descriptors_.capacity() * sizeof(descriptors_[0])
        + body_order_.capacity() * sizeof(body_order_[0])
        + contact_order_.capacity() * sizeof(contact_order_[0])
        + contact_colors_.capacity() * sizeof(contact_colors_[0]);
}

namespace {

constexpr std::size_t kMaximumProjectionWorkers = 64U;

struct ProjectionWorkerStats final {
    std::uint64_t islands{};
    std::uint64_t contacts{};
    std::uint64_t adjustments{};
};

[[nodiscard]] ProjectionWorkerStats run_projection_worker(
    std::span<Fixed::rep> values,
    std::span<const SweptContact> contacts,
    const ContactIslandWorkspace& workspace,
    std::size_t iterations,
    std::size_t worker,
    std::size_t worker_count) {
    const auto islands = workspace.islands();
    const auto order = workspace.contact_order();
    const auto colors = workspace.contact_colors();
    ProjectionWorkerStats local;
    for (std::size_t island_index = worker; island_index < islands.size();
         island_index += worker_count) {
        const ContactIslandDescriptor& island = islands[island_index];
        ++local.islands;
        for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
            for (std::size_t color = 0U; color < island.color_count; ++color) {
                for (std::size_t offset = 0U; offset < island.contact_count; ++offset) {
                    const std::size_t ordered_index = island.contact_begin + offset;
                    if (colors[ordered_index] != color) continue;
                    const SweptContact& contact = contacts[order[ordered_index]];
                    const auto [first, second] = canonical_pair(contact);
                    ++local.contacts;
                    if (values[first] <= values[second]) continue;
                    const WideInteger sum = static_cast<WideInteger>(values[first])
                        + static_cast<WideInteger>(values[second]);
                    const WideInteger lower = floor_div_two(sum);
                    values[first] = checked_rep(lower);
                    values[second] = checked_rep(sum - lower);
                    ++local.adjustments;
                }
            }
        }
    }
    return local;
}

[[nodiscard]] IslandProjectionStats finalize_projection_stats(
    std::span<const Fixed::rep> values,
    std::span<const SweptContact> contacts,
    std::span<const ProjectionWorkerStats> worker_stats,
    std::size_t workers_used,
    std::uint64_t violations_before) {
    IslandProjectionStats result;
    result.workers_used = workers_used;
    result.violations_before = violations_before;
    for (const ProjectionWorkerStats& local : worker_stats) {
        result.islands_processed += local.islands;
        result.contacts_processed += local.contacts;
        result.pair_adjustments += local.adjustments;
    }
    for (const SweptContact& contact : contacts) {
        const auto [first, second] = canonical_pair(contact);
        result.violations_after += values[first] > values[second] ? 1U : 0U;
    }
    return result;
}

[[nodiscard]] std::uint64_t count_violations(
    std::span<const Fixed::rep> values,
    std::span<const SweptContact> contacts) noexcept {
    std::uint64_t result = 0U;
    for (const SweptContact& contact : contacts) {
        const auto [first, second] = canonical_pair(contact);
        result += values[first] > values[second] ? 1U : 0U;
    }
    return result;
}

} // namespace

struct PersistentIslandWorkerPool::Impl final {
    struct Job final {
        Fixed::rep* values{};
        std::size_t value_count{};
        const SweptContact* contacts{};
        std::size_t contact_count{};
        const ContactIslandWorkspace* workspace{};
        std::size_t iterations{};
        std::size_t active_workers{};
    };

    explicit Impl(std::size_t worker_count)
        : stats(worker_count) {
        if (worker_count == 0U || worker_count > kMaximumProjectionWorkers) {
            throw std::invalid_argument("Persistent worker pool requires 1..64 workers");
        }
        threads.reserve(worker_count);
        for (std::size_t worker = 0U; worker < worker_count; ++worker) {
            threads.emplace_back([this, worker] { worker_loop(worker); });
        }
    }

    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            ++generation;
        }
        start.notify_all();
        for (std::thread& thread : threads) {
            if (thread.joinable()) thread.join();
        }
    }

    void worker_loop(std::size_t worker) {
        std::uint64_t observed = 0U;
        for (;;) {
            Job local_job;
            {
                std::unique_lock lock(mutex);
                start.wait(lock, [&] { return stopping || generation != observed; });
                if (stopping) return;
                observed = generation;
                local_job = job;
            }
            if (worker >= local_job.active_workers) continue;
            stats[worker] = run_projection_worker(
                {local_job.values, local_job.value_count},
                {local_job.contacts, local_job.contact_count},
                *local_job.workspace,
                local_job.iterations,
                worker,
                local_job.active_workers);
            {
                std::lock_guard lock(mutex);
                ++completed;
                if (completed == local_job.active_workers) done.notify_one();
            }
        }
    }

    [[nodiscard]] std::span<const ProjectionWorkerStats> dispatch(
        std::span<Fixed::rep> values,
        std::span<const SweptContact> contacts,
        const ContactIslandWorkspace& workspace,
        std::size_t iterations,
        std::size_t worker_count) {
        if (worker_count == 0U || worker_count > threads.size()) {
            throw std::invalid_argument("Requested workers exceed persistent pool capacity");
        }
        {
            std::lock_guard lock(mutex);
            completed = 0U;
            job = Job{
                .values = values.data(),
                .value_count = values.size(),
                .contacts = contacts.data(),
                .contact_count = contacts.size(),
                .workspace = &workspace,
                .iterations = iterations,
                .active_workers = worker_count,
            };
            for (std::size_t worker = 0U; worker < worker_count; ++worker) {
                stats[worker] = {};
            }
            ++generation;
        }
        start.notify_all();
        std::unique_lock lock(mutex);
        done.wait(lock, [&] { return completed == worker_count; });
        return {stats.data(), worker_count};
    }

    std::mutex mutex{};
    std::condition_variable start{};
    std::condition_variable done{};
    bool stopping{};
    std::uint64_t generation{};
    std::size_t completed{};
    Job job{};
    std::vector<std::thread> threads{};
    std::vector<ProjectionWorkerStats> stats{};
};

PersistentIslandWorkerPool::PersistentIslandWorkerPool(std::size_t worker_count)
    : impl_(std::make_unique<Impl>(worker_count)) {}

PersistentIslandWorkerPool::~PersistentIslandWorkerPool() = default;

std::size_t PersistentIslandWorkerPool::worker_count() const noexcept {
    return impl_->threads.size();
}

std::size_t PersistentIslandWorkerPool::reserved_bytes() const noexcept {
    return impl_->threads.capacity() * sizeof(std::thread)
        + impl_->stats.capacity() * sizeof(ProjectionWorkerStats);
}

IslandProjectionStats project_contact_islands_monotone(
    std::span<Fixed::rep> values,
    std::span<const SweptContact> contacts,
    const ContactIslandWorkspace& workspace,
    std::size_t iterations,
    std::size_t worker_count) {
    if (values.size() < workspace.body_count()) {
        throw std::invalid_argument("Projection value array is smaller than body count");
    }
    if (contacts.size() != workspace.contact_count()) {
        throw std::invalid_argument("Projection contacts differ from classified contacts");
    }
    if (iterations == 0U) {
        throw std::invalid_argument("Projection requires at least one iteration");
    }
    const auto islands = workspace.islands();
    worker_count = std::max<std::size_t>(1U, std::min(worker_count, islands.size()));
    if (islands.empty()) worker_count = 1U;
    if (worker_count > kMaximumProjectionWorkers) {
        throw std::invalid_argument("Island projection supports at most 64 workers");
    }

    const std::uint64_t violations_before = count_violations(values, contacts);
    std::array<ProjectionWorkerStats, kMaximumProjectionWorkers> worker_stats{};
    if (worker_count == 1U) {
        worker_stats[0] = run_projection_worker(
            values, contacts, workspace, iterations, 0U, 1U);
    } else {
        std::array<std::thread, kMaximumProjectionWorkers - 1U> workers{};
        for (std::size_t worker = 1U; worker < worker_count; ++worker) {
            workers[worker - 1U] = std::thread([&, worker] {
                worker_stats[worker] = run_projection_worker(
                    values, contacts, workspace, iterations, worker, worker_count);
            });
        }
        worker_stats[0] = run_projection_worker(
            values, contacts, workspace, iterations, 0U, worker_count);
        for (std::size_t worker = 1U; worker < worker_count; ++worker) {
            workers[worker - 1U].join();
        }
    }
    return finalize_projection_stats(
        values, contacts,
        {worker_stats.data(), worker_count}, worker_count, violations_before);
}

IslandProjectionStats project_contact_islands_monotone_pooled(
    std::span<Fixed::rep> values,
    std::span<const SweptContact> contacts,
    const ContactIslandWorkspace& workspace,
    std::size_t iterations,
    PersistentIslandWorkerPool& pool,
    std::size_t worker_count) {
    if (values.size() < workspace.body_count()) {
        throw std::invalid_argument("Projection value array is smaller than body count");
    }
    if (contacts.size() != workspace.contact_count()) {
        throw std::invalid_argument("Projection contacts differ from classified contacts");
    }
    if (iterations == 0U) {
        throw std::invalid_argument("Projection requires at least one iteration");
    }
    const auto islands = workspace.islands();
    worker_count = std::max<std::size_t>(1U, std::min(worker_count, islands.size()));
    if (islands.empty()) worker_count = 1U;
    worker_count = std::min(worker_count, pool.worker_count());
    const std::uint64_t violations_before = count_violations(values, contacts);
    const auto stats = pool.impl_->dispatch(
        values, contacts, workspace, iterations, worker_count);
    return finalize_projection_stats(
        values, contacts, stats, worker_count, violations_before);
}

std::uint64_t island_projection_hash(
    std::span<const Fixed::rep> values,
    const ContactIslandWorkspace& workspace) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    auto mix = [&](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xFFU;
            hash *= 1099511628211ULL;
        }
    };
    mix(values.size());
    for (const Fixed::rep value : values) mix(static_cast<std::uint64_t>(value));
    for (const ContactIslandDescriptor& island : workspace.islands()) {
        mix(island.minimum_body);
        mix(island.body_count);
        mix(island.contact_count);
        mix(island.color_count);
        mix(static_cast<std::uint64_t>(island.topology));
    }
    return hash;
}

} // namespace neoeng::core
