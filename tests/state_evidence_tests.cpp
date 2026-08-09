#include "neoeng/core/network_security.hpp"
#include "neoeng/core/state_evidence.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace neoeng::core;

namespace {

int failures{};

#define CHECK(name, expression) do { \
    if (!(expression)) { \
        std::cerr << "FAIL " << (name) << ": " << #expression << '\n'; \
        ++failures; \
    } \
} while (false)

WorldState make_world(std::uint64_t frame, std::size_t body_count) {
    WorldState state{.frame = frame};
    state.bodies.reserve(body_count);
    for (std::size_t index = 0; index < body_count; ++index) {
        const auto signed_index = static_cast<std::int64_t>(index);
        state.bodies.push_back({
            .id = static_cast<EntityId>(index + 1U),
            .position = {
                Fixed::from_raw(1000 + signed_index * 17),
                Fixed::from_raw(-2000 - signed_index * 31),
            },
            .velocity = {
                Fixed::from_raw(3000 + signed_index * 43),
                Fixed::from_raw(-4000 + signed_index * 59),
            },
        });
    }
    return state;
}

AuthenticationKey test_key() {
    AuthenticationKey key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(index * 7U + 3U);
    }
    return key;
}

class TestHmacProvider final : public EvidenceSigner, public EvidenceSignatureVerifier {
public:
    explicit TestHmacProvider(AuthenticationKey key) : key_(key) {}

    EvidenceSignatureAlgorithm algorithm() const noexcept override {
        return EvidenceSignatureAlgorithm::HmacSha256TestOnly;
    }
    std::string_view key_id() const noexcept override { return "test-key-v1"; }
    std::vector<std::uint8_t> sign(std::span<const std::uint8_t> message) const override {
        const AuthenticationTag tag = hmac_sha256(key_, message);
        return {tag.begin(), tag.end()};
    }
    bool verify(
        std::span<const std::uint8_t> message,
        const EvidenceSignature& signature) const noexcept override {
        if (signature.algorithm != algorithm() || signature.key_id != key_id()
            || signature.bytes.size() != kSha256DigestBytes) {
            return false;
        }
        const AuthenticationTag expected = hmac_sha256(key_, message);
        return authentication_tags_equal(expected, signature.bytes);
    }

private:
    AuthenticationKey key_{};
};

class UnknownAlgorithmProvider final : public EvidenceSigner {
public:
    EvidenceSignatureAlgorithm algorithm() const noexcept override {
        return static_cast<EvidenceSignatureAlgorithm>(0xFFFFU);
    }
    std::string_view key_id() const noexcept override { return "unknown-algorithm"; }
    std::vector<std::uint8_t> sign(std::span<const std::uint8_t>) const override {
        return std::vector<std::uint8_t>(kSha256DigestBytes, 0xA5U);
    }
};

void test_sha256_vectors() {
    constexpr const char* name = "sha256_vectors";
    const std::array<std::uint8_t, 0> empty{};
    const std::array<std::uint8_t, 3> abc{'a', 'b', 'c'};
    CHECK(name, sha256_hex(sha256(empty))
        == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(name, sha256_hex(sha256(abc))
        == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    Sha256Builder split;
    split.update(std::span<const std::uint8_t>(abc.data(), 1U));
    split.update(std::span<const std::uint8_t>(abc.data() + 1U, 2U));
    CHECK(name, split.finish() == sha256(abc));
    CHECK(name, split.finish() == sha256(abc));
}

void test_merkle_proof_and_bit_tampering() {
    constexpr const char* name = "merkle_proof_and_bit_tampering";
    const WorldState world = make_world(77U, 11U);
    const StateMerkleSnapshot snapshot = state_merkle_sha256(world, 4U);
    CHECK(name, snapshot.chunk_count == 3U);
    CHECK(name, snapshot.leaf_capacity == 4U);

    const StateMerkleProof proof = make_state_merkle_proof(world, 1U, 4U);
    const std::span<const Body> chunk(world.bodies.data() + 4U, 4U);
    CHECK(name, verify_state_merkle_proof(chunk, proof, snapshot.root));

    std::vector<Body> tampered(chunk.begin(), chunk.end());
    tampered.front().velocity.x = Fixed::from_raw(tampered.front().velocity.x.raw() ^ 1);
    CHECK(name, !verify_state_merkle_proof(tampered, proof, snapshot.root));

    StateMerkleProof bad_path = proof;
    bad_path.path.front().sibling.front() ^= 1U;
    CHECK(name, !verify_state_merkle_proof(chunk, bad_path, snapshot.root));

    StateMerkleProof bad_direction = proof;
    bad_direction.path.front().sibling_on_left = !bad_direction.path.front().sibling_on_left;
    CHECK(name, !verify_state_merkle_proof(chunk, bad_direction, snapshot.root));
}

void test_signed_chain_state_verification_and_traces() {
    constexpr const char* name = "signed_chain_state_verification_and_traces";
    TestHmacProvider provider(test_key());
    TraceBuffer traces(32U);
    EvidenceChain chain(0xD004U, "neoeng-dcore-tests", 4U);
    WorldState frame10 = make_world(10U, 9U);
    WorldState frame11 = frame10;
    frame11.frame = 11U;
    frame11.bodies.at(2U).position.x = Fixed::from_raw(0x123456789LL);

    const SignedStateEvidence first = chain.append(frame10, 1001U, 900U, &provider, &traces);
    const SignedStateEvidence second = chain.append(frame11, 1002U, 901U, &provider, &traces);
    CHECK(name, second.envelope.sequence == 1U);
    const std::vector<SignedStateEvidence> two_records = chain.records();
    const EvidenceChainAnchor anchor = make_evidence_chain_anchor(two_records);
    CHECK(name, verify_evidence_chain(two_records, &provider, true, &traces, &anchor).accepted());
    CHECK(name, verify_state_evidence(frame10, first, 4U, &provider, true, &traces).accepted());
    CHECK(name, traces.by_correlation(1001U).front().code == TraceCode::EvidenceCreated);

    WorldState frame12 = frame11;
    frame12.frame = 12U;
    frame12.bodies.at(3U).velocity.x = Fixed::from_raw(4444);
    const SignedStateEvidence inserted = chain.append(frame12, 1003U, 902U, &provider);
    CHECK(name, inserted.envelope.sequence == 2U);
    CHECK(name, verify_evidence_chain(chain.records(), &provider, true, nullptr, &anchor).reason
        == EvidenceVerifyReason::AnchorMismatch);

    WorldState altered = frame10;
    altered.bodies.front().position.y = Fixed::from_raw(altered.bodies.front().position.y.raw() ^ 1);
    CHECK(name, verify_state_evidence(altered, first, 4U, &provider, true, &traces).reason
        == EvidenceVerifyReason::StableHashMismatch);

    std::vector<SignedStateEvidence> reordered = two_records;
    std::swap(reordered[0], reordered[1]);
    CHECK(name, verify_evidence_chain(reordered, &provider, true).reason
        == EvidenceVerifyReason::BranchMetadataMismatch);

    std::vector<SignedStateEvidence> removed = two_records;
    removed.erase(removed.begin());
    CHECK(name, verify_evidence_chain(removed, &provider, true).reason
        == EvidenceVerifyReason::BranchMetadataMismatch);

    std::vector<SignedStateEvidence> trailing_removed = two_records;
    trailing_removed.pop_back();
    CHECK(name, verify_evidence_chain(trailing_removed, &provider, true, nullptr, &anchor).reason
        == EvidenceVerifyReason::AnchorMismatch);

    std::vector<SignedStateEvidence> tampered = two_records;
    tampered[1].envelope.canonical_state_digest.front() ^= 1U;
    CHECK(name, verify_evidence_chain(tampered, &provider, true).reason
        == EvidenceVerifyReason::EnvelopeHashMismatch);

    std::vector<SignedStateEvidence> bad_signature = two_records;
    bad_signature[1].signature.bytes.front() ^= 1U;
    CHECK(name, verify_evidence_chain(bad_signature, &provider, true).reason
        == EvidenceVerifyReason::SignatureRejected);

    SignedStateEvidence timestamp_only = first;
    timestamp_only.envelope.monotonic_time_ns += 999U;
    CHECK(name, evidence_envelope_hash(timestamp_only.envelope) == timestamp_only.envelope_hash);
}

void test_rollback_branch_is_formally_linked() {
    constexpr const char* name = "rollback_branch_is_formally_linked";
    TestHmacProvider provider(test_key());
    EvidenceChain main_chain(1U, "main", 2U);
    const WorldState frame1 = make_world(1U, 3U);
    WorldState frame2 = frame1;
    frame2.frame = 2U;
    frame2.bodies.front().position.x = Fixed::from_raw(101U);
    const SignedStateEvidence parent = main_chain.append(frame1, 1U, 1U, &provider);
    const SignedStateEvidence main_second = main_chain.append(frame2, 2U, 2U, &provider);
    CHECK(name, main_second.envelope.sequence == 1U);

    EvidenceChain branch = EvidenceChain::fork_from(parent, 2U, "rollback-branch", 2U);
    WorldState replayed = frame1;
    replayed.frame = 2U;
    replayed.bodies.front().position.x = Fixed::from_raw(-202);
    const SignedStateEvidence child = branch.append(replayed, 3U, 3U, &provider);

    CHECK(name, verify_evidence_chain(branch.records(), &provider, true).accepted());
    CHECK(name, child.envelope.branch_id == 2U);
    CHECK(name, child.envelope.branch_parent_frame == parent.envelope.frame);
    CHECK(name, child.envelope.branch_parent_hash == parent.envelope_hash);
    CHECK(name, sha256_is_zero(child.envelope.previous_envelope_hash));
    CHECK(name, child.envelope.merkle_root != main_chain.records().back().envelope.merkle_root);

    EvidenceChain zero_main(3U, "frame-zero-main", 2U);
    const SignedStateEvidence zero_parent = zero_main.append(make_world(0U, 2U), 4U, 4U, &provider);
    EvidenceChain zero_branch = EvidenceChain::fork_from(
        zero_parent, 4U, "frame-zero-branch", 2U);
    WorldState zero_replay = make_world(1U, 2U);
    const SignedStateEvidence zero_child = zero_branch.append(zero_replay, 5U, 5U, &provider);
    CHECK(name, zero_child.envelope.branch_parent_frame == 0U);
    CHECK(name, !sha256_is_zero(zero_child.envelope.branch_parent_hash));
    CHECK(name, verify_evidence_chain(zero_branch.records(), &provider, true).accepted());

    bool same_branch_rejected = false;
    try {
        static_cast<void>(EvidenceChain::fork_from(parent, parent.envelope.branch_id,
            "invalid-same-branch", 2U));
    } catch (const std::invalid_argument&) {
        same_branch_rejected = true;
    }
    CHECK(name, same_branch_rejected);

    UnknownAlgorithmProvider unknown;
    EvidenceChain invalid_signer_chain(5U, "invalid-signer", 2U);
    bool unknown_algorithm_rejected = false;
    try {
        static_cast<void>(invalid_signer_chain.append(make_world(1U, 1U), 6U, 6U, &unknown));
    } catch (const std::invalid_argument&) {
        unknown_algorithm_rejected = true;
    }
    CHECK(name, unknown_algorithm_rejected);
}

} // namespace

int main() {
    test_sha256_vectors();
    test_merkle_proof_and_bit_tampering();
    test_signed_chain_state_verification_and_traces();
    test_rollback_branch_is_formally_linked();
    if (failures != 0) {
        std::cerr << failures << " state-evidence checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "state-evidence checks passed\n";
    return EXIT_SUCCESS;
}
