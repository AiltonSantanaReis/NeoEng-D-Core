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

PROJECT_VERSION = "1.10.0"
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
    if not manifest_path.is_file():
        errors.append("missing SHA256SUMS.txt")
        lines: list[str] = []
    else:
        lines = manifest_path.read_text(encoding="utf-8").splitlines()

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
    missing_required = sorted(REQUIRED - set(entries))
    if missing_required:
        errors.append(f"required artifacts absent from manifest: {', '.join(missing_required)}")

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
        if document.get("project_version") != PROJECT_VERSION and name != "raw-probe.json":
            errors.append(f"project_version mismatch: {name}")

    source = documents.get("source-identity.json", {})
    if not re.fullmatch(r"[0-9a-f]{40}", str(source.get("commit", ""))):
        errors.append("source commit is not a full Git object id")
    if source.get("worktree_dirty") is not False:
        errors.append("campaign source identity is not clean")

    configuration = documents.get("configuration.json", {})
    if (
        configuration.get("transport") != "udp-loopback"
        or configuration.get("instances") != 2
        or configuration.get("frames", 0) < 4096
    ):
        errors.append("configuration does not meet CS010 campaign")

    raw_probe = documents.get("raw-probe.json", {})
    summary = documents.get("result-summary.json", {})
    if (
        summary.get("status") != "passed"
        or summary.get("tests_exit_code") != 0
        or summary.get("probe_exit_code") != 0
        or raw_probe.get("before") != "divergent"
        or raw_probe.get("localized") is not True
        or raw_probe.get("after") != "converged"
        or raw_probe.get("canonical_state_equal") is not True
        or summary.get("probe") != raw_probe
    ):
        errors.append("campaign semantic result rejected")
    raw_tests = directory / "raw-tests.txt"
    if raw_tests.is_file() and "distributed_reference_tests=passed" not in raw_tests.read_text(
        encoding="utf-8"
    ):
        errors.append("raw tests do not report success")

    limitations = documents.get("limitations.json", {})
    if (
        limitations.get("native_or_external_results_may_be_inferred") is not False
        or len(limitations.get("limitations", [])) < 5
        or summary.get("native_or_external_results_inferred") is not False
        or summary.get("cross_architecture_claim_promoted") is not False
    ):
        errors.append("limitations or non-inference policy rejected")

    return {
        "schema": "neoeng.dcore.distributed-reference-independent-verification.v1",
        "project_version": PROJECT_VERSION,
        "status": "passed" if not errors else "failed",
        "errors": errors,
        "manifest_entries_verified": len(entries),
        "report_covered_by_manifest": False,
        "capability_proof_scope": "recorded campaign only",
        "native_or_external_results_inferred": False,
    }


def write_fixture(directory: Path) -> None:
    values: dict[str, Any] = {
        "source-identity.json": {
            "project_version": PROJECT_VERSION,
            "commit": "a" * 40,
            "worktree_dirty": False,
        },
        "build-identity.json": {"project_version": PROJECT_VERSION},
        "configuration.json": {
            "project_version": PROJECT_VERSION,
            "transport": "udp-loopback",
            "instances": 2,
            "frames": 4096,
        },
        "raw-probe.json": {
            "before": "divergent",
            "localized": True,
            "after": "converged",
            "canonical_state_equal": True,
        },
        "result-summary.json": {
            "project_version": PROJECT_VERSION,
            "status": "passed",
            "tests_exit_code": 0,
            "probe_exit_code": 0,
            "probe": {
                "before": "divergent",
                "localized": True,
                "after": "converged",
                "canonical_state_equal": True,
            },
            "native_or_external_results_inferred": False,
            "cross_architecture_claim_promoted": False,
        },
        "limitations.json": {
            "project_version": PROJECT_VERSION,
            "limitations": ["a", "b", "c", "d", "e"],
            "native_or_external_results_may_be_inferred": False,
        },
    }
    for name, value in values.items():
        (directory / name).write_text(
            json.dumps(value, sort_keys=True) + "\n", encoding="utf-8"
        )
    (directory / "raw-tests.txt").write_text(
        "distributed_reference_tests=passed\n", encoding="utf-8"
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
        print("distributed_reference_evidence_self_test=" + ("passed" if passed else "failed"))
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
