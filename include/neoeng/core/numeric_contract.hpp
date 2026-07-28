#pragma once

#include "neoeng/core/fixed.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace neoeng::core {

enum class FixedPrimitiveOperation : std::uint8_t {
    Add,
    Subtract,
    Negate,
    Multiply,
    Divide,
};

enum class FixedPrimitiveStatus : std::uint8_t {
    Representable,
    OverflowRejected,
    DivisionByZeroRejected,
};

struct FixedPrimitiveDecision final {
    FixedPrimitiveStatus status{FixedPrimitiveStatus::Representable};
    Fixed::rep raw{};
};

// Independent policy classifier for the Q32.32 primitive implemented by Fixed.
// It uses a signed 128-bit exact intermediate and never narrows an
// unrepresentable result.
[[nodiscard]] FixedPrimitiveDecision classify_fixed_primitive(
    FixedPrimitiveOperation operation,
    Fixed::rep lhs,
    Fixed::rep rhs = 0) noexcept;

enum class ObliqueEvidenceKind : std::uint8_t {
    ExactContinuousRational,
    FiniteGrid,
    ResidualCertificate,
    ConnectedCoordinateFallback,
};

enum class ObliqueCertificationDisposition : std::uint8_t {
    CertifiedDeclaredScope,
    CertifiedFiniteGridOnly,
    CertifiedResidualOnly,
    OperationalOnly,
    Rejected,
};

struct ObliqueEvidence final {
    ObliqueEvidenceKind kind{ObliqueEvidenceKind::ConnectedCoordinateFallback};
    std::size_t body_count{};
    std::size_t contact_count{};
    bool input_valid{};
    bool tree_valid{};
    bool certificate_passed{};
};

[[nodiscard]] ObliqueCertificationDisposition classify_oblique_evidence(
    const ObliqueEvidence& evidence) noexcept;

struct NumericClosurePolicyV1 final {
    std::string_view schema{"neoeng.dcore.numeric-closure.v1"};
    bool y1_o4_runtime_claim_allowed{};
    bool global_composed_numeric_certificate_claim_allowed{};
    bool fixed_primitives_use_exact_wide_intermediate{true};
    bool unrepresentable_fixed_result_is_rejected{true};
    std::size_t exact_oblique_maximum_bodies{10U};
    std::size_t exact_oblique_maximum_contacts{9U};
    std::size_t raa_minimum_terms{2U};
    std::size_t raa_maximum_terms{16U};
};

[[nodiscard]] constexpr NumericClosurePolicyV1 numeric_closure_policy_v1() noexcept {
    return {};
}

[[nodiscard]] const char* to_string(FixedPrimitiveStatus status) noexcept;
[[nodiscard]] const char* to_string(
    ObliqueCertificationDisposition disposition) noexcept;

} // namespace neoeng::core
