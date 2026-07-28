#include "neoeng/core/exact_oblique_tree_oracle.hpp"
#include "neoeng/core/fixed_raa_microkernel.hpp"
#include "neoeng/core/numeric_contract.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using neoeng::core::Fixed;
using neoeng::core::FixedPrimitiveDecision;
using neoeng::core::FixedPrimitiveOperation;
using neoeng::core::FixedPrimitiveStatus;
using neoeng::core::NormalContact;
using neoeng::core::NormalQ30;
using neoeng::core::ObliqueCertificationDisposition;
using neoeng::core::ObliqueEvidence;
using neoeng::core::ObliqueEvidenceKind;

void check(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

[[nodiscard]] Fixed::rep execute_fixed(
    FixedPrimitiveOperation operation,
    Fixed::rep lhs,
    Fixed::rep rhs) {
    const Fixed left = Fixed::from_raw(lhs);
    const Fixed right = Fixed::from_raw(rhs);
    switch (operation) {
    case FixedPrimitiveOperation::Add: return (left + right).raw();
    case FixedPrimitiveOperation::Subtract: return (left - right).raw();
    case FixedPrimitiveOperation::Negate: return (-left).raw();
    case FixedPrimitiveOperation::Multiply: return (left * right).raw();
    case FixedPrimitiveOperation::Divide: return (left / right).raw();
    }
    throw std::logic_error("unknown primitive");
}

void test_fixed_classifier_matches_primitive() {
    constexpr std::array<Fixed::rep, 11U> values{
        std::numeric_limits<Fixed::rep>::min(),
        std::numeric_limits<Fixed::rep>::min() + 1,
        -2 * Fixed::scale,
        -Fixed::scale,
        -1,
        0,
        1,
        Fixed::scale,
        2 * Fixed::scale,
        std::numeric_limits<Fixed::rep>::max() - 1,
        std::numeric_limits<Fixed::rep>::max(),
    };
    constexpr std::array operations{
        FixedPrimitiveOperation::Add,
        FixedPrimitiveOperation::Subtract,
        FixedPrimitiveOperation::Multiply,
        FixedPrimitiveOperation::Divide,
    };
    for (const FixedPrimitiveOperation operation : operations) {
        for (const Fixed::rep lhs : values) {
            for (const Fixed::rep rhs : values) {
                const FixedPrimitiveDecision decision =
                    neoeng::core::classify_fixed_primitive(operation, lhs, rhs);
                try {
                    const Fixed::rep actual = execute_fixed(operation, lhs, rhs);
                    check(
                        decision.status == FixedPrimitiveStatus::Representable,
                        "representable primitive was not accepted");
                    check(decision.raw == actual, "classifier raw differs from Fixed");
                } catch (const std::overflow_error&) {
                    check(
                        decision.status == FixedPrimitiveStatus::OverflowRejected,
                        "overflow was not classified fail-closed");
                } catch (const std::domain_error&) {
                    check(
                        decision.status
                            == FixedPrimitiveStatus::DivisionByZeroRejected,
                        "division by zero was not classified fail-closed");
                }
            }
        }
    }
    for (const Fixed::rep value : values) {
        const FixedPrimitiveDecision decision =
            neoeng::core::classify_fixed_primitive(
                FixedPrimitiveOperation::Negate, value);
        try {
            const Fixed::rep actual =
                execute_fixed(FixedPrimitiveOperation::Negate, value, 0);
            check(
                decision.status == FixedPrimitiveStatus::Representable,
                "representable negation was not accepted");
            check(decision.raw == actual, "negation raw differs from Fixed");
        } catch (const std::overflow_error&) {
            check(
                decision.status == FixedPrimitiveStatus::OverflowRejected,
                "negation overflow was not rejected");
        }
    }
}

void test_construction_boundaries() {
    const Fixed minimum = Fixed::from_integer(
        std::numeric_limits<std::int32_t>::min());
    check(
        minimum.raw() == std::numeric_limits<Fixed::rep>::min(),
        "minimum whole Q32.32 value");
    const Fixed maximum = Fixed::from_integer(
        std::numeric_limits<std::int32_t>::max());
    check(
        maximum.raw()
            == static_cast<Fixed::rep>(std::numeric_limits<std::int32_t>::max())
                * Fixed::scale,
        "maximum whole Q32.32 value");

    bool overflow = false;
    try {
        static_cast<void>(Fixed::from_integer(
            static_cast<Fixed::rep>(std::numeric_limits<std::int32_t>::max())
            + 1));
    } catch (const std::overflow_error&) {
        overflow = true;
    }
    check(overflow, "out-of-domain whole value must be rejected");
}

void test_policy_rejects_unsupported_global_claims() {
    constexpr auto policy = neoeng::core::numeric_closure_policy_v1();
    check(
        policy.schema == "neoeng.dcore.numeric-closure.v1",
        "numeric policy schema");
    check(!policy.y1_o4_runtime_claim_allowed, "Y1-O4 runtime claim rejected");
    check(
        !policy.global_composed_numeric_certificate_claim_allowed,
        "global composed certificate rejected");
    check(
        policy.fixed_primitives_use_exact_wide_intermediate,
        "exact intermediate policy");
    check(
        policy.unrepresentable_fixed_result_is_rejected,
        "fail-closed narrowing policy");
    check(
        policy.raa_minimum_terms == 2U && policy.raa_maximum_terms == 16U,
        "RAA laboratory capacity");
}

void test_raa_remains_bounded_research_evidence() {
    const auto result = neoeng::core::run_fixed_raa_operation_probe(256U, 8U);
    check(result.iterations == 256U, "RAA operation iterations");
    check(result.maximum_terms == 8U, "RAA maximum terms");
    check(result.disjoint_product_cases == 256U, "RAA adversarial products");

    bool rejected = false;
    try {
        static_cast<void>(
            neoeng::core::run_fixed_raa_operation_probe(1U, 17U));
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    check(rejected, "RAA capacity outside the laboratory domain rejected");
}

void test_oblique_scope_is_explicit() {
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
    const auto exact = neoeng::core::solve_exact_oblique_tree_active_sets(
        input_x, input_y, masses, contacts);
    check(exact.valid_input, "exact oracle input valid");
    check(exact.tree_valid, "exact oracle tree valid");
    check(exact.certified_continuous, "exact small tree certified");

    check(
        neoeng::core::classify_oblique_evidence(ObliqueEvidence{
            .kind = ObliqueEvidenceKind::ExactContinuousRational,
            .body_count = input_x.size(),
            .contact_count = contacts.size(),
            .input_valid = exact.valid_input,
            .tree_valid = exact.tree_valid,
            .certificate_passed = exact.certified_continuous,
        }) == ObliqueCertificationDisposition::CertifiedDeclaredScope,
        "exact rational certificate accepted in declared scope");
    check(
        neoeng::core::classify_oblique_evidence(ObliqueEvidence{
            .kind = ObliqueEvidenceKind::ExactContinuousRational,
            .body_count = 11U,
            .contact_count = 10U,
            .input_valid = true,
            .tree_valid = true,
            .certificate_passed = true,
        }) == ObliqueCertificationDisposition::Rejected,
        "exact certificate outside declared size rejected");
    check(
        neoeng::core::classify_oblique_evidence(ObliqueEvidence{
            .kind = ObliqueEvidenceKind::FiniteGrid,
            .body_count = 32U,
            .contact_count = 31U,
            .input_valid = true,
            .tree_valid = true,
            .certificate_passed = true,
        }) == ObliqueCertificationDisposition::CertifiedFiniteGridOnly,
        "finite grid is not promoted to continuous certificate");
    check(
        neoeng::core::classify_oblique_evidence(ObliqueEvidence{
            .kind = ObliqueEvidenceKind::ConnectedCoordinateFallback,
            .body_count = 3U,
            .contact_count = 2U,
            .input_valid = true,
            .tree_valid = true,
            .certificate_passed = false,
        }) == ObliqueCertificationDisposition::OperationalOnly,
        "connected coordinate fallback remains operational only");
}

} // namespace

int main() {
    try {
        test_fixed_classifier_matches_primitive();
        test_construction_boundaries();
        test_policy_rejects_unsupported_global_claims();
        test_raa_remains_bounded_research_evidence();
        test_oblique_scope_is_explicit();
        std::cout << "numeric_closure_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "numeric_closure_tests_error=" << error.what() << '\n';
        return 1;
    }
}
