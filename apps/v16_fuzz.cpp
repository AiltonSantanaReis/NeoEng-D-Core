#include "neoeng/core/atomic_temporal_physics.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace neoeng::core;
constexpr std::int32_t kOne = 1 << 30;
constexpr std::array<NormalQ30, 4> kNormals{{
    {kOne, 0}, {0, kOne}, {759'250'125, 759'250'125}, {644'245'094, 858'993'459}
}};

class Rng final {
public:
    explicit Rng(std::uint64_t state) : state_(state) {}
    std::uint64_t next() noexcept {
        state_ ^= state_ << 13U; state_ ^= state_ >> 7U; state_ ^= state_ << 17U;
        return state_;
    }
    std::size_t bounded(std::size_t bound) noexcept { return static_cast<std::size_t>(next() % bound); }
private:
    std::uint64_t state_;
};

Fixed::rep mul_q30(std::int32_t normal, Fixed::rep magnitude) {
    return static_cast<Fixed::rep>(static_cast<WideInteger>(normal) * magnitude / (WideInteger{1} << 30U));
}

struct Dataset final {
    std::vector<Fixed::rep> px, py, vx, vy;
    std::vector<std::uint32_t> masses;
    std::vector<NormalContact> contacts;
};

Dataset make_dataset(std::size_t bodies, Rng& rng) {
    Dataset data;
    data.px.resize(bodies); data.py.resize(bodies); data.vx.resize(bodies); data.vy.resize(bodies);
    data.masses.resize(bodies); data.contacts.reserve(bodies / 2U);
    const Fixed::rep separation = Fixed::from_ratio(3, 4).raw();
    const Fixed::rep speed = Fixed::from_ratio(1, 16).raw();
    constexpr std::size_t columns = 8U;
    for (std::size_t pair = 0U; pair < bodies / 2U; ++pair) {
        const std::size_t first = pair * 2U;
        const std::size_t second = first + 1U;
        const NormalQ30 normal = kNormals[rng.bounded(kNormals.size())];
        const Fixed::rep center_x = Fixed::from_integer(static_cast<Fixed::rep>((pair % columns) * 4U)).raw();
        const Fixed::rep center_y = Fixed::from_integer(static_cast<Fixed::rep>((pair / columns) * 4U)).raw();
        const Fixed::rep dx = mul_q30(normal.x, separation);
        const Fixed::rep dy = mul_q30(normal.y, separation);
        data.px[first] = center_x - dx / 2; data.py[first] = center_y - dy / 2;
        data.px[second] = center_x + dx / 2; data.py[second] = center_y + dy / 2;
        data.vx[first] = mul_q30(normal.x, speed); data.vy[first] = mul_q30(normal.y, speed);
        data.vx[second] = -data.vx[first]; data.vy[second] = -data.vy[first];
        data.masses[first] = 1U + static_cast<std::uint32_t>(rng.bounded(16U));
        data.masses[second] = 1U + static_cast<std::uint32_t>(rng.bounded(16U));
        data.contacts.push_back({first, second, normal});
    }
    return data;
}

std::uint64_t mix(std::uint64_t hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
    return hash;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t scenarios = argc > 1 ? static_cast<std::size_t>(std::stoull(argv[1])) : 500U;
    std::uint64_t aggregate = 0xCBF29CE484222325ULL;
    for (std::size_t scenario = 0U; scenario < scenarios; ++scenario) {
        Rng rng(0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(scenario + 1U));
        const std::size_t bodies = 16U + 2U * rng.bounded(17U);
        Dataset data = make_dataset(bodies, rng);
        AtomicTemporalPhysicsConfig temporal_config{
            .bodies = bodies, .contacts = data.contacts.size(),
            .maximum_candidate_pairs = data.contacts.size() * 3U + 32U,
            .history_capacity = 32U, .horizon_frames = 16U,
            .maximum_velocity_mutations = 2U, .maximum_mass_mutations = 1U,
            .maximum_contact_mutations = 1U,
            .half_extent = Fixed::from_ratio(1, 2),
            .projection = {.maximum_iterations = 32U, .feasibility_tolerance_raw = 16U},
            .force_rebuild_each_frame = false,
        };
        AtomicTemporalPhysicsConfig rebuild_config = temporal_config;
        rebuild_config.force_rebuild_each_frame = true;
        AtomicTemporalPhysicsEngine temporal(temporal_config), clean(temporal_config), rebuild(rebuild_config);
        temporal.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        clean.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);
        rebuild.initialize(data.px, data.py, data.vx, data.vy, data.masses, data.contacts);

        std::array<VelocityMutation, 12> original{};
        std::array<VelocityMutation, 12> corrected{};
        for (std::uint64_t frame = 1U; frame <= original.size(); ++frame) {
            const std::size_t body = rng.bounded(bodies);
            const Fixed::rep dx = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng.next() % 2001U) - 1000);
            const Fixed::rep dy = static_cast<Fixed::rep>(static_cast<std::int64_t>(rng.next() % 2001U) - 1000);
            original[frame - 1U] = {body, dx, dy}; corrected[frame - 1U] = original[frame - 1U];
        }
        corrected[4].delta_x += 777;
        corrected[4].delta_y -= 313;
        for (std::uint64_t frame = 1U; frame <= original.size(); ++frame) {
            temporal.set_input(frame, {.velocity = std::span<const VelocityMutation>(&original[frame - 1U], 1U)});
            clean.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
            rebuild.set_input(frame, {.velocity = std::span<const VelocityMutation>(&corrected[frame - 1U], 1U)});
        }
        temporal.simulate_to(original.size());
        clean.simulate_to(original.size());
        rebuild.simulate_to(original.size());
        temporal.correct_and_resimulate(5U,
            {.velocity = std::span<const VelocityMutation>(&corrected[4], 1U)}, original.size());
        if (!temporal.equivalent_to(clean)) {
            throw std::runtime_error("v0.16 temporal rollback diverged from clean temporal execution");
        }
        if (!temporal.physically_equivalent_to(rebuild)) {
            throw std::runtime_error("v0.16 temporal cache diverged from full broadphase rebuild");
        }
        aggregate = mix(aggregate, temporal.hash());
        aggregate = mix(aggregate, rebuild.physical_hash());
        aggregate = mix(aggregate, temporal.stats().broadphase_builds);
        aggregate = mix(aggregate, temporal.stats().broadphase_reuses);
    }
    std::cout << "NeoEng v0.16 fuzz scenarios=" << scenarios
              << " aggregate=0x" << std::hex << std::uppercase << aggregate << std::dec << '\n';
    return EXIT_SUCCESS;
}
