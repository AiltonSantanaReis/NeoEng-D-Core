#!/usr/bin/env python3
"""Evaluate authoritative GCC and Clang NeoEng v0.28 Y1-G3 profiles together."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


def read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, help="directory containing gcc/ and clang/")
    args = parser.parse_args()
    errors: list[str] = []
    profiles: dict[str, dict] = {}
    for name, expected_compiler in (("gcc", "GCC"), ("clang", "Clang")):
        path = args.root / name / "gate_evaluation.json"
        try:
            record = read_json(path)
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"cannot read {path}: {exc}")
            continue
        if record.get("compiler_ids") != [expected_compiler]:
            errors.append(f"{name} profile compiler mismatch: {record.get('compiler_ids')}")
        if not bool(record.get("gate_passed", False)):
            errors.append(f"{name} profile gate is not approved")
        profiles[name] = record

    physical_hash_sets = {tuple(record.get("physical_hashes", [])) for record in profiles.values()}
    pair_hash_sets = {tuple(record.get("pair_hashes", [])) for record in profiles.values()}
    architecture_sets = {tuple(record.get("architectures", [])) for record in profiles.values()}
    requested_cpu_sets = {tuple(record.get("requested_cpus", [])) for record in profiles.values()}
    source_manifest_hashes = {record.get("source_manifest_sha256") for record in profiles.values()}
    if len(physical_hash_sets) > 1: errors.append("physical hashes differ between compiler profiles")
    if len(pair_hash_sets) > 1: errors.append("pair hashes differ between compiler profiles")
    if len(architecture_sets) > 1: errors.append("architectures differ between compiler profiles")
    if len(requested_cpu_sets) > 1: errors.append("requested CPU differs between compiler profiles")
    if len(source_manifest_hashes) > 1 or None in source_manifest_hashes:
        errors.append("source manifests differ between compiler profiles or are missing")

    result = {
        "schema": "neoeng.v0.28.y1-g3-matrix-evaluation.v2",
        "version": "0.28.0-development",
        "profiles_found": sorted(profiles),
        "gate_passed": len(profiles) == 2 and not errors,
        "errors": errors,
        "physical_hash_sets": [list(values) for values in sorted(physical_hash_sets)],
        "pair_hash_sets": [list(values) for values in sorted(pair_hash_sets)],
        "architecture_sets": [list(values) for values in sorted(architecture_sets)],
        "requested_cpu_sets": [list(values) for values in sorted(requested_cpu_sets)],
        "source_manifest_hashes": sorted(str(value) for value in source_manifest_hashes),
    }
    args.root.mkdir(parents=True, exist_ok=True)
    (args.root / "matrix_gate_evaluation.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0 if result["gate_passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
