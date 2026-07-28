#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
PROJECT_VERSION = "1.10.0"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run(command: list[str], cwd: Path = ROOT) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def write_json(path: Path, value: Any) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def git_text(*args: str) -> str:
    result = run(["git", *args])
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {result.stdout}")
    return result.stdout.strip()


def project_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(
        r"project\(NeoEngDCore VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES C CXX\)",
        text,
    )
    if not match:
        raise RuntimeError("cannot determine project version")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tests", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler-id", required=True)
    parser.add_argument("--build-config", default="Release")
    args = parser.parse_args()

    tests = args.tests.resolve()
    probe = args.probe.resolve()
    output = args.output.resolve()
    if not tests.is_file() or not probe.is_file():
        parser.error("--tests and --probe must identify built executables")
    if project_version() != PROJECT_VERSION:
        raise RuntimeError(
            f"campaign expects {PROJECT_VERSION}, repository is {project_version()}"
        )
    output.mkdir(parents=True, exist_ok=True)

    source_identity = {
        "schema": "neoeng.dcore.source-identity.v1",
        "project_version": PROJECT_VERSION,
        "commit": git_text("rev-parse", "HEAD"),
        "branch": git_text("branch", "--show-current"),
        "worktree_dirty": bool(git_text("status", "--porcelain=v1")),
    }
    write_json(output / "source-identity.json", source_identity)

    cmake_version = run(["cmake", "--version"]).stdout.splitlines()[0]
    build_identity = {
        "schema": "neoeng.dcore.build-identity.v1",
        "project_version": PROJECT_VERSION,
        "compiler_id": args.compiler_id,
        "build_config": args.build_config,
        "cmake": cmake_version,
        "python": platform.python_version(),
        "tests_binary_sha256": sha256(tests),
        "probe_binary_sha256": sha256(probe),
    }
    write_json(output / "build-identity.json", build_identity)

    configuration = {
        "schema": "neoeng.dcore.distributed-reference-configuration.v1",
        "project_version": PROJECT_VERSION,
        "transport": "udp-loopback",
        "instances": 2,
        "frames": 4096,
        "divergence_frame": 1024,
        "maximum_datagram_bytes": 1472,
        "maximum_pending_datagrams": 64,
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor() or os.environ.get("PROCESSOR_IDENTIFIER", ""),
    }
    write_json(output / "configuration.json", configuration)

    tests_result = run([str(tests)])
    (output / "raw-tests.txt").write_text(
        tests_result.stdout, encoding="utf-8", newline="\n"
    )
    probe_result = run([str(probe)])
    (output / "raw-probe.json").write_text(
        probe_result.stdout, encoding="utf-8", newline="\n"
    )
    try:
        probe_json = json.loads(probe_result.stdout)
    except json.JSONDecodeError:
        probe_json = {}

    passed = (
        tests_result.returncode == 0
        and probe_result.returncode == 0
        and probe_json.get("before") == "divergent"
        and probe_json.get("localized") is True
        and probe_json.get("after") == "converged"
        and probe_json.get("canonical_state_equal") is True
    )
    summary = {
        "schema": "neoeng.dcore.distributed-reference-campaign-summary.v1",
        "project_version": PROJECT_VERSION,
        "status": "passed" if passed else "failed",
        "tests_exit_code": tests_result.returncode,
        "probe_exit_code": probe_result.returncode,
        "probe": probe_json,
        "native_or_external_results_inferred": False,
        "cross_architecture_claim_promoted": False,
    }
    write_json(output / "result-summary.json", summary)

    limitations = {
        "schema": "neoeng.dcore.distributed-reference-limitations.v1",
        "project_version": PROJECT_VERSION,
        "limitations": [
            "UDP v1 is a loopback reference, not a production remote transport.",
            "Datagram SHA-256 detects corruption but does not authenticate or encrypt.",
            "The campaign does not implement consensus, quorum, BFT or multiwriter ordering.",
            "An x86_64 result does not prove ARM64 equivalence.",
            "Measurements describe only the recorded host and configuration.",
        ],
        "native_or_external_results_may_be_inferred": False,
    }
    write_json(output / "limitations.json", limitations)

    manifest_entries = sorted(
        path
        for path in output.iterdir()
        if path.is_file()
        and path.name not in {"SHA256SUMS.txt", "independent-verification.json"}
    )
    manifest = "".join(f"{sha256(path)}  {path.name}\n" for path in manifest_entries)
    (output / "SHA256SUMS.txt").write_text(
        manifest, encoding="utf-8", newline="\n"
    )

    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
