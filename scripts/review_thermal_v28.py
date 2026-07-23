#!/usr/bin/env python3
"""Create a hash-bound manual thermal review for an authoritative Y1-G3 profile."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("telemetry", type=Path)
    parser.add_argument("--reviewer", required=True)
    parser.add_argument("--notes", required=True)
    parser.add_argument("--confirm-no-throttling", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.confirm_no_throttling:
        parser.error("manual confirmation is required; do not pass it without reviewing telemetry and system logs")
    if not args.telemetry.is_file():
        parser.error("telemetry file does not exist")
    output = args.output or args.telemetry.with_name("thermal_review.json")
    review = {
        "schema": "neoeng.v0.28.thermal-review.v1",
        "reviewed_at_utc": datetime.now(timezone.utc).isoformat(),
        "reviewer": args.reviewer.strip(),
        "notes": args.notes.strip(),
        "confirmed_no_throttling": True,
        "telemetry_file": args.telemetry.name,
        "telemetry_sha256": hashlib.sha256(args.telemetry.read_bytes()).hexdigest(),
    }
    if not review["reviewer"] or not review["notes"]:
        parser.error("reviewer and notes must be non-empty")
    output.write_text(json.dumps(review, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps(review, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
