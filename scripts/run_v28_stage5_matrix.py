#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import statistics
import subprocess
import sys
from pathlib import Path
from typing import Any

TIMING_FIELDS = (
    "classifier_p95_us",
    "full_raa_p95_us",
    "selective_kernel_p95_us",
    "selective_total_p95_us",
    "shadow_p95_us",
    "shadow_maximum_us",
)


def parse_terms(value: str) -> tuple[int, ...]:
    terms = tuple(int(item.strip()) for item in value.split(",") if item.strip())
    if not terms or any(item <= 0 for item in terms):
        raise argparse.ArgumentTypeError("terms must contain positive comma-separated integers")
    return terms


def run_one(executable: Path, output: Path, groups: int, frames: int, terms: int, seed: int) -> dict[str, Any]:
    command = [str(executable), str(output), str(groups), str(frames), str(terms), hex(seed)]
    completed = subprocess.run(command, text=True, capture_output=True, check=False)
    (output.parent / f"{output.name}.stdout.log").write_text(completed.stdout, encoding="utf-8")
    (output.parent / f"{output.name}.stderr.log").write_text(completed.stderr, encoding="utf-8")
    if completed.returncode != 0:
        raise RuntimeError(
            f"run failed rc={completed.returncode}: {' '.join(command)}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    summary_path = output / "active_island_shadow_summary.json"
    if not summary_path.is_file():
        raise RuntimeError(f"missing summary: {summary_path}")
    return json.loads(summary_path.read_text(encoding="utf-8"))


def distribution(values: list[float]) -> dict[str, float]:
    return {
        "median": statistics.median(values),
        "minimum": min(values),
        "maximum": max(values),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the v0.28 Stage 5 active-island shadow matrix.")
    parser.add_argument("--gcc-exe", type=Path, required=True)
    parser.add_argument("--clang-exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seeds", type=int, default=32)
    parser.add_argument("--groups", type=int, default=4)
    parser.add_argument("--frames", type=int, default=6)
    parser.add_argument("--terms", type=parse_terms, default=(8, 12, 16))
    parser.add_argument("--base-seed", type=lambda text: int(text, 0), default=0x2805000000001000)
    args = parser.parse_args()

    if args.seeds <= 0 or args.groups <= 0 or args.frames <= 0:
        parser.error("seeds, groups, and frames must be positive")
    for executable in (args.gcc_exe, args.clang_exe):
        if not executable.is_file():
            parser.error(f"missing executable: {executable}")

    args.output.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, Any]] = []
    differences: list[dict[str, Any]] = []
    island_csv_mismatches: list[dict[str, int]] = []
    executables = {"gcc": args.gcc_exe, "clang": args.clang_exe}

    for seed_index in range(args.seeds):
        seed = args.base_seed + seed_index
        for terms in args.terms:
            summaries: dict[str, dict[str, Any]] = {}
            outputs: dict[str, Path] = {}
            for compiler in ("gcc", "clang"):
                output = args.output / f"{compiler}_s{seed_index:02d}_t{terms}"
                summary = run_one(executables[compiler], output, args.groups, args.frames, terms, seed)
                summaries[compiler] = summary
                outputs[compiler] = output
                rows.append({
                    "compiler": compiler,
                    "seed_index": seed_index,
                    "seed": f"0x{seed:016X}",
                    "requested_maximum_terms": terms,
                    **summary,
                })

            for field, gcc_value in summaries["gcc"].items():
                if field.endswith("_us"):
                    continue
                clang_value = summaries["clang"].get(field)
                if clang_value != gcc_value:
                    differences.append({
                        "seed_index": seed_index,
                        "maximum_terms": terms,
                        "field": field,
                        "gcc": gcc_value,
                        "clang": clang_value,
                    })

            gcc_csv = outputs["gcc"] / "active_island_shadow.csv"
            clang_csv = outputs["clang"] / "active_island_shadow.csv"
            if gcc_csv.read_bytes() != clang_csv.read_bytes():
                island_csv_mismatches.append({"seed_index": seed_index, "maximum_terms": terms})

    matrix_path = args.output / "active_island_shadow_matrix.csv"
    with matrix_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)

    gcc_rows = [row for row in rows if row["compiler"] == "gcc"]
    base_rows = [row for row in gcc_rows if int(row["requested_maximum_terms"]) == min(args.terms)]
    summary = {
        "runs": len(rows),
        "paired_configurations": len(rows) // 2,
        "seeds": args.seeds,
        "groups_per_topology": args.groups,
        "frames": args.frames,
        "terms": list(args.terms),
        "non_timing_differences": len(differences),
        "island_csv_mismatches": len(island_csv_mismatches),
        "unique_contact_occurrences_base_capacity": sum(int(row["contacts_observed"]) for row in base_rows),
        "unique_island_occurrences_base_capacity": sum(int(row["islands_observed"]) for row in base_rows),
        "unique_oracle_vulnerable_base_capacity": sum(int(row["oracle_vulnerable"]) for row in base_rows),
        "unique_selected_base_capacity": sum(int(row["selected"]) for row in base_rows),
        "unique_false_positives_base_capacity": sum(int(row["false_positives"]) for row in base_rows),
        "unique_false_negatives_base_capacity": sum(int(row["false_negatives"]) for row in base_rows),
        "evaluated_contact_occurrences_all_capacities": sum(int(row["contacts_observed"]) for row in gcc_rows),
        "evaluated_island_occurrences_all_capacities": sum(int(row["islands_observed"]) for row in gcc_rows),
        "false_positives_all_capacities": sum(int(row["false_positives"]) for row in gcc_rows),
        "false_negatives_all_capacities": sum(int(row["false_negatives"]) for row in gcc_rows),
        "authoritative_state_mismatches": sum(int(row["authoritative_state_mismatches"]) for row in gcc_rows),
        "center_mismatches": sum(int(row["center_mismatches"]) for row in gcc_rows),
        "selected_state_mismatches": sum(int(row["selected_state_mismatches"]) for row in gcc_rows),
        "authoritative_timing": False,
        "environment_scope": "comparison_only_unless_run_under_the_bare_metal_protocol",
    }

    timing_summary: dict[str, Any] = {}
    for terms in args.terms:
        timing_summary[str(terms)] = {}
        for compiler in ("gcc", "clang"):
            subset = [
                row for row in rows
                if row["compiler"] == compiler and int(row["requested_maximum_terms"]) == terms
            ]
            metrics = {
                field: distribution([float(row[field]) for row in subset])
                for field in TIMING_FIELDS
            }
            full = metrics["full_raa_p95_us"]["median"]
            selective = metrics["selective_total_p95_us"]["median"]
            metrics["selective_reduction_percent_from_median_p95"] = (
                0.0 if full == 0.0 else (1.0 - selective / full) * 100.0
            )
            timing_summary[str(terms)][compiler] = metrics

    for path, data in (
        (args.output / "active_island_shadow_matrix_summary.json", summary),
        (args.output / "active_island_shadow_timing_summary.json", timing_summary),
        (args.output / "non_timing_differences.json", differences),
        (args.output / "island_csv_mismatches.json", island_csv_mismatches),
    ):
        path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print(json.dumps(summary, sort_keys=True))
    if differences or island_csv_mismatches:
        print("Stage 5 cross-compiler comparison failed.", file=sys.stderr)
        return 1
    if any(summary[key] != 0 for key in (
        "unique_false_negatives_base_capacity",
        "false_negatives_all_capacities",
        "authoritative_state_mismatches",
        "center_mismatches",
        "selected_state_mismatches",
    )):
        print("Stage 5 correctness gate failed.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
