#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path

TIMING_FIELDS = {
    f"{mode}_{stat}_us"
    for mode in ("nominal", "classifier", "full", "selective_kernel", "selective_total")
    for stat in ("p50", "p95", "p99", "max")
}
TIMING_FIELDS.add("selective_total_over_full_p95")


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def key(row: dict[str, str]) -> tuple[str, str]:
    return row["profile"], row["maximum_terms"]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("gcc_csv", type=Path)
    parser.add_argument("clang_csv", type=Path)
    parser.add_argument("gcc_fuzz_csv", type=Path)
    parser.add_argument("clang_fuzz_csv", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    gcc_rows = read_csv(args.gcc_csv)
    clang_rows = read_csv(args.clang_csv)
    if len(gcc_rows) != 15 or len(clang_rows) != 15:
        raise SystemExit("expected exactly 15 benchmark rows per compiler")
    clang_by_key = {key(row): row for row in clang_rows}
    if len(clang_by_key) != len(clang_rows):
        raise SystemExit("duplicate Clang benchmark key")

    mismatches: list[dict[str, str]] = []
    comparison: list[dict[str, str | float | int]] = []
    for gcc in gcc_rows:
        clang = clang_by_key.get(key(gcc))
        if clang is None:
            raise SystemExit(f"missing Clang row {key(gcc)}")
        for field, gcc_value in gcc.items():
            if field in TIMING_FIELDS:
                continue
            if clang[field] != gcc_value:
                mismatches.append({
                    "profile": gcc["profile"],
                    "maximum_terms": gcc["maximum_terms"],
                    "field": field,
                    "gcc": gcc_value,
                    "clang": clang[field],
                })
        comparison.append({
            "profile": gcc["profile"],
            "maximum_terms": int(gcc["maximum_terms"]),
            "contacts": int(gcc["contacts"]),
            "oracle_vulnerable": int(gcc["oracle_vulnerable"]),
            "selected": int(gcc["selected"]),
            "false_positives": int(gcc["false_positives"]),
            "false_negatives": int(gcc["false_negatives"]),
            "selection_percent": float(gcc["selection_percent"]),
            "precision_percent": float(gcc["precision_percent"]),
            "gcc_full_p95_us": float(gcc["full_p95_us"]),
            "gcc_selective_total_p95_us": float(gcc["selective_total_p95_us"]),
            "gcc_p95_ratio": float(gcc["selective_total_over_full_p95"]),
            "clang_full_p95_us": float(clang["full_p95_us"]),
            "clang_selective_total_p95_us": float(clang["selective_total_p95_us"]),
            "clang_p95_ratio": float(clang["selective_total_over_full_p95"]),
        })

    with (args.output / "cross_compiler_mismatches.json").open("w", encoding="utf-8") as handle:
        json.dump(mismatches, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    if mismatches:
        raise SystemExit(f"non-timing cross-compiler mismatches: {len(mismatches)}")

    gcc_fuzz_hash = sha256(args.gcc_fuzz_csv)
    clang_fuzz_hash = sha256(args.clang_fuzz_csv)
    if gcc_fuzz_hash != clang_fuzz_hash:
        raise SystemExit("GCC and Clang selective fuzz CSV files differ")

    fields = list(comparison[0].keys())
    with (args.output / "selective_raa_cross_compiler.csv").open(
        "w", newline="", encoding="utf-8"
    ) as handle:
        writer = csv.DictWriter(handle, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(comparison)

    total_contacts = sum(int(row["contacts"]) for row in gcc_rows)
    total_oracle = sum(int(row["oracle_vulnerable"]) for row in gcc_rows)
    total_selected = sum(int(row["selected"]) for row in gcc_rows)
    total_fp = sum(int(row["false_positives"]) for row in gcc_rows)
    total_fn = sum(int(row["false_negatives"]) for row in gcc_rows)
    max_gcc_ratio = max(float(row["gcc_p95_ratio"]) for row in comparison)
    max_clang_ratio = max(float(row["clang_p95_ratio"]) for row in comparison)
    summary = {
        "rows": len(comparison),
        "non_timing_mismatches": 0,
        "fuzz_csv_sha256": gcc_fuzz_hash,
        "fuzz_csv_byte_identical": True,
        "benchmark_contact_evaluations": total_contacts,
        "oracle_vulnerable": total_oracle,
        "selected": total_selected,
        "false_positives": total_fp,
        "false_negatives": total_fn,
        "recall": 1.0 if total_oracle == 0 else (total_oracle - total_fn) / total_oracle,
        "precision": 1.0 if total_selected == 0 else (total_selected - total_fp) / total_selected,
        "maximum_selective_over_full_p95_ratio_gcc": max_gcc_ratio,
        "maximum_selective_over_full_p95_ratio_clang": max_clang_ratio,
        "authoritative_timing": False,
        "environment_scope": "virtualized_comparison_only",
    }
    with (args.output / "summary.json").open("w", encoding="utf-8") as handle:
        json.dump(summary, handle, indent=2, ensure_ascii=False)
        handle.write("\n")
    print(json.dumps(summary, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
