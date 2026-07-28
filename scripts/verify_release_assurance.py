#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import json
import re
import sys
from pathlib import Path
from typing import Any

from generate_public_claims import ALLOWED_STATUSES, is_public, render

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_VERSION = "1.14.0"
CLAIMS = ROOT / "audit/PRODUCT_CLAIMS_LEDGER.json"
CAPABILITIES = ROOT / "audit/PRODUCT_CAPABILITY_SURFACE.json"
POLICY = ROOT / "audit/RELEASE_ASSURANCE_POLICY.json"
PUBLIC_CLAIMS = ROOT / "docs/commercial/PUBLIC_CLAIMS.md"


def load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"root must be object: {path}")
    return value


def repository_text(root: Path, pattern: str) -> str:
    return "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in sorted(root.glob(pattern))
        if path.is_file()
    )


def verify_documents(
    root: Path,
    claims: dict[str, Any],
    capabilities: dict[str, Any],
    policy: dict[str, Any],
    *,
    workflow_override: str | None = None,
    cmake_override: str | None = None,
    host_header_override: str | None = None,
    public_claims_override: str | None = None,
) -> list[str]:
    errors: list[str] = []
    for name, document in (
        ("claims", claims),
        ("capabilities", capabilities),
        ("policy", policy),
    ):
        if document.get("project_version") != EXPECTED_VERSION:
            errors.append(f"{name} project_version mismatch")

    claim_rows = claims.get("claims")
    delivery_rows = capabilities.get("claim_delivery")
    surfaces = capabilities.get("surfaces")
    if not isinstance(claim_rows, list):
        errors.append("claims rows missing")
        claim_rows = []
    if not isinstance(delivery_rows, list):
        errors.append("claim delivery rows missing")
        delivery_rows = []
    if not isinstance(surfaces, dict) or not surfaces:
        errors.append("supported surfaces missing")
        surfaces = {}

    public_ids = {
        row.get("claim_id")
        for row in claim_rows
        if isinstance(row, dict) and is_public(row)
    }
    mapped_ids: set[str] = set()
    for row in delivery_rows:
        if not isinstance(row, dict):
            errors.append("invalid claim delivery row")
            continue
        claim_id = row.get("claim_id")
        mapped = row.get("surfaces")
        if claim_id in mapped_ids:
            errors.append(f"duplicate claim delivery: {claim_id}")
        mapped_ids.add(str(claim_id))
        if claim_id not in public_ids:
            errors.append(f"non-public claim mapped as capability: {claim_id}")
        if not isinstance(mapped, list) or not mapped:
            errors.append(f"claim has no supported surface: {claim_id}")
            continue
        for surface in mapped:
            if surface not in surfaces:
                errors.append(f"unknown surface for {claim_id}: {surface}")
    if mapped_ids != public_ids:
        errors.append(
            "public claim coverage mismatch: "
            f"missing={sorted(public_ids - mapped_ids)} "
            f"extra={sorted(mapped_ids - public_ids)}"
        )

    cmake = (
        cmake_override if cmake_override is not None
        else (root / "CMakeLists.txt").read_text(encoding="utf-8")
            + repository_text(root, "modules/*/CMakeLists.txt")
    )
    for surface in surfaces.values():
        if not isinstance(surface, dict):
            errors.append("invalid surface definition")
            continue
        for key in ("installed_headers", "installed_header", "source_of_truth",
                    "claims_ledger", "public_claims"):
            value = surface.get(key)
            if isinstance(value, str) and not (root / value).exists():
                errors.append(f"surface path missing: {value}")
        target = surface.get("cmake_target")
        if isinstance(target, str):
            export_name = target.split("::")[-1]
            if export_name not in cmake:
                errors.append(f"surface CMake target missing: {target}")
        for target_name in surface.get("targets", []):
            if target_name not in cmake:
                errors.append(f"official tool target missing: {target_name}")

    fuzz = policy.get("coverage_guided_fuzzing", {})
    if (
        fuzz.get("engine") != "LLVM libFuzzer"
        or set(fuzz.get("sanitizers", [])) != {"address", "undefined"}
        or len(fuzz.get("targets", [])) < 2
        or fuzz.get("untrusted_binary_ingress_covered") is not True
    ):
        errors.append("coverage-guided fuzzing policy incomplete")
    for target in fuzz.get("targets", []):
        if target not in cmake:
            errors.append(f"libFuzzer target missing: {target}")
    for corpus in fuzz.get("seed_corpora", []):
        path = root / corpus
        if not path.is_dir() or not any(path.iterdir()):
            errors.append(f"seed corpus missing or empty: {corpus}")

    sanitizer = policy.get("sanitizer_gate", {})
    if (
        set(sanitizer.get("sanitizers", [])) != {"address", "undefined"}
        or sanitizer.get("full_supported_surface") is not True
    ):
        errors.append("sanitizer gate incomplete")
    static = policy.get("static_analysis_gate", {})
    if static.get("engine") != "clang-tidy" or static.get("blocking") is not True:
        errors.append("blocking static analysis is not configured")

    authenticity = policy.get("release_authenticity", {})
    if (
        authenticity.get("publication_requires_external_signed_attestation")
        is not True
        or authenticity.get("provider")
        != "Sigstore Public Good Instance (Fulcio and Rekor) via Cosign 3.0.6"
        or authenticity.get("verification_tool")
        != "cosign verify-blob-attestation"
        or authenticity.get("verification_bundle_format") != "Sigstore bundle"
        or authenticity.get("certificate_oidc_issuer")
        != "https://token.actions.githubusercontent.com"
        or authenticity.get("public_transparency_log_required") is not True
        or "source contents are not published"
        not in str(authenticity.get("public_metadata_disclosure", ""))
        or authenticity.get("private_signing_key_in_repository") is not False
        or authenticity.get("local_unsigned_candidate_is_publishable") is not False
    ):
        errors.append("release authenticity policy is not fail-closed")
    update = policy.get("secure_update", {})
    if (
        update.get("verify_attestation_before_archive_use") is not True
        or update.get("verify_archive_sha256") is not True
        or update.get("verify_internal_manifest_before_install") is not True
        or update.get("automatic_execution_of_downloaded_content") is not False
    ):
        errors.append("secure update policy is not fail-closed")
    entitlement = policy.get("commercial_entitlement", {})
    if (
        entitlement.get("model") != "distribution_and_contract_controlled"
        or entitlement.get("runtime_entitlement_enforcement_included") is not False
        or entitlement.get("canonical_state_contains_entitlement_decisions") is not False
        or entitlement.get("public_redistribution_permission_inferred") is not False
    ):
        errors.append("commercial entitlement crosses the canonical boundary")

    workflow = (
        workflow_override if workflow_override is not None
        else (root / ".github/workflows/cs014-release-assurance.yml").read_text(
            encoding="utf-8", errors="replace"
        ) if (root / ".github/workflows/cs014-release-assurance.yml").is_file()
        else ""
    )
    for token in (
        "release-matrix",
        "sanitizers",
        "coverage-guided-fuzzing",
        "static-analysis",
        "consolidated-release",
        "archive-reproducibility.txt",
        "sigstore/cosign-installer@v4.1.0",
        'cosign-release: "v3.0.6"',
        "cosign attest-blob",
        "cosign verify-blob-attestation",
        "id-token: write",
    ):
        if token not in workflow:
            errors.append(f"release workflow token missing: {token}")

    host_header = (
        host_header_override if host_header_override is not None
        else (root / "modules/host_sdk/include/neoeng/dcore_host.h").read_text(
            encoding="utf-8", errors="replace"
        )
    )
    for macro, value in (
        ("NEOENG_DCORE_RUNTIME_VERSION_MAJOR", "1"),
        ("NEOENG_DCORE_RUNTIME_VERSION_MINOR", "14"),
        ("NEOENG_DCORE_RUNTIME_VERSION_PATCH", "0"),
    ):
        if not re.search(
            rf"#define\s+{macro}\s+UINT16_C\({value}\)", host_header
        ):
            errors.append(f"Host SDK runtime macro mismatch: {macro}")

    public_claims = (
        public_claims_override if public_claims_override is not None
        else PUBLIC_CLAIMS.read_text(encoding="utf-8")
        if PUBLIC_CLAIMS.is_file() else ""
    )
    expected_claims = render(claims)
    if public_claims != expected_claims:
        errors.append("generated public claims are absent or divergent")
    for row in claim_rows:
        if (
            isinstance(row, dict)
            and row.get("status") not in ALLOWED_STATUSES
            and str(row.get("statement", "")) in public_claims
        ):
            errors.append(f"non-public statement leaked: {row.get('claim_id')}")

    required_paths = (
        "scripts/create_consolidated_release.py",
        "scripts/verify_consolidated_release.py",
        "scripts/run_static_analysis.py",
        "scripts/qualification/write_sigstore_verification_receipt.py",
        "docs/contracts/RELEASE_ASSURANCE_V1.md",
        "docs/governance/COMMERCIAL_DELIVERY_POLICY.md",
    )
    for relative in required_paths:
        if not (root / relative).is_file():
            errors.append(f"release assurance artifact missing: {relative}")
    return errors


def self_test(
    root: Path,
    claims: dict[str, Any],
    capabilities: dict[str, Any],
    policy: dict[str, Any],
) -> list[str]:
    failures: list[str] = []
    mutations: list[tuple[str, dict[str, Any], dict[str, Any], dict[str, Any],
                          dict[str, str]]] = []

    missing_claim = copy.deepcopy(capabilities)
    missing_claim["claim_delivery"] = missing_claim["claim_delivery"][1:]
    mutations.append(("missing-public-claim", claims, missing_claim, policy, {}))

    mapped_prohibited = copy.deepcopy(capabilities)
    mapped_prohibited["claim_delivery"].append({
        "claim_id": "CLAIM-PROD-READY-001",
        "surfaces": ["cpp_sdk"],
    })
    mutations.append(("mapped-prohibited-claim", claims, mapped_prohibited, policy, {}))

    embedded_key = copy.deepcopy(policy)
    embedded_key["release_authenticity"]["private_signing_key_in_repository"] = True
    mutations.append(("embedded-private-key", claims, capabilities, embedded_key, {}))

    canonical_license = copy.deepcopy(policy)
    canonical_license["commercial_entitlement"][
        "canonical_state_contains_entitlement_decisions"
    ] = True
    mutations.append(("canonical-entitlement", claims, capabilities, canonical_license, {}))

    unsigned_publish = copy.deepcopy(policy)
    unsigned_publish["release_authenticity"][
        "local_unsigned_candidate_is_publishable"
    ] = True
    mutations.append(("unsigned-publish", claims, capabilities, unsigned_publish, {}))

    private_log = copy.deepcopy(policy)
    private_log["release_authenticity"][
        "public_transparency_log_required"
    ] = False
    mutations.append(("private-transparency-log", claims, capabilities, private_log, {}))

    for name, claim_doc, capability_doc, policy_doc, overrides in mutations:
        errors = verify_documents(
            root, claim_doc, capability_doc, policy_doc,
            workflow_override=overrides.get("workflow"),
        )
        if not errors:
            failures.append(f"mutation accepted: {name}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    claims = load(CLAIMS)
    capabilities = load(CAPABILITIES)
    policy = load(POLICY)
    if args.self_test:
        failures = self_test(ROOT, claims, capabilities, policy)
        if failures:
            print("\n".join(failures))
            return 1
        print("release_assurance_self_test=passed")
        return 0
    errors = verify_documents(ROOT, claims, capabilities, policy)
    if errors:
        print("\n".join(errors))
        return 1
    print(
        "release_assurance=passed; "
        f"public_claims={len(capabilities['claim_delivery'])}; "
        f"libfuzzer_targets={len(policy['coverage_guided_fuzzing']['targets'])}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
