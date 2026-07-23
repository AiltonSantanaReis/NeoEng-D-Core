#pragma once

#include "neoeng/core/crypto_hash.hpp"
#include "neoeng/core/observability.hpp"
#include "neoeng/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neoeng::core {

inline constexpr std::uint16_t kStateEvidenceSchemaVersion = 1U;
inline constexpr std::uint16_t kStateMerkleFormatVersion = 1U;
inline constexpr std::size_t kDefaultStateMerkleChunkBodies = 64U;
inline constexpr std::size_t kMaximumEvidenceProducerIdBytes = 128U;
inline constexpr std::size_t kMaximumEvidenceKeyIdBytes = 128U;
inline constexpr std::size_t kMaximumEvidenceSignatureBytes = 4'096U;

using EvidenceBranchId = std::uint64_t;

enum class EvidenceSignatureAlgorithm : std::uint16_t {
    None = 0U,
    HmacSha256TestOnly = 1U,
    Ed25519 = 0x0101U,
    EcdsaP256Sha256 = 0x0102U,
    RsaPssSha256 = 0x0103U,
    ExternalProviderPrivate = 0x8000U,
};

struct EvidenceSignature final {
    EvidenceSignatureAlgorithm algorithm{EvidenceSignatureAlgorithm::None};
    std::string key_id{};
    std::vector<std::uint8_t> bytes{};
};

class EvidenceSigner {
public:
    virtual ~EvidenceSigner() = default;
    [[nodiscard]] virtual EvidenceSignatureAlgorithm algorithm() const noexcept = 0;
    [[nodiscard]] virtual std::string_view key_id() const noexcept = 0;
    [[nodiscard]] virtual std::vector<std::uint8_t> sign(
        std::span<const std::uint8_t> message) const = 0;
};

class EvidenceSignatureVerifier {
public:
    virtual ~EvidenceSignatureVerifier() = default;
    [[nodiscard]] virtual bool verify(
        std::span<const std::uint8_t> message,
        const EvidenceSignature& signature) const noexcept = 0;
};

struct StateMerkleSnapshot final {
    Sha256Digest root{};
    std::size_t chunk_size{};
    std::size_t chunk_count{};
    std::size_t leaf_capacity{};
};

struct StateMerkleProofStep final {
    Sha256Digest sibling{};
    bool sibling_on_left{};
};

struct StateMerkleProof final {
    std::uint16_t format_version{kStateMerkleFormatVersion};
    std::uint64_t frame{};
    std::size_t body_count{};
    std::size_t chunk_size{};
    std::size_t chunk_count{};
    std::size_t leaf_capacity{};
    std::size_t chunk_index{};
    std::vector<StateMerkleProofStep> path{};
};

[[nodiscard]] Sha256Digest canonical_state_sha256(const WorldState& state);
[[nodiscard]] StateMerkleSnapshot state_merkle_sha256(
    const WorldState& state,
    std::size_t chunk_size = kDefaultStateMerkleChunkBodies);
[[nodiscard]] StateMerkleProof make_state_merkle_proof(
    const WorldState& state,
    std::size_t chunk_index,
    std::size_t chunk_size = kDefaultStateMerkleChunkBodies);
[[nodiscard]] bool verify_state_merkle_proof(
    std::span<const Body> chunk_bodies,
    const StateMerkleProof& proof,
    const Sha256Digest& expected_root) noexcept;

struct StateEvidenceEnvelope final {
    std::uint16_t schema_version{kStateEvidenceSchemaVersion};
    std::uint64_t sequence{};
    std::uint64_t frame{};
    std::uint64_t body_count{};
    std::uint64_t stable_state_hash{};
    Sha256Digest canonical_state_digest{};
    Sha256Digest merkle_root{};
    Sha256Digest previous_envelope_hash{};
    EvidenceBranchId branch_id{};
    Sha256Digest branch_parent_hash{};
    std::uint64_t branch_parent_frame{};
    CorrelationId correlation_id{};
    std::string producer_id{};
    std::uint64_t monotonic_time_ns{}; // metadata; excluded from canonical envelope hash
};

struct SignedStateEvidence final {
    StateEvidenceEnvelope envelope{};
    Sha256Digest envelope_hash{};
    EvidenceSignature signature{};
};

enum class EvidenceVerifyReason : std::uint8_t {
    None,
    InvalidSchema,
    InvalidMetadata,
    EnvelopeHashMismatch,
    SequenceMismatch,
    PreviousHashMismatch,
    BranchMetadataMismatch,
    SignatureRequired,
    SignatureRejected,
    StableHashMismatch,
    CanonicalDigestMismatch,
    MerkleRootMismatch,
    AnchorMismatch,
};


struct EvidenceChainAnchor final {
    std::uint16_t schema_version{kStateEvidenceSchemaVersion};
    EvidenceBranchId branch_id{};
    std::uint64_t record_count{};
    Sha256Digest head_envelope_hash{};
    Sha256Digest branch_parent_hash{};
    std::uint64_t branch_parent_frame{};
};

struct EvidenceVerifyResult final {
    EvidenceVerifyReason reason{EvidenceVerifyReason::None};
    std::size_t record_index{};

    [[nodiscard]] bool accepted() const noexcept {
        return reason == EvidenceVerifyReason::None;
    }
};

[[nodiscard]] std::vector<std::uint8_t> canonical_evidence_envelope_bytes(
    const StateEvidenceEnvelope& envelope);
[[nodiscard]] Sha256Digest evidence_envelope_hash(const StateEvidenceEnvelope& envelope);
[[nodiscard]] EvidenceVerifyResult verify_state_evidence(
    const WorldState& state,
    const SignedStateEvidence& evidence,
    std::size_t chunk_size = kDefaultStateMerkleChunkBodies,
    const EvidenceSignatureVerifier* verifier = nullptr,
    bool require_signature = false,
    TraceBuffer* traces = nullptr) noexcept;
[[nodiscard]] EvidenceVerifyResult verify_evidence_chain(
    std::span<const SignedStateEvidence> records,
    const EvidenceSignatureVerifier* verifier = nullptr,
    bool require_signatures = false,
    TraceBuffer* traces = nullptr,
    const EvidenceChainAnchor* expected_anchor = nullptr) noexcept;

[[nodiscard]] EvidenceChainAnchor make_evidence_chain_anchor(
    std::span<const SignedStateEvidence> records);

class EvidenceChain final {
public:
    EvidenceChain(
        EvidenceBranchId branch_id,
        std::string producer_id,
        std::size_t merkle_chunk_size = kDefaultStateMerkleChunkBodies);

    [[nodiscard]] static EvidenceChain fork_from(
        const SignedStateEvidence& parent,
        EvidenceBranchId branch_id,
        std::string producer_id,
        std::size_t merkle_chunk_size = kDefaultStateMerkleChunkBodies);

    [[nodiscard]] SignedStateEvidence append(
        const WorldState& state,
        CorrelationId correlation_id = 0U,
        std::uint64_t monotonic_time_ns = 0U,
        const EvidenceSigner* signer = nullptr,
        TraceBuffer* traces = nullptr);

    [[nodiscard]] const std::vector<SignedStateEvidence>& records() const noexcept {
        return records_;
    }
    [[nodiscard]] EvidenceBranchId branch_id() const noexcept { return branch_id_; }
    [[nodiscard]] std::size_t merkle_chunk_size() const noexcept { return merkle_chunk_size_; }

private:
    EvidenceChain(
        EvidenceBranchId branch_id,
        std::string producer_id,
        std::size_t merkle_chunk_size,
        Sha256Digest branch_parent_hash,
        std::uint64_t branch_parent_frame);

    EvidenceBranchId branch_id_{};
    std::string producer_id_{};
    std::size_t merkle_chunk_size_{};
    Sha256Digest branch_parent_hash_{};
    std::uint64_t branch_parent_frame_{};
    std::vector<SignedStateEvidence> records_{};
};

[[nodiscard]] std::string export_evidence_chain_json(
    std::span<const SignedStateEvidence> records);
[[nodiscard]] const char* to_string(EvidenceSignatureAlgorithm algorithm) noexcept;
[[nodiscard]] const char* to_string(EvidenceVerifyReason reason) noexcept;

} // namespace neoeng::core
