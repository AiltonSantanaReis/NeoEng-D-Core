#include "neoeng/core/production_security.hpp"
#include "neoeng/core/operational_runtime.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace neoeng::core;

void check(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] Sha256Digest digest(std::string_view value) {
    return sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}

[[nodiscard]] AuthenticationKey test_key() {
    AuthenticationKey key{};
    for (std::size_t index = 0U; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(index + 1U);
    }
    return key;
}

[[nodiscard]] std::vector<std::uint8_t> authenticated_bytes(
    std::span<const std::uint8_t> aad,
    std::span<const std::uint8_t> nonce,
    std::span<const std::uint8_t> ciphertext) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(aad.size() + nonce.size() + ciphertext.size());
    bytes.insert(bytes.end(), aad.begin(), aad.end());
    bytes.insert(bytes.end(), nonce.begin(), nonce.end());
    bytes.insert(bytes.end(), ciphertext.begin(), ciphertext.end());
    return bytes;
}

class DeterministicTestEncryption final : public ArtifactEncryptionProvider {
public:
    explicit DeterministicTestEncryption(AuthenticationKey key) : key_(key) {}

    ArtifactEncryptionAlgorithm algorithm() const noexcept override {
        return ArtifactEncryptionAlgorithm::DeterministicTestOnly;
    }

    std::string_view key_id() const noexcept override {
        return "bundle-test-key";
    }

    ProviderSealedBytes seal(
        std::span<const std::uint8_t> plaintext,
        std::span<const std::uint8_t> aad,
        std::span<const std::uint8_t> nonce) const override {
        ProviderSealedBytes sealed;
        sealed.ciphertext.resize(plaintext.size());
        for (std::size_t index = 0U; index < plaintext.size(); ++index) {
            sealed.ciphertext[index] = static_cast<std::uint8_t>(
                plaintext[index] ^ key_[index % key_.size()]
                ^ nonce[index % nonce.size()]);
        }
        const AuthenticationTag tag = hmac_sha256(
            key_, authenticated_bytes(aad, nonce, sealed.ciphertext));
        sealed.authentication_tag.assign(tag.begin(), tag.end());
        return sealed;
    }

    bool open(
        std::span<const std::uint8_t> ciphertext,
        std::span<const std::uint8_t> authentication_tag,
        std::span<const std::uint8_t> aad,
        std::span<const std::uint8_t> nonce,
        std::vector<std::uint8_t>& plaintext) const noexcept override {
        const AuthenticationTag expected = hmac_sha256(
            key_, authenticated_bytes(aad, nonce, ciphertext));
        if (!authentication_tags_equal(expected, authentication_tag)) {
            return false;
        }
        try {
            plaintext.resize(ciphertext.size());
            for (std::size_t index = 0U; index < ciphertext.size(); ++index) {
                plaintext[index] = static_cast<std::uint8_t>(
                    ciphertext[index] ^ key_[index % key_.size()]
                    ^ nonce[index % nonce.size()]);
            }
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    AuthenticationKey key_{};
};

class DeterministicTestAnchor final : public EvidenceAnchorAdapter {
public:
    explicit DeterministicTestAnchor(AuthenticationKey key) : key_(key) {}

    std::string_view provider_id() const noexcept override {
        return "test-anchor-adapter";
    }

    EvidenceAnchorReceipt publish(
        std::span<const std::uint8_t> canonical_anchor) const override {
        const AuthenticationTag tag = hmac_sha256(key_, canonical_anchor);
        return {
            .provider_id = std::string(provider_id()),
            .anchor_id = "test-anchor-1",
            .anchor_sha256 = sha256(canonical_anchor),
            .provider_receipt = std::vector<std::uint8_t>(tag.begin(), tag.end()),
        };
    }

    bool verify(
        std::span<const std::uint8_t> canonical_anchor,
        const EvidenceAnchorReceipt& receipt) const noexcept override {
        const AuthenticationTag expected = hmac_sha256(key_, canonical_anchor);
        return receipt.provider_id == provider_id()
            && sha256_equal(receipt.anchor_sha256, sha256(canonical_anchor))
            && authentication_tags_equal(expected, receipt.provider_receipt);
    }

private:
    AuthenticationKey key_{};
};

[[nodiscard]] TransportSecurityContext secure_transport() {
    return {
        .transport = ConfidentialTransport::Tls13,
        .confidentiality_protected = true,
        .peer_authenticated = true,
        .channel_bound = true,
        .forward_secrecy = true,
        .channel_binding = digest("tls-exporter-binding"),
    };
}

[[nodiscard]] AuthorizationSubject operator_subject() {
    return {
        .role = SessionRole::Operator,
        .origin = 77U,
        .key_id = 4U,
        .key_epoch = 3U,
    };
}

[[nodiscard]] SupportBundlePolicy support_policy() {
    return {
        .maximum_trace_events = 16U,
        .maximum_entry_bytes = 1024U * 1024U,
        .maximum_total_bytes = 4U * 1024U * 1024U,
        .include_time_travel = false,
        .time_travel_payload_authorized = false,
        .include_visual_correlation = false,
        .include_monotonic_timestamps = false,
        .pseudonymization_salt = "cs013-test-case-salt",
    };
}

[[nodiscard]] SupportBundleArtifact support_bundle() {
    const SupportBundleContext context{
        .project_version = "1.13.0-test",
        .environment_id = "unit-test",
        .hardware_profile = "not-qualified",
        .seed = 13U,
    };
    const SupportBundlePolicy policy = support_policy();
    return build_support_bundle(context, policy);
}

[[nodiscard]] ExternalKeyDescriptor active_bundle_key() {
    return {
        .key_id = "bundle-test-key",
        .epoch = 2U,
        .purpose = ExternalKeyPurpose::BundleEncryption,
        .lifecycle = ExternalKeyLifecycle::Active,
        .not_before_ms = 1'000U,
        .not_after_ms = 10'000U,
        .provider_backed = true,
        .private_material_exportable = false,
    };
}

void test_command_and_entity_authorization() {
    CommandAuthorizationPolicy policy(8U, true);
    check(policy.add_rule({
        .rule_id = 1U,
        .role = SessionRole::Operator,
        .operation = SecurityOperation::SubmitInput,
        .origin = 77U,
        .key_id = 4U,
        .minimum_key_epoch = 3U,
        .maximum_key_epoch = 5U,
        .first_entity = 100U,
        .last_entity = 199U,
        .any_entity = false,
        .not_before_ms = 1'000U,
        .not_after_ms = 10'000U,
    }), "valid authorization rule rejected");
    check(!policy.add_rule({
        .rule_id = 1U,
        .role = SessionRole::Operator,
        .operation = SecurityOperation::SubmitInput,
        .minimum_key_epoch = 1U,
        .maximum_key_epoch = 1U,
        .any_entity = true,
        .not_before_ms = 1U,
        .not_after_ms = 2U,
    }), "duplicate rule id accepted");

    const AuthorizationRequest accepted{
        .subject = operator_subject(),
        .operation = SecurityOperation::SubmitInput,
        .entity = 150U,
        .now_ms = 2'000U,
        .transport = secure_transport(),
    };
    check(policy.authorize(accepted).accepted(), "authorized request rejected");

    AuthorizationRequest denied = accepted;
    denied.entity = 200U;
    check(policy.authorize(denied).reason == AuthorizationReason::EntityDenied,
        "out-of-scope entity accepted");
    denied = accepted;
    denied.subject.key_epoch = 2U;
    check(policy.authorize(denied).reason == AuthorizationReason::KeyDenied,
        "old key epoch accepted");
    denied = accepted;
    denied.now_ms = 10'000U;
    check(policy.authorize(denied).reason == AuthorizationReason::RuleNotActive,
        "expired authorization rule accepted");
    denied = accepted;
    denied.transport = {};
    check(policy.authorize(denied).reason == AuthorizationReason::InsecureTransport,
        "plaintext transport accepted");
    denied = accepted;
    denied.operation = SecurityOperation::ManageKeys;
    check(policy.authorize(denied).reason == AuthorizationReason::RuleNotFound,
        "undeclared operation accepted");

    const std::array<InputCommand, 2U> commands{{
        {.entity = 101U},
        {.entity = 199U},
    }};
    check(policy.authorize_input_batch(
        operator_subject(), secure_transport(), 2'000U, commands).accepted(),
        "authorized input batch rejected");
    auto invalid_commands = commands;
    invalid_commands[1].entity = 999U;
    const AuthorizationDecision batch = policy.authorize_input_batch(
        operator_subject(), secure_transport(), 2'000U, invalid_commands);
    check(batch.reason == AuthorizationReason::EntityDenied
        && batch.command_index == 1U,
        "batch did not identify denied command");
}

void test_key_policy_and_protected_bundle() {
    const SupportBundlePolicy bundle_policy = support_policy();
    const SupportBundleArtifact bundle = support_bundle();
    const DeterministicTestEncryption provider(test_key());
    const ExternalKeyDescriptor key = active_bundle_key();
    const std::array<std::uint8_t, 12U> nonce{
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U,
    };
    const BundleProtectionPolicy test_policy{
        .maximum_plaintext_bytes = 4U * 1024U * 1024U,
        .maximum_ciphertext_bytes = 4U * 1024U * 1024U,
        .allow_test_provider = true,
    };
    check(external_key_descriptor_valid(key), "valid provider key rejected");

    const ProtectSupportBundleResult protected_result = protect_support_bundle(
        bundle, bundle_policy, test_policy, key, 2'000U, nonce, provider);
    check(protected_result.accepted(), "support bundle protection failed");
    const OpenSupportBundleResult opened = open_protected_support_bundle(
        protected_result.protected_bundle,
        bundle_policy,
        test_policy,
        key,
        2'000U,
        provider);
    check(opened.accepted(), "protected support bundle did not open");
    check(sha256_equal(opened.bundle.manifest_sha256, bundle.manifest_sha256),
        "opened bundle manifest changed");

    BundleProtectionPolicy production_policy = test_policy;
    production_policy.allow_test_provider = false;
    check(protect_support_bundle(
        bundle, bundle_policy, production_policy, key, 2'000U, nonce, provider).reason
        == BundleProtectionReason::UnsupportedAlgorithm,
        "test-only provider accepted in production policy");

    ProtectedSupportBundle tampered = protected_result.protected_bundle;
    tampered.ciphertext.front() ^= 0x80U;
    check(open_protected_support_bundle(
        tampered, bundle_policy, test_policy, key, 2'000U, provider).reason
        == BundleProtectionReason::AuthenticationFailed,
        "tampered ciphertext accepted");
    tampered = protected_result.protected_bundle;
    tampered.authentication_tag.front() ^= 0x01U;
    check(open_protected_support_bundle(
        tampered, bundle_policy, test_policy, key, 2'000U, provider).reason
        == BundleProtectionReason::AuthenticationFailed,
        "tampered tag accepted");

    ExternalKeyDescriptor expired = key;
    expired.lifecycle = ExternalKeyLifecycle::Retired;
    check(protect_support_bundle(
        bundle, bundle_policy, test_policy, expired, 2'000U, nonce, provider).reason
        == BundleProtectionReason::KeyNotActive,
        "retired key accepted for encryption");
    ExternalKeyDescriptor exportable = key;
    exportable.private_material_exportable = true;
    check(!external_key_descriptor_valid(exportable),
        "exportable private key accepted by production policy");
}

void test_operational_runtime_authorization_path() {
    OperationalRuntimeConfig config{};
    config.network.require_established_session = true;
    config.network.maximum_input_commands = 4U;
    config.enable_wall_clock_budget_tracing = false;
    OperationalRuntime runtime(
        {
            .frame = 0U,
            .bodies = {{
                .id = 150U,
                .position = {},
                .velocity = {},
            }},
        },
        test_key(),
        config);
    check(runtime.install_authenticated_session(
        77U,
        {
            .session_id = 55U,
            .client_to_server_key = test_key(),
            .key_id = 4U,
            .key_epoch = 3U,
            .authorized_role = static_cast<std::uint8_t>(SessionRole::Operator),
            .expires_at_ms = 10'000U,
        },
        1'000U),
        "authenticated session install failed");

    CommandAuthorizationPolicy policy(2U, true);
    check(policy.add_rule({
        .rule_id = 7U,
        .role = SessionRole::Operator,
        .operation = SecurityOperation::SubmitInput,
        .origin = 77U,
        .key_id = 4U,
        .minimum_key_epoch = 3U,
        .maximum_key_epoch = 3U,
        .first_entity = 150U,
        .last_entity = 150U,
        .any_entity = false,
        .not_before_ms = 1'000U,
        .not_after_ms = 10'000U,
    }), "runtime authorization rule rejected");

    const std::array<InputCommand, 1U> accepted_commands{{
        {
            .entity = 150U,
            .acceleration = {
                Fixed::from_integer(1),
                Fixed::from_integer(0),
            },
        },
    }};
    const std::vector<std::uint8_t> accepted_packet =
        encode_authenticated_packet(
            test_key(),
            55U,
            0U,
            2'000U,
            encode_input_payload(accepted_commands));
    const OperationalStepResult accepted = runtime.ingest_authorized_input(
        77U, 2'000U, 700U, accepted_packet, secure_transport(), policy);
    check(accepted.advanced
        && accepted.authorization_reason == AuthorizationReason::None,
        "authorized runtime input did not advance");

    auto denied_commands = accepted_commands;
    denied_commands.front().entity = 151U;
    const std::vector<std::uint8_t> denied_packet =
        encode_authenticated_packet(
            test_key(),
            55U,
            1U,
            2'001U,
            encode_input_payload(denied_commands));
    const OperationalStepResult denied = runtime.ingest_authorized_input(
        77U, 2'001U, 701U, denied_packet, secure_transport(), policy);
    check(!denied.advanced
        && denied.authorization_reason == AuthorizationReason::EntityDenied,
        "unauthorized runtime entity advanced canonical state");
}

void test_external_anchor_adapter_boundary() {
    EvidenceChainAnchor anchor{
        .branch_id = 7U,
        .record_count = 12U,
        .head_envelope_hash = digest("head"),
        .branch_parent_hash = digest("parent"),
        .branch_parent_frame = 9U,
    };
    const DeterministicTestAnchor adapter(test_key());
    const EvidenceAnchorResult published = publish_evidence_anchor(anchor, adapter);
    check(published.accepted(), "external anchor publication failed");
    check(verify_evidence_anchor_receipt(
        anchor, published.receipt, adapter) == EvidenceAnchorReason::None,
        "external anchor receipt rejected");
    ++anchor.record_count;
    check(verify_evidence_anchor_receipt(
        anchor, published.receipt, adapter) == EvidenceAnchorReason::VerificationFailed,
        "anchor receipt accepted for altered chain");
}

void test_normative_non_claims() {
    check(!kProductionAsymmetricSignatureProviderIncluded,
        "product must not claim an included asymmetric provider");
    check(!transport_security_context_valid({}),
        "empty transport context accepted");
    TransportSecurityContext incomplete = secure_transport();
    incomplete.channel_bound = false;
    check(!transport_security_context_valid(incomplete),
        "unbound secure channel accepted");
}

} // namespace

int main() {
    try {
        test_command_and_entity_authorization();
        test_key_policy_and_protected_bundle();
        test_operational_runtime_authorization_path();
        test_external_anchor_adapter_boundary();
        test_normative_non_claims();
        std::cout << "production_security_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "production_security_tests_error=" << error.what() << '\n';
        return 1;
    }
}
