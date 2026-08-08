#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any

VERSION = "1.14.0"
MANIFEST_LINE = re.compile(r"^([0-9a-f]{64})  ([^\r\n]+)$")
REQUIRED = {
    "source-identity.json",
    "build-identity.json",
    "configuration.json",
    "result-summary.json",
    "limitations.json",
    "final-acceptance-validation.json",
    "prior-release-verification.json",
    "raw/linux-gcc-ctest.txt",
    "raw/linux-clang-ctest.txt",
    "raw/windows-clang-cl-ctest.txt",
    "raw/linux-gcc-build-identity.txt",
    "raw/linux-clang-build-identity.txt",
    "raw/windows-clang-cl-build-identity.txt",
    "raw/governance-verifiers.txt",
}
EXTERNAL_ATTESTATION_FILES = {
    "provenance-attestation.sigstore.json",
    "attestation-verification.json",
}
PROVENANCE_PREDICATE = "https://neoeng.dev/attestations/final-acceptance-provenance/v1"
SIGSTORE_ISSUER = "https://token.actions.githubusercontent.com"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_object(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path.name} root must be an object")
    return value


def verify(directory: Path, *, require_external_attestation: bool = False) -> dict[str, Any]:
    errors: list[str] = []
    manifest = directory / "SHA256SUMS.txt"
    lines = manifest.read_text(encoding="utf-8").splitlines() if manifest.is_file() else []
    entries: dict[str, str] = {}
    if not lines:
        errors.append("missing or empty SHA256SUMS.txt")
    for line in lines:
        match = MANIFEST_LINE.fullmatch(line)
        if not match:
            errors.append(f"invalid manifest line: {line!r}")
            continue
        expected, relative = match.groups()
        pure = PurePosixPath(relative)
        if pure.is_absolute() or ".." in pure.parts or "\\" in relative:
            errors.append(f"unsafe manifest path: {relative}")
            continue
        if relative in entries:
            errors.append(f"duplicate manifest entry: {relative}")
            continue
        entries[relative] = expected
        path = directory / Path(*pure.parts)
        if not path.is_file():
            errors.append(f"missing artifact: {relative}")
        elif sha256(path) != expected:
            errors.append(f"sha256 mismatch: {relative}")
    missing = sorted(REQUIRED - set(entries))
    if missing:
        errors.append(f"required artifacts absent: {', '.join(missing)}")
    material = {
        path.relative_to(directory).as_posix()
        for path in directory.rglob("*")
        if path.is_file()
        and path.name
        not in {"SHA256SUMS.txt", "independent-verification.json", *EXTERNAL_ATTESTATION_FILES}
    }
    additional = sorted(material - set(entries))
    if additional:
        errors.append(f"unmanifested artifacts present: {', '.join(additional)}")

    documents: dict[str, dict[str, Any]] = {}
    document_names = [
        "source-identity.json",
        "build-identity.json",
        "configuration.json",
        "result-summary.json",
        "limitations.json",
        "final-acceptance-validation.json",
        "prior-release-verification.json",
    ]
    if require_external_attestation:
        document_names.append("source-provenance.json")
    for name in document_names:
        try:
            documents[name] = load_object(directory / name)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            errors.append(f"invalid {name}: {exc}")
    for name, value in documents.items():
        if value.get("project_version") != VERSION:
            errors.append(f"project_version mismatch: {name}")

    source = documents.get("source-identity.json", {})
    if (
        not re.fullmatch(r"[0-9a-f]{40}", str(source.get("commit", "")))
        or not source.get("repository")
        or not str(source.get("ref", "")).startswith("refs/")
        or not str(source.get("workflow_run_id", "")).isdigit()
        or not str(source.get("workflow_run_attempt", "")).isdigit()
        or source.get("worktree_dirty") is not False
    ):
        errors.append("source identity rejected")

    provenance = documents.get("source-provenance.json", {})
    if require_external_attestation and (
        provenance.get("schema") != "neoeng.dcore.final-acceptance-provenance.v1"
        or provenance.get("repository") != source.get("repository")
        or provenance.get("commit") != source.get("commit")
        or provenance.get("ref") != source.get("ref")
        or provenance.get("workflow_run_id") != source.get("workflow_run_id")
        or provenance.get("workflow_run_attempt") != source.get("workflow_run_attempt")
        or not re.fullmatch(r"[0-9a-f]{40}", str(provenance.get("tree", "")))
        or not str(provenance.get("workflow_ref", "")).startswith(
            f"{source.get('repository', '')}/.github/workflows/"
        )
    ):
        errors.append("source provenance rejected")

    build = documents.get("build-identity.json", {})
    if (
        build.get("workflow_run_id") != source.get("workflow_run_id")
        or build.get("workflow_run_attempt") != source.get("workflow_run_attempt")
        or build.get("compiler_identity_artifacts") != [
            "raw/linux-gcc-build-identity.txt",
            "raw/linux-clang-build-identity.txt",
            "raw/windows-clang-cl-build-identity.txt",
        ]
    ):
        errors.append("build identity rejected")

    configuration = documents.get("configuration.json", {})
    if (
        configuration.get("supported_regression_targets") != [
            "linux-x86_64-gcc",
            "linux-x86_64-clang",
            "windows-x86_64-clang-cl",
        ]
        or configuration.get("prior_cs014_evidence_reverification_required") is not True
        or configuration.get("independent_verification_required") is not True
        or configuration.get("sha256_manifest_required") is not True
    ):
        errors.append("campaign configuration rejected")

    summary = documents.get("result-summary.json", {})
    state = summary.get("acceptance_state")
    gates = summary.get("gates")
    if (
        summary.get("status") != "passed"
        or state not in {"closure_candidate", "accepted"}
        or not isinstance(gates, dict)
        or set(gates) != {
            "linux_gcc_regression",
            "linux_clang_regression",
            "windows_clang_cl_regression",
            "governance_fail_closed",
            "public_claim_reconciliation",
            "prior_release_assurance_reverification",
        }
        or set(gates.values()) != {"passed"}
        or summary.get("unrestricted_production_readiness_inferred") is not False
        or summary.get("native_or_hardware_profile_qualification_inferred") is not False
        or summary.get("certification_or_external_audit_inferred") is not False
        or summary.get("performance_on_other_hardware_inferred") is not False
    ):
        errors.append("campaign semantic result rejected")

    final_validation = documents.get("final-acceptance-validation.json", {})
    if (
        final_validation.get("status") != "passed"
        or final_validation.get("acceptance_state") != state
        or final_validation.get("prior_release_assurance_evidence_verified") is not True
        or final_validation.get("unsupported_or_prohibited_public_claims") != 0
    ):
        errors.append("final acceptance validation rejected")
    prior = documents.get("prior-release-verification.json", {})
    if (
        prior.get("status") != "passed"
        or prior.get("external_signed_attestations_verified") is not True
        or prior.get("commercial_product_complete") is not False
    ):
        errors.append("prior release-assurance verification rejected")

    for name in (
        "raw/linux-gcc-ctest.txt",
        "raw/linux-clang-ctest.txt",
        "raw/windows-clang-cl-ctest.txt",
    ):
        path = directory / name
        if path.is_file():
            text = path.read_text(encoding="utf-8", errors="replace")
            if (
                "100% tests passed" not in text
                or re.search(r"\b[1-9][0-9]* tests failed out of", text)
            ):
                errors.append(f"CTest success marker absent: {name}")
    governance = directory / "raw/governance-verifiers.txt"
    if governance.is_file():
        text = governance.read_text(encoding="utf-8", errors="replace")
        for marker in (
            "product_contract=passed",
            "product_assurance=passed",
            "release_assurance=passed",
            "final_acceptance=passed",
            "public_claims=passed",
            "manifest=passed",
        ):
            if marker not in text:
                errors.append(f"governance marker absent: {marker}")

    limitations = documents.get("limitations.json", {})
    if (
        len(limitations.get("limitations", [])) < 6
        or limitations.get("unrestricted_production_readiness_may_be_inferred") is not False
        or limitations.get("native_or_hardware_profile_qualification_may_be_inferred") is not False
        or limitations.get("certification_or_external_audit_may_be_inferred") is not False
        or limitations.get("performance_on_other_hardware_may_be_inferred") is not False
    ):
        errors.append("limitations or non-inference policy rejected")

    if require_external_attestation:
        bundle = directory / "provenance-attestation.sigstore.json"
        receipt_path = directory / "attestation-verification.json"
        artifact = directory / "SHA256SUMS.txt"
        if not bundle.is_file() or not receipt_path.is_file():
            errors.append("external provenance attestation is absent")
        else:
            try:
                receipt = load_object(receipt_path)
                expected_identity = f"https://github.com/{provenance['workflow_ref']}"
                if (
                    receipt.get("schema")
                    != "neoeng.dcore.sigstore-attestation-verification.v1"
                    or receipt.get("status") != "passed"
                    or receipt.get("artifact") != artifact.name
                    or receipt.get("artifact_sha256") != sha256(artifact)
                    or receipt.get("bundle") != bundle.name
                    or receipt.get("bundle_sha256") != sha256(bundle)
                    or receipt.get("predicate_type") != PROVENANCE_PREDICATE
                    or receipt.get("repository") != source.get("repository")
                    or receipt.get("commit") != source.get("commit")
                    or receipt.get("ref") != source.get("ref")
                    or receipt.get("certificate_identity") != expected_identity
                    or receipt.get("certificate_oidc_issuer") != SIGSTORE_ISSUER
                    or receipt.get("public_transparency_log_verified") is not True
                    or provenance.get("artifact") != artifact.name
                ):
                    errors.append("external provenance receipt rejected")
                cosign = shutil.which("cosign")
                if cosign is None:
                    errors.append("cosign is required for external provenance verification")
                else:
                    checked = subprocess.run(
                        [
                            cosign,
                            "verify-blob-attestation",
                            "--bundle",
                            str(bundle),
                            "--type",
                            PROVENANCE_PREDICATE,
                            "--certificate-identity",
                            expected_identity,
                            "--certificate-oidc-issuer",
                            SIGSTORE_ISSUER,
                            "--certificate-github-workflow-repository",
                            str(source.get("repository")),
                            "--certificate-github-workflow-sha",
                            str(source.get("commit")),
                            "--certificate-github-workflow-ref",
                            str(source.get("ref")),
                            str(artifact),
                        ],
                        check=False,
                        capture_output=True,
                        text=True,
                        encoding="utf-8",
                        errors="replace",
                    )
                    if checked.returncode != 0:
                        errors.append(
                            "cosign provenance verification failed: "
                            + (checked.stdout + checked.stderr).strip()
                        )
            except (OSError, ValueError, json.JSONDecodeError, KeyError) as exc:
                errors.append(f"external provenance verification error: {exc}")
    return {
        "schema": "neoeng.dcore.final-acceptance-independent-verification.v1",
        "project_version": VERSION,
        "status": "passed" if not errors else "failed",
        "acceptance_state": state if state in {"closure_candidate", "accepted"} else "invalid",
        "errors": errors,
        "manifest_entries_verified": len(entries),
        "report_covered_by_manifest": False,
        "horizontal_product_baseline_accepted": state == "accepted" and not errors,
        "unrestricted_production_readiness_inferred": False,
        "native_or_external_results_inferred": False,
    }


def _fixture(directory: Path) -> None:
    raw = directory / "raw"
    raw.mkdir(parents=True)
    source = {
        "schema": "neoeng.dcore.source-identity.v1",
        "project_version": VERSION,
        "repository": "example/NeoEng-D-Core",
        "commit": "a" * 40,
        "ref": "refs/heads/changeset-015",
        "workflow_run_id": "1",
        "workflow_run_attempt": "1",
        "worktree_dirty": False,
    }
    documents: dict[str, object] = {
        "source-identity.json": source,
        "source-provenance.json": {
            "schema": "neoeng.dcore.final-acceptance-provenance.v1",
            "project_version": VERSION,
            "repository": source["repository"],
            "commit": source["commit"],
            "tree": "b" * 40,
            "ref": source["ref"],
            "workflow_run_id": source["workflow_run_id"],
            "workflow_run_attempt": source["workflow_run_attempt"],
            "workflow_ref": "example/NeoEng-D-Core/.github/workflows/cs015-final-acceptance.yml@refs/heads/changeset-015",
        },
        "build-identity.json": {
            "schema": "neoeng.dcore.final-acceptance-build-identity.v1",
            "project_version": VERSION,
            "workflow_run_id": "1",
            "workflow_run_attempt": "1",
            "compiler_identity_artifacts": [
                "raw/linux-gcc-build-identity.txt",
                "raw/linux-clang-build-identity.txt",
                "raw/windows-clang-cl-build-identity.txt",
            ],
        },
        "configuration.json": {
            "schema": "neoeng.dcore.final-acceptance-configuration.v1",
            "project_version": VERSION,
            "supported_regression_targets": [
                "linux-x86_64-gcc",
                "linux-x86_64-clang",
                "windows-x86_64-clang-cl",
            ],
            "prior_cs014_evidence_reverification_required": True,
            "independent_verification_required": True,
            "sha256_manifest_required": True,
        },
        "result-summary.json": {
            "schema": "neoeng.dcore.final-acceptance-campaign-summary.v1",
            "project_version": VERSION,
            "status": "passed",
            "acceptance_state": "closure_candidate",
            "gates": {
                "linux_gcc_regression": "passed",
                "linux_clang_regression": "passed",
                "windows_clang_cl_regression": "passed",
                "governance_fail_closed": "passed",
                "public_claim_reconciliation": "passed",
                "prior_release_assurance_reverification": "passed",
            },
            "unrestricted_production_readiness_inferred": False,
            "native_or_hardware_profile_qualification_inferred": False,
            "certification_or_external_audit_inferred": False,
            "performance_on_other_hardware_inferred": False,
        },
        "final-acceptance-validation.json": {
            "schema": "neoeng.dcore.final-acceptance-validation.v1",
            "project_version": VERSION,
            "status": "passed",
            "acceptance_state": "closure_candidate",
            "prior_release_assurance_evidence_verified": True,
            "unsupported_or_prohibited_public_claims": 0,
        },
        "prior-release-verification.json": {
            "schema": "neoeng.dcore.release-assurance-independent-verification.v1",
            "project_version": VERSION,
            "status": "passed",
            "external_signed_attestations_verified": True,
            "commercial_product_complete": False,
        },
        "limitations.json": {
            "schema": "neoeng.dcore.final-acceptance-limitations.v1",
            "project_version": VERSION,
            "limitations": [str(i) for i in range(6)],
            "unrestricted_production_readiness_may_be_inferred": False,
            "native_or_hardware_profile_qualification_may_be_inferred": False,
            "certification_or_external_audit_may_be_inferred": False,
            "performance_on_other_hardware_may_be_inferred": False,
        },
    }
    for name, value in documents.items():
        (directory / name).write_text(
            json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    for name in (
        "linux-gcc-ctest.txt",
        "linux-clang-ctest.txt",
        "windows-clang-cl-ctest.txt",
    ):
        (raw / name).write_text("100% tests passed, 0 tests failed out of 54\n", encoding="utf-8")
    for name in (
        "linux-gcc-build-identity.txt",
        "linux-clang-build-identity.txt",
        "windows-clang-cl-build-identity.txt",
    ):
        (raw / name).write_text("compiler fixture\n", encoding="utf-8")
    (raw / "governance-verifiers.txt").write_text(
        "\n".join(
            f"{name}=passed"
            for name in (
                "product_contract",
                "product_assurance",
                "release_assurance",
                "final_acceptance",
                "public_claims",
                "manifest",
            )
        ) + "\n",
        encoding="utf-8",
    )
    material = sorted(path for path in directory.rglob("*") if path.is_file())
    (directory / "SHA256SUMS.txt").write_text(
        "".join(
            f"{sha256(path)}  {path.relative_to(directory).as_posix()}\n"
            for path in material
        ),
        encoding="utf-8",
    )


def self_test() -> bool:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        _fixture(root)
        if verify(root)["status"] != "passed":
            return False
        unsigned = verify(root, require_external_attestation=True)
        if unsigned["status"] != "failed" or not any(
            "external provenance attestation is absent" in error
            for error in unsigned["errors"]
        ):
            return False
        target = root / "raw/linux-gcc-ctest.txt"
        target.write_text("99% tests passed, 1 tests failed out of 54\n", encoding="utf-8")
        failed = verify(root)
        return failed["status"] == "failed" and any(
            "sha256 mismatch" in error or "CTest success marker absent" in error
            for error in failed["errors"]
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path, nargs="?")
    parser.add_argument("--write-report", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        passed = self_test()
        print(
            "final_acceptance_evidence_self_test="
            + ("passed" if passed else "failed")
        )
        return 0 if passed else 1
    if args.directory is None:
        parser.error("directory is required unless --self-test is used")
    directory = args.directory.resolve()
    result = verify(directory, require_external_attestation=True)
    if args.write_report:
        (directory / "independent-verification.json").write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
