#!/usr/bin/env python3
"""Strictly evaluate ten independent NeoEng v0.28 Y1-G3 bare-metal runs."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import sys

EXPECTED_PHYSICAL_HASH = "0x5F2E9261E2D9B633"
EXPECTED_PAIR_HASH = "0x219D1F55E5DC6A8C"


def parse_state(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    try:
        for line in path.read_text(encoding="utf-8").splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                result[key.strip()] = value.strip()
    except OSError:
        pass
    return result


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_thermal_review(root: Path, review_path: Path | None, errors: list[str]) -> bool:
    if review_path is None:
        errors.append("thermal review file was not provided")
        return False
    try:
        review = json.loads(review_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"cannot read thermal review {review_path}: {exc}")
        return False
    if review.get("schema") != "neoeng.v0.28.thermal-review.v1":
        errors.append("thermal review schema mismatch")
    if review.get("confirmed_no_throttling") is not True:
        errors.append("thermal review does not confirm absence of throttling")
    if not str(review.get("reviewer", "")).strip():
        errors.append("thermal review is missing reviewer")
    if not str(review.get("notes", "")).strip():
        errors.append("thermal review is missing notes")
    telemetry_name = review.get("telemetry_file")
    telemetry = root / str(telemetry_name) if telemetry_name else None
    if telemetry is None or not telemetry.is_file():
        errors.append("thermal review telemetry file is missing")
    elif review.get("telemetry_sha256") != sha256(telemetry):
        errors.append("thermal review telemetry SHA-256 mismatch")
    return not any(message.startswith("thermal review") for message in errors)


def count_csv_samples(path: Path) -> int:
    with path.open(newline="", encoding="utf-8") as stream:
        return sum(1 for _ in csv.DictReader(stream))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("--virtualized", action="store_true", help="force non-authoritative evaluation")
    parser.add_argument("--thermal-review-file", type=Path)
    args = parser.parse_args()

    errors: list[str] = []
    state = parse_state(args.root / "protocol_state.txt")
    state_virtualized = state.get("virtualized") != "0"
    virtualized = args.virtualized or state_virtualized
    if not (args.root / "protocol_state.txt").is_file():
        errors.append("protocol_state.txt is missing")
    if not (args.root / "source_sha256.txt").is_file():
        errors.append("source_sha256.txt is missing")
    for required in ("lscpu.txt", "uname.txt", "cxx-version.txt", "system-before.txt", "system-after.txt"):
        if not (args.root / required).is_file():
            errors.append(f"required environment evidence is missing: {required}")

    thermal_review_valid = validate_thermal_review(args.root, args.thermal_review_file, errors)
    summaries = sorted(args.root.glob("run-*/summary.json"))
    if len(summaries) != 10:
        errors.append(f"expected 10 run summaries, found {len(summaries)}")

    records: list[tuple[Path, dict]] = []
    for path in summaries:
        try:
            records.append((path, json.loads(path.read_text(encoding="utf-8"))))
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"cannot read {path}: {exc}")

    compiler_ids = {record.get("compiler_id") for _, record in records}
    compiler_versions = {record.get("compiler_version") for _, record in records}
    architectures = {record.get("architecture") for _, record in records}
    physical_hashes = {record.get("physical_hash") for _, record in records}
    pair_hashes = {record.get("pair_hash") for _, record in records}
    requested_cpus = {record.get("requested_cpu") for _, record in records}
    measured_counts = {record.get("measured_samples") for _, record in records}
    if len(compiler_ids) != 1 or None in compiler_ids:
        errors.append("compiler ID is missing or differs across runs")
    if len(compiler_versions) != 1 or None in compiler_versions:
        errors.append("compiler version is missing or differs across runs")
    if architectures != {"x86_64"}:
        errors.append(f"architecture must be x86_64 for this profile, found {sorted(str(x) for x in architectures)}")
    if physical_hashes != {EXPECTED_PHYSICAL_HASH}:
        errors.append(f"physical hash mismatch: expected {EXPECTED_PHYSICAL_HASH}, found {sorted(str(x) for x in physical_hashes)}")
    if pair_hashes != {EXPECTED_PAIR_HASH}:
        errors.append(f"pair hash mismatch: expected {EXPECTED_PAIR_HASH}, found {sorted(str(x) for x in pair_hashes)}")
    if len(requested_cpus) != 1 or None in requested_cpus:
        errors.append("requested CPU is missing or differs across runs")
    if any(not isinstance(value, int) or value < 200 for value in measured_counts):
        errors.append(f"each run must contain at least 200 measured samples, found {sorted(str(x) for x in measured_counts)}")

    run_checks = []
    for path, record in records:
        run_errors: list[str] = []
        p95 = float(record.get("p95_ms", float("inf")))
        p99 = float(record.get("p99_ms", float("inf")))
        maximum = float(record.get("maximum_ms", float("inf")))
        calibrated = bool(record.get("allocation_probe_calibrated", False))
        c_supported = bool(record.get("c_allocation_probe_supported", False))
        cpp_alloc = int(record.get("maximum_cpp_allocations", -1))
        c_alloc = int(record.get("maximum_c_allocations", -1))
        migration = bool(record.get("cpu_migration_detected", True))
        affinity_requested = bool(record.get("affinity_requested", False))
        affinity_applied = bool(record.get("affinity_applied", False))
        semantic = bool(record.get("semantic_gate_passed", False))
        measured = int(record.get("measured_samples", -1))
        csv_path = path.parent / "rollback_samples.csv"
        for evidence in (csv_path, path.parent / "stdout.txt", path.parent / "system-before.txt", path.parent / "system-after.txt"):
            if not evidence.is_file():
                run_errors.append(f"missing {evidence.name}")
        if csv_path.is_file():
            try:
                csv_count = count_csv_samples(csv_path)
                if csv_count != measured:
                    run_errors.append(f"CSV sample count {csv_count} differs from summary {measured}")
            except (OSError, csv.Error) as exc:
                run_errors.append(f"cannot validate rollback_samples.csv: {exc}")
        passed = (
            p95 <= 2.0 and p99 <= 2.0 and maximum <= 2.0
            and semantic and calibrated and c_supported
            and cpp_alloc == 0 and c_alloc == 0 and not migration
            and affinity_requested and affinity_applied
            and measured >= 200 and not run_errors
        )
        run_checks.append({
            "run": path.parent.name,
            "p95_ms": p95,
            "p99_ms": p99,
            "maximum_ms": maximum,
            "semantic_gate": semantic,
            "allocation_probe_calibrated": calibrated,
            "c_probe_supported": c_supported,
            "cpp_allocations": cpp_alloc,
            "c_allocations": c_alloc,
            "cpu_migration": migration,
            "affinity_requested": affinity_requested,
            "affinity_applied": affinity_applied,
            "measured_samples": measured,
            "errors": run_errors,
            "passed": passed,
        })

    data_complete = len(records) == 10 and not [e for e in errors if not e.startswith("thermal review")]
    timing_and_semantic = data_complete and all(item["passed"] for item in run_checks)
    authoritative = not virtualized and thermal_review_valid
    gate_passed = authoritative and timing_and_semantic and not errors

    result = {
        "schema": "neoeng.v0.28.y1-g3-profile-evaluation.v2",
        "version": "0.28.0-development",
        "runs_found": len(records),
        "virtualized": virtualized,
        "thermal_review_valid": thermal_review_valid,
        "authoritative_environment": authoritative,
        "timing_and_semantic_checks_passed": timing_and_semantic,
        "gate_passed": gate_passed,
        "compiler_ids": sorted(str(value) for value in compiler_ids),
        "compiler_versions": sorted(str(value) for value in compiler_versions),
        "architectures": sorted(str(value) for value in architectures),
        "requested_cpus": sorted(str(value) for value in requested_cpus),
        "physical_hashes": sorted(str(value) for value in physical_hashes),
        "pair_hashes": sorted(str(value) for value in pair_hashes),
        "expected_physical_hash": EXPECTED_PHYSICAL_HASH,
        "expected_pair_hash": EXPECTED_PAIR_HASH,
        "source_manifest_sha256": sha256(args.root / "source_sha256.txt") if (args.root / "source_sha256.txt").is_file() else None,
        "errors": errors,
        "runs": run_checks,
    }
    args.root.mkdir(parents=True, exist_ok=True)
    (args.root / "gate_evaluation.json").write_text(
        json.dumps(result, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    print(json.dumps(result, indent=2, ensure_ascii=False))
    if errors:
        return 2
    return 0 if gate_passed else 1


if __name__ == "__main__":
    sys.exit(main())
