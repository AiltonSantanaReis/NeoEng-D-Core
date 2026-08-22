#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
from pathlib import Path

SNAPSHOT = Path(__file__).with_name("verify_ev00_dlab_evidence_r19.py")
if not SNAPSHOT.is_file():
    raise SystemExit(f"preserved R19 verifier missing: {SNAPSHOT}")

spec = importlib.util.spec_from_file_location("neoeng_ev00_r19_verifier", SNAPSHOT)
if spec is None or spec.loader is None:
    raise SystemExit("unable to load preserved R19 verifier")
r19 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r19)

RESEARCH_SURFACE_EXPECTED = {
    "neoeng_dcore_replay_smoke",
    "neoeng_dcore_history_smoke",
    "neoeng_temporal_closure_tests",
}
RESEARCH_ONLY_FORBIDDEN_IN_SUPPORTED = {
    "neoeng_dcore_replay_smoke",
    "neoeng_dcore_history_smoke",
}
SUPPORTED_TEMPORAL_TEST = "neoeng_temporal_closure_tests"


def check_ctest_fixed(evidence: Path, repo_root: Path) -> None:
    module = r19.r15.module
    records = module.command_records(evidence)
    configure = module.require_command(records, "cmake-configure")
    configure_args = r19.r15._arguments(configure)
    if "-DNEOENG_DCORE_BUILD_RESEARCH_TOOLS=OFF" not in configure_args:
        raise module.EvidenceError("supported-surface configure is not research-OFF")
    if "-DNEOENG_DCORE_BUILD_FULL_TOOLSET=OFF" not in configure_args:
        raise module.EvidenceError("supported-surface configure is not full-toolset-OFF")

    row = module.require_command(records, "ctest-dcore")
    ctest_args = r19.r15._arguments(row)
    if r19.r15._arg_after(ctest_args, "-L") != "dcore":
        raise module.EvidenceError("supported-surface CTest did not select label dcore")
    text = module.resolve_logged_file(evidence, row["stdout"]).read_text(encoding="utf-8", errors="replace")
    total, failed, names = r19.r15.parse_ctest_compat(text)

    historical_path = repo_root / r19.r15.NORMATIVE_CTEST
    if not historical_path.is_file():
        raise module.EvidenceError(f"normative CS015 CTest reference missing: {r19.r15.NORMATIVE_CTEST}")
    hist_total, hist_failed, hist_names = r19.r15.parse_ctest_compat(
        historical_path.read_text(encoding="utf-8", errors="replace")
    )
    if None in (total, failed, hist_total, hist_failed):
        raise module.EvidenceError("cannot parse supported/normative CTest summaries")
    if total != 54 or hist_total != 54 or failed != 0 or hist_failed != 0:
        raise module.EvidenceError(
            f"supported/normative CTest totals are not exact 54/0: local={total}/{failed} historical={hist_total}/{hist_failed}"
        )
    if SUPPORTED_TEMPORAL_TEST not in hist_names:
        raise module.EvidenceError("normative CS015 supported inventory is missing neoeng_temporal_closure_tests")
    if RESEARCH_ONLY_FORBIDDEN_IN_SUPPORTED & hist_names:
        raise module.EvidenceError("normative CS015 unexpectedly contains research-only replay/history tests")
    leaked = RESEARCH_ONLY_FORBIDDEN_IN_SUPPORTED & names
    if leaked:
        raise module.EvidenceError(f"research-only replay/history tests leaked into supported surface: {sorted(leaked)}")
    if names != hist_names:
        raise module.EvidenceError("supported-surface CTest inventory differs from normative CS015 54-test inventory")


def supported_surface_contract_self_test() -> None:
    module = r19.r15.module
    repo_root = Path(__file__).resolve().parents[3]
    normative_path = repo_root / r19.r15.NORMATIVE_CTEST
    normative = normative_path.read_text(encoding="utf-8", errors="replace")
    total, failed, names = r19.r15.parse_ctest_compat(normative)
    if total != 54 or failed != 0 or len(names) != 54:
        raise SystemExit("R20 supported-surface self-test failed: normative CS015 is not exact 54/0")
    if SUPPORTED_TEMPORAL_TEST not in names:
        raise SystemExit("R20 supported-surface self-test failed: temporal closure absent from normative 54")
    if RESEARCH_ONLY_FORBIDDEN_IN_SUPPORTED & names:
        raise SystemExit("R20 supported-surface self-test failed: replay/history present in normative 54")

    with tempfile.TemporaryDirectory() as tmp:
        evidence = Path(tmp) / "evidence"
        commands = evidence / "raw" / "commands"
        logs = evidence / "raw" / "logs"
        commands.mkdir(parents=True)
        logs.mkdir(parents=True)
        build = r"C:\tmp\run\build"
        common = {
            "schema": "neoeng.dlab.command-record.v1",
            "working_directory": r"C:\tmp\run",
            "started_at_utc": "2026-08-22T00:00:00Z",
            "finished_at_utc": "2026-08-22T00:00:01Z",
            "exit_code": 0,
            "classification": "PASS",
        }
        configure = dict(common)
        configure.update({
            "name": "cmake-configure",
            "executable": "cmake",
            "arguments": ["-S", r"C:\tmp\run\source", "-B", build, "-G", "Ninja", "-DNEOENG_DCORE_BUILD_FULL_TOOLSET=OFF", "-DNEOENG_DCORE_BUILD_RESEARCH_TOOLS=OFF"],
            "stdout": "raw/logs/cmake-configure.stdout.txt",
            "stderr": "raw/logs/cmake-configure.stderr.txt",
        })
        ctest = dict(common)
        ctest.update({
            "name": "ctest-dcore",
            "executable": "ctest",
            "arguments": ["--test-dir", build, "-C", "Release", "--output-on-failure", "-L", "dcore"],
            "stdout": "raw/logs/ctest-dcore.stdout.txt",
            "stderr": "raw/logs/ctest-dcore.stderr.txt",
        })
        (commands / "cmake-configure.json").write_text(json.dumps(configure), encoding="utf-8")
        (commands / "ctest-dcore.json").write_text(json.dumps(ctest), encoding="utf-8")
        for name in ["cmake-configure", "ctest-dcore"]:
            (logs / f"{name}.stderr.txt").write_text("", encoding="utf-8")
        (logs / "cmake-configure.stdout.txt").write_text("fixture configure\n", encoding="utf-8")
        (logs / "ctest-dcore.stdout.txt").write_text(normative, encoding="utf-8")

        check_ctest_fixed(evidence, repo_root)

        leaked = normative.replace(SUPPORTED_TEMPORAL_TEST, "neoeng_dcore_replay_smoke")
        (logs / "ctest-dcore.stdout.txt").write_text(leaked, encoding="utf-8")
        try:
            check_ctest_fixed(evidence, repo_root)
        except module.EvidenceError as exc:
            if "research-only replay/history tests leaked" not in str(exc):
                raise SystemExit(f"R20 supported-surface self-test failed with wrong rejection: {exc}")
        else:
            raise SystemExit("R20 supported-surface self-test failed: replay leak accepted")


# Keep Build-B semantics exactly three tests; only the Build-A forbidden set changes.
r19.r15.EXPECTED_RESEARCH_TESTS = set(RESEARCH_SURFACE_EXPECTED)
r19.r15.check_ctest = check_ctest_fixed
r19.r15.module.check_ctest = check_ctest_fixed
r19.r15.module.CHECKS["ctest"] = check_ctest_fixed

if __name__ == "__main__":
    if "--self-test" in sys.argv:
        r19.r15.parser_self_test()
        r19.research_contract_self_test_fixed()
        supported_surface_contract_self_test()
    raise SystemExit(r19.r15.module.main())
