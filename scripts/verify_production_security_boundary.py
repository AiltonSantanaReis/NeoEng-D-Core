#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def source_texts(root: Path) -> tuple[str, str, str, str, str]:
    return (
        (root / "CMakeLists.txt").read_text(encoding="utf-8"),
        (root / "include/neoeng/core/production_security.hpp").read_text(
            encoding="utf-8"
        ),
        (root / "src/production_security.cpp").read_text(encoding="utf-8"),
        (root / "docs/contracts/PRODUCTION_SECURITY_V1.md").read_text(
            encoding="utf-8"
        ),
        (root / "tests/production_security_tests.cpp").read_text(
            encoding="utf-8"
        ),
    )


def validate(
    cmake: str, header: str, source: str, contract: str, tests: str
) -> list[str]:
    errors: list[str] = []
    version = re.search(
        r"project\(NeoEngDCore VERSION (\d+)\.(\d+)\.(\d+) LANGUAGES C CXX\)",
        cmake,
    )
    if version is None or tuple(map(int, version.groups())) < (1, 13, 0):
        errors.append("unexpected project baseline")
    for required in (
        "src/production_security.cpp",
        "neoeng_production_security_tests",
        "neoeng_production_security_probe",
    ):
        if required not in cmake:
            errors.append(f"production-security integration missing: {required}")
    for required in (
        "kProductionAsymmetricSignatureProviderIncluded = false",
        "CommandAuthorizationPolicy",
        "ArtifactEncryptionProvider",
        "ProtectedSupportBundle",
        "EvidenceAnchorAdapter",
        "ExternalKeyDescriptor",
    ):
        if required not in header:
            errors.append(f"public security contract missing: {required}")
    for required in (
        "transport_security_context_valid",
        "verify_support_bundle",
        "AuthenticationFailed",
        "private_material_exportable",
        "provider_algorithm_allowed",
    ):
        if required not in source:
            errors.append(f"fail-closed security implementation missing: {required}")
    normalized = " ".join(contract.lower().split())
    for required in (
        "does not include a production asymmetric",
        "authenticated confidential transport",
        "command and entity authorization",
        "test-only providers",
        "external anchor adapter",
        "machines with lower or higher capability",
    ):
        if required not in normalized:
            errors.append(f"normative security decision missing: {required}")
    for required in (
        "out-of-scope entity accepted",
        "plaintext transport accepted",
        "tampered ciphertext accepted",
        "retired key accepted",
        "asymmetric provider",
    ):
        if required not in tests:
            errors.append(f"required security test coverage missing: {required}")
    return errors


def self_test(texts: tuple[str, str, str, str, str]) -> bool:
    cmake, header, source, contract, tests = texts
    mutations = (
        (
            cmake.replace("src/production_security.cpp", ""),
            header,
            source,
            contract,
            tests,
        ),
        (
            cmake,
            header.replace(
                "kProductionAsymmetricSignatureProviderIncluded = false",
                "kProductionAsymmetricSignatureProviderIncluded = true",
            ),
            source,
            contract,
            tests,
        ),
        (
            cmake,
            header,
            source.replace("verify_support_bundle", "verification_removed"),
            contract,
            tests,
        ),
        (
            cmake,
            header,
            source,
            contract.replace(
                "The product does not include",
                "The product includes",
            ),
            tests,
        ),
        (
            cmake,
            header,
            source,
            contract,
            tests.replace("tampered ciphertext accepted", "tampering omitted"),
        ),
    )
    return all(validate(*mutation) for mutation in mutations)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    texts = source_texts(ROOT)
    if args.self_test:
        passed = self_test(texts)
        print(
            "production_security_boundary_self_test="
            + ("passed" if passed else "failed")
        )
        return 0 if passed else 1
    errors = validate(*texts)
    if errors:
        print("\n".join(errors))
        return 1
    print(
        "OK: production transport, authorization, provider, protected-bundle "
        "and external-anchor boundaries are explicit."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
