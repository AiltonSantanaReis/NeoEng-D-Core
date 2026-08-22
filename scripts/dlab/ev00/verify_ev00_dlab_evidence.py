#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

NORMATIVE_CTEST = Path("docs/changesets/015/evidence/github-actions-run-30375982639/raw/windows-clang-cl-ctest.txt")
SNAPSHOT = Path(__file__).with_name("verify_ev00_dlab_evidence_r11.py")

if not SNAPSHOT.is_file():
    raise SystemExit(f"preserved R11 verifier missing: {SNAPSHOT}")

spec = importlib.util.spec_from_file_location("neoeng_ev00_r11_verifier", SNAPSHOT)
if spec is None or spec.loader is None:
    raise SystemExit("unable to load preserved R11 verifier")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
module.HISTORICAL_CTEST = NORMATIVE_CTEST

if __name__ == "__main__":
    raise SystemExit(module.main())
