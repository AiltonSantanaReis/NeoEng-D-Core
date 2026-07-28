#include "neoeng/core/production_security.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace neoeng::core {
namespace {

constexpr std::string_view kBundleDomain{"NEOENG-DCORE-PROTECTED-BUNDLE-V1"};
constexpr std::string_view kAnchorDomain{"NEOENG-DCORE-EVIDENCE-ANCHOR-V1"};

template <typename T>
void append_little_endian(std::vector<std::uint8_t>& output, T value) {
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(U); ++index) {
        output.push_back(static_cast<std::uint8_t>(bits & U{0xFF}));
        bits >>= 8U;
    }
}

void append_bytes(std::vector<std::uint8_t>& output, std::string_view value) {
    output.insert(output.end(), value.begin(), value.end());
}

void append_bytes(std::vector<std::uint8_t>& output, const Sha256Digest& value) {
    output.insert(output.end(), value.begin(), value.end());
}

void append_sized_bytes(
    std::vector<std::uint8_t>& output,
    std::span<const std::uint8_t> value) {
    append_little_endian(output, static_cast<std::uint64_t>(value.size()));
    output.insert(output.end(), value.begin(), value.end());
}

void append_sized_string(std::vector<std::uint8_t>& output, std::string_view value) {
    append_little_endian(output, static_cast<std::uint64_t>(value.size()));
    append_bytes(output, value);
}

template <typename T>
bool read_little_endian(
    std::span<const std::uint8_t> input,
    std::size_t& cursor,
    T& value) noexcept {
    using U = std::make_unsigned_t<T>;
    static_assert(sizeof(U) <= sizeof(std::uintmax_t));
    if (cursor > input.size() || input.size() - cursor < sizeof(U)) {
        return false;
    }
    std::uintmax_t bits{};
    for (std::size_t index = 0; index < sizeof(U); ++index) {
        bits |= static_cast<std::uintmax_t>(input[cursor + index])
            << (8U * index);
    }
    cursor += sizeof(U);
    value = static_cast<T>(static_cast<U>(bits));
    return true;
}

bool read_sized_bytes(
    std::span<const std::uint8_t> input,
    std::size_t& cursor,
    std::size_t maximum,
    std::vector<std::uint8_t>& output) {
    std::uint64_t size{};
    if (!read_little_endian(input, cursor, size)
        || size > maximum
        || size > input.size() - cursor) {
        return false;
    }
    const std::size_t native_size = static_cast<std::size_t>(size);
    output.assign(input.begin() + static_cast<std::ptrdiff_t>(cursor),
        input.begin() + static_cast<std::ptrdiff_t>(cursor + native_size));
    cursor += native_size;
    return true;
}

bool read_sized_string(
    std::span<const std::uint8_t> input,
    std::size_t& cursor,
    std::size_t maximum,
    std::string& output) {
    std::vector<std::uint8_t> bytes;
    if (!read_sized_bytes(input, cursor, maximum, bytes)) {
        return false;
    }
    output.assign(bytes.begin(), bytes.end());
    return true;
}

[[nodiscard]] bool digest_is_nonzero(const Sha256Digest& digest) noexcept {
    return std::any_of(digest.begin(), digest.end(),
        [](std::uint8_t byte) { return byte != 0U; });
}

[[nodiscard]] bool role_valid(SessionRole role) noexcept {
    return session_role_mask(role) != 0U;
}

[[nodiscard]] bool operation_valid(SecurityOperation operation) noexcept {
    switch (operation) {
    case SecurityOperation::SubmitInput:
    case SecurityOperation::ReadSnapshot:
    case SecurityOperation::ReadTrace:
    case SecurityOperation::ExportEvidence:
    case SecurityOperation::ExportSupportBundle:
    case SecurityOperation::ManageKeys:
    case SecurityOperation::RecoverRuntime:
        return true;
    }
    return false;
}

[[nodiscard]] bool rule_valid(const AuthorizationRule& rule) noexcept {
    return rule.rule_id != 0U
        && role_valid(rule.role)
        && operation_valid(rule.operation)
        && rule.minimum_key_epoch != 0U
        && rule.maximum_key_epoch >= rule.minimum_key_epoch
        && (rule.any_entity || (
            rule.first_entity != 0U && rule.last_entity >= rule.first_entity))
        && rule.not_after_ms > rule.not_before_ms;
}

[[nodiscard]] bool provider_algorithm_allowed(
    ArtifactEncryptionAlgorithm algorithm,
    const BundleProtectionPolicy& policy) noexcept {
    switch (algorithm) {
    case ArtifactEncryptionAlgorithm::Aes256Gcm:
    case ArtifactEncryptionAlgorithm::ChaCha20Poly1305:
    case ArtifactEncryptionAlgorithm::ExternalProviderPrivate:
        return true;
    case ArtifactEncryptionAlgorithm::DeterministicTestOnly:
        return policy.allow_test_provider;
    case ArtifactEncryptionAlgorithm::None:
        return false;
    }
    return false;
}

[[nodiscard]] bool key_active_for(
    const ExternalKeyDescriptor& key,
    ExternalKeyPurpose purpose,
    std::uint64_t now_ms) noexcept {
    return external_key_descriptor_valid(key)
        && key.purpose == purpose
        && key.lifecycle == ExternalKeyLifecycle::Active
        && now_ms >= key.not_before_ms
        && now_ms < key.not_after_ms;
}

[[nodiscard]] std::vector<std::uint8_t> canonical_bundle_bytes(
    const SupportBundleArtifact& bundle,
    std::size_t maximum_bytes) {
    std::vector<std::uint8_t> bytes;
    append_bytes(bytes, kBundleDomain);
    append_little_endian(bytes, kProductionSecuritySchemaVersion);
    append_little_endian(bytes, static_cast<std::uint64_t>(bundle.entries.size()));
    for (const SupportBundleEntry& entry : bundle.entries) {
        append_sized_string(bytes, entry.path);
        append_sized_string(bytes, entry.content);
        append_bytes(bytes, entry.sha256);
        if (bytes.size() > maximum_bytes) {
            throw std::length_error("support bundle exceeds protection limit");
        }
    }
    append_sized_string(bytes, bundle.manifest_json);
    append_bytes(bytes, bundle.manifest_sha256);
    if (bytes.size() > maximum_bytes) {
        throw std::length_error("support bundle exceeds protection limit");
    }
    return bytes;
}

[[nodiscard]] bool decode_bundle_bytes(
    std::span<const std::uint8_t> bytes,
    std::size_t maximum_bytes,
    SupportBundleArtifact& bundle) {
    if (bytes.size() > maximum_bytes
        || bytes.size() < kBundleDomain.size() + sizeof(std::uint16_t)) {
        return false;
    }
    if (!std::equal(kBundleDomain.begin(), kBundleDomain.end(), bytes.begin())) {
        return false;
    }
    std::size_t cursor = kBundleDomain.size();
    std::uint16_t version{};
    std::uint64_t entry_count{};
    if (!read_little_endian(bytes, cursor, version)
        || version != kProductionSecuritySchemaVersion
        || !read_little_endian(bytes, cursor, entry_count)
        || entry_count > 1'024U) {
        return false;
    }

    SupportBundleArtifact decoded;
    decoded.entries.reserve(static_cast<std::size_t>(entry_count));
    for (std::uint64_t index = 0U; index < entry_count; ++index) {
        SupportBundleEntry entry;
        if (!read_sized_string(bytes, cursor, 4'096U, entry.path)
            || !read_sized_string(bytes, cursor, maximum_bytes, entry.content)
            || cursor > bytes.size()
            || bytes.size() - cursor < entry.sha256.size()) {
            return false;
        }
        std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
            entry.sha256.size(), entry.sha256.begin());
        cursor += entry.sha256.size();
        decoded.entries.push_back(std::move(entry));
    }
    if (!read_sized_string(bytes, cursor, maximum_bytes, decoded.manifest_json)
        || cursor > bytes.size()
        || bytes.size() - cursor != decoded.manifest_sha256.size()) {
        return false;
    }
    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
        decoded.manifest_sha256.size(), decoded.manifest_sha256.begin());
    bundle = std::move(decoded);
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> bundle_aad(
    ArtifactEncryptionAlgorithm algorithm,
    std::string_view key_id,
    std::uint32_t key_epoch,
    std::span<const std::uint8_t> nonce,
    const Sha256Digest& plaintext_sha256,
    const Sha256Digest& manifest_sha256) {
    std::vector<std::uint8_t> aad;
    append_bytes(aad, kBundleDomain);
    append_little_endian(aad, kProductionSecuritySchemaVersion);
    append_little_endian(aad, static_cast<std::uint16_t>(algorithm));
    append_sized_string(aad, key_id);
    append_little_endian(aad, key_epoch);
    append_sized_bytes(aad, nonce);
    append_bytes(aad, plaintext_sha256);
    append_bytes(aad, manifest_sha256);
    return aad;
}

[[nodiscard]] std::vector<std::uint8_t> canonical_anchor_bytes(
    const EvidenceChainAnchor& anchor) {
    std::vector<std::uint8_t> bytes;
    append_bytes(bytes, kAnchorDomain);
    append_little_endian(bytes, kProductionSecuritySchemaVersion);
    append_little_endian(bytes, anchor.schema_version);
    append_little_endian(bytes, anchor.branch_id);
    append_little_endian(bytes, anchor.record_count);
    append_bytes(bytes, anchor.head_envelope_hash);
    append_bytes(bytes, anchor.branch_parent_hash);
    append_little_endian(bytes, anchor.branch_parent_frame);
    return bytes;
}

[[nodiscard]] bool anchor_valid(const EvidenceChainAnchor& anchor) noexcept {
    return anchor.schema_version == kStateEvidenceSchemaVersion
        && anchor.branch_id != 0U
        && anchor.record_count != 0U
        && digest_is_nonzero(anchor.head_envelope_hash);
}

} // namespace

CommandAuthorizationPolicy::CommandAuthorizationPolicy(
    std::size_t maximum_rules,
    bool require_confidential_transport)
    : maximum_rules_(maximum_rules),
      require_confidential_transport_(require_confidential_transport) {
    if (maximum_rules == 0U || maximum_rules > kMaximumAuthorizationRules) {
        throw std::invalid_argument("authorization rule capacity is invalid");
    }
    rules_.reserve(maximum_rules);
}

bool CommandAuthorizationPolicy::add_rule(AuthorizationRule rule) noexcept {
    if (!rule_valid(rule) || rules_.size() >= maximum_rules_) {
        return false;
    }
    if (std::any_of(rules_.begin(), rules_.end(),
        [&rule](const AuthorizationRule& current) {
            return current.rule_id == rule.rule_id;
        })) {
        return false;
    }
    try {
        rules_.push_back(rule);
        return true;
    } catch (const std::bad_alloc&) {
        return false;
    }
}

AuthorizationDecision CommandAuthorizationPolicy::authorize(
    const AuthorizationRequest& request) const noexcept {
    if (!role_valid(request.subject.role)
        || request.subject.origin == 0U
        || request.subject.key_id == 0U
        || request.subject.key_epoch == 0U
        || !operation_valid(request.operation)) {
        return {.reason = AuthorizationReason::InvalidRequest};
    }
    if (require_confidential_transport_
        && !transport_security_context_valid(request.transport)) {
        return {.reason = AuthorizationReason::InsecureTransport};
    }

    bool subject_operation_match{};
    bool key_match{};
    bool entity_match{};
    bool inactive_match{};
    for (const AuthorizationRule& rule : rules_) {
        if (rule.role != request.subject.role
            || rule.operation != request.operation
            || (rule.origin != 0U && rule.origin != request.subject.origin)) {
            continue;
        }
        subject_operation_match = true;
        if ((rule.key_id != 0U && rule.key_id != request.subject.key_id)
            || request.subject.key_epoch < rule.minimum_key_epoch
            || request.subject.key_epoch > rule.maximum_key_epoch) {
            continue;
        }
        key_match = true;
        if (!rule.any_entity
            && (request.entity < rule.first_entity
                || request.entity > rule.last_entity)) {
            continue;
        }
        entity_match = true;
        if (request.now_ms < rule.not_before_ms
            || request.now_ms >= rule.not_after_ms) {
            inactive_match = true;
            continue;
        }
        return {
            .reason = AuthorizationReason::None,
            .rule_id = rule.rule_id,
        };
    }
    if (inactive_match) {
        return {.reason = AuthorizationReason::RuleNotActive};
    }
    if (entity_match || key_match) {
        return {.reason = entity_match
            ? AuthorizationReason::RuleNotFound
            : AuthorizationReason::EntityDenied};
    }
    if (subject_operation_match) {
        return {.reason = AuthorizationReason::KeyDenied};
    }
    return {.reason = AuthorizationReason::RuleNotFound};
}

AuthorizationDecision CommandAuthorizationPolicy::authorize_input_batch(
    const AuthorizationSubject& subject,
    const TransportSecurityContext& transport,
    std::uint64_t now_ms,
    std::span<const InputCommand> commands) const noexcept {
    if (commands.empty()) {
        return {.reason = AuthorizationReason::InvalidRequest};
    }
    for (std::size_t index = 0U; index < commands.size(); ++index) {
        AuthorizationDecision decision = authorize({
            .subject = subject,
            .operation = SecurityOperation::SubmitInput,
            .entity = commands[index].entity,
            .now_ms = now_ms,
            .transport = transport,
        });
        if (!decision.accepted()) {
            decision.command_index = index;
            return decision;
        }
    }
    return {};
}

bool transport_security_context_valid(
    const TransportSecurityContext& context) noexcept {
    return context.transport != ConfidentialTransport::None
        && context.confidentiality_protected
        && context.peer_authenticated
        && context.channel_bound
        && digest_is_nonzero(context.channel_binding);
}

bool external_key_descriptor_valid(const ExternalKeyDescriptor& key) noexcept {
    return !key.key_id.empty()
        && key.key_id.size() <= kMaximumEvidenceKeyIdBytes
        && key.epoch != 0U
        && key.not_after_ms > key.not_before_ms
        && key.provider_backed
        && !key.private_material_exportable;
}

ProtectSupportBundleResult protect_support_bundle(
    const SupportBundleArtifact& bundle,
    const SupportBundlePolicy& bundle_policy,
    const BundleProtectionPolicy& protection_policy,
    const ExternalKeyDescriptor& key,
    std::uint64_t now_ms,
    std::span<const std::uint8_t> nonce,
    const ArtifactEncryptionProvider& provider) noexcept {
    try {
        if (!verify_support_bundle(bundle, bundle_policy).accepted()) {
            return {.reason = BundleProtectionReason::InvalidBundle};
        }
        if (!external_key_descriptor_valid(key)) {
            return {.reason = BundleProtectionReason::InvalidKeyDescriptor};
        }
        if (!key_active_for(key, ExternalKeyPurpose::BundleEncryption, now_ms)) {
            return {.reason = BundleProtectionReason::KeyNotActive};
        }
        if (provider.key_id() != key.key_id) {
            return {.reason = BundleProtectionReason::ProviderMismatch};
        }
        const ArtifactEncryptionAlgorithm algorithm = provider.algorithm();
        if (!provider_algorithm_allowed(algorithm, protection_policy)) {
            return {.reason = BundleProtectionReason::UnsupportedAlgorithm};
        }
        if (nonce.size() < 12U || nonce.size() > 32U
            || std::none_of(nonce.begin(), nonce.end(),
                [](std::uint8_t byte) { return byte != 0U; })) {
            return {.reason = BundleProtectionReason::InvalidNonce};
        }
        const std::vector<std::uint8_t> plaintext = canonical_bundle_bytes(
            bundle, protection_policy.maximum_plaintext_bytes);
        const Sha256Digest plaintext_digest = sha256(plaintext);
        const std::vector<std::uint8_t> aad = bundle_aad(
            algorithm, key.key_id, key.epoch, nonce,
            plaintext_digest, bundle.manifest_sha256);
        ProviderSealedBytes sealed = provider.seal(plaintext, aad, nonce);
        if (sealed.ciphertext.empty()
            || sealed.ciphertext.size() > protection_policy.maximum_ciphertext_bytes
            || sealed.authentication_tag.size() < 16U
            || sealed.authentication_tag.size() > 64U) {
            return {.reason = BundleProtectionReason::EncryptionFailed};
        }
        ProtectedSupportBundle result{
            .algorithm = algorithm,
            .key_id = key.key_id,
            .key_epoch = key.epoch,
            .nonce = std::vector<std::uint8_t>(nonce.begin(), nonce.end()),
            .plaintext_sha256 = plaintext_digest,
            .manifest_sha256 = bundle.manifest_sha256,
            .ciphertext = std::move(sealed.ciphertext),
            .authentication_tag = std::move(sealed.authentication_tag),
        };
        return {
            .reason = BundleProtectionReason::None,
            .protected_bundle = std::move(result),
        };
    } catch (...) {
        return {.reason = BundleProtectionReason::EncryptionFailed};
    }
}

OpenSupportBundleResult open_protected_support_bundle(
    const ProtectedSupportBundle& protected_bundle,
    const SupportBundlePolicy& bundle_policy,
    const BundleProtectionPolicy& protection_policy,
    const ExternalKeyDescriptor& key,
    std::uint64_t now_ms,
    const ArtifactEncryptionProvider& provider) noexcept {
    try {
        if (protected_bundle.schema_version != kProductionSecuritySchemaVersion
            || protected_bundle.key_id.empty()
            || protected_bundle.key_epoch == 0U) {
            return {.reason = BundleProtectionReason::DecodeFailed};
        }
        if (!external_key_descriptor_valid(key)) {
            return {.reason = BundleProtectionReason::InvalidKeyDescriptor};
        }
        if (!key_active_for(key, ExternalKeyPurpose::BundleEncryption, now_ms)) {
            return {.reason = BundleProtectionReason::KeyNotActive};
        }
        if (provider.key_id() != protected_bundle.key_id
            || provider.key_id() != key.key_id
            || provider.algorithm() != protected_bundle.algorithm
            || key.epoch != protected_bundle.key_epoch) {
            return {.reason = BundleProtectionReason::ProviderMismatch};
        }
        if (!provider_algorithm_allowed(
            protected_bundle.algorithm, protection_policy)) {
            return {.reason = BundleProtectionReason::UnsupportedAlgorithm};
        }
        if (protected_bundle.nonce.size() < 12U
            || protected_bundle.nonce.size() > 32U
            || protected_bundle.ciphertext.empty()
            || protected_bundle.ciphertext.size()
                > protection_policy.maximum_ciphertext_bytes
            || protected_bundle.authentication_tag.size() < 16U
            || protected_bundle.authentication_tag.size() > 64U) {
            return {.reason = BundleProtectionReason::SizeLimitExceeded};
        }
        const std::vector<std::uint8_t> aad = bundle_aad(
            protected_bundle.algorithm,
            protected_bundle.key_id,
            protected_bundle.key_epoch,
            protected_bundle.nonce,
            protected_bundle.plaintext_sha256,
            protected_bundle.manifest_sha256);
        std::vector<std::uint8_t> plaintext;
        if (!provider.open(
            protected_bundle.ciphertext,
            protected_bundle.authentication_tag,
            aad,
            protected_bundle.nonce,
            plaintext)) {
            return {.reason = BundleProtectionReason::AuthenticationFailed};
        }
        if (plaintext.size() > protection_policy.maximum_plaintext_bytes
            || !sha256_equal(sha256(plaintext), protected_bundle.plaintext_sha256)) {
            return {.reason = BundleProtectionReason::PlaintextHashMismatch};
        }
        SupportBundleArtifact bundle;
        if (!decode_bundle_bytes(
            plaintext, protection_policy.maximum_plaintext_bytes, bundle)) {
            return {.reason = BundleProtectionReason::DecodeFailed};
        }
        if (!sha256_equal(bundle.manifest_sha256, protected_bundle.manifest_sha256)
            || !verify_support_bundle(bundle, bundle_policy).accepted()) {
            return {.reason = BundleProtectionReason::BundleVerificationFailed};
        }
        return {
            .reason = BundleProtectionReason::None,
            .bundle = std::move(bundle),
        };
    } catch (...) {
        return {.reason = BundleProtectionReason::DecodeFailed};
    }
}

EvidenceAnchorResult publish_evidence_anchor(
    const EvidenceChainAnchor& anchor,
    const EvidenceAnchorAdapter& adapter) noexcept {
    if (!anchor_valid(anchor)) {
        return {.reason = EvidenceAnchorReason::InvalidAnchor};
    }
    if (adapter.provider_id().empty()
        || adapter.provider_id().size() > kMaximumProviderIdentifierBytes) {
        return {.reason = EvidenceAnchorReason::InvalidProvider};
    }
    try {
        const std::vector<std::uint8_t> canonical = canonical_anchor_bytes(anchor);
        EvidenceAnchorReceipt receipt = adapter.publish(canonical);
        if (receipt.schema_version != kProductionSecuritySchemaVersion
            || receipt.provider_id != adapter.provider_id()
            || receipt.anchor_id.empty()
            || receipt.anchor_id.size() > kMaximumProviderIdentifierBytes
            || receipt.provider_receipt.empty()
            || receipt.provider_receipt.size() > kMaximumProviderReceiptBytes
            || !sha256_equal(receipt.anchor_sha256, sha256(canonical))) {
            return {.reason = EvidenceAnchorReason::PublicationFailed};
        }
        if (!adapter.verify(canonical, receipt)) {
            return {.reason = EvidenceAnchorReason::VerificationFailed};
        }
        return {
            .reason = EvidenceAnchorReason::None,
            .receipt = std::move(receipt),
        };
    } catch (...) {
        return {.reason = EvidenceAnchorReason::PublicationFailed};
    }
}

EvidenceAnchorReason verify_evidence_anchor_receipt(
    const EvidenceChainAnchor& anchor,
    const EvidenceAnchorReceipt& receipt,
    const EvidenceAnchorAdapter& adapter) noexcept {
    if (!anchor_valid(anchor)) {
        return EvidenceAnchorReason::InvalidAnchor;
    }
    if (adapter.provider_id().empty()
        || adapter.provider_id() != receipt.provider_id) {
        return EvidenceAnchorReason::InvalidProvider;
    }
    try {
        const std::vector<std::uint8_t> canonical = canonical_anchor_bytes(anchor);
        if (!sha256_equal(receipt.anchor_sha256, sha256(canonical))
            || !adapter.verify(canonical, receipt)) {
            return EvidenceAnchorReason::VerificationFailed;
        }
        return EvidenceAnchorReason::None;
    } catch (...) {
        return EvidenceAnchorReason::VerificationFailed;
    }
}

const char* to_string(SecurityOperation operation) noexcept {
    switch (operation) {
    case SecurityOperation::SubmitInput: return "submit_input";
    case SecurityOperation::ReadSnapshot: return "read_snapshot";
    case SecurityOperation::ReadTrace: return "read_trace";
    case SecurityOperation::ExportEvidence: return "export_evidence";
    case SecurityOperation::ExportSupportBundle: return "export_support_bundle";
    case SecurityOperation::ManageKeys: return "manage_keys";
    case SecurityOperation::RecoverRuntime: return "recover_runtime";
    }
    return "unknown";
}

const char* to_string(ConfidentialTransport transport) noexcept {
    switch (transport) {
    case ConfidentialTransport::None: return "none";
    case ConfidentialTransport::Tls13: return "tls_1_3";
    case ConfidentialTransport::QuicV1: return "quic_v1";
    case ConfidentialTransport::Ipsec: return "ipsec";
    case ConfidentialTransport::ExternalAuthenticatedEncryption:
        return "external_authenticated_encryption";
    }
    return "unknown";
}

const char* to_string(AuthorizationReason reason) noexcept {
    switch (reason) {
    case AuthorizationReason::None: return "none";
    case AuthorizationReason::InvalidRequest: return "invalid_request";
    case AuthorizationReason::InsecureTransport: return "insecure_transport";
    case AuthorizationReason::RuleNotFound: return "rule_not_found";
    case AuthorizationReason::RuleNotActive: return "rule_not_active";
    case AuthorizationReason::EntityDenied: return "entity_denied";
    case AuthorizationReason::KeyDenied: return "key_denied";
    case AuthorizationReason::CapacityReached: return "capacity_reached";
    }
    return "unknown";
}

const char* to_string(ArtifactEncryptionAlgorithm algorithm) noexcept {
    switch (algorithm) {
    case ArtifactEncryptionAlgorithm::None: return "none";
    case ArtifactEncryptionAlgorithm::DeterministicTestOnly:
        return "deterministic_test_only";
    case ArtifactEncryptionAlgorithm::Aes256Gcm: return "aes_256_gcm";
    case ArtifactEncryptionAlgorithm::ChaCha20Poly1305:
        return "chacha20_poly1305";
    case ArtifactEncryptionAlgorithm::ExternalProviderPrivate:
        return "external_provider_private";
    }
    return "unknown";
}

const char* to_string(BundleProtectionReason reason) noexcept {
    switch (reason) {
    case BundleProtectionReason::None: return "none";
    case BundleProtectionReason::InvalidBundle: return "invalid_bundle";
    case BundleProtectionReason::InvalidKeyDescriptor: return "invalid_key_descriptor";
    case BundleProtectionReason::KeyNotActive: return "key_not_active";
    case BundleProtectionReason::ProviderMismatch: return "provider_mismatch";
    case BundleProtectionReason::UnsupportedAlgorithm: return "unsupported_algorithm";
    case BundleProtectionReason::InvalidNonce: return "invalid_nonce";
    case BundleProtectionReason::SizeLimitExceeded: return "size_limit_exceeded";
    case BundleProtectionReason::EncryptionFailed: return "encryption_failed";
    case BundleProtectionReason::AuthenticationFailed: return "authentication_failed";
    case BundleProtectionReason::PlaintextHashMismatch: return "plaintext_hash_mismatch";
    case BundleProtectionReason::DecodeFailed: return "decode_failed";
    case BundleProtectionReason::BundleVerificationFailed:
        return "bundle_verification_failed";
    }
    return "unknown";
}

const char* to_string(EvidenceAnchorReason reason) noexcept {
    switch (reason) {
    case EvidenceAnchorReason::None: return "none";
    case EvidenceAnchorReason::InvalidAnchor: return "invalid_anchor";
    case EvidenceAnchorReason::InvalidProvider: return "invalid_provider";
    case EvidenceAnchorReason::PublicationFailed: return "publication_failed";
    case EvidenceAnchorReason::VerificationFailed: return "verification_failed";
    }
    return "unknown";
}

} // namespace neoeng::core
