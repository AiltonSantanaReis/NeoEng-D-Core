#include "neoeng/core/state_evidence.hpp"

#include "neoeng/core/diagnostics.hpp"
#include "neoeng/core/hash.hpp"
#include "neoeng/core/simulation.hpp"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace neoeng::core {
namespace {

constexpr std::string_view kLeafDomain{"NEOENG-DCORE-STATE-LEAF-V1"};
constexpr std::string_view kEmptyDomain{"NEOENG-DCORE-STATE-EMPTY-V1"};
constexpr std::string_view kNodeDomain{"NEOENG-DCORE-STATE-NODE-V1"};
constexpr std::string_view kRootDomain{"NEOENG-DCORE-STATE-ROOT-V1"};
constexpr std::string_view kEnvelopeDomain{"NEOENG-DCORE-EVIDENCE-ENVELOPE-V1"};

void append_bytes(std::vector<std::uint8_t>& output, std::string_view text) {
    output.insert(output.end(), text.begin(), text.end());
}

void append_bytes(std::vector<std::uint8_t>& output, const Sha256Digest& digest) {
    output.insert(output.end(), digest.begin(), digest.end());
}

template <typename T>
void append_little_endian(std::vector<std::uint8_t>& output, T value) {
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(U); ++index) {
        output.push_back(static_cast<std::uint8_t>(bits & U{0xFF}));
        bits >>= 8U;
    }
}

void append_body(std::vector<std::uint8_t>& output, const Body& body) {
    append_little_endian(output, body.id);
    append_little_endian(output, body.position.x.raw());
    append_little_endian(output, body.position.y.raw());
    append_little_endian(output, body.velocity.x.raw());
    append_little_endian(output, body.velocity.y.raw());
}

[[nodiscard]] std::size_t checked_chunk_count(std::size_t body_count, std::size_t chunk_size) {
    if (chunk_size == 0U) {
        throw std::invalid_argument("Merkle chunk size must be greater than zero");
    }
    return body_count == 0U ? 0U : 1U + (body_count - 1U) / chunk_size;
}

[[nodiscard]] Sha256Digest hash_leaf(
    std::size_t chunk_index,
    std::size_t first_body,
    std::span<const Body> bodies) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kLeafDomain.size() + 32U + bodies.size() * 40U);
    append_bytes(bytes, kLeafDomain);
    append_little_endian(bytes, kStateMerkleFormatVersion);
    append_little_endian(bytes, static_cast<std::uint64_t>(chunk_index));
    append_little_endian(bytes, static_cast<std::uint64_t>(first_body));
    append_little_endian(bytes, static_cast<std::uint64_t>(bodies.size()));
    for (const Body& body : bodies) {
        append_body(bytes, body);
    }
    return sha256(bytes);
}

[[nodiscard]] Sha256Digest hash_empty_leaf(std::size_t chunk_index) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kEmptyDomain.size() + 16U);
    append_bytes(bytes, kEmptyDomain);
    append_little_endian(bytes, kStateMerkleFormatVersion);
    append_little_endian(bytes, static_cast<std::uint64_t>(chunk_index));
    return sha256(bytes);
}

[[nodiscard]] Sha256Digest hash_node(
    std::size_t level,
    const Sha256Digest& left,
    const Sha256Digest& right) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kNodeDomain.size() + 72U);
    append_bytes(bytes, kNodeDomain);
    append_little_endian(bytes, kStateMerkleFormatVersion);
    append_little_endian(bytes, static_cast<std::uint64_t>(level));
    append_bytes(bytes, left);
    append_bytes(bytes, right);
    return sha256(bytes);
}

[[nodiscard]] Sha256Digest hash_root(
    std::uint64_t frame,
    std::size_t body_count,
    std::size_t chunk_size,
    std::size_t chunk_count,
    std::size_t leaf_capacity,
    const Sha256Digest& tree_root) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kRootDomain.size() + 80U);
    append_bytes(bytes, kRootDomain);
    append_little_endian(bytes, kStateMerkleFormatVersion);
    append_little_endian(bytes, kCanonicalWorldFormatVersion);
    append_little_endian(bytes, frame);
    append_little_endian(bytes, static_cast<std::uint64_t>(body_count));
    append_little_endian(bytes, static_cast<std::uint64_t>(chunk_size));
    append_little_endian(bytes, static_cast<std::uint64_t>(chunk_count));
    append_little_endian(bytes, static_cast<std::uint64_t>(leaf_capacity));
    append_bytes(bytes, tree_root);
    return sha256(bytes);
}

struct MerkleLevels final {
    std::size_t chunk_count{};
    std::size_t leaf_capacity{};
    std::vector<std::vector<Sha256Digest>> levels{};
};

[[nodiscard]] MerkleLevels build_levels(const WorldState& state, std::size_t chunk_size) {
    validate_world(state);
    const std::size_t chunk_count = checked_chunk_count(state.bodies.size(), chunk_size);
    const std::size_t leaf_capacity = chunk_count <= 1U ? 1U : std::bit_ceil(chunk_count);

    MerkleLevels result{.chunk_count = chunk_count, .leaf_capacity = leaf_capacity};
    result.levels.emplace_back();
    result.levels.front().reserve(leaf_capacity);
    for (std::size_t index = 0; index < leaf_capacity; ++index) {
        if (index >= chunk_count) {
            result.levels.front().push_back(hash_empty_leaf(index));
            continue;
        }
        const std::size_t begin = index * chunk_size;
        const std::size_t end = std::min(begin + chunk_size, state.bodies.size());
        result.levels.front().push_back(hash_leaf(
            index, begin,
            std::span<const Body>(state.bodies.data() + begin, end - begin)));
    }

    std::size_t level = 0U;
    while (result.levels.back().size() > 1U) {
        const auto& current = result.levels.back();
        std::vector<Sha256Digest> next;
        next.reserve(current.size() / 2U);
        for (std::size_t index = 0; index < current.size(); index += 2U) {
            next.push_back(hash_node(level, current[index], current[index + 1U]));
        }
        result.levels.push_back(std::move(next));
        ++level;
    }
    return result;
}

[[nodiscard]] bool metadata_valid(const StateEvidenceEnvelope& envelope) noexcept {
    return envelope.schema_version == kStateEvidenceSchemaVersion
        && envelope.branch_id != 0U
        && !envelope.producer_id.empty()
        && envelope.producer_id.size() <= kMaximumEvidenceProducerIdBytes;
}

[[nodiscard]] bool signature_algorithm_valid(EvidenceSignatureAlgorithm algorithm) noexcept {
    switch (algorithm) {
    case EvidenceSignatureAlgorithm::HmacSha256TestOnly:
    case EvidenceSignatureAlgorithm::Ed25519:
    case EvidenceSignatureAlgorithm::EcdsaP256Sha256:
    case EvidenceSignatureAlgorithm::RsaPssSha256:
    case EvidenceSignatureAlgorithm::ExternalProviderPrivate:
        return true;
    case EvidenceSignatureAlgorithm::None:
    default:
        return false;
    }
}

[[nodiscard]] bool signature_metadata_valid(const EvidenceSignature& signature) noexcept {
    const bool none = signature.algorithm == EvidenceSignatureAlgorithm::None;
    if (none) {
        return signature.key_id.empty() && signature.bytes.empty();
    }
    if (!signature_algorithm_valid(signature.algorithm)) {
        return false;
    }
    return !signature.key_id.empty()
        && signature.key_id.size() <= kMaximumEvidenceKeyIdBytes
        && !signature.bytes.empty()
        && signature.bytes.size() <= kMaximumEvidenceSignatureBytes;
}

void record_evidence_trace(
    TraceBuffer* traces,
    const StateEvidenceEnvelope* envelope,
    TraceOutcome outcome,
    TraceCode code) noexcept {
    if (traces == nullptr) {
        return;
    }
    traces->record({
        .correlation_id = envelope == nullptr ? 0U : envelope->correlation_id,
        .frame = envelope == nullptr ? 0U : envelope->frame,
        .monotonic_time_ns = envelope == nullptr ? 0U : envelope->monotonic_time_ns,
        .category = TraceCategory::Evidence,
        .outcome = outcome,
        .code = code,
    });
}

[[nodiscard]] EvidenceVerifyResult fail(
    EvidenceVerifyReason reason,
    std::size_t index,
    TraceBuffer* traces,
    const StateEvidenceEnvelope* envelope,
    TraceCode code) noexcept {
    record_evidence_trace(traces, envelope, TraceOutcome::Rejected, code);
    return {.reason = reason, .record_index = index};
}

[[nodiscard]] std::size_t utf8_sequence_length(std::string_view value, std::size_t index) noexcept {
    const auto byte = [&value](std::size_t offset) {
        return static_cast<unsigned char>(value[offset]);
    };
    const unsigned char lead = byte(index);
    std::size_t length{};
    if (lead < 0x80U) {
        return 1U;
    } else if (lead >= 0xC2U && lead <= 0xDFU) {
        length = 2U;
    } else if (lead >= 0xE0U && lead <= 0xEFU) {
        length = 3U;
    } else if (lead >= 0xF0U && lead <= 0xF4U) {
        length = 4U;
    } else {
        return 0U;
    }
    if (index + length > value.size()) {
        return 0U;
    }
    for (std::size_t offset = 1U; offset < length; ++offset) {
        if ((byte(index + offset) & 0xC0U) != 0x80U) {
            return 0U;
        }
    }
    const unsigned char second = byte(index + 1U);
    if ((length == 3U && ((lead == 0xE0U && second < 0xA0U)
                          || (lead == 0xEDU && second > 0x9FU)))
        || (length == 4U && ((lead == 0xF0U && second < 0x90U)
                             || (lead == 0xF4U && second > 0x8FU)))) {
        return 0U;
    }
    return length;
}

void append_json_string(std::ostringstream& stream, std::string_view value) {
    stream << '"';
    for (std::size_t index = 0U; index < value.size();) {
        const char character = value[index];
        switch (character) {
        case '\\': stream << "\\\\"; ++index; break;
        case '"': stream << "\\\""; ++index; break;
        case '<': stream << "\\u003c"; ++index; break;
        case '>': stream << "\\u003e"; ++index; break;
        case '&': stream << "\\u0026"; ++index; break;
        case '\n': stream << "\\n"; ++index; break;
        case '\r': stream << "\\r"; ++index; break;
        case '\t': stream << "\\t"; ++index; break;
        default:
            if (static_cast<unsigned char>(character) < 0x20U) {
                stream << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<unsigned int>(static_cast<unsigned char>(character))
                       << std::dec;
                ++index;
            } else if (static_cast<unsigned char>(character) < 0x80U) {
                stream << character;
                ++index;
            } else {
                const std::size_t length = utf8_sequence_length(value, index);
                if (length == 0U) {
                    stream << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                           << static_cast<unsigned int>(static_cast<unsigned char>(character))
                           << std::dec;
                    ++index;
                } else {
                    stream.write(value.data() + index, static_cast<std::streamsize>(length));
                    index += length;
                }
            }
        }
    }
    stream << '"';
}

} // namespace

Sha256Digest canonical_state_sha256(const WorldState& state) {
    validate_world(state);
    return sha256(canonical_serialize(state));
}

StateMerkleSnapshot state_merkle_sha256(const WorldState& state, std::size_t chunk_size) {
    const MerkleLevels levels = build_levels(state, chunk_size);
    return {
        .root = hash_root(state.frame, state.bodies.size(), chunk_size,
            levels.chunk_count, levels.leaf_capacity, levels.levels.back().front()),
        .chunk_size = chunk_size,
        .chunk_count = levels.chunk_count,
        .leaf_capacity = levels.leaf_capacity,
    };
}

StateMerkleProof make_state_merkle_proof(
    const WorldState& state,
    std::size_t chunk_index,
    std::size_t chunk_size) {
    const MerkleLevels levels = build_levels(state, chunk_size);
    if (chunk_index >= levels.chunk_count) {
        throw std::out_of_range("Merkle proof chunk index is outside the state");
    }
    StateMerkleProof proof{
        .frame = state.frame,
        .body_count = state.bodies.size(),
        .chunk_size = chunk_size,
        .chunk_count = levels.chunk_count,
        .leaf_capacity = levels.leaf_capacity,
        .chunk_index = chunk_index,
    };
    std::size_t cursor = chunk_index;
    for (std::size_t level = 0; level + 1U < levels.levels.size(); ++level) {
        const std::size_t sibling_index = cursor ^ 1U;
        proof.path.push_back({
            .sibling = levels.levels[level][sibling_index],
            .sibling_on_left = sibling_index < cursor,
        });
        cursor /= 2U;
    }
    return proof;
}

bool verify_state_merkle_proof(
    std::span<const Body> chunk_bodies,
    const StateMerkleProof& proof,
    const Sha256Digest& expected_root) noexcept {
    try {
        if (proof.format_version != kStateMerkleFormatVersion
            || proof.chunk_size == 0U
            || proof.chunk_count == 0U
            || proof.chunk_index >= proof.chunk_count
            || proof.leaf_capacity < proof.chunk_count
            || !std::has_single_bit(proof.leaf_capacity)) {
            return false;
        }
        const std::size_t expected_path = static_cast<std::size_t>(std::bit_width(proof.leaf_capacity) - 1);
        if (proof.path.size() != expected_path) {
            return false;
        }
        const std::size_t first_body = proof.chunk_index * proof.chunk_size;
        if (first_body >= proof.body_count) {
            return false;
        }
        const std::size_t expected_bodies = std::min(
            proof.chunk_size, proof.body_count - first_body);
        if (chunk_bodies.size() != expected_bodies) {
            return false;
        }

        Sha256Digest current = hash_leaf(proof.chunk_index, first_body, chunk_bodies);
        std::size_t cursor = proof.chunk_index;
        for (std::size_t level = 0; level < proof.path.size(); ++level) {
            const bool expected_left = (cursor & 1U) != 0U;
            if (proof.path[level].sibling_on_left != expected_left) {
                return false;
            }
            current = expected_left
                ? hash_node(level, proof.path[level].sibling, current)
                : hash_node(level, current, proof.path[level].sibling);
            cursor /= 2U;
        }
        const Sha256Digest wrapped = hash_root(
            proof.frame, proof.body_count, proof.chunk_size, proof.chunk_count,
            proof.leaf_capacity, current);
        return sha256_equal(wrapped, expected_root);
    } catch (...) {
        return false;
    }
}

std::vector<std::uint8_t> canonical_evidence_envelope_bytes(
    const StateEvidenceEnvelope& envelope) {
    if (!metadata_valid(envelope)) {
        throw std::invalid_argument("Evidence envelope metadata is invalid");
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kEnvelopeDomain.size() + 256U + envelope.producer_id.size());
    append_bytes(bytes, kEnvelopeDomain);
    append_little_endian(bytes, envelope.schema_version);
    append_little_endian(bytes, std::uint16_t{0});
    append_little_endian(bytes, envelope.sequence);
    append_little_endian(bytes, envelope.frame);
    append_little_endian(bytes, envelope.body_count);
    append_little_endian(bytes, envelope.stable_state_hash);
    append_bytes(bytes, envelope.canonical_state_digest);
    append_bytes(bytes, envelope.merkle_root);
    append_bytes(bytes, envelope.previous_envelope_hash);
    append_little_endian(bytes, envelope.branch_id);
    append_bytes(bytes, envelope.branch_parent_hash);
    append_little_endian(bytes, envelope.branch_parent_frame);
    append_little_endian(bytes, envelope.correlation_id);
    if (envelope.producer_id.size() > std::numeric_limits<std::uint16_t>::max()) {
        throw std::length_error("Evidence producer ID exceeds wire format");
    }
    append_little_endian(bytes, static_cast<std::uint16_t>(envelope.producer_id.size()));
    bytes.insert(bytes.end(), envelope.producer_id.begin(), envelope.producer_id.end());
    return bytes;
}

Sha256Digest evidence_envelope_hash(const StateEvidenceEnvelope& envelope) {
    return sha256(canonical_evidence_envelope_bytes(envelope));
}

EvidenceVerifyResult verify_state_evidence(
    const WorldState& state,
    const SignedStateEvidence& evidence,
    std::size_t chunk_size,
    const EvidenceSignatureVerifier* verifier,
    bool require_signature,
    TraceBuffer* traces) noexcept {
    try {
        if (!metadata_valid(evidence.envelope)) {
            return fail(EvidenceVerifyReason::InvalidMetadata, 0U, traces,
                &evidence.envelope, TraceCode::EvidenceVerificationFailed);
        }
        const Sha256Digest computed_envelope = evidence_envelope_hash(evidence.envelope);
        if (!sha256_equal(computed_envelope, evidence.envelope_hash)) {
            return fail(EvidenceVerifyReason::EnvelopeHashMismatch, 0U, traces,
                &evidence.envelope, TraceCode::EvidenceVerificationFailed);
        }
        if (!signature_metadata_valid(evidence.signature)) {
            return fail(EvidenceVerifyReason::InvalidMetadata, 0U, traces,
                &evidence.envelope, TraceCode::EvidenceSignatureRejected);
        }
        const bool has_signature = evidence.signature.algorithm != EvidenceSignatureAlgorithm::None;
        if (require_signature && !has_signature) {
            return fail(EvidenceVerifyReason::SignatureRequired, 0U, traces,
                &evidence.envelope, TraceCode::EvidenceSignatureRejected);
        }
        if (require_signature && verifier == nullptr) {
            return fail(EvidenceVerifyReason::SignatureRejected, 0U, traces,
                &evidence.envelope, TraceCode::EvidenceSignatureRejected);
        }
        if (has_signature && verifier != nullptr
            && !verifier->verify(evidence.envelope_hash, evidence.signature)) {
            return fail(EvidenceVerifyReason::SignatureRejected, 0U, traces,
                &evidence.envelope, TraceCode::EvidenceSignatureRejected);
        }
        if (evidence.envelope.frame != state.frame
            || evidence.envelope.body_count != state.bodies.size()
            || evidence.envelope.stable_state_hash != stable_hash(state)) {
            return fail(EvidenceVerifyReason::StableHashMismatch, 0U, traces,
                &evidence.envelope, TraceCode::StateDivergence);
        }
        if (!sha256_equal(evidence.envelope.canonical_state_digest, canonical_state_sha256(state))) {
            return fail(EvidenceVerifyReason::CanonicalDigestMismatch, 0U, traces,
                &evidence.envelope, TraceCode::StateDivergence);
        }
        if (!sha256_equal(evidence.envelope.merkle_root,
                state_merkle_sha256(state, chunk_size).root)) {
            return fail(EvidenceVerifyReason::MerkleRootMismatch, 0U, traces,
                &evidence.envelope, TraceCode::MerkleProofRejected);
        }
        return {};
    } catch (...) {
        return fail(EvidenceVerifyReason::InvalidMetadata, 0U, traces,
            &evidence.envelope, TraceCode::EvidenceVerificationFailed);
    }
}

EvidenceVerifyResult verify_evidence_chain(
    std::span<const SignedStateEvidence> records,
    const EvidenceSignatureVerifier* verifier,
    bool require_signatures,
    TraceBuffer* traces,
    const EvidenceChainAnchor* expected_anchor) noexcept {
    Sha256Digest previous{};
    EvidenceBranchId branch_id{};
    Sha256Digest parent_hash{};
    std::uint64_t parent_frame{};
    for (std::size_t index = 0; index < records.size(); ++index) {
        const SignedStateEvidence& record = records[index];
        try {
            if (record.envelope.schema_version != kStateEvidenceSchemaVersion) {
                return fail(EvidenceVerifyReason::InvalidSchema, index, traces,
                    &record.envelope, TraceCode::EvidenceVerificationFailed);
            }
            if (!metadata_valid(record.envelope)) {
                return fail(EvidenceVerifyReason::InvalidMetadata, index, traces,
                    &record.envelope, TraceCode::EvidenceVerificationFailed);
            }
            if (index == 0U) {
                branch_id = record.envelope.branch_id;
                parent_hash = record.envelope.branch_parent_hash;
                parent_frame = record.envelope.branch_parent_frame;
                if (record.envelope.sequence != 0U
                    || !sha256_is_zero(record.envelope.previous_envelope_hash)
                    || (sha256_is_zero(parent_hash) && parent_frame != 0U)) {
                    return fail(EvidenceVerifyReason::BranchMetadataMismatch, index, traces,
                        &record.envelope, TraceCode::EvidenceChainBroken);
                }
            } else {
                if (record.envelope.sequence != index) {
                    return fail(EvidenceVerifyReason::SequenceMismatch, index, traces,
                        &record.envelope, TraceCode::EvidenceChainBroken);
                }
                if (!sha256_equal(record.envelope.previous_envelope_hash, previous)) {
                    return fail(EvidenceVerifyReason::PreviousHashMismatch, index, traces,
                        &record.envelope, TraceCode::EvidenceChainBroken);
                }
                if (record.envelope.branch_id != branch_id
                    || !sha256_equal(record.envelope.branch_parent_hash, parent_hash)
                    || record.envelope.branch_parent_frame != parent_frame) {
                    return fail(EvidenceVerifyReason::BranchMetadataMismatch, index, traces,
                        &record.envelope, TraceCode::EvidenceChainBroken);
                }
            }
            const Sha256Digest computed = evidence_envelope_hash(record.envelope);
            if (!sha256_equal(computed, record.envelope_hash)) {
                return fail(EvidenceVerifyReason::EnvelopeHashMismatch, index, traces,
                    &record.envelope, TraceCode::EvidenceVerificationFailed);
            }
            if (!signature_metadata_valid(record.signature)) {
                return fail(EvidenceVerifyReason::InvalidMetadata, index, traces,
                    &record.envelope, TraceCode::EvidenceSignatureRejected);
            }
            const bool has_signature = record.signature.algorithm != EvidenceSignatureAlgorithm::None;
            if (require_signatures && !has_signature) {
                return fail(EvidenceVerifyReason::SignatureRequired, index, traces,
                    &record.envelope, TraceCode::EvidenceSignatureRejected);
            }
            if (require_signatures && verifier == nullptr) {
                return fail(EvidenceVerifyReason::SignatureRejected, index, traces,
                    &record.envelope, TraceCode::EvidenceSignatureRejected);
            }
            if (has_signature && verifier != nullptr
                && !verifier->verify(record.envelope_hash, record.signature)) {
                return fail(EvidenceVerifyReason::SignatureRejected, index, traces,
                    &record.envelope, TraceCode::EvidenceSignatureRejected);
            }
            previous = record.envelope_hash;
        } catch (...) {
            return fail(EvidenceVerifyReason::InvalidMetadata, index, traces,
                &record.envelope, TraceCode::EvidenceVerificationFailed);
        }
    }
    if (expected_anchor != nullptr) {
        const bool empty_mismatch = records.empty()
            || expected_anchor->schema_version != kStateEvidenceSchemaVersion;
        if (empty_mismatch
            || expected_anchor->record_count != records.size()
            || expected_anchor->branch_id != branch_id
            || !sha256_equal(expected_anchor->head_envelope_hash, previous)
            || !sha256_equal(expected_anchor->branch_parent_hash, parent_hash)
            || expected_anchor->branch_parent_frame != parent_frame) {
            const StateEvidenceEnvelope* envelope = records.empty() ? nullptr : &records.back().envelope;
            return fail(EvidenceVerifyReason::AnchorMismatch, records.size(), traces,
                envelope, TraceCode::EvidenceChainBroken);
        }
    }
    return {};
}

EvidenceChainAnchor make_evidence_chain_anchor(
    std::span<const SignedStateEvidence> records) {
    if (records.empty()) {
        throw std::invalid_argument("Cannot anchor an empty evidence chain");
    }
    const EvidenceVerifyResult verification = verify_evidence_chain(records);
    if (!verification.accepted()) {
        throw std::invalid_argument("Cannot anchor an invalid evidence chain");
    }
    const SignedStateEvidence& first = records.front();
    const SignedStateEvidence& last = records.back();
    return {
        .branch_id = first.envelope.branch_id,
        .record_count = static_cast<std::uint64_t>(records.size()),
        .head_envelope_hash = last.envelope_hash,
        .branch_parent_hash = first.envelope.branch_parent_hash,
        .branch_parent_frame = first.envelope.branch_parent_frame,
    };
}

EvidenceChain::EvidenceChain(
    EvidenceBranchId branch_id,
    std::string producer_id,
    std::size_t merkle_chunk_size)
    : EvidenceChain(branch_id, std::move(producer_id), merkle_chunk_size, {}, 0U) {}

EvidenceChain::EvidenceChain(
    EvidenceBranchId branch_id,
    std::string producer_id,
    std::size_t merkle_chunk_size,
    Sha256Digest branch_parent_hash,
    std::uint64_t branch_parent_frame)
    : branch_id_(branch_id),
      producer_id_(std::move(producer_id)),
      merkle_chunk_size_(merkle_chunk_size),
      branch_parent_hash_(branch_parent_hash),
      branch_parent_frame_(branch_parent_frame) {
    if (branch_id_ == 0U) {
        throw std::invalid_argument("Evidence branch ID must be nonzero");
    }
    if (producer_id_.empty() || producer_id_.size() > kMaximumEvidenceProducerIdBytes) {
        throw std::invalid_argument("Evidence producer ID is invalid");
    }
    if (merkle_chunk_size_ == 0U) {
        throw std::invalid_argument("Evidence Merkle chunk size must be nonzero");
    }
    if (sha256_is_zero(branch_parent_hash_) && branch_parent_frame_ != 0U) {
        throw std::invalid_argument("Evidence branch parent metadata is inconsistent");
    }
}

EvidenceChain EvidenceChain::fork_from(
    const SignedStateEvidence& parent,
    EvidenceBranchId branch_id,
    std::string producer_id,
    std::size_t merkle_chunk_size) {
    if (branch_id == parent.envelope.branch_id) {
        throw std::invalid_argument("Evidence fork branch ID must differ from parent");
    }
    if (!metadata_valid(parent.envelope)
        || !sha256_equal(parent.envelope_hash, evidence_envelope_hash(parent.envelope))) {
        throw std::invalid_argument("Evidence branch parent is invalid");
    }
    return EvidenceChain(branch_id, std::move(producer_id), merkle_chunk_size,
        parent.envelope_hash, parent.envelope.frame);
}

SignedStateEvidence EvidenceChain::append(
    const WorldState& state,
    CorrelationId correlation_id,
    std::uint64_t monotonic_time_ns,
    const EvidenceSigner* signer,
    TraceBuffer* traces) {
    const BudgetMonitor budget_monitor;
    ScopedBudgetMeasurement budget_scope(
        traces != nullptr ? &budget_monitor : nullptr,
        traces,
        {
            .id = BudgetId::EvidenceCheckpoint,
            .subsystem = TraceSubsystem::Evidence,
            .limit_ns = 0U,
            .exceed_severity = TraceSeverity::Warning,
        },
        correlation_id,
        state.frame);
    validate_world(state);
    const StateMerkleSnapshot merkle = state_merkle_sha256(state, merkle_chunk_size_);
    SignedStateEvidence record;
    record.envelope = {
        .sequence = static_cast<std::uint64_t>(records_.size()),
        .frame = state.frame,
        .body_count = static_cast<std::uint64_t>(state.bodies.size()),
        .stable_state_hash = stable_hash(state),
        .canonical_state_digest = canonical_state_sha256(state),
        .merkle_root = merkle.root,
        .previous_envelope_hash = records_.empty() ? Sha256Digest{} : records_.back().envelope_hash,
        .branch_id = branch_id_,
        .branch_parent_hash = branch_parent_hash_,
        .branch_parent_frame = branch_parent_frame_,
        .correlation_id = correlation_id,
        .producer_id = producer_id_,
        .monotonic_time_ns = monotonic_time_ns,
    };
    record.envelope_hash = evidence_envelope_hash(record.envelope);
    if (signer != nullptr) {
        const std::string_view key = signer->key_id();
        if (!signature_algorithm_valid(signer->algorithm())
            || key.empty() || key.size() > kMaximumEvidenceKeyIdBytes) {
            throw std::invalid_argument("Evidence signer metadata is invalid");
        }
        record.signature = {
            .algorithm = signer->algorithm(),
            .key_id = std::string(key),
            .bytes = signer->sign(record.envelope_hash),
        };
        if (record.signature.bytes.empty()
            || record.signature.bytes.size() > kMaximumEvidenceSignatureBytes) {
            throw std::runtime_error("Evidence signer returned an invalid signature");
        }
    }
    records_.push_back(std::move(record));
    record_evidence_trace(traces, &records_.back().envelope,
        TraceOutcome::Applied, TraceCode::EvidenceCreated);
    return records_.back();
}

std::string export_evidence_chain_json(std::span<const SignedStateEvidence> records) {
    std::ostringstream stream;
    stream << "{\n  \"schema\": \"neoeng.dcore.state-evidence-chain.v1\",\n"
           << "  \"records\": [";
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto& record = records[index];
        stream << (index == 0U ? "\n" : ",\n")
               << "    {\"sequence\":" << record.envelope.sequence
               << ",\"frame\":" << record.envelope.frame
               << ",\"body_count\":" << record.envelope.body_count
               << ",\"stable_hash\":\"" << hash_hex(record.envelope.stable_state_hash)
               << "\",\"canonical_sha256\":\"" << sha256_hex(record.envelope.canonical_state_digest)
               << "\",\"merkle_root_sha256\":\"" << sha256_hex(record.envelope.merkle_root)
               << "\",\"previous_envelope_sha256\":\"" << sha256_hex(record.envelope.previous_envelope_hash)
               << "\",\"envelope_sha256\":\"" << sha256_hex(record.envelope_hash)
               << "\",\"branch_id\":" << record.envelope.branch_id
               << ",\"branch_parent_sha256\":\"" << sha256_hex(record.envelope.branch_parent_hash)
               << "\",\"branch_parent_frame\":" << record.envelope.branch_parent_frame
               << ",\"correlation_id\":" << record.envelope.correlation_id
               << ",\"producer_id\":";
        append_json_string(stream, record.envelope.producer_id);
        stream << ",\"monotonic_time_ns\":" << record.envelope.monotonic_time_ns
               << ",\"signature_algorithm\":\"" << to_string(record.signature.algorithm)
               << "\",\"signature_key_id\":";
        append_json_string(stream, record.signature.key_id);
        stream << ",\"signature_hex\":\"";
        stream << std::hex << std::setfill('0');
        for (const std::uint8_t byte : record.signature.bytes) {
            stream << std::setw(2) << static_cast<unsigned int>(byte);
        }
        stream << std::dec << "\"}";
    }
    if (!records.empty()) {
        stream << '\n';
    }
    stream << "  ]\n}\n";
    return stream.str();
}

const char* to_string(EvidenceSignatureAlgorithm algorithm) noexcept {
    switch (algorithm) {
    case EvidenceSignatureAlgorithm::None: return "none";
    case EvidenceSignatureAlgorithm::HmacSha256TestOnly: return "hmac-sha256-test-only";
    case EvidenceSignatureAlgorithm::Ed25519: return "ed25519";
    case EvidenceSignatureAlgorithm::EcdsaP256Sha256: return "ecdsa-p256-sha256";
    case EvidenceSignatureAlgorithm::RsaPssSha256: return "rsa-pss-sha256";
    case EvidenceSignatureAlgorithm::ExternalProviderPrivate: return "external-provider-private";
    }
    return "unknown";
}

const char* to_string(EvidenceVerifyReason reason) noexcept {
    switch (reason) {
    case EvidenceVerifyReason::None: return "none";
    case EvidenceVerifyReason::InvalidSchema: return "invalid_schema";
    case EvidenceVerifyReason::InvalidMetadata: return "invalid_metadata";
    case EvidenceVerifyReason::EnvelopeHashMismatch: return "envelope_hash_mismatch";
    case EvidenceVerifyReason::SequenceMismatch: return "sequence_mismatch";
    case EvidenceVerifyReason::PreviousHashMismatch: return "previous_hash_mismatch";
    case EvidenceVerifyReason::BranchMetadataMismatch: return "branch_metadata_mismatch";
    case EvidenceVerifyReason::SignatureRequired: return "signature_required";
    case EvidenceVerifyReason::SignatureRejected: return "signature_rejected";
    case EvidenceVerifyReason::StableHashMismatch: return "stable_hash_mismatch";
    case EvidenceVerifyReason::CanonicalDigestMismatch: return "canonical_digest_mismatch";
    case EvidenceVerifyReason::MerkleRootMismatch: return "merkle_root_mismatch";
    case EvidenceVerifyReason::AnchorMismatch: return "anchor_mismatch";
    }
    return "unknown";
}

} // namespace neoeng::core
