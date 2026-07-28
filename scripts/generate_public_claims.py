#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
LEDGER = ROOT / "audit/PRODUCT_CLAIMS_LEDGER.json"
OUTPUT = ROOT / "docs/commercial/PUBLIC_CLAIMS.md"
ALLOWED_STATUSES = {
    "implemented",
    "verified",
    "native_qualified",
    "independently_audited",
    "certified",
}


def load_ledger(path: Path = LEDGER) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict) or not isinstance(value.get("claims"), list):
        raise ValueError("claims ledger root is invalid")
    return value


def is_public(row: dict[str, Any]) -> bool:
    public_use = str(row.get("public_use", ""))
    return (
        row.get("status") in ALLOWED_STATUSES
        and public_use
        and not public_use.startswith("prohibited")
        and public_use != "original_positive_claim_prohibited"
    )


def render(ledger: dict[str, Any]) -> str:
    rows = [
        row for row in ledger["claims"]
        if isinstance(row, dict) and is_public(row)
    ]
    rows.sort(key=lambda row: str(row["claim_id"]))
    lines = [
        "# NeoEng D-Core — public technical claims",
        "",
        f"Generated from the normative claims ledger for baseline "
        f"`{ledger['project_version']}`.",
        "",
        "Only the statements and scopes below are authorized. Planned, "
        "unsupported, removed and prohibited claims are intentionally absent.",
        "",
    ]
    for row in rows:
        lines.extend([
            f"## {row['claim_id']}",
            "",
            str(row["statement"]).strip(),
            "",
            f"- Status: `{row['status']}`",
            f"- Scope: {str(row['scope']).strip()}",
            f"- Public use: `{row['public_use']}`",
            "- Evidence:",
        ])
        for evidence in row.get("evidence", []):
            lines.append(f"  - `{evidence}`")
        lines.append("")
    lines.extend([
        "## Mandatory exclusions",
        "",
        "This document does not authorize claims of unrestricted production "
        "readiness, ARM64 equivalence, contractual hardware performance, "
        "certification, independent audit, ROI, included asymmetric signing "
        "or universal sector readiness.",
        "",
    ])
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    content = render(load_ledger())
    if args.check:
        if not OUTPUT.is_file() or OUTPUT.read_text(encoding="utf-8") != content:
            print("PUBLIC_CLAIMS.md absent or divergent")
            return 1
        print("public_claims_check=passed")
        return 0
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(content, encoding="utf-8", newline="\n")
    print(f"written: {OUTPUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
