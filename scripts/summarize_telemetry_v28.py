#!/usr/bin/env python3
"""Summarize Y1-G3 telemetry without making an automatic throttling claim."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("telemetry", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    output = args.output or args.telemetry.with_name("telemetry_summary.json")
    rows: list[dict] = []
    errors: list[str] = []
    try:
        for line_number, line in enumerate(args.telemetry.read_text(encoding="utf-8").splitlines(), 1):
            if not line.strip():
                continue
            try:
                rows.append(json.loads(line))
            except json.JSONDecodeError as exc:
                errors.append(f"line {line_number}: {exc}")
    except OSError as exc:
        errors.append(str(exc))

    frequencies = [
        int(row["scaling_cur_freq_khz"])
        for row in rows if row.get("scaling_cur_freq_khz") is not None
    ]
    temperatures: dict[str, list[int]] = {}
    for row in rows:
        for name, value in (row.get("temperatures_millic") or {}).items():
            temperatures.setdefault(str(name), []).append(int(value))
    summary = {
        "schema": "neoeng.v0.28.telemetry-summary.v1",
        "telemetry_file": args.telemetry.name,
        "telemetry_sha256": hashlib.sha256(args.telemetry.read_bytes()).hexdigest() if args.telemetry.exists() else None,
        "samples": len(rows),
        "duration_seconds": (
            (int(rows[-1]["monotonic_ns"]) - int(rows[0]["monotonic_ns"])) / 1_000_000_000.0
            if len(rows) >= 2 else 0.0
        ),
        "cpu_values": sorted({row.get("cpu") for row in rows}),
        "frequency_khz": {
            "samples": len(frequencies),
            "minimum": min(frequencies) if frequencies else None,
            "maximum": max(frequencies) if frequencies else None,
        },
        "temperatures_millic": {
            name: {"samples": len(values), "minimum": min(values), "maximum": max(values)}
            for name, values in sorted(temperatures.items())
        },
        "automatic_throttling_verdict": "not_provided",
        "errors": errors,
    }
    output.write_text(json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    return 0 if rows and not errors else 2


if __name__ == "__main__":
    raise SystemExit(main())
