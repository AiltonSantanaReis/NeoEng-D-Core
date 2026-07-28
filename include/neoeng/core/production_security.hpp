#pragma once

#include "neoeng/core/session_security.hpp"
#include "neoeng/core/state_evidence.hpp"
#include "neoeng/core/support_bundle.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace neoeng::core {

inline constexpr std::uint16_t kProductionSecuritySchemaVersion = 1U;
inline constexpr bool kProductionAsymmetricSignatureProviderIncluded = false;
inline constexpr std::size_t kMaximumAuthorizationRules = 1'024U;
inline constexpr std::size_t kMaximumProtectedArtifactBytes = 64U * 1024U * 1024U;
inline constexpr std::size_t kMaximumProviderIdentifierBytes = 128U;
inline constexpr std::size_t kMaximumProviderReceiptBytes = 4'096U;

enum class SecurityOperation : std::uint8_t {
    SubmitInput = 1U,
    ReadSnapshot = 2U,
    ReadTrace = 3U,
    ExportEvidence = 4U,
    ExportSupportBundle = 5U,
    ManageKeys = 6U,
    RecoverRuntime = 7U,
};

enum class ConfidentialTransport : std::uint8_t {
    None,
    Tls13,
    QuicV1,
    Ipsec,
    ExternalAuthenticatedEncryption,
};

struct TransportSecurityContext final {
    ConfidentialTransport transport{ConfidentialTransport::None};
    bool confidentiality_protected{};
    bool peer_authenticated{};
    bool channel_bound{};
    bool forward_secrecy{};
    Sha256Digest channel_binding{};
};

struct AuthorizationSubject final {
    SessionRole role{SessionRole::Player};
    OriginId origin{};
    RootKeyId key_id{};
    RootKeyEpoch key_epoch{};
};

struct AuthorizationRequest final {
    AuthorizationSubject subject{};
    SecurityOperation operation{SecurityOperation::SubmitInput};
    EntityId entity{};
    std::uint64_t now_ms{};
    TransportSecurityContext transport{};
};

struct AuthorizationRule final {
    std::uint32_t rule_id{};
    SessionRole role{SessionRole::Player};
    SecurityOperation operation{SecurityOperation::SubmitInput};
    OriginId origin{}; // zero means any authenticated origin
    RootKeyId key_id{}; // zero means any allowed key
    RootKeyEpoch minimum_key_epoch{1U};
    RootKeyEpoch maximum_key_epoch{1U};
    EntityId first_entity{};
    EntityId last_entity{};
    bool any_entity{};
    std::uint64_t not_before_ms{};
    std::uint64_t not_after_ms{};
};

enum class AuthorizationReason : std::uint8_t {
    None,
    InvalidRequest,
    InsecureTransport,
    RuleNotFound,
    RuleNotActive,
    EntityDenied,
    KeyDenied,
    CapacityReached,
};

struct AuthorizationDecision final {
    AuthorizationReason reason{AuthorizationReason::None};
    std::uint32_t rule_id{};
    std::size_t command_index{};

    [[nodiscard]] bool accepted() const noexcept {
        return reason == AuthorizationReason::None;
    }
};

class CommandAuthorizationPolicy final {
public:
    explicit CommandAuthorizationPolicy(
        std::size_t maximum_rules = kMaximumAuthorizationRules,
        bool require_confidential_transport = true);

    [[nodiscard]] bool add_rule(AuthorizationRule rule) noexcept;
    [[nodiscard]] AuthorizationDecision authorize(
        const AuthorizationRequest& request) const noexcept;
    [[nodiscard]] AuthorizationDecision authorize_input_batch(
        const AuthorizationSubject& subject,
        const TransportSecurityContext& transport,
        std::uint64_t now_ms,
        std::span<const InputCommand> commands) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return rules_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return maximum_rules_; }
    [[nodiscard]] bool requires_confidential_transport() const noexcept {
        return require_confidential_transport_;
    }

private:
    std::size_t maximum_rules_{};
    bool require_confidential_transport_{};
    std::vector<AuthorizationRule> rules_{};
};

enum class ExternalKeyPurpose : std::uint8_t {
    BundleEncryption,
    EvidenceSignatureVerification,
    EvidenceAnchor,
};

enum class ExternalKeyLifecycle : std::uint8_t {
    Active,
    Retired,
    Revoked,
};

struct ExternalKeyDescriptor final {
    std::string key_id{};
    std::uint32_t epoch{};
    ExternalKeyPurpose purpose{ExternalKeyPurpose::BundleEncryption};
    ExternalKeyLifecycle lifecycle{ExternalKeyLifecycle::Active};
    std::uint64_t not_before_ms{};
    std::uint64_t not_after_ms{};
    bool provider_backed{};
    bool private_material_exportable{};
};

enum class ArtifactEncryptionAlgorithm : std::uint16_t {
    None = 0U,
    DeterministicTestOnly = 1U,
    Aes256Gcm = 0x0101U,
    ChaCha20Poly1305 = 0x0102U,
    ExternalProviderPrivate = 0x8000U,
};

struct ProviderSealedBytes final {
    std::vector<std::uint8_t> ciphertext{};
    std::vector<std::uint8_t> authentication_tag{};
};

class ArtifactEncryptionProvider {
public:
    virtual ~ArtifactEncryptionProvider() = default;
    [[nodiscard]] virtual ArtifactEncryptionAlgorithm algorithm() const noexcept = 0;
    [[nodiscard]] virtual std::string_view key_id() const noexcept = 0;
    [[nodiscard]] virtual ProviderSealedBytes seal(
        std::span<const std::uint8_t> plaintext,
        std::span<const std::uint8_t> additional_authenticated_data,
        std::span<const std::uint8_t> nonce) const = 0;
    [[nodiscard]] virtual bool open(
        std::span<const std::uint8_t> ciphertext,
        std::span<const std::uint8_t> authentication_tag,
        std::span<const std::uint8_t> additional_authenticated_data,
        std::span<const std::uint8_t> nonce,
        std::vector<std::uint8_t>& plaintext) const noexcept = 0;
};

struct ProtectedSupportBundle final {
    std::uint16_t schema_version{kProductionSecuritySchemaVersion};
    ArtifactEncryptionAlgorithm algorithm{ArtifactEncryptionAlgorithm::None};
    std::string key_id{};
    std::uint32_t key_epoch{};
    std::vector<std::uint8_t> nonce{};
    Sha256Digest plaintext_sha256{};
    Sha256Digest manifest_sha256{};
    std::vector<std::uint8_t> ciphertext{};
    std::vector<std::uint8_t> authentication_tag{};
};

struct BundleProtectionPolicy final {
    std::size_t maximum_plaintext_bytes{kMaximumProtectedArtifactBytes};
    std::size_t maximum_ciphertext_bytes{kMaximumProtectedArtifactBytes};
    bool allow_test_provider{};
};

enum class BundleProtectionReason : std::uint8_t {
    None,
    InvalidBundle,
    InvalidKeyDescriptor,
    KeyNotActive,
    ProviderMismatch,
    UnsupportedAlgorithm,
    InvalidNonce,
    SizeLimitExceeded,
    EncryptionFailed,
    AuthenticationFailed,
    PlaintextHashMismatch,
    DecodeFailed,
    BundleVerificationFailed,
};

struct ProtectSupportBundleResult final {
    BundleProtectionReason reason{BundleProtectionReason::None};
    ProtectedSupportBundle protected_bundle{};

    [[nodiscard]] bool accepted() const noexcept {
        return reason == BundleProtectionReason::None;
    }
};

struct OpenSupportBundleResult final {
    BundleProtectionReason reason{BundleProtectionReason::None};
    SupportBundleArtifact bundle{};

    [[nodiscard]] bool accepted() const noexcept {
        return reason == BundleProtectionReason::None;
    }
};

[[nodiscard]] ProtectSupportBundleResult protect_support_bundle(
    const SupportBundleArtifact& bundle,
    const SupportBundlePolicy& bundle_policy,
    const BundleProtectionPolicy& protection_policy,
    const ExternalKeyDescriptor& key,
    std::uint64_t now_ms,
    std::span<const std::uint8_t> nonce,
    const ArtifactEncryptionProvider& provider) noexcept;

[[nodiscard]] OpenSupportBundleResult open_protected_support_bundle(
    const ProtectedSupportBundle& protected_bundle,
    const SupportBundlePolicy& bundle_policy,
    const BundleProtectionPolicy& protection_policy,
    const ExternalKeyDescriptor& key,
    std::uint64_t now_ms,
    const ArtifactEncryptionProvider& provider) noexcept;

struct EvidenceAnchorReceipt final {
    std::uint16_t schema_version{kProductionSecuritySchemaVersion};
    std::string provider_id{};
    std::string anchor_id{};
    Sha256Digest anchor_sha256{};
    std::vector<std::uint8_t> provider_receipt{};
};

class EvidenceAnchorAdapter {
public:
    virtual ~EvidenceAnchorAdapter() = default;
    [[nodiscard]] virtual std::string_view provider_id() const noexcept = 0;
    [[nodiscard]] virtual EvidenceAnchorReceipt publish(
        std::span<const std::uint8_t> canonical_anchor) const = 0;
    [[nodiscard]] virtual bool verify(
        std::span<const std::uint8_t> canonical_anchor,
        const EvidenceAnchorReceipt& receipt) const noexcept = 0;
};

enum class EvidenceAnchorReason : std::uint8_t {
    None,
    InvalidAnchor,
    InvalidProvider,
    PublicationFailed,
    VerificationFailed,
};

struct EvidenceAnchorResult final {
    EvidenceAnchorReason reason{EvidenceAnchorReason::None};
    EvidenceAnchorReceipt receipt{};

    [[nodiscard]] bool accepted() const noexcept {
        return reason == EvidenceAnchorReason::None;
    }
};

[[nodiscard]] EvidenceAnchorResult publish_evidence_anchor(
    const EvidenceChainAnchor& anchor,
    const EvidenceAnchorAdapter& adapter) noexcept;
[[nodiscard]] EvidenceAnchorReason verify_evidence_anchor_receipt(
    const EvidenceChainAnchor& anchor,
    const EvidenceAnchorReceipt& receipt,
    const EvidenceAnchorAdapter& adapter) noexcept;

[[nodiscard]] bool transport_security_context_valid(
    const TransportSecurityContext& context) noexcept;
[[nodiscard]] bool external_key_descriptor_valid(
    const ExternalKeyDescriptor& key) noexcept;
[[nodiscard]] const char* to_string(SecurityOperation operation) noexcept;
[[nodiscard]] const char* to_string(ConfidentialTransport transport) noexcept;
[[nodiscard]] const char* to_string(AuthorizationReason reason) noexcept;
[[nodiscard]] const char* to_string(ArtifactEncryptionAlgorithm algorithm) noexcept;
[[nodiscard]] const char* to_string(BundleProtectionReason reason) noexcept;
[[nodiscard]] const char* to_string(EvidenceAnchorReason reason) noexcept;

} // namespace neoeng::core
