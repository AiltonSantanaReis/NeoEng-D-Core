#include "neoeng/core/session_security.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace neoeng::core;

namespace {

std::uint64_t next_random(std::uint64_t& state) noexcept {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
}

AuthenticationKey test_key() {
    AuthenticationKey key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(index * 5U + 1U);
    }
    return key;
}

HandshakeNonce test_nonce() {
    HandshakeNonce nonce{};
    for (std::size_t index = 0; index < nonce.size(); ++index) {
        nonce[index] = static_cast<std::uint8_t>(index * 3U + 1U);
    }
    return nonce;
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t iterations = argc > 1
        ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
        : 100'000U;
    const AuthenticationKey root = test_key();
    SessionKeyRing ring(2U);
    if (!ring.upsert({
            .key_id = 1U,
            .epoch = 1U,
            .key = root,
            .allowed_role_mask = session_role_mask(SessionRole::Player),
            .not_before_ms = 1U,
            .not_after_ms = 1'000'000U,
        })) {
        return EXIT_FAILURE;
    }
    SessionHandshakeServer server(ring, {
        .maximum_clock_skew_ms = 1'000'000U,
        .session_lifetime_ms = 10'000U,
        .maximum_root_keys = 2U,
        .maximum_handshake_replay_entries = 128U,
    });
    const ClientHello valid = create_client_hello(root, {
        .origin = 9U,
        .role = SessionRole::Player,
        .key_id = 1U,
        .key_epoch = 1U,
        .sent_at_ms = 100U,
        .client_nonce = test_nonce(),
    });

    std::uint64_t random_state = 0x9E3779B97F4A7C15ULL;
    std::size_t accepted{};
    std::size_t rejected{};
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        std::vector<std::uint8_t> datagram;
        if ((iteration & 1U) == 0U) {
            datagram = valid.datagram;
            const std::size_t offset = static_cast<std::size_t>(
                next_random(random_state) % datagram.size());
            datagram[offset] ^= static_cast<std::uint8_t>(
                (next_random(random_state) & 0xFFU) | 1U);
        } else {
            const std::size_t size = static_cast<std::size_t>(next_random(random_state) % 161U);
            datagram.resize(size);
            std::generate(datagram.begin(), datagram.end(), [&random_state] {
                return static_cast<std::uint8_t>(next_random(random_state));
            });
        }
        const HandshakeNonce server_nonce = {
            static_cast<std::uint8_t>((iteration & 0xFFU) + 1U), 2U, 3U, 4U,
            5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U, 13U, 14U, 15U, 16U,
        };
        const ServerHandshakeDecision decision = server.accept(
            100U, datagram, static_cast<std::uint64_t>(iteration) + 1U, server_nonce);
        if (decision.accepted()) {
            ++accepted;
        } else {
            ++rejected;
        }
    }
    std::cout << "iterations=" << iterations << '\n'
              << "accepted=" << accepted << '\n'
              << "rejected=" << rejected << '\n';
    return accepted + rejected == iterations ? EXIT_SUCCESS : EXIT_FAILURE;
}
