#include "neoeng/core/production_security.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using namespace neoeng::core;

[[nodiscard]] AuthenticationKey test_key() {
    AuthenticationKey key{};
    for (std::size_t index = 0U; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(0xA5U ^ index);
    }
    return key;
}

[[nodiscard]] std::vector<std::uint8_t> authenticated_bytes(
    std::span<const std::uint8_t> aad,
    std::span<const std::uint8_t> nonce,
    std::span<const std::uint8_t> ciphertext) {
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), aad.begin(), aad.end());
    bytes.insert(bytes.end(), nonce.begin(), nonce.end());
    bytes.insert(bytes.end(), ciphertext.begin(), ciphertext.end());
    return bytes;
}

class ProbeEncryption final : public ArtifactEncryptionProvider {
public:
    ArtifactEncryptionAlgorithm algorithm() const noexcept override {
        return ArtifactEncryptionAlgorithm::DeterministicTestOnly;
    }
    std::string_view key_id() const noexcept override {
        return "cs013-probe-test-key";
    }
    ProviderSealedBytes seal(
        std::span<const std::uint8_t> plaintext,
        std::span<const std::uint8_t> aad,
        std::span<const std::uint8_t> nonce) const override {
        const AuthenticationKey key = test_key();
        ProviderSealedBytes result;
        result.ciphertext.resize(plaintext.size());
        for (std::size_t index = 0U; index < plaintext.size(); ++index) {
            result.ciphertext[index] = static_cast<std::uint8_t>(
                plaintext[index] ^ key[index % key.size()]
                ^ nonce[index % nonce.size()]);
        }
        const AuthenticationTag tag = hmac_sha256(
            key, authenticated_bytes(aad, nonce, result.ciphertext));
        result.authentication_tag.assign(tag.begin(), tag.end());
        return result;
    }
    bool open(
        std::span<const std::uint8_t> ciphertext,
        std::span<const std::uint8_t> tag,
        std::span<const std::uint8_t> aad,
        std::span<const std::uint8_t> nonce,
        std::vector<std::uint8_t>& plaintext) const noexcept override {
        const AuthenticationKey key = test_key();
        const AuthenticationTag expected = hmac_sha256(
            key, authenticated_bytes(aad, nonce, ciphertext));
        if (!authentication_tags_equal(expected, tag)) {
            return false;
        }
        try {
            plaintext.resize(ciphertext.size());
            for (std::size_t index = 0U; index < ciphertext.size(); ++index) {
                plaintext[index] = static_cast<std::uint8_t>(
                    ciphertext[index] ^ key[index % key.size()]
                    ^ nonce[index % nonce.size()]);
            }
            return true;
        } catch (...) {
            return false;
        }
    }
};

[[nodiscard]] Sha256Digest digest(std::string_view value) {
    return sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}

} // namespace

int main() {
    try {
        constexpr std::size_t authorization_decisions = 4'096U;
        constexpr std::size_t protected_bundles = 512U;

        CommandAuthorizationPolicy authorization(4U, true);
        if (!authorization.add_rule({
            .rule_id = 13U,
            .role = SessionRole::Service,
            .operation = SecurityOperation::SubmitInput,
            .origin = 91U,
            .key_id = 8U,
            .minimum_key_epoch = 2U,
            .maximum_key_epoch = 2U,
            .first_entity = 1U,
            .last_entity = 4'096U,
            .any_entity = false,
            .not_before_ms = 1U,
            .not_after_ms = 100'000U,
        })) {
            throw std::runtime_error("authorization rule rejected");
        }
        const TransportSecurityContext transport{
            .transport = ConfidentialTransport::Tls13,
            .confidentiality_protected = true,
            .peer_authenticated = true,
            .channel_bound = true,
            .forward_secrecy = true,
            .channel_binding = digest("cs013-probe-channel"),
        };
        const AuthorizationSubject subject{
            .role = SessionRole::Service,
            .origin = 91U,
            .key_id = 8U,
            .key_epoch = 2U,
        };
        std::size_t accepted{};
        std::size_t rejected{};
        for (std::size_t index = 0U; index < authorization_decisions; ++index) {
            AuthorizationRequest request{
                .subject = subject,
                .operation = SecurityOperation::SubmitInput,
                .entity = static_cast<EntityId>(index + 1U),
                .now_ms = 2'000U,
                .transport = transport,
            };
            if (authorization.authorize(request).accepted()) {
                ++accepted;
            }
            request.entity = static_cast<EntityId>(authorization_decisions + index + 1U);
            if (!authorization.authorize(request).accepted()) {
                ++rejected;
            }
        }

        const SupportBundlePolicy bundle_policy{
            .maximum_trace_events = 16U,
            .maximum_entry_bytes = 1024U * 1024U,
            .maximum_total_bytes = 4U * 1024U * 1024U,
            .include_time_travel = false,
            .time_travel_payload_authorized = false,
            .include_visual_correlation = false,
            .include_monotonic_timestamps = false,
            .pseudonymization_salt = "cs013-probe-salt",
        };
        const SupportBundleContext context{
            .project_version = "1.14.1",
            .environment_id = "recorded-campaign",
            .hardware_profile = "not-qualified",
            .seed = 13U,
        };
        const SupportBundleArtifact bundle =
            build_support_bundle(context, bundle_policy);
        const BundleProtectionPolicy protection_policy{
            .maximum_plaintext_bytes = 4U * 1024U * 1024U,
            .maximum_ciphertext_bytes = 4U * 1024U * 1024U,
            .allow_test_provider = true,
        };
        const ExternalKeyDescriptor key{
            .key_id = "cs013-probe-test-key",
            .epoch = 1U,
            .purpose = ExternalKeyPurpose::BundleEncryption,
            .lifecycle = ExternalKeyLifecycle::Active,
            .not_before_ms = 1U,
            .not_after_ms = 100'000U,
            .provider_backed = true,
            .private_material_exportable = false,
        };
        const ProbeEncryption provider;
        std::size_t bundle_roundtrips{};
        std::size_t tamper_rejections{};
        Sha256Digest last_ciphertext{};
        for (std::size_t index = 0U; index < protected_bundles; ++index) {
            std::array<std::uint8_t, 12U> nonce{};
            for (std::size_t byte = 0U; byte < nonce.size(); ++byte) {
                nonce[byte] = static_cast<std::uint8_t>(
                    ((index + 1U) * (byte + 3U)) & 0xFFU);
            }
            nonce.front() |= 1U;
            const ProtectSupportBundleResult sealed = protect_support_bundle(
                bundle, bundle_policy, protection_policy,
                key, 2'000U, nonce, provider);
            if (!sealed.accepted()) {
                throw std::runtime_error("bundle protection failed");
            }
            const OpenSupportBundleResult opened = open_protected_support_bundle(
                sealed.protected_bundle, bundle_policy, protection_policy,
                key, 2'000U, provider);
            if (!opened.accepted()) {
                throw std::runtime_error("bundle open failed");
            }
            ++bundle_roundtrips;
            ProtectedSupportBundle tampered = sealed.protected_bundle;
            tampered.ciphertext.front() ^= 1U;
            if (!open_protected_support_bundle(
                tampered, bundle_policy, protection_policy,
                key, 2'000U, provider).accepted()) {
                ++tamper_rejections;
            }
            last_ciphertext = sha256(sealed.protected_bundle.ciphertext);
        }

        std::cout
            << "{\"schema\":\"neoeng.dcore.production-security.v1\""
            << ",\"status\":\"passed\""
            << ",\"authorization_accepts\":" << accepted
            << ",\"authorization_rejections\":" << rejected
            << ",\"bundle_roundtrips\":" << bundle_roundtrips
            << ",\"tamper_rejections\":" << tamper_rejections
            << ",\"last_ciphertext_sha256\":\"" << sha256_hex(last_ciphertext) << '"'
            << ",\"included_asymmetric_provider\":false"
            << ",\"test_provider_used\":true"
            << ",\"external_provider_required_for_production\":true"
            << ",\"external_anchor_trust_claimed\":false"
            << ",\"cross_architecture_claim_promoted\":false"
            << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "production_security_probe_error=" << error.what() << '\n';
        return 1;
    }
}
