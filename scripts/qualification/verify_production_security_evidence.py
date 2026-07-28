#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tempfile
from pathlib import Path
from typing import Any

PROJECT_VERSION = "1.13.0"
REQUIRED = {
    "source-identity.json",
    "build-identity.json",
    "configuration.json",
    "raw-tests.txt",
    "raw-probe.json",
    "result-summary.json",
    "limitations.json",
}
MANIFEST_PATTERN = re.compile(r"^([0-9a-f]{64})  ([A-Za-z0-9._-]+)$")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"root must be object: {path.name}")
    return value


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
        expected, name = match.groups()
        if name in entries:
            errors.append(f"duplicate manifest entry: {name}")
            continue
        entries[name] = expected
        path = directory / name
        if not path.is_file():
            errors.append(f"missing artifact: {name}")
        elif sha256(path) != expected:
            errors.append(f"sha256 mismatch: {name}")
    missing = sorted(REQUIRED - set(entries))
    if missing:
        errors.append(f"required artifacts absent: {', '.join(missing)}")

    documents: dict[str, dict[str, Any]] = {}
    for name in (
        "source-identity.json",
        "build-identity.json",
        "configuration.json",
        "raw-probe.json",
        "result-summary.json",
        "limitations.json",
    ):
        try:
            documents[name] = load_json(directory / name)
        except (OSError, ValueError, json.JSONDecodeError) as exc:
            errors.append(f"invalid {name}: {exc}")
    for name, document in documents.items():
        if name != "raw-probe.json" and document.get(
            "project_version"
        ) != PROJECT_VERSION:
            errors.append(f"project_version mismatch: {name}")

    source = documents.get("source-identity.json", {})
    if not re.fullmatch(r"[0-9a-f]{40}", str(source.get("commit", ""))):
        errors.append("source commit is not a full Git object id")
    if source.get("worktree_dirty") is not False:
        errors.append("campaign source identity is not clean")

    configuration = documents.get("configuration.json", {})
    if (
        configuration.get("authorization_decisions", 0) < 4096
        or configuration.get("protected_bundles", 0) < 512
        or configuration.get("confidential_transport_required") is not True
        or configuration.get("production_provider_external") is not True
        or configuration.get("included_asymmetric_provider") is not False
    ):
        errors.append("configuration does not meet CS013 campaign")

    probe = documents.get("raw-probe.json", {})
    summary = documents.get("result-summary.json", {})
    if (
        summary.get("status") != "passed"
        or summary.get("tests_exit_code") != 0
        or summary.get("probe_exit_code") != 0
        or probe.get("status") != "passed"
        or probe.get("authorization_accepts") != 4096
        or probe.get("authorization_rejections") != 4096
        or probe.get("bundle_roundtrips") != 512
        or probe.get("tamper_rejections") != 512
        or probe.get("included_asymmetric_provider") is not False
        or probe.get("test_provider_used") is not True
        or probe.get("external_provider_required_for_production") is not True
        or probe.get("external_anchor_trust_claimed") is not False
        or probe.get("cross_architecture_claim_promoted") is not False
        or not re.fullmatch(
            r"[0-9a-f]{64}",
            str(probe.get("last_ciphertext_sha256", "")),
        )
        or summary.get("probe") != probe
    ):
        errors.append("campaign semantic result rejected")

    raw_tests = directory / "raw-tests.txt"
    if raw_tests.is_file() and "production_security_tests=passed" not in (
        raw_tests.read_text(encoding="utf-8")
    ):
        errors.append("raw tests do not report success")
    limitations = documents.get("limitations.json", {})
    if (
        limitations.get("native_or_external_results_may_be_inferred") is not False
        or len(limitations.get("limitations", [])) < 10
        or summary.get("native_or_external_results_inferred") is not False
        or summary.get("cross_architecture_claim_promoted") is not False
        or summary.get("included_asymmetric_provider_claimed") is not False
        or summary.get("production_provider_security_proven_by_test_provider")
        is not False
        or summary.get("external_anchor_trust_claimed") is not False
    ):
        errors.append("limitations or non-inference policy rejected")

    return {
        "schema":
            "neoeng.dcore.production-security-independent-verification.v1",
        "project_version": PROJECT_VERSION,
        "status": "passed" if not errors else "failed",
        "errors": errors,
        "manifest_entries_verified": len(entries),
        "report_covered_by_manifest": False,
        "capability_proof_scope": "recorded campaign only",
        "native_or_external_results_inferred": False,
    }


def write_fixture(directory: Path) -> None:
    probe = {
        "status": "passed",
        "authorization_accepts": 4096,
        "authorization_rejections": 4096,
        "bundle_roundtrips": 512,
        "tamper_rejections": 512,
        "last_ciphertext_sha256": "a" * 64,
        "included_asymmetric_provider": False,
        "test_provider_used": True,
        "external_provider_required_for_production": True,
        "external_anchor_trust_claimed": False,
        "cross_architecture_claim_promoted": False,
    }
    values: dict[str, Any] = {
        "source-identity.json": {
            "project_version": PROJECT_VERSION,
            "commit": "a" * 40,
            "worktree_dirty": False,
        },
        "build-identity.json": {"project_version": PROJECT_VERSION},
        "configuration.json": {
            "project_version": PROJECT_VERSION,
            "authorization_decisions": 4096,
            "protected_bundles": 512,
            "confidential_transport_required": True,
            "production_provider_external": True,
            "included_asymmetric_provider": False,
        },
        "raw-probe.json": probe,
        "result-summary.json": {
            "project_version": PROJECT_VERSION,
            "status": "passed",
            "tests_exit_code": 0,
            "probe_exit_code": 0,
            "probe": probe,
            "native_or_external_results_inferred": False,
            "cross_architecture_claim_promoted": False,
            "included_asymmetric_provider_claimed": False,
            "production_provider_security_proven_by_test_provider": False,
            "external_anchor_trust_claimed": False,
        },
        "limitations.json": {
            "project_version": PROJECT_VERSION,
            "limitations": list("abcdefghij"),
            "native_or_external_results_may_be_inferred": False,
        },
    }
    for name, value in values.items():
        (directory / name).write_text(
            json.dumps(value, sort_keys=True) + "\n",
            encoding="utf-8",
        )
    (directory / "raw-tests.txt").write_text(
        "production_security_tests=passed\n", encoding="utf-8"
    )
    (directory / "SHA256SUMS.txt").write_text(
        "".join(
            f"{sha256(directory / name)}  {name}\n"
            for name in sorted(REQUIRED)
        ),
        encoding="utf-8",
    )


def self_test() -> bool:
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        write_fixture(directory)
        if verify(directory)["status"] != "passed":
            return False
        with (directory / "raw-tests.txt").open(
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
            "production_security_evidence_self_test="
            + ("passed" if passed else "failed")
        )
        return 0 if passed else 1
    if args.directory is None:
        parser.error("directory is required unless --self-test is used")
    directory = args.directory.resolve()
    result = verify(directory)
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
