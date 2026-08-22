#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import re
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


def parse_ctest_compat(text: str) -> tuple[int | None, int | None, set[str]]:
    names = set(re.findall(r"Test\s+#?\d+:\s+([A-Za-z0-9_.-]+)", text))
    if not names:
        names = set(re.findall(r"Start\s+\d+:\s+([A-Za-z0-9_.-]+)", text))
    modern = re.search(r"(\d+)% tests passed,\s+(\d+) tests failed out of (\d+)", text)
    if modern:
        return int(modern.group(3)), int(modern.group(2)), names
    historical_all_pass = re.search(r"100% tests passed out of (\d+)", text)
    if historical_all_pass:
        return int(historical_all_pass.group(1)), 0, names
    return None, None, names


def parser_self_test() -> None:
    modern = "Start 1: a\nStart 2: b\n100% tests passed, 0 tests failed out of 2\n"
    historical = "Start 1: a\nStart 2: b\n100% tests passed out of 2\n"
    ambiguous = "Start 1: a\nStart 2: b\n98% tests passed out of 2\n"
    if parse_ctest_compat(modern) != (2, 0, {"a", "b"}):
        raise SystemExit("R14 parser self-test failed: modern summary")
    if parse_ctest_compat(historical) != (2, 0, {"a", "b"}):
        raise SystemExit("R14 parser self-test failed: historical all-pass summary")
    total, failed, _ = parse_ctest_compat(ambiguous)
    if total is not None or failed is not None:
        raise SystemExit("R14 parser self-test failed: ambiguous historical summary accepted")


module.parse_ctest = parse_ctest_compat

if __name__ == "__main__":
    if "--self-test" in sys.argv:
        parser_self_test()
    raise SystemExit(module.main())
