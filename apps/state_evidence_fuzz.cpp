#include "neoeng/core/state_evidence.hpp"
#include "neoeng/core/fuzz_cli.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace neoeng::core;

int main(int argc, char** argv) {
    std::size_t iterations{};
    if (!parse_fuzz_iteration_count(
            argc, argv, 25'000U, 1'000'000U, "neoeng_state_evidence_fuzz", iterations)) {
        return EXIT_FAILURE;
    }
    std::mt19937_64 random(0xD0045EEDULL);
    std::uint64_t accepted_valid{};
    std::uint64_t rejected_tampered{};

    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        const std::size_t body_count = 1U + static_cast<std::size_t>(random() % 48U);
        const std::size_t chunk_size = 1U + static_cast<std::size_t>(random() % 12U);
        WorldState state{.frame = iteration + 1U};
        state.bodies.reserve(body_count);
        for (std::size_t index = 0; index < body_count; ++index) {
            state.bodies.push_back({
                .id = static_cast<EntityId>(index + 1U),
                .position = {Fixed::from_raw(static_cast<std::int64_t>(random())),
                             Fixed::from_raw(static_cast<std::int64_t>(random()))},
                .velocity = {Fixed::from_raw(static_cast<std::int64_t>(random())),
                             Fixed::from_raw(static_cast<std::int64_t>(random()))},
            });
        }
        const StateMerkleSnapshot snapshot = state_merkle_sha256(state, chunk_size);
        const std::size_t chunk_index = static_cast<std::size_t>(random() % snapshot.chunk_count);
        const StateMerkleProof proof = make_state_merkle_proof(state, chunk_index, chunk_size);
        const std::size_t begin = chunk_index * chunk_size;
        const std::size_t count = std::min(chunk_size, body_count - begin);
        const std::span<const Body> chunk(state.bodies.data() + begin, count);
        if (!verify_state_merkle_proof(chunk, proof, snapshot.root)) {
            std::cerr << "valid proof rejected at iteration " << iteration << '\n';
            return EXIT_FAILURE;
        }
        ++accepted_valid;

        std::vector<Body> tampered(chunk.begin(), chunk.end());
        const std::size_t selected = static_cast<std::size_t>(random() % tampered.size());
        switch (random() % 4U) {
        case 0U: tampered[selected].position.x = Fixed::from_raw(tampered[selected].position.x.raw() ^ 1); break;
        case 1U: tampered[selected].position.y = Fixed::from_raw(tampered[selected].position.y.raw() ^ 1); break;
        case 2U: tampered[selected].velocity.x = Fixed::from_raw(tampered[selected].velocity.x.raw() ^ 1); break;
        default: tampered[selected].velocity.y = Fixed::from_raw(tampered[selected].velocity.y.raw() ^ 1); break;
        }
        if (verify_state_merkle_proof(tampered, proof, snapshot.root)) {
            std::cerr << "tampered proof accepted at iteration " << iteration << '\n';
            return EXIT_FAILURE;
        }
        ++rejected_tampered;
    }

    std::cout << "iterations=" << iterations << '\n'
              << "valid_accepted=" << accepted_valid << '\n'
              << "tampered_rejected=" << rejected_tampered << '\n';
    return EXIT_SUCCESS;
}
