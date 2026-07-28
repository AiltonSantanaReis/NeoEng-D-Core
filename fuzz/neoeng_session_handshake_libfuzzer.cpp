#include "neoeng/core/session_security.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(
    const std::uint8_t* data,
    std::size_t size) {
    using namespace neoeng::core;
    AuthenticationKey root{};
    for (std::size_t index = 0; index < root.size(); ++index) {
        root[index] = static_cast<std::uint8_t>(index * 5U + 1U);
    }
    SessionKeyRing ring(2U);
    if (!ring.upsert({
            .key_id = 1U,
            .epoch = 1U,
            .key = root,
            .allowed_role_mask = session_role_mask(SessionRole::Player),
            .not_before_ms = 1U,
            .not_after_ms = 1'000'000U,
        })) {
        return 0;
    }
    SessionHandshakeServer server(ring, {
        .maximum_clock_skew_ms = 1'000'000U,
        .session_lifetime_ms = 10'000U,
        .maximum_root_keys = 2U,
        .maximum_handshake_replay_entries = 8U,
    });
    HandshakeNonce nonce{};
    for (std::size_t index = 0; index < nonce.size(); ++index) {
        nonce[index] = static_cast<std::uint8_t>(
            index < size ? data[index] : index + 1U);
    }
    const std::span<const std::uint8_t> datagram{
        data == nullptr ? reinterpret_cast<const std::uint8_t*>("") : data,
        size,
    };
    static_cast<void>(server.accept(
        1'000U,
        datagram,
        size == 0U ? 1U : static_cast<std::uint64_t>(data[0]) + 1U,
        nonce));
    return 0;
}
