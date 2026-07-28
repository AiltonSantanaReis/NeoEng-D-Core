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

PROJECT_VERSION = "1.11.0"
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
        if manifest_path.is_file()
        else []
    )
    if not manifest_path.is_file():
        errors.append("missing SHA256SUMS.txt")

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
        errors.append(f"required artifacts absent from manifest: {', '.join(missing)}")

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
        if name != "raw-probe.json" and document.get("project_version") != PROJECT_VERSION:
            errors.append(f"project_version mismatch: {name}")

    source = documents.get("source-identity.json", {})
    if not re.fullmatch(r"[0-9a-f]{40}", str(source.get("commit", ""))):
        errors.append("source commit is not a full Git object id")
    if source.get("worktree_dirty") is not False:
        errors.append("campaign source identity is not clean")

    configuration = documents.get("configuration.json", {})
    if (
        configuration.get("fixed_samples", 0) < 4096
        or configuration.get("raa_operation_iterations", 0) < 4096
        or configuration.get("exact_oblique_maximum_bodies") != 10
    ):
        errors.append("configuration does not meet CS011 campaign")

    probe = documents.get("raw-probe.json", {})
    summary = documents.get("result-summary.json", {})
    if (
        summary.get("status") != "passed"
        or summary.get("tests_exit_code") != 0
        or summary.get("probe_exit_code") != 0
        or probe.get("fixed_operation_cases", 0) < 20_480
        or probe.get("overflow_rejected", 0) <= 0
        or probe.get("division_by_zero_rejected", 0) <= 0
        or probe.get("raa_disjoint_products", 0) < 4096
        or probe.get("exact_oblique") != "certified_declared_scope"
        or probe.get("connected_fallback") != "operational_only"
        or probe.get("y1_o4_runtime_claim_allowed") is not False
        or probe.get("global_numeric_certificate_claim_allowed") is not False
        or summary.get("probe") != probe
    ):
        errors.append("campaign semantic result rejected")

    raw_tests = directory / "raw-tests.txt"
    if raw_tests.is_file() and "numeric_closure_tests=passed" not in raw_tests.read_text(
        encoding="utf-8"
    ):
        errors.append("raw tests do not report success")

    limitations = documents.get("limitations.json", {})
    if (
        limitations.get("native_or_external_results_may_be_inferred") is not False
        or len(limitations.get("limitations", [])) < 7
        or summary.get("native_or_external_results_inferred") is not False
        or summary.get("cross_architecture_claim_promoted") is not False
        or summary.get("global_numeric_certificate_promoted") is not False
    ):
        errors.append("limitations or non-inference policy rejected")

    return {
        "schema": "neoeng.dcore.numeric-closure-independent-verification.v1",
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
        "fixed_operation_cases": 20_480,
        "overflow_rejected": 1,
        "division_by_zero_rejected": 1,
        "raa_disjoint_products": 4096,
        "exact_oblique": "certified_declared_scope",
        "connected_fallback": "operational_only",
        "y1_o4_runtime_claim_allowed": False,
        "global_numeric_certificate_claim_allowed": False,
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
            "fixed_samples": 4096,
            "raa_operation_iterations": 4096,
            "exact_oblique_maximum_bodies": 10,
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
            "global_numeric_certificate_promoted": False,
        },
        "limitations.json": {
            "project_version": PROJECT_VERSION,
            "limitations": list("abcdefgh"),
            "native_or_external_results_may_be_inferred": False,
        },
    }
    for name, value in values.items():
        (directory / name).write_text(
            json.dumps(value, sort_keys=True) + "\n", encoding="utf-8"
        )
    (directory / "raw-tests.txt").write_text(
        "numeric_closure_tests=passed\n", encoding="utf-8"
    )
    manifest = "".join(
        f"{sha256(directory / name)}  {name}\n" for name in sorted(REQUIRED)
    )
    (directory / "SHA256SUMS.txt").write_text(manifest, encoding="utf-8")


def self_test() -> bool:
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        write_fixture(directory)
        if verify(directory)["status"] != "passed":
            return False
        with (directory / "raw-tests.txt").open("a", encoding="utf-8") as handle:
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
        print("numeric_closure_evidence_self_test=" + ("passed" if passed else "failed"))
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
