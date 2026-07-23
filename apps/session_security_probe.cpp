#include "neoeng/core/network_security.hpp"
#include "neoeng/core/session_security.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

using namespace neoeng::core;

namespace {

AuthenticationKey seeded_key(std::uint8_t seed) {
    AuthenticationKey key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(seed + index * 9U);
    }
    return key;
}

HandshakeNonce seeded_nonce(std::uint8_t seed) {
    HandshakeNonce nonce{};
    for (std::size_t index = 0; index < nonce.size(); ++index) {
        nonce[index] = static_cast<std::uint8_t>(seed + index * 13U);
    }
    return nonce;
}

} // namespace

int main() {
    const AuthenticationKey root = seeded_key(7U);
    SessionKeyRing ring(2U);
    if (!ring.upsert({
            .key_id = 1U,
            .epoch = 1U,
            .key = root,
            .allowed_role_mask = session_role_mask(SessionRole::Player),
            .not_before_ms = 1'000U,
            .not_after_ms = 100'000U,
        })) {
        return EXIT_FAILURE;
    }
    SessionHandshakeServer server(ring, {
        .maximum_clock_skew_ms = 1'000U,
        .session_lifetime_ms = 30'000U,
        .maximum_root_keys = 2U,
        .maximum_handshake_replay_entries = 16U,
    });
    const ClientHello hello = create_client_hello(root, {
        .origin = 77U,
        .role = SessionRole::Player,
        .key_id = 1U,
        .key_epoch = 1U,
        .sent_at_ms = 2'000U,
        .client_nonce = seeded_nonce(17U),
    });
    const ServerHandshakeDecision server_decision = server.accept(
        2'001U, hello.datagram, 0xA11CEU, seeded_nonce(51U));
    const ClientHandshakeDecision client_decision = finalize_client_handshake(
        root, hello.context, 2'002U, server_decision.response);
    if (!server_decision.accepted() || !client_decision.accepted()
        || server_decision.secrets.client_to_server_key
            != client_decision.secrets.client_to_server_key) {
        return EXIT_FAILURE;
    }

    NetworkSecurityGateway gateway(seeded_key(201U), {
        .maximum_payload_bytes = 64U,
        .maximum_input_commands = 4U,
        .maximum_tracked_origins = 4U,
        .maximum_clock_skew_ms = 1'000U,
        .session_timeout_ms = 60'000U,
        .rate_limit_packets_per_second = 60U,
        .rate_limit_burst_packets = 60U,
        .require_established_session = true,
    });
    if (!gateway.install_session(77U, server_decision.secrets.gateway_binding(), 2'001U)) {
        return EXIT_FAILURE;
    }
    const std::array<std::uint8_t, 2> payload{0xD0U, 0xC0U};
    const auto packet = encode_authenticated_packet(
        client_decision.secrets.client_to_server_key, 0xA11CEU, 1U, 2'010U, payload);
    const PacketDecision accepted = gateway.process(77U, 2'010U, packet);
    const PacketDecision replay = gateway.process(77U, 2'011U, packet);
    const ServerHandshakeDecision handshake_replay = server.accept(
        2'012U, hello.datagram, 0xA11CFU, seeded_nonce(52U));

    std::cout << "session_handshake=" << to_string(server_decision.reason) << '\n'
              << "client_finalize=" << to_string(client_decision.reason) << '\n'
              << "packet=" << to_string(accepted.reason) << '\n'
              << "packet_replay=" << to_string(replay.reason) << '\n'
              << "handshake_replay=" << to_string(handshake_replay.reason) << '\n'
              << "key_epoch=" << server_decision.secrets.key_epoch << '\n'
              << "expires_at_ms=" << server_decision.secrets.expires_at_ms << '\n';
    return accepted.accepted()
        && replay.reason == PacketRejectReason::ReplayDuplicate
        && handshake_replay.reason == HandshakeRejectReason::ReplayDetected
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
