#include "neoeng/core/numeric_contract.hpp"

#include <limits>

namespace neoeng::core {
namespace {

[[nodiscard]] FixedPrimitiveDecision narrow_or_reject(WideInteger value) noexcept {
    constexpr WideInteger minimum =
        static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::min());
    constexpr WideInteger maximum =
        static_cast<WideInteger>(std::numeric_limits<Fixed::rep>::max());
    if (value < minimum || value > maximum) {
        return {.status = FixedPrimitiveStatus::OverflowRejected};
    }
    return {
        .status = FixedPrimitiveStatus::Representable,
        .raw = static_cast<Fixed::rep>(value),
    };
}

} // namespace

FixedPrimitiveDecision classify_fixed_primitive(
    FixedPrimitiveOperation operation,
    Fixed::rep lhs,
    Fixed::rep rhs) noexcept {
    const WideInteger wide_lhs = static_cast<WideInteger>(lhs);
    const WideInteger wide_rhs = static_cast<WideInteger>(rhs);
    switch (operation) {
    case FixedPrimitiveOperation::Add:
        return narrow_or_reject(wide_lhs + wide_rhs);
    case FixedPrimitiveOperation::Subtract:
        return narrow_or_reject(wide_lhs - wide_rhs);
    case FixedPrimitiveOperation::Negate:
        return narrow_or_reject(-wide_lhs);
    case FixedPrimitiveOperation::Multiply:
        return narrow_or_reject(
            (wide_lhs * wide_rhs) / static_cast<WideInteger>(Fixed::scale));
    case FixedPrimitiveOperation::Divide:
        if (rhs == 0) {
            return {.status = FixedPrimitiveStatus::DivisionByZeroRejected};
        }
        return narrow_or_reject(
            (wide_lhs * static_cast<WideInteger>(Fixed::scale)) / wide_rhs);
    }
    return {.status = FixedPrimitiveStatus::OverflowRejected};
}

ObliqueCertificationDisposition classify_oblique_evidence(
    const ObliqueEvidence& evidence) noexcept {
    if (!evidence.input_valid) {
        return ObliqueCertificationDisposition::Rejected;
    }
    switch (evidence.kind) {
    case ObliqueEvidenceKind::ExactContinuousRational: {
        constexpr NumericClosurePolicyV1 policy = numeric_closure_policy_v1();
        const bool declared_tree =
            evidence.tree_valid
            && evidence.body_count > 0U
            && evidence.body_count <= policy.exact_oblique_maximum_bodies
            && evidence.contact_count <= policy.exact_oblique_maximum_contacts
            && evidence.contact_count + 1U == evidence.body_count;
        return declared_tree && evidence.certificate_passed
            ? ObliqueCertificationDisposition::CertifiedDeclaredScope
            : ObliqueCertificationDisposition::Rejected;
    }
    case ObliqueEvidenceKind::FiniteGrid:
        return evidence.tree_valid && evidence.certificate_passed
            ? ObliqueCertificationDisposition::CertifiedFiniteGridOnly
            : ObliqueCertificationDisposition::Rejected;
    case ObliqueEvidenceKind::ResidualCertificate:
        return evidence.certificate_passed
            ? ObliqueCertificationDisposition::CertifiedResidualOnly
            : ObliqueCertificationDisposition::OperationalOnly;
    case ObliqueEvidenceKind::ConnectedCoordinateFallback:
        return ObliqueCertificationDisposition::OperationalOnly;
    }
    return ObliqueCertificationDisposition::Rejected;
}

const char* to_string(FixedPrimitiveStatus status) noexcept {
    switch (status) {
    case FixedPrimitiveStatus::Representable: return "representable";
    case FixedPrimitiveStatus::OverflowRejected: return "overflow_rejected";
    case FixedPrimitiveStatus::DivisionByZeroRejected:
        return "division_by_zero_rejected";
    }
    return "unknown";
}

const char* to_string(
    ObliqueCertificationDisposition disposition) noexcept {
    switch (disposition) {
    case ObliqueCertificationDisposition::CertifiedDeclaredScope:
        return "certified_declared_scope";
    case ObliqueCertificationDisposition::CertifiedFiniteGridOnly:
        return "certified_finite_grid_only";
    case ObliqueCertificationDisposition::CertifiedResidualOnly:
        return "certified_residual_only";
    case ObliqueCertificationDisposition::OperationalOnly:
        return "operational_only";
    case ObliqueCertificationDisposition::Rejected: return "rejected";
    }
    return "unknown";
}

} // namespace neoeng::core
