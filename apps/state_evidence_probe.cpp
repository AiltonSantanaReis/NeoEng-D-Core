#include "neoeng/core/state_evidence.hpp"

#include <cstdlib>
#include <iostream>

using namespace neoeng::core;

namespace {
WorldState probe_world(std::uint64_t frame) {
    return {
        .frame = frame,
        .bodies = {
            {.id = 1U,
             .position = {Fixed::from_raw(11), Fixed::from_raw(-22)},
             .velocity = {Fixed::from_raw(33), Fixed::from_raw(-44)}},
            {.id = 2U,
             .position = {Fixed::from_raw(55), Fixed::from_raw(-66)},
             .velocity = {Fixed::from_raw(77), Fixed::from_raw(-88)}},
            {.id = 3U,
             .position = {Fixed::from_raw(99), Fixed::from_raw(-111)},
             .velocity = {Fixed::from_raw(123), Fixed::from_raw(-135)}},
        },
    };
}
} // namespace

int main() {
    const WorldState frame40 = probe_world(40U);
    WorldState frame41 = frame40;
    frame41.frame = 41U;
    frame41.bodies.at(1U).velocity.y = Fixed::from_raw(-999);

    EvidenceChain main_chain(100U, "state-evidence-probe", 2U);
    const SignedStateEvidence parent = main_chain.append(frame40, 700U, 1'000U);
    const SignedStateEvidence latest = main_chain.append(frame41, 701U, 1'001U);
    const StateMerkleProof proof = make_state_merkle_proof(frame41, 1U, 2U);
    const std::span<const Body> final_chunk(frame41.bodies.data() + 2U, 1U);

    EvidenceChain branch = EvidenceChain::fork_from(parent, 101U, "state-evidence-probe", 2U);
    WorldState replayed = frame40;
    replayed.frame = 41U;
    replayed.bodies.at(1U).velocity.y = Fixed::from_raw(777);
    const SignedStateEvidence branch_record = branch.append(replayed, 702U, 1'002U);

    const bool proof_ok = verify_state_merkle_proof(final_chunk, proof, latest.envelope.merkle_root);
    const bool chain_ok = verify_evidence_chain(main_chain.records()).accepted();
    const bool branch_ok = verify_evidence_chain(branch.records()).accepted();

    std::cout << "schema=neoeng.dcore.state-evidence-chain.v1\n"
              << "canonical_sha256=" << sha256_hex(latest.envelope.canonical_state_digest) << '\n'
              << "merkle_root_sha256=" << sha256_hex(latest.envelope.merkle_root) << '\n'
              << "envelope_sha256=" << sha256_hex(latest.envelope_hash) << '\n'
              << "branch_parent_sha256=" << sha256_hex(branch_record.envelope.branch_parent_hash) << '\n'
              << "branch_envelope_sha256=" << sha256_hex(branch_record.envelope_hash) << '\n'
              << "proof=" << (proof_ok ? "accepted" : "rejected") << '\n'
              << "chain=" << (chain_ok ? "accepted" : "rejected") << '\n'
              << "branch=" << (branch_ok ? "accepted" : "rejected") << '\n';
    return proof_ok && chain_ok && branch_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
