#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
from pathlib import Path

SNAPSHOT = Path(__file__).with_name("verify_ev00_dlab_evidence_r15.py")
if not SNAPSHOT.is_file():
    raise SystemExit(f"preserved R15 verifier missing: {SNAPSHOT}")

spec = importlib.util.spec_from_file_location("neoeng_ev00_r15_verifier", SNAPSHOT)
if spec is None or spec.loader is None:
    raise SystemExit("unable to load preserved R15 verifier")
r15 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r15)


def research_contract_self_test_fixed() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        evidence = Path(tmp) / "evidence"
        commands = evidence / "raw" / "commands"
        logs = evidence / "raw" / "logs"
        commands.mkdir(parents=True)
        logs.mkdir(parents=True)
        research = r"C:\tmp\run\research-build"
        rows = {
            "research-cmake-configure": ["-S", r"C:\tmp\run\source", "-B", research, "-G", "Ninja", "-DNEOENG_DCORE_BUILD_FULL_TOOLSET=OFF", "-DNEOENG_DCORE_BUILD_RESEARCH_TOOLS=ON"],
            "research-cmake-build": ["--build", research, "--target", "neoeng_dcore_preclosure", "neoeng_temporal_closure_tests", "--parallel", "2"],
            "ctest-replay-rollback": ["--test-dir", research, "-C", "Release", "--output-on-failure", "-R", "^(neoeng_dcore_replay_smoke|neoeng_dcore_history_smoke|neoeng_temporal_closure_tests)$"],
        }
        for name, args in rows.items():
            stdout = f"raw/logs/{name}.stdout.txt"
            (commands / f"{name}.json").write_text(json.dumps({
                "schema": "neoeng.dlab.command-record.v1",
                "name": name,
                "executable": "cmake" if "cmake" in name else "ctest",
                "arguments": args,
                "working_directory": r"C:\tmp\run",
                "started_at_utc": "2026-08-22T00:00:00Z",
                "finished_at_utc": "2026-08-22T00:00:01Z",
                "exit_code": 0,
                "classification": "PASS",
                "stdout": stdout,
                "stderr": f"raw/logs/{name}.stderr.txt",
            }), encoding="utf-8")
            (logs / f"{name}.stderr.txt").write_text("", encoding="utf-8")
            (logs / f"{name}.stdout.txt").write_text("", encoding="utf-8")
        (logs / "ctest-replay-rollback.stdout.txt").write_text(
            "Start 1: neoeng_dcore_replay_smoke\nStart 2: neoeng_dcore_history_smoke\nStart 3: neoeng_temporal_closure_tests\n100% tests passed, 0 tests failed out of 3\n",
            encoding="utf-8",
        )
        (evidence / "replay-rollback-validation.json").write_text(json.dumps({
            "schema": "neoeng.dlab.ev00-replay-rollback-validation.v1",
            "build_configuration": "research_enabled_isolated",
            "research_tools": True,
            "full_toolset": False,
            "expected_tests": sorted(r15.EXPECTED_RESEARCH_TESTS),
            "observed_tests": sorted(r15.EXPECTED_RESEARCH_TESTS),
            "total_tests": 3,
            "failed_tests": 0,
            "exact_inventory": True,
        }), encoding="utf-8")
        r15.check_replay_rollback(evidence, Path(tmp))

        bad = json.loads((commands / "research-cmake-configure.json").read_text(encoding="utf-8"))
        bad["arguments"] = [x.replace("RESEARCH_TOOLS=ON", "RESEARCH_TOOLS=OFF") for x in bad["arguments"]]
        (commands / "research-cmake-configure.json").write_text(json.dumps(bad), encoding="utf-8")
        try:
            r15.check_replay_rollback(evidence, Path(tmp))
        except r15.module.EvidenceError:
            pass
        else:
            raise SystemExit("R16 research contract self-test failed: research-OFF evidence accepted")


if __name__ == "__main__":
    if "--self-test" in sys.argv:
        r15.parser_self_test()
        research_contract_self_test_fixed()
    raise SystemExit(r15.module.main())
