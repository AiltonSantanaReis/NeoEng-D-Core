#include "neoeng/core/exact_oblique_tree_oracle.hpp"
#include "neoeng/core/fixed_raa_microkernel.hpp"
#include "neoeng/core/numeric_contract.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <random>

namespace {

void mix(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        hash ^= (value >> (byte * 8U)) & 0xFFU;
        hash *= 0x100000001B3ULL;
    }
}

} // namespace

int main() {
    try {
        using namespace neoeng::core;
        constexpr std::size_t samples = 4'096U;
        constexpr std::array operations{
            FixedPrimitiveOperation::Add,
            FixedPrimitiveOperation::Subtract,
            FixedPrimitiveOperation::Negate,
            FixedPrimitiveOperation::Multiply,
            FixedPrimitiveOperation::Divide,
        };
        std::mt19937_64 random(0xC5011000C5011000ULL);
        std::uint64_t representable = 0U;
        std::uint64_t overflow_rejected = 0U;
        std::uint64_t division_by_zero_rejected = 0U;
        std::uint64_t decision_hash = 0xCBF29CE484222325ULL;
        for (std::size_t sample = 0U; sample < samples; ++sample) {
            const auto lhs = static_cast<Fixed::rep>(random());
            Fixed::rep rhs = static_cast<Fixed::rep>(random());
            if (sample % 257U == 0U) rhs = 0;
            for (const FixedPrimitiveOperation operation : operations) {
                const FixedPrimitiveDecision decision =
                    classify_fixed_primitive(operation, lhs, rhs);
                switch (decision.status) {
                case FixedPrimitiveStatus::Representable: ++representable; break;
                case FixedPrimitiveStatus::OverflowRejected:
                    ++overflow_rejected;
                    break;
                case FixedPrimitiveStatus::DivisionByZeroRejected:
                    ++division_by_zero_rejected;
                    break;
                }
                mix(decision_hash, static_cast<std::uint8_t>(operation));
                mix(decision_hash, static_cast<std::uint8_t>(decision.status));
                mix(decision_hash, static_cast<std::uint64_t>(decision.raw));
            }
        }

        const auto raa = run_fixed_raa_operation_probe(samples, 8U);
        const std::array<Fixed::rep, 2U> input_x{
            Fixed::from_integer(1).raw(),
            Fixed::from_integer(-1).raw(),
        };
        const std::array<Fixed::rep, 2U> input_y{};
        const std::array<std::uint32_t, 2U> masses{1U, 1U};
        const std::array<NormalContact, 1U> contacts{{
            {
                .first = 0U,
                .second = 1U,
                .normal = NormalQ30{.x = 1 << 30, .y = 0},
            },
        }};
        const auto exact = solve_exact_oblique_tree_active_sets(
            input_x, input_y, masses, contacts);
        const ObliqueCertificationDisposition exact_disposition =
            classify_oblique_evidence(ObliqueEvidence{
                .kind = ObliqueEvidenceKind::ExactContinuousRational,
                .body_count = input_x.size(),
                .contact_count = contacts.size(),
                .input_valid = exact.valid_input,
                .tree_valid = exact.tree_valid,
                .certificate_passed = exact.certified_continuous,
            });
        const ObliqueCertificationDisposition connected_disposition =
            classify_oblique_evidence(ObliqueEvidence{
                .kind = ObliqueEvidenceKind::ConnectedCoordinateFallback,
                .body_count = 3U,
                .contact_count = 2U,
                .input_valid = true,
                .tree_valid = true,
                .certificate_passed = false,
            });
        constexpr NumericClosurePolicyV1 policy = numeric_closure_policy_v1();

        std::cout
            << "{\n"
            << "  \"schema\": \"neoeng.dcore.numeric-closure-result.v1\",\n"
            << "  \"fixed_samples\": " << samples << ",\n"
            << "  \"fixed_operation_cases\": "
            << samples * operations.size() << ",\n"
            << "  \"representable\": " << representable << ",\n"
            << "  \"overflow_rejected\": " << overflow_rejected << ",\n"
            << "  \"division_by_zero_rejected\": "
            << division_by_zero_rejected << ",\n"
            << "  \"fixed_decision_hash\": \"0x" << std::hex
            << std::uppercase << decision_hash << std::dec << "\",\n"
            << "  \"raa_operation_hash\": \"0x" << std::hex
            << std::uppercase << raa.hash << std::dec << "\",\n"
            << "  \"raa_disjoint_products\": "
            << raa.disjoint_product_cases << ",\n"
            << "  \"exact_oblique\": \""
            << to_string(exact_disposition) << "\",\n"
            << "  \"exact_oblique_hash\": \"0x" << std::hex
            << std::uppercase << exact.hash << std::dec << "\",\n"
            << "  \"connected_fallback\": \""
            << to_string(connected_disposition) << "\",\n"
            << "  \"y1_o4_runtime_claim_allowed\": "
            << (policy.y1_o4_runtime_claim_allowed ? "true" : "false")
            << ",\n"
            << "  \"global_numeric_certificate_claim_allowed\": "
            << (policy.global_composed_numeric_certificate_claim_allowed
                    ? "true" : "false")
            << "\n"
            << "}\n";
        return exact_disposition
                    == ObliqueCertificationDisposition::CertifiedDeclaredScope
                && connected_disposition
                    == ObliqueCertificationDisposition::OperationalOnly
                && !policy.y1_o4_runtime_claim_allowed
                && !policy.global_composed_numeric_certificate_claim_allowed
                && division_by_zero_rejected > 0U
                && overflow_rejected > 0U
            ? 0
            : 1;
    } catch (const std::exception& error) {
        std::cerr << "numeric_closure_probe_error=" << error.what() << '\n';
        return 2;
    }
}
