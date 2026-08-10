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
}
EXTERNAL_ATTESTATION_FILES = {
    "provenance-attestation.sigstore.json",
    "attestation-verification.json",
}


class AssemblyError(RuntimeError):
    pass


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


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


def run_verifier(arguments: list[str], marker: str) -> str:
    result = subprocess.run(
        [sys.executable, *arguments],
        cwd=ROOT,
        check=False,
        text=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise AssemblyError(
            f"{marker} verifier failed:\n{result.stdout}{result.stderr}"
        )
    return f"{marker}=passed\n{result.stdout}{result.stderr}".rstrip() + "\n"


def assert_ctest(path: Path) -> None:
    text = path.read_text(encoding="utf-8", errors="replace")
    if (
        "100% tests passed" not in text
        or re.search(r"\b[1-9][0-9]* tests failed out of", text)
    ):
        raise AssemblyError(f"CTest success marker absent: {path.name}")


def assemble(raw: Path, output: Path, *, verify_output: bool = True) -> dict[str, Any]:
    if output.exists() and any(output.iterdir()):
        raise AssemblyError(f"refusing to overwrite non-empty evidence: {output}")
    output.mkdir(parents=True, exist_ok=True)
    raw_output = output / "raw"
    raw_output.mkdir()
    for name in RAW_REQUIRED:
        destination = raw_output / name
        shutil.copyfile(locate(raw, name), destination)
        if name.endswith("-ctest.txt"):
            assert_ctest(destination)

    repository = os.environ.get("CS015_REPOSITORY", "")
    commit = os.environ.get("CS015_COMMIT", "")
    ref = os.environ.get("CS015_REF", "")
    run_id = os.environ.get("CS015_RUN_ID", "")
    run_attempt = os.environ.get("CS015_RUN_ATTEMPT", "")
    if (
        not repository
        or not re.fullmatch(r"[0-9a-f]{40}", commit)
        or not ref.startswith("refs/")
        or not run_id.isdigit()
        or not run_attempt.isdigit()
    ):
        raise AssemblyError("GitHub Actions source identity is incomplete")
    workflow_ref = os.environ.get(
        "CS015_WORKFLOW_REF",
        f"{repository}/.github/workflows/cs015-final-acceptance.yml@{ref}",
    )
    try:
        tree = subprocess.run(
            ["git", "rev-parse", f"{commit}^{{tree}}"],
            cwd=ROOT,
            check=True,
            text=True,
            capture_output=True,
            encoding="utf-8",
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as exc:
        raise AssemblyError(f"unable to resolve Git tree for {commit}: {exc}") from exc
    if not re.fullmatch(r"[0-9a-f]{40}", tree):
        raise AssemblyError("resolved Git tree is not a full lowercase SHA-1")

    verifier_specs = (
        (["scripts/verify_product_contract.py", "--check-report"], "product_contract"),
        (["scripts/verify_product_contract.py", "--self-test"], "product_contract_self_test"),
        (["scripts/verify_product_assurance.py", "--check-report"], "product_assurance"),
        (["scripts/verify_product_assurance.py", "--self-test"], "product_assurance_self_test"),
        (["scripts/verify_release_assurance.py"], "release_assurance"),
        (["scripts/verify_release_assurance.py", "--self-test"], "release_assurance_self_test"),
        (["scripts/verify_final_acceptance.py", "--check-report"], "final_acceptance"),
        (["scripts/verify_final_acceptance.py", "--self-test"], "final_acceptance_self_test"),
        (["scripts/generate_public_claims.py", "--check"], "public_claims"),
        (["scripts/generate_manifest.py", "--check"], "manifest"),
    )
    governance = "".join(
        run_verifier(list(arguments), marker)
        for arguments, marker in verifier_specs
    )
    (raw_output / "governance-verifiers.txt").write_text(
        governance, encoding="utf-8", newline="\n"
    )

    final_validation = json.loads(
        (ROOT / "audit/FINAL_ACCEPTANCE_VALIDATION.json").read_text(
            encoding="utf-8"
        )
    )
    prior_verification = json.loads(
        subprocess.run(
            [
                sys.executable,
                str(
                    ROOT
                    / "scripts/qualification/verify_release_assurance_evidence.py"
                ),
                str(
                    ROOT
                    / "docs/changesets/014/evidence/github-actions-run-30367653644"
                ),
            ],
            cwd=ROOT,
            check=True,
            text=True,
            capture_output=True,
            encoding="utf-8",
        ).stdout
    )
    if final_validation.get("status") != "passed":
        raise AssemblyError("stored final acceptance validation is not passed")
    if prior_verification.get("status") != "passed":
        raise AssemblyError("prior release assurance evidence was rejected")
    write_json(output / "final-acceptance-validation.json", final_validation)
    write_json(output / "prior-release-verification.json", prior_verification)
    write_json(
        output / "source-identity.json",
        {
            "schema": "neoeng.dcore.source-identity.v1",
            "project_version": VERSION,
            "repository": repository,
            "commit": commit,
            "ref": ref,
            "workflow_run_id": run_id,
            "workflow_run_attempt": run_attempt,
            "worktree_dirty": False,
        },
    )
    write_json(
        output / "build-identity.json",
        {
            "schema": "neoeng.dcore.final-acceptance-build-identity.v1",
            "project_version": VERSION,
            "workflow_run_id": run_id,
            "workflow_run_attempt": run_attempt,
            "compiler_identity_artifacts": [
                "raw/linux-gcc-build-identity.txt",
                "raw/linux-clang-build-identity.txt",
                "raw/windows-clang-cl-build-identity.txt",
            ],
            "assembly_runner": platform.platform(),
        },
    )
    write_json(
        output / "source-provenance.json",
        {
            "schema": "neoeng.dcore.final-acceptance-provenance.v1",
            "project_version": VERSION,
            "repository": repository,
            "commit": commit,
            "tree": tree,
            "ref": ref,
            "workflow_ref": workflow_ref,
            "workflow_run_id": run_id,
            "workflow_run_attempt": run_attempt,
            "artifact": "SHA256SUMS.txt",
        },
    )
    write_json(
        output / "configuration.json",
        {
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
    )
    state = str(final_validation["acceptance_state"])
    summary = {
        "schema": "neoeng.dcore.final-acceptance-campaign-summary.v1",
        "project_version": VERSION,
        "status": "passed",
        "acceptance_state": state,
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
    }
    write_json(output / "result-summary.json", summary)
    write_json(
        output / "limitations.json",
        {
            "schema": "neoeng.dcore.final-acceptance-limitations.v1",
            "project_version": VERSION,
            "limitations": [
                "Acceptance is limited to generated public claims and recorded product boundaries.",
                "No native ARM64 or P0-P4 hardware profile qualification is inferred.",
                "No certification or independent external audit is inferred.",
                "No unrestricted or mission-critical production-readiness claim is authorized.",
                "Fuzzing, sanitizers, static analysis and regression reduce risk but do not prove absence of defects.",
                "Performance observed on one host is not generalized; weaker or stronger hardware may produce worse or better results.",
            ],
            "unrestricted_production_readiness_may_be_inferred": False,
            "native_or_hardware_profile_qualification_may_be_inferred": False,
            "certification_or_external_audit_may_be_inferred": False,
            "performance_on_other_hardware_may_be_inferred": False,
        },
    )
    material = sorted(
        path
        for path in output.rglob("*")
        if path.is_file()
        and path.name
        not in {"SHA256SUMS.txt", "independent-verification.json", *EXTERNAL_ATTESTATION_FILES}
    )
    (output / "SHA256SUMS.txt").write_text(
        "".join(
            f"{sha256(path)}  {path.relative_to(output).as_posix()}\n"
            for path in material
        ),
        encoding="utf-8",
        newline="\n",
    )
    if verify_output:
        verification = subprocess.run(
            [
                sys.executable,
                str(
                    ROOT
                    / "scripts/qualification/verify_final_acceptance_evidence.py"
                ),
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
                "independent final acceptance verification failed:\n"
                + verification.stdout
                + verification.stderr
            )
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--raw", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--defer-external-verification", action="store_true")
    args = parser.parse_args()
    try:
        result = assemble(
            args.raw.resolve(),
            args.output.resolve(),
            verify_output=not args.defer_external_verification,
        )
    except (OSError, ValueError, json.JSONDecodeError, AssemblyError, subprocess.CalledProcessError) as exc:
        print(f"final-acceptance evidence assembly failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
