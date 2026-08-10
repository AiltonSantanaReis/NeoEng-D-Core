#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any

VERSION = "1.14.1"
MANIFEST_PATTERN = re.compile(r"^([0-9a-f]{64})  ([^\r\n]+)$")
REQUIRED = {
    "source-identity.json",
    "build-identity.json",
    "configuration.json",
    "result-summary.json",
    "limitations.json",
    "release-artifacts.json",
    "independent-release-verification.json",
    "attestation-verification.json",
    "sbom-attestation-verification.json",
    "NeoEng-D-Core-1.14.1.provenance.sigstore.json",
    "NeoEng-D-Core-1.14.1.sbom.sigstore.json",
    "archive-reproducibility.txt",
    "raw/linux-gcc-ctest.txt",
    "raw/linux-clang-ctest.txt",
    "raw/windows-clang-cl-ctest.txt",
    "raw/linux-gcc-build-identity.txt",
    "raw/linux-clang-build-identity.txt",
    "raw/windows-clang-cl-build-identity.txt",
    "raw/sanitizer-ctest.txt",
    "raw/network-packet.txt",
    "raw/session-handshake.txt",
    "raw/raw-static-analysis.txt",
    "raw/static-analysis-summary.json",
}
SIGSTORE_PROVIDER = "Sigstore Public Good Instance (Fulcio and Rekor)"
SIGSTORE_ISSUER = "https://token.actions.githubusercontent.com"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_object(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path.name} root must be object")
    return value


def attestation_receipt_valid(
    value: Any,
    predicate_type: str,
    *,
    artifact_sha256: str,
    bundle_name: str,
    bundle_sha256: str,
    repository: str,
    commit: str,
    ref: str,
) -> bool:
    expected_identity = (
        f"https://github.com/{repository}/"
        f".github/workflows/cs014-release-assurance.yml@{ref}"
    )
    return (
        isinstance(value, dict)
        and value.get("schema")
        == "neoeng.dcore.sigstore-attestation-verification.v1"
        and value.get("project_version") == VERSION
        and value.get("status") == "passed"
        and value.get("provider") == SIGSTORE_PROVIDER
        and value.get("verifier") == "cosign 3.0.6"
        and value.get("artifact") == "NeoEng-D-Core-1.14.1.zip"
        and value.get("artifact_sha256") == artifact_sha256
        and value.get("bundle") == bundle_name
        and value.get("bundle_sha256") == bundle_sha256
        and value.get("predicate_type") == predicate_type
        and value.get("certificate_identity") == expected_identity
        and value.get("certificate_oidc_issuer") == SIGSTORE_ISSUER
        and value.get("repository") == repository
        and value.get("commit") == commit
        and value.get("ref") == ref
        and value.get("public_transparency_log_verified") is True
    )


def verify(directory: Path) -> dict[str, Any]:
    errors: list[str] = []
    manifest_path = directory / "SHA256SUMS.txt"
    lines = (
        manifest_path.read_text(encoding="utf-8").splitlines()
        if manifest_path.is_file() else []
    )
    if not lines:
        errors.append("missing or empty SHA256SUMS.txt")
    entries: dict[str, str] = {}
    for line in lines:
        match = MANIFEST_PATTERN.fullmatch(line)
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

    documents: dict[str, dict[str, Any]] = {}
    for name in (
        "source-identity.json",
        "build-identity.json",
        "configuration.json",
        "result-summary.json",
        "limitations.json",
        "release-artifacts.json",
        "independent-release-verification.json",
        "attestation-verification.json",
        "sbom-attestation-verification.json",
        "raw/static-analysis-summary.json",
    ):
        try:
            documents[name] = load_object(directory / name)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            errors.append(f"invalid {name}: {exc}")

    for name, document in documents.items():
        if document.get("project_version") != VERSION:
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
    build = documents.get("build-identity.json", {})
    if (
        build.get("workflow_run_id") != source.get("workflow_run_id")
        or build.get("workflow_run_attempt") != source.get("workflow_run_attempt")
        or len(build.get("release_compiler_identity_artifacts", [])) != 3
        or build.get("sanitizer_configuration") != ["address", "undefined"]
        or build.get("fuzzing_engine") != "LLVM libFuzzer"
        or build.get("static_analysis_engine") != "clang-tidy"
    ):
        errors.append("build identity rejected")

    configuration = documents.get("configuration.json", {})
    if (
        configuration.get("linux_compilers") != ["gcc", "clang"]
        or configuration.get("windows_compilers") != ["clang-cl"]
        or configuration.get("sanitizers") != ["address", "undefined"]
        or len(configuration.get("coverage_guided_fuzz_targets", [])) != 2
        or configuration.get("static_analysis_blocking") is not True
        or configuration.get("single_cumulative_archive") is not True
        or configuration.get("spdx_sbom_required") is not True
        or configuration.get("external_provenance_attestation_required") is not True
        or configuration.get("external_sbom_attestation_required") is not True
        or configuration.get("attestation_provider") != SIGSTORE_PROVIDER
        or configuration.get("public_transparency_log_required") is not True
    ):
        errors.append("campaign configuration rejected")

    summary = documents.get("result-summary.json", {})
    expected_gates = {
        "linux_gcc",
        "linux_clang",
        "windows_clang_cl",
        "asan_ubsan",
        "coverage_guided_fuzzing",
        "blocking_static_analysis",
        "consolidated_release_verification",
        "deterministic_archive_repeat",
        "provenance_attestation_verification",
        "sbom_attestation_verification",
    }
    gates = summary.get("gates", {})
    if (
        summary.get("status") != "passed"
        or not isinstance(gates, dict)
        or set(gates) != expected_gates
        or set(gates.values()) != {"passed"}
        or not re.fullmatch(
            r"[0-9a-f]{64}", str(summary.get("archive_sha256", ""))
        )
        or summary.get("external_signed_attestations_verified") is not True
        or summary.get("local_unsigned_candidate_publishable") is not False
        or summary.get("arm64_or_other_hardware_result_inferred") is not False
        or summary.get("certification_or_external_audit_claimed") is not False
        or summary.get("commercial_product_complete") is not False
    ):
        errors.append("campaign semantic result rejected")

    artifacts = documents.get("release-artifacts.json", {})
    package_verification = documents.get("independent-release-verification.json", {})
    if (
        artifacts.get("status") != "created"
        or artifacts.get("archive_sha256") != summary.get("archive_sha256")
        or artifacts.get("external_signed_attestation_required") is not True
        or artifacts.get("local_unsigned_candidate_publishable") is not False
        or package_verification.get("status") != "passed"
        or package_verification.get("local_unsigned_candidate_publishable") is not False
    ):
        errors.append("release artifact or package verification rejected")

    bundle_names = (
        "NeoEng-D-Core-1.14.1.provenance.sigstore.json",
        "NeoEng-D-Core-1.14.1.sbom.sigstore.json",
    )
    for name in bundle_names:
        try:
            bundle = json.loads((directory / name).read_text(encoding="utf-8"))
            if (
                not isinstance(bundle, dict)
                or "sigstore.bundle" not in str(bundle.get("mediaType", ""))
                or not isinstance(bundle.get("verificationMaterial"), dict)
                or not isinstance(bundle.get("dsseEnvelope"), dict)
            ):
                errors.append(f"Sigstore bundle structure rejected: {name}")
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"invalid Sigstore bundle {name}: {exc}")
    receipt_specs = (
        (
            "attestation-verification.json",
            "https://neoeng.dev/attestations/release-provenance/v1",
            bundle_names[0],
        ),
        (
            "sbom-attestation-verification.json",
            "https://spdx.dev/Document",
            bundle_names[1],
        ),
    )
    for receipt_name, predicate_type, bundle_name in receipt_specs:
        bundle_path = directory / bundle_name
        if not attestation_receipt_valid(
            documents.get(receipt_name, {}),
            predicate_type,
            artifact_sha256=str(artifacts.get("archive_sha256", "")),
            bundle_name=bundle_name,
            bundle_sha256=sha256(bundle_path) if bundle_path.is_file() else "",
            repository=str(source.get("repository", "")),
            commit=str(source.get("commit", "")),
            ref=str(source.get("ref", "")),
        ):
            errors.append(f"attestation receipt rejected: {receipt_name}")
    reproducibility = directory / "archive-reproducibility.txt"
    if reproducibility.is_file():
        hashes = [
            line.split()[0]
            for line in reproducibility.read_text(
                encoding="utf-8"
            ).splitlines()
            if line.split()
        ]
        if (
            len(hashes) != 2
            or len(set(hashes)) != 1
            or hashes[0] != summary.get("archive_sha256")
        ):
            errors.append("deterministic archive repeat rejected")

    static = documents.get("raw/static-analysis-summary.json", {})
    if (
        static.get("status") != "passed"
        or static.get("blocking") is not True
        or int(static.get("sources_analyzed", 0)) < 1
        or static.get("failed_sources") != []
    ):
        errors.append("blocking static-analysis result rejected")
    for name in (
        "raw/linux-gcc-ctest.txt",
        "raw/linux-clang-ctest.txt",
        "raw/windows-clang-cl-ctest.txt",
        "raw/sanitizer-ctest.txt",
    ):
        path = directory / name
        if path.is_file():
            text = path.read_text(encoding="utf-8", errors="replace")
            if (
                "100% tests passed" not in text
                or re.search(r"\b[1-9][0-9]* tests failed out of", text)
            ):
                errors.append(f"CTest success marker absent: {name}")
    for name in ("raw/network-packet.txt", "raw/session-handshake.txt"):
        path = directory / name
        if path.is_file():
            text = path.read_text(encoding="utf-8", errors="replace")
            if "DONE" not in text or "ERROR:" in text:
                errors.append(f"libFuzzer success marker absent: {name}")

    limitations = documents.get("limitations.json", {})
    if (
        len(limitations.get("limitations", [])) < 7
        or limitations.get("native_or_external_results_may_be_inferred") is not False
        or limitations.get("commercial_product_complete_may_be_inferred") is not False
    ):
        errors.append("limitations or non-inference policy rejected")
    return {
        "schema": "neoeng.dcore.release-assurance-independent-verification.v1",
        "project_version": VERSION,
        "status": "passed" if not errors else "failed",
        "errors": errors,
        "manifest_entries_verified": len(entries),
        "report_covered_by_manifest": False,
        "external_signed_attestations_verified": not any(
            "attestation" in error for error in errors
        ),
        "native_or_external_results_inferred": False,
        "commercial_product_complete": False,
    }


def write_fixture(directory: Path) -> None:
    provenance_bundle = {
        "mediaType": "application/vnd.dev.sigstore.bundle.v0.3+json",
        "verificationMaterial": {"fixture": True},
        "dsseEnvelope": {"fixture": True},
    }
    sbom_bundle = {
        "mediaType": "application/vnd.dev.sigstore.bundle.v0.3+json",
        "verificationMaterial": {"fixture": True},
        "dsseEnvelope": {"fixture": True},
    }
    identity = (
        "https://github.com/owner/repository/"
        ".github/workflows/cs014-release-assurance.yml@refs/heads/main"
    )
    values: dict[str, object] = {
        "source-identity.json": {
            "project_version": VERSION,
            "repository": "owner/repository",
            "commit": "a" * 40,
            "ref": "refs/heads/main",
            "workflow_run_id": "123",
            "workflow_run_attempt": "1",
            "worktree_dirty": False,
        },
        "build-identity.json": {
            "project_version": VERSION,
            "workflow_run_id": "123",
            "workflow_run_attempt": "1",
            "release_compiler_identity_artifacts": [
                "raw/linux-gcc-build-identity.txt",
                "raw/linux-clang-build-identity.txt",
                "raw/windows-clang-cl-build-identity.txt",
            ],
            "sanitizer_configuration": ["address", "undefined"],
            "fuzzing_engine": "LLVM libFuzzer",
            "static_analysis_engine": "clang-tidy",
        },
        "configuration.json": {
            "project_version": VERSION,
            "linux_compilers": ["gcc", "clang"],
            "windows_compilers": ["clang-cl"],
            "sanitizers": ["address", "undefined"],
            "coverage_guided_fuzz_targets": ["one", "two"],
            "static_analysis_blocking": True,
            "single_cumulative_archive": True,
            "spdx_sbom_required": True,
            "external_provenance_attestation_required": True,
            "external_sbom_attestation_required": True,
            "attestation_provider": SIGSTORE_PROVIDER,
            "public_transparency_log_required": True,
        },
        "result-summary.json": {
            "project_version": VERSION,
            "status": "passed",
            "gates": {
                name: "passed" for name in (
                    "linux_gcc", "linux_clang", "windows_clang_cl",
                    "asan_ubsan", "coverage_guided_fuzzing",
                    "blocking_static_analysis",
                    "consolidated_release_verification",
                    "deterministic_archive_repeat",
                    "provenance_attestation_verification",
                    "sbom_attestation_verification",
                )
            },
            "archive_sha256": "b" * 64,
            "external_signed_attestations_verified": True,
            "local_unsigned_candidate_publishable": False,
            "arm64_or_other_hardware_result_inferred": False,
            "certification_or_external_audit_claimed": False,
            "commercial_product_complete": False,
        },
        "limitations.json": {
            "project_version": VERSION,
            "limitations": list("abcdefg"),
            "native_or_external_results_may_be_inferred": False,
            "commercial_product_complete_may_be_inferred": False,
        },
        "release-artifacts.json": {
            "project_version": VERSION,
            "status": "created",
            "archive_sha256": "b" * 64,
            "external_signed_attestation_required": True,
            "local_unsigned_candidate_publishable": False,
        },
        "independent-release-verification.json": {
            "project_version": VERSION,
            "status": "passed",
            "local_unsigned_candidate_publishable": False,
        },
        "NeoEng-D-Core-1.14.1.provenance.sigstore.json": provenance_bundle,
        "NeoEng-D-Core-1.14.1.sbom.sigstore.json": sbom_bundle,
        "attestation-verification.json": {
            "schema": "neoeng.dcore.sigstore-attestation-verification.v1",
            "project_version": VERSION,
            "status": "passed",
            "provider": SIGSTORE_PROVIDER,
            "verifier": "cosign 3.0.6",
            "artifact": "NeoEng-D-Core-1.14.1.zip",
            "artifact_sha256": "b" * 64,
            "bundle": "NeoEng-D-Core-1.14.1.provenance.sigstore.json",
            "bundle_sha256": "",
            "predicate_type":
                "https://neoeng.dev/attestations/release-provenance/v1",
            "certificate_identity": identity,
            "certificate_oidc_issuer": SIGSTORE_ISSUER,
            "repository": "owner/repository",
            "commit": "a" * 40,
            "ref": "refs/heads/main",
            "public_transparency_log_verified": True,
        },
        "sbom-attestation-verification.json": {
            "schema": "neoeng.dcore.sigstore-attestation-verification.v1",
            "project_version": VERSION,
            "status": "passed",
            "provider": SIGSTORE_PROVIDER,
            "verifier": "cosign 3.0.6",
            "artifact": "NeoEng-D-Core-1.14.1.zip",
            "artifact_sha256": "b" * 64,
            "bundle": "NeoEng-D-Core-1.14.1.sbom.sigstore.json",
            "bundle_sha256": "",
            "predicate_type": "https://spdx.dev/Document",
            "certificate_identity": identity,
            "certificate_oidc_issuer": SIGSTORE_ISSUER,
            "repository": "owner/repository",
            "commit": "a" * 40,
            "ref": "refs/heads/main",
            "public_transparency_log_verified": True,
        },
        "raw/static-analysis-summary.json": {
            "project_version": VERSION,
            "status": "passed",
            "blocking": True,
            "sources_analyzed": 1,
            "failed_sources": [],
        },
    }
    for name, value in values.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value) + "\n", encoding="utf-8")
    for receipt_name, bundle_name in (
        (
            "attestation-verification.json",
            "NeoEng-D-Core-1.14.1.provenance.sigstore.json",
        ),
        (
            "sbom-attestation-verification.json",
            "NeoEng-D-Core-1.14.1.sbom.sigstore.json",
        ),
    ):
        receipt = values[receipt_name]
        assert isinstance(receipt, dict)
        receipt["bundle_sha256"] = sha256(directory / bundle_name)
        (directory / receipt_name).write_text(
            json.dumps(receipt) + "\n", encoding="utf-8"
        )
    for name in (
        "raw/linux-gcc-ctest.txt",
        "raw/linux-clang-ctest.txt",
        "raw/windows-clang-cl-ctest.txt",
        "raw/sanitizer-ctest.txt",
    ):
        summary = (
            "100% tests passed out of 1\n"
            if name == "raw/windows-clang-cl-ctest.txt"
            else "100% tests passed, 0 tests failed out of 1\n"
        )
        (directory / name).write_text(
            summary, encoding="utf-8"
        )
    for name in (
        "raw/linux-gcc-build-identity.txt",
        "raw/linux-clang-build-identity.txt",
        "raw/windows-clang-cl-build-identity.txt",
    ):
        (directory / name).write_text(
            "compiler version fixture\n", encoding="utf-8"
        )
    for name in ("raw/network-packet.txt", "raw/session-handshake.txt"):
        (directory / name).write_text("DONE\n", encoding="utf-8")
    (directory / "raw/raw-static-analysis.txt").write_text(
        "exit_code=0\n", encoding="utf-8"
    )
    (directory / "archive-reproducibility.txt").write_text(
        f"{'b' * 64}  first-build\n{'b' * 64}  independent-repeat\n",
        encoding="utf-8",
    )
    manifest_files = sorted(
        path for path in directory.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS.txt"
    )
    (directory / "SHA256SUMS.txt").write_text(
        "".join(
            f"{sha256(path)}  {path.relative_to(directory).as_posix()}\n"
            for path in manifest_files
        ),
        encoding="utf-8",
    )


def self_test() -> bool:
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        write_fixture(directory)
        if verify(directory)["status"] != "passed":
            return False
        with (directory / "raw/network-packet.txt").open(
            "a", encoding="utf-8"
        ) as handle:
            handle.write("tampered\n")
        result = verify(directory)
        return result["status"] == "failed" and any(
            "sha256 mismatch" in error for error in result["errors"]
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
            "release_assurance_evidence_self_test="
            + ("passed" if passed else "failed")
        )
        return 0 if passed else 1
    if args.directory is None:
        parser.error("directory is required unless --self-test is used")
    directory = args.directory.resolve()
    report = verify(directory)
    if args.write_report:
        (directory / "independent-verification.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
