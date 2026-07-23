#!/usr/bin/env python3
"""Compare native x86_64 and ARM64 campaign evidence without comparing timing."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from pathlib import Path


def normalized_arch(value: str) -> str:
    return value.lower().replace("amd64", "x86_64").replace("arm64", "aarch64")


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("left", type=Path)
    parser.add_argument("right", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    verifier = Path(__file__).with_name("verify_qualification_campaign.py")
    for campaign in (args.left, args.right):
        subprocess.run([sys.executable, str(verifier), str(campaign)], check=True)

    inventories = [json.loads((path / "hardware-inventory.json").read_text(encoding="utf-8")) for path in (args.left, args.right)]
    summaries = [json.loads((path / "campaign-summary.json").read_text(encoding="utf-8")) for path in (args.left, args.right)]
    architectures = [normalized_arch(item["observed"]["architecture"]) for item in inventories]
    if set(architectures) != {"x86_64", "aarch64"}:
        raise RuntimeError(f"expected one x86_64 and one aarch64 campaign, got {architectures}")
    if any(item.get("execution_kind") != "native_physical" for item in summaries):
        raise RuntimeError("cross-architecture acceptance requires two native physical campaigns")
    for item in summaries:
        if item.get("full_tests_passed") is not True \
                or item.get("determinism_passed") is not True \
                or item.get("serialization_passed") is not True:
            raise RuntimeError("cross-architecture acceptance requires passing full tests and semantic probes")

    files = [
        "source-MANIFEST.sha256",
        "determinism-probe.stdout.txt",
        "state-evidence-probe.stdout.txt",
    ]
    comparisons = []
    accepted = True
    for name in files:
        left_hash = sha256_file(args.left / name)
        right_hash = sha256_file(args.right / name)
        equal = left_hash == right_hash
        accepted = accepted and equal
        comparisons.append({"file": name, "left_sha256": left_hash, "right_sha256": right_hash, "equal": equal})
    report = {
        "schema": "neoeng.dcore.cross-architecture-comparison.v1",
        "architectures": architectures,
        "performance_compared": False,
        "comparisons": comparisons,
        "accepted": accepted,
    }
    text = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8", newline="\n")
    else:
        print(text, end="")
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
