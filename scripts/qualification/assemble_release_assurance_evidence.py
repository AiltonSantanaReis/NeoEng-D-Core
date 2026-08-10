#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
VERSION = "1.14.1"
RAW_REQUIRED = {
    "linux-gcc-ctest.txt",
    "linux-clang-ctest.txt",
    "windows-clang-cl-ctest.txt",
    "linux-gcc-build-identity.txt",
    "linux-clang-build-identity.txt",
    "windows-clang-cl-build-identity.txt",
    "sanitizer-ctest.txt",
    "network-packet.txt",
    "session-handshake.txt",
    "raw-static-analysis.txt",
    "static-analysis-summary.json",
}
RELEASE_REQUIRED = {
    "release-artifacts.json",
    "independent-verification.json",
    "attestation-verification.json",
    "sbom-attestation-verification.json",
    "NeoEng-D-Core-1.14.1.provenance.sigstore.json",
    "NeoEng-D-Core-1.14.1.sbom.sigstore.json",
    "archive-reproducibility.txt",
}
SIGSTORE_PROVIDER = "Sigstore Public Good Instance (Fulcio and Rekor)"
SIGSTORE_ISSUER = "https://token.actions.githubusercontent.com"


class AssemblyError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def locate(directory: Path, name: str) -> Path:
    matches = sorted(directory.rglob(name))
    if len(matches) != 1:
        raise AssemblyError(
            f"expected exactly one {name!r} below {directory}, found {len(matches)}"
        )
    return matches[0]


def assert_ctest_passed(path: Path) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    if (
        "100% tests passed" not in text
        or re.search(r"\b[1-9][0-9]* tests failed out of", text)
    ):
        raise AssemblyError(f"CTest success marker absent: {path.name}")


def assert_fuzzer_passed(path: Path) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    if "DONE" not in text or "ERROR:" in text:
        raise AssemblyError(f"libFuzzer completion marker absent: {path.name}")


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


def assemble(raw: Path, release: Path, output: Path) -> dict[str, Any]:
    if output.exists() and any(output.iterdir()):
        raise AssemblyError(f"refusing to overwrite non-empty evidence: {output}")
    output.mkdir(parents=True, exist_ok=True)
    raw_output = output / "raw"
    raw_output.mkdir()

    raw_paths = {name: locate(raw, name) for name in RAW_REQUIRED}
    for name, source in raw_paths.items():
        shutil.copyfile(source, raw_output / name)
    for name in RELEASE_REQUIRED:
        destination = (
            "independent-release-verification.json"
            if name == "independent-verification.json"
            else name
        )
        shutil.copyfile(locate(release, name), output / destination)

    for name in (
        "linux-gcc-ctest.txt",
        "linux-clang-ctest.txt",
        "windows-clang-cl-ctest.txt",
        "sanitizer-ctest.txt",
    ):
        assert_ctest_passed(raw_output / name)
    for name in ("network-packet.txt", "session-handshake.txt"):
        assert_fuzzer_passed(raw_output / name)

    static_summary = load_json(raw_output / "static-analysis-summary.json")
    release_artifacts = load_json(output / "release-artifacts.json")
    release_verification = load_json(
        output / "independent-release-verification.json"
    )
    provenance_attestation = load_json(output / "attestation-verification.json")
    sbom_attestation = load_json(output / "sbom-attestation-verification.json")
    reproducibility_lines = (
        output / "archive-reproducibility.txt"
    ).read_text(encoding="utf-8").splitlines()
    if (
        not isinstance(static_summary, dict)
        or static_summary.get("status") != "passed"
        or static_summary.get("blocking") is not True
    ):
        raise AssemblyError("blocking static-analysis summary rejected")
    if (
        not isinstance(release_artifacts, dict)
        or release_artifacts.get("status") != "created"
        or release_artifacts.get("project_version") != VERSION
        or release_artifacts.get("external_signed_attestation_required") is not True
        or release_artifacts.get("local_unsigned_candidate_publishable") is not False
    ):
        raise AssemblyError("release artifact record rejected")
    if (
        not isinstance(release_verification, dict)
        or release_verification.get("status") != "passed"
        or release_verification.get("project_version") != VERSION
    ):
        raise AssemblyError("independent release verification rejected")

    repository = os.environ.get("CS014_REPOSITORY", "")
    commit = os.environ.get("CS014_COMMIT", "")
    ref = os.environ.get("CS014_REF", "")
    run_id = os.environ.get("CS014_RUN_ID", "")
    run_attempt = os.environ.get("CS014_RUN_ATTEMPT", "")
    if (
        not repository
        or len(commit) != 40
        or not ref.startswith("refs/")
        or not run_id
        or not run_attempt
    ):
        raise AssemblyError("GitHub Actions source identity is incomplete")

    archive_sha256 = str(release_artifacts.get("archive_sha256", ""))
    if not attestation_receipt_valid(
        provenance_attestation,
        "https://neoeng.dev/attestations/release-provenance/v1",
        artifact_sha256=archive_sha256,
        bundle_name="NeoEng-D-Core-1.14.1.provenance.sigstore.json",
        bundle_sha256=sha256(
            output / "NeoEng-D-Core-1.14.1.provenance.sigstore.json"
        ),
        repository=repository,
        commit=commit,
        ref=ref,
    ):
        raise AssemblyError("external provenance attestation rejected")
    if not attestation_receipt_valid(
        sbom_attestation,
        "https://spdx.dev/Document",
        artifact_sha256=archive_sha256,
        bundle_name="NeoEng-D-Core-1.14.1.sbom.sigstore.json",
        bundle_sha256=sha256(
            output / "NeoEng-D-Core-1.14.1.sbom.sigstore.json"
        ),
        repository=repository,
        commit=commit,
        ref=ref,
    ):
        raise AssemblyError("external SBOM attestation rejected")
    reproducibility_hashes = [
        line.split()[0] for line in reproducibility_lines if line.split()
    ]
    if (
        len(reproducibility_hashes) != 2
        or len(set(reproducibility_hashes)) != 1
        or reproducibility_hashes[0] != release_artifacts.get("archive_sha256")
    ):
        raise AssemblyError("deterministic archive repeat rejected")

    write_json(output / "source-identity.json", {
        "schema": "neoeng.dcore.source-identity.v1",
        "project_version": VERSION,
        "repository": repository,
        "commit": commit,
        "ref": ref,
        "workflow_run_id": run_id,
        "workflow_run_attempt": run_attempt,
        "worktree_dirty": False,
    })
    write_json(output / "build-identity.json", {
        "schema": "neoeng.dcore.release-assurance-build-identity.v1",
        "project_version": VERSION,
        "workflow_run_id": run_id,
        "workflow_run_attempt": run_attempt,
        "release_compiler_identity_artifacts": [
            "raw/linux-gcc-build-identity.txt",
            "raw/linux-clang-build-identity.txt",
            "raw/windows-clang-cl-build-identity.txt",
        ],
        "sanitizer_configuration": ["address", "undefined"],
        "fuzzing_engine": "LLVM libFuzzer",
        "static_analysis_engine": "clang-tidy",
        "assembly_runner": platform.platform(),
    })
    write_json(output / "configuration.json", {
        "schema": "neoeng.dcore.release-assurance-configuration.v1",
        "project_version": VERSION,
        "linux_compilers": ["gcc", "clang"],
        "windows_compilers": ["clang-cl"],
        "sanitizers": ["address", "undefined"],
        "coverage_guided_fuzz_targets": [
            "neoeng_network_packet_libfuzzer",
            "neoeng_session_handshake_libfuzzer",
        ],
        "static_analysis_blocking": True,
        "single_cumulative_archive": True,
        "spdx_sbom_required": True,
        "external_provenance_attestation_required": True,
        "external_sbom_attestation_required": True,
        "attestation_provider": SIGSTORE_PROVIDER,
        "public_transparency_log_required": True,
    })
    summary = {
        "schema": "neoeng.dcore.release-assurance-campaign-summary.v1",
        "project_version": VERSION,
        "status": "passed",
        "gates": {
            "linux_gcc": "passed",
            "linux_clang": "passed",
            "windows_clang_cl": "passed",
            "asan_ubsan": "passed",
            "coverage_guided_fuzzing": "passed",
            "blocking_static_analysis": "passed",
            "consolidated_release_verification": "passed",
            "deterministic_archive_repeat": "passed",
            "provenance_attestation_verification": "passed",
            "sbom_attestation_verification": "passed",
        },
        "archive_sha256": release_artifacts["archive_sha256"],
        "external_signed_attestations_verified": True,
        "local_unsigned_candidate_publishable": False,
        "arm64_or_other_hardware_result_inferred": False,
        "certification_or_external_audit_claimed": False,
        "commercial_product_complete": False,
    }
    write_json(output / "result-summary.json", summary)
    write_json(output / "limitations.json", {
        "schema": "neoeng.dcore.release-assurance-limitations.v1",
        "project_version": VERSION,
        "limitations": [
            "The campaign does not prove ARM64 behavior.",
            "The campaign is not an independent security audit or certification.",
            "Coverage-guided fuzzing reduces risk but does not prove absence of defects.",
            "Sanitizer and static-analysis success do not prove absence of defects.",
            "Performance results from one host are not generalized to other hardware.",
            "The public Sigstore log exposes repository, workflow, ref and commit metadata but not source contents.",
            "Commercial completion remains governed by the CS015 acceptance decision.",
        ],
        "native_or_external_results_may_be_inferred": False,
        "commercial_product_complete_may_be_inferred": False,
    })

    manifest_files = sorted(
        path for path in output.rglob("*")
        if path.is_file()
        and path.name not in {
            "SHA256SUMS.txt",
            "independent-verification.json",
        }
    )
    (output / "SHA256SUMS.txt").write_text(
        "".join(
            f"{sha256(path)}  {path.relative_to(output).as_posix()}\n"
            for path in manifest_files
        ),
        encoding="utf-8",
        newline="\n",
    )
    verification = subprocess.run(
        [
            sys.executable,
            str(ROOT / "scripts/qualification/verify_release_assurance_evidence.py"),
            str(output),
            "--write-report",
        ],
        cwd=ROOT,
        check=False,
        text=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    if verification.returncode != 0:
        raise AssemblyError(
            "independent campaign verification failed:\n"
            + verification.stdout
            + verification.stderr
        )
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw", required=True, type=Path)
    parser.add_argument("--release", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        summary = assemble(
            args.raw.resolve(), args.release.resolve(), args.output.resolve()
        )
    except (OSError, ValueError, json.JSONDecodeError, AssemblyError) as exc:
        print(f"release-assurance evidence assembly failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
