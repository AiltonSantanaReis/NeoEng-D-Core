#!/usr/bin/env python3
"""Independently verify the complete Y1-O2 ECS evidence scope.

The verifier does not trust benchmark summary decisions. It recomputes sample
counts, percentiles, semantic mappings and stream hashes from raw CSV files.
Evidence completeness is separate from the P1 timing and zero-allocation gates.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any

SCHEMA = "neoeng.dcore.ecs-scope-evidence-verification.v1"
BENCHMARK_SCHEMA = "neoeng.dcore.ecs-maintenance-benchmark.v2"
SCOPE_SCHEMA = "neoeng.dcore.ecs-scope-evidence.v1"
PROJECT_VERSION = "1.12.0"
WORKLOAD = "Y1-O2-SPARSE-COMPONENT-MAINTENANCE-V1"
REQUIRED_STREAMS = {
    "ecs_maintenance_samples.csv",
    "index_maintenance_samples.csv",
    "general_allocation_samples.csv",
    "arena_samples.csv",
    "copy_on_write_samples.csv",
    "summary.json",
}


class VerificationError(RuntimeError):
    pass


def canonical_json(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise VerificationError(f"JSON root must be an object: {path.name}")
    return value


def read_csv(path: Path, expected_fields: list[str]) -> list[dict[str, int]]:
    with path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != expected_fields:
            raise VerificationError(f"unexpected CSV header in {path.name}: {reader.fieldnames}")
        rows: list[dict[str, int]] = []
        for line_number, raw in enumerate(reader, start=2):
            converted: dict[str, int] = {}
            for key in expected_fields:
                value = raw.get(key)
                if value is None or not value or not value.isdigit():
                    raise VerificationError(f"invalid unsigned integer at {path.name}:{line_number}:{key}")
                converted[key] = int(value)
            rows.append(converted)
    return rows


def percentile(sorted_values: list[int], numerator: int, denominator: int) -> int:
    if not sorted_values:
        raise VerificationError("percentile input is empty")
    rank = (numerator * len(sorted_values) + denominator - 1) // denominator
    return sorted_values[max(1, rank) - 1]


def require_bool(mapping: dict[str, Any], key: str) -> bool:
    value = mapping.get(key)
    if not isinstance(value, bool):
        raise VerificationError(f"summary field must be boolean: {key}")
    return value


def require_positive_int(mapping: dict[str, Any], key: str) -> int:
    value = mapping.get(key)
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise VerificationError(f"summary field must be a positive integer: {key}")
    return value


def validate_indices(rows_by_name: dict[str, list[dict[str, int]]], count: int) -> None:
    expected = list(range(count))
    for name, rows in rows_by_name.items():
        observed = [row["sample"] for row in rows]
        if observed != expected:
            raise VerificationError(f"sample sequence mismatch in {name}")


def verify_directory(root: Path, verify_saved_report: bool = True) -> dict[str, Any]:
    root = root.resolve()
    if not root.is_dir():
        raise VerificationError(f"ECS evidence directory not found: {root}")
    missing = sorted(name for name in REQUIRED_STREAMS if not (root / name).is_file())
    if missing:
        raise VerificationError(f"required ECS evidence files are missing: {missing}")

    summary = load_json(root / "summary.json")
    if summary.get("schema") != BENCHMARK_SCHEMA:
        raise VerificationError("ECS benchmark schema mismatch")
    if summary.get("ecs_scope_schema") != SCOPE_SCHEMA:
        raise VerificationError("ECS scope schema mismatch")
    if summary.get("project_version") != PROJECT_VERSION:
        raise VerificationError("ECS evidence project version mismatch")
    if summary.get("workload_id") != WORKLOAD:
        raise VerificationError("ECS workload identity mismatch")

    body_count = require_positive_int(summary, "body_count")
    active_count = require_positive_int(summary, "active_body_count")
    measured = require_positive_int(summary, "measured_samples")
    if active_count > body_count:
        raise VerificationError("active body count exceeds body count")
    if summary.get("page_size") != 64:
        raise VerificationError("ECS page size must be 64")
    if summary.get("scope_streams") != [
        "general_allocation", "arena", "copy_on_write", "index_maintenance"
    ]:
        raise VerificationError("ECS scope stream declaration mismatch")

    legacy = read_csv(root / "ecs_maintenance_samples.csv", [
        "sample", "duration_ns", "component_pages_allocated", "directories_allocated",
        "component_values_copied", "directory_entries_copied", "candidate_bodies_scanned",
        "changed_bodies",
    ])
    index_rows = read_csv(root / "index_maintenance_samples.csv", [
        "sample", "duration_ns", "candidate_bodies_scanned", "inactive_bodies_skipped",
        "changed_bodies",
    ])
    allocation_rows = read_csv(root / "general_allocation_samples.csv", [
        "sample", "probe_calibrated", "cpp_heap_allocations", "cpp_heap_bytes",
    ])
    arena_rows = read_csv(root / "arena_samples.csv", [
        "sample", "allocations", "bytes_requested", "bytes_committed", "overflow_blocks",
        "bytes_per_epoch", "retained_epochs",
    ])
    cow_rows = read_csv(root / "copy_on_write_samples.csv", [
        "sample", "component_pages_allocated", "directories_allocated", "component_values_copied",
        "directory_entries_copied", "candidate_bodies_scanned", "changed_bodies",
        "body_reconstructions",
    ])
    rows_by_name = {
        "legacy": legacy,
        "index": index_rows,
        "allocation": allocation_rows,
        "arena": arena_rows,
        "copy_on_write": cow_rows,
    }
    if any(len(rows) != measured for rows in rows_by_name.values()):
        raise VerificationError("ECS evidence sample counts differ from summary")
    validate_indices(rows_by_name, measured)

    durations = sorted(row["duration_ns"] for row in index_rows)
    calculated = {
        "p50_ns": percentile(durations, 50, 100),
        "p95_ns": percentile(durations, 95, 100),
        "p99_ns": percentile(durations, 99, 100),
        "maximum_ns": durations[-1],
    }
    for key, value in calculated.items():
        if summary.get(key) != value:
            raise VerificationError(f"summary {key} differs from raw index samples")

    for index, (legacy_row, index_row, cow_row) in enumerate(zip(legacy, index_rows, cow_rows)):
        if legacy_row["duration_ns"] != index_row["duration_ns"]:
            raise VerificationError(f"legacy/index duration mismatch at sample {index}")
        for key in (
            "component_pages_allocated", "directories_allocated", "component_values_copied",
            "directory_entries_copied", "candidate_bodies_scanned", "changed_bodies",
        ):
            if legacy_row[key] != cow_row[key]:
                raise VerificationError(f"legacy/COW mapping mismatch at sample {index}:{key}")
        if index_row["candidate_bodies_scanned"] != cow_row["candidate_bodies_scanned"] \
                or index_row["changed_bodies"] != cow_row["changed_bodies"]:
            raise VerificationError(f"index/COW semantic mismatch at sample {index}")
        if index_row["candidate_bodies_scanned"] != active_count \
                or index_row["changed_bodies"] != active_count \
                or index_row["inactive_bodies_skipped"] != body_count - active_count:
            raise VerificationError(f"index scope mismatch at sample {index}")
        if cow_row["component_pages_allocated"] <= 0 \
                or cow_row["directories_allocated"] <= 0 \
                or cow_row["component_values_copied"] < cow_row["component_pages_allocated"] \
                or cow_row["directory_entries_copied"] < cow_row["directories_allocated"] \
                or cow_row["body_reconstructions"] != 0:
            raise VerificationError(f"invalid COW evidence at sample {index}")

    if any(row["probe_calibrated"] != 1 for row in allocation_rows):
        raise VerificationError("general-allocation probe is not calibrated for every sample")
    general_events = sum(row["cpp_heap_allocations"] for row in allocation_rows)
    general_bytes = sum(row["cpp_heap_bytes"] for row in allocation_rows)
    general_zero = all(
        row["cpp_heap_allocations"] == 0 and row["cpp_heap_bytes"] == 0
        for row in allocation_rows
    )
    if summary.get("general_allocation_events") != general_events \
            or summary.get("general_allocation_bytes") != general_bytes \
            or require_bool(summary, "general_allocation_zero") != general_zero \
            or require_bool(summary, "allocation_probe_calibrated") is not True:
        raise VerificationError("general-allocation summary differs from raw samples")

    for index, row in enumerate(arena_rows):
        if row["allocations"] != 2 or row["bytes_requested"] <= 0 \
                or row["bytes_committed"] != 0 or row["overflow_blocks"] != 0 \
                or row["bytes_per_epoch"] < row["bytes_requested"] \
                or row["retained_epochs"] != 16:
            raise VerificationError(f"invalid arena evidence at sample {index}")
    arena_zero = all(row["overflow_blocks"] == 0 for row in arena_rows)
    if require_bool(summary, "arena_overflow_zero") != arena_zero:
        raise VerificationError("arena summary differs from raw samples")

    cow_valid = True
    if require_bool(summary, "cow_semantics_observed") is not cow_valid:
        raise VerificationError("COW semantic summary mismatch")
    final_hash = summary.get("final_hash")
    if not isinstance(final_hash, str) or not final_hash.startswith("0x") or len(final_hash) != 18:
        raise VerificationError("final_hash format is invalid")

    stream_hashes = {name: sha256_file(root / name) for name in sorted(REQUIRED_STREAMS)}
    report = {
        "schema": SCHEMA,
        "project_version": PROJECT_VERSION,
        "workload_id": WORKLOAD,
        "status": "passed",
        "scope_complete": True,
        "sample_count": measured,
        "general_allocation_zero": general_zero,
        "general_allocation_events": general_events,
        "general_allocation_bytes": general_bytes,
        "arena_overflow_zero": arena_zero,
        "copy_on_write_semantics_valid": cow_valid,
        "index_maintenance_semantics_valid": True,
        "p50_ns": calculated["p50_ns"],
        "p95_ns": calculated["p95_ns"],
        "p99_ns": calculated["p99_ns"],
        "maximum_ns": calculated["maximum_ns"],
        "final_hash": final_hash,
        "stream_sha256": stream_hashes,
        "qualification_policy": "Evidence completeness does not imply native P1 timing or zero-allocation qualification.",
    }
    saved = root / "ecs_scope_verification.json"
    if verify_saved_report and saved.is_file():
        if load_json(saved) != report:
            raise VerificationError("saved ECS scope verification differs from independent recomputation")
    return report


def create_self_test_fixture(root: Path) -> None:
    root.mkdir(parents=True, exist_ok=True)
    (root / "ecs_maintenance_samples.csv").write_text(
        "sample,duration_ns,component_pages_allocated,directories_allocated,component_values_copied,directory_entries_copied,candidate_bodies_scanned,changed_bodies\n"
        "0,10,4,4,256,64,2,2\n", encoding="utf-8")
    (root / "index_maintenance_samples.csv").write_text(
        "sample,duration_ns,candidate_bodies_scanned,inactive_bodies_skipped,changed_bodies\n0,10,2,2,2\n",
        encoding="utf-8")
    (root / "general_allocation_samples.csv").write_text(
        "sample,probe_calibrated,cpp_heap_allocations,cpp_heap_bytes\n0,1,1,32\n", encoding="utf-8")
    (root / "arena_samples.csv").write_text(
        "sample,allocations,bytes_requested,bytes_committed,overflow_blocks,bytes_per_epoch,retained_epochs\n0,2,128,0,0,65536,16\n",
        encoding="utf-8")
    (root / "copy_on_write_samples.csv").write_text(
        "sample,component_pages_allocated,directories_allocated,component_values_copied,directory_entries_copied,candidate_bodies_scanned,changed_bodies,body_reconstructions\n"
        "0,4,4,256,64,2,2,0\n", encoding="utf-8")
    summary = {
        "schema": BENCHMARK_SCHEMA,
        "ecs_scope_schema": SCOPE_SCHEMA,
        "project_version": PROJECT_VERSION,
        "workload_id": WORKLOAD,
        "body_count": 4,
        "active_body_count": 2,
        "page_size": 64,
        "warmup_samples": 1,
        "measured_samples": 1,
        "p50_ns": 10,
        "p95_ns": 10,
        "p99_ns": 10,
        "maximum_ns": 10,
        "final_hash": "0x0000000000000001",
        "allocation_probe_calibrated": True,
        "general_allocation_zero": False,
        "general_allocation_events": 1,
        "general_allocation_bytes": 32,
        "arena_overflow_zero": True,
        "cow_semantics_observed": True,
        "scope_streams": ["general_allocation", "arena", "copy_on_write", "index_maintenance"],
        "qualification_note": "self-test",
    }
    (root / "summary.json").write_text(canonical_json(summary), encoding="utf-8")


def expect_rejected(root: Path, label: str) -> None:
    try:
        verify_directory(root, verify_saved_report=False)
    except VerificationError:
        return
    raise VerificationError(f"self-test accepted tampered {label} evidence")


def run_self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="neoeng-ecs-scope-") as directory:
        baseline = Path(directory) / "baseline"
        create_self_test_fixture(baseline)
        report = verify_directory(baseline, verify_saved_report=False)
        (baseline / "ecs_scope_verification.json").write_text(
            canonical_json(report), encoding="utf-8")
        verify_directory(baseline)

        mutations = {
            "allocation calibration": (
                "general_allocation_samples.csv", "0,1,1,32", "0,0,1,32"),
            "arena overflow": (
                "arena_samples.csv", "0,2,128,0,0,65536,16", "0,2,128,0,1,65536,16"),
            "copy-on-write reconstruction": (
                "copy_on_write_samples.csv", "0,4,4,256,64,2,2,0", "0,4,4,256,64,2,2,1"),
            "index inactive count": (
                "index_maintenance_samples.csv", "0,10,2,2,2", "0,10,2,1,2"),
            "legacy mapping": (
                "ecs_maintenance_samples.csv", "0,10,4,4,256,64,2,2", "0,10,5,4,256,64,2,2"),
            "summary decision": (
                "summary.json", '"general_allocation_zero": false', '"general_allocation_zero": true'),
        }
        for number, (label, mutation) in enumerate(mutations.items()):
            target = Path(directory) / f"tamper-{number}"
            shutil.copytree(baseline, target)
            path = target / mutation[0]
            original = path.read_text(encoding="utf-8")
            changed = original.replace(mutation[1], mutation[2], 1)
            if changed == original:
                raise VerificationError(f"self-test mutation did not apply: {label}")
            path.write_text(changed, encoding="utf-8")
            expect_rejected(target, label)
    print("ecs_scope_evidence_self_test=passed tamper_classes=6")

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence_directory", nargs="?", type=Path)
    parser.add_argument("--write-report", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        run_self_test()
        return 0
    if args.evidence_directory is None:
        parser.error("evidence_directory is required unless --self-test is used")
    report = verify_directory(args.evidence_directory, verify_saved_report=not args.write_report)
    if args.write_report:
        (args.evidence_directory.resolve() / "ecs_scope_verification.json").write_text(
            canonical_json(report), encoding="utf-8", newline="\n")
    print("ecs_scope_evidence_verification=passed")
    print(f"scope_complete={1 if report['scope_complete'] else 0}")
    print(f"general_allocation_zero={1 if report['general_allocation_zero'] else 0}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (VerificationError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"ecs_scope_evidence_verification=failed: {error}", file=sys.stderr)
        raise SystemExit(1)
