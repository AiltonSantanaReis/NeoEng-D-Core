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

ROOT = Path(__file__).resolve().parents[2]
PROJECT_VERSION = "1.13.0"
AUTHORIZATION_DECISIONS = 4096
PROTECTED_BUNDLES = 512


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )


def git_text(*arguments: str) -> str:
    result = run(["git", *arguments])
    if result.returncode != 0:
        raise RuntimeError(f"git {' '.join(arguments)} failed: {result.stdout}")
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

    write_json(output / "source-identity.json", {
        "schema": "neoeng.dcore.source-identity.v1",
        "project_version": PROJECT_VERSION,
        "commit": git_text("rev-parse", "HEAD"),
        "branch": git_text("branch", "--show-current"),
        "worktree_dirty": bool(git_text("status", "--porcelain=v1")),
    })
    write_json(output / "build-identity.json", {
        "schema": "neoeng.dcore.build-identity.v1",
        "project_version": PROJECT_VERSION,
        "compiler_id": args.compiler_id,
        "build_config": args.build_config,
        "cmake": run(["cmake", "--version"]).stdout.splitlines()[0],
        "python": platform.python_version(),
        "tests_binary_sha256": sha256(tests),
        "probe_binary_sha256": sha256(probe),
    })
    write_json(output / "configuration.json", {
        "schema": "neoeng.dcore.production-security-configuration.v1",
        "project_version": PROJECT_VERSION,
        "authorization_decisions": AUTHORIZATION_DECISIONS,
        "protected_bundles": PROTECTED_BUNDLES,
        "confidential_transport_required": True,
        "production_provider_external": True,
        "included_asymmetric_provider": False,
        "platform": platform.platform(),
        "machine": platform.machine(),
        "processor": platform.processor()
        or os.environ.get("PROCESSOR_IDENTIFIER", ""),
    })

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
        and probe_json.get("schema")
        == "neoeng.dcore.production-security.v1"
        and probe_json.get("status") == "passed"
        and probe_json.get("authorization_accepts")
        == AUTHORIZATION_DECISIONS
        and probe_json.get("authorization_rejections")
        == AUTHORIZATION_DECISIONS
        and probe_json.get("bundle_roundtrips") == PROTECTED_BUNDLES
        and probe_json.get("tamper_rejections") == PROTECTED_BUNDLES
        and probe_json.get("included_asymmetric_provider") is False
        and probe_json.get("test_provider_used") is True
        and probe_json.get("external_provider_required_for_production") is True
        and probe_json.get("external_anchor_trust_claimed") is False
        and probe_json.get("cross_architecture_claim_promoted") is False
        and re.fullmatch(
            r"[0-9a-f]{64}",
            str(probe_json.get("last_ciphertext_sha256", "")),
        )
        is not None
    )
    summary = {
        "schema":
            "neoeng.dcore.production-security-campaign-summary.v1",
        "project_version": PROJECT_VERSION,
        "status": "passed" if passed else "failed",
        "tests_exit_code": tests_result.returncode,
        "probe_exit_code": probe_result.returncode,
        "probe": probe_json,
        "native_or_external_results_inferred": False,
        "cross_architecture_claim_promoted": False,
        "included_asymmetric_provider_claimed": False,
        "production_provider_security_proven_by_test_provider": False,
        "external_anchor_trust_claimed": False,
    }
    write_json(output / "result-summary.json", summary)
    write_json(output / "limitations.json", {
        "schema": "neoeng.dcore.production-security-limitations.v1",
        "project_version": PROJECT_VERSION,
        "limitations": [
            "The product does not include a production asymmetric State Signature provider.",
            "The HMAC packet format does not provide confidentiality.",
            "Production confidentiality requires a conforming external transport and provider.",
            "Test-only providers prove integration behavior, not cryptographic strength.",
            "Private-key custody, entropy and trusted time remain deployment responsibilities.",
            "The product does not supply an external WORM/notary trust domain.",
            "Forward secrecy depends on the selected deployment transport.",
            "No independent cryptographic audit or certification was performed.",
            "An x86_64 result does not prove ARM64 equivalence.",
            "Measurements describe only the recorded host and configuration.",
        ],
        "native_or_external_results_may_be_inferred": False,
    })

    manifest_entries = sorted(
        path for path in output.iterdir()
        if path.is_file()
        and path.name not in {
            "SHA256SUMS.txt",
            "independent-verification.json",
        }
    )
    (output / "SHA256SUMS.txt").write_text(
        "".join(
            f"{sha256(path)}  {path.name}\n" for path in manifest_entries
        ),
        encoding="utf-8",
        newline="\n",
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
