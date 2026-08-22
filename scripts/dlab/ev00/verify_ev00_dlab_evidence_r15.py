#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

NORMATIVE_CTEST = Path("docs/changesets/015/evidence/github-actions-run-30375982639/raw/windows-clang-cl-ctest.txt")
SNAPSHOT = Path(__file__).with_name("verify_ev00_dlab_evidence_r11.py")
EXPECTED_RESEARCH_TESTS = {
    "neoeng_dcore_replay_smoke",
    "neoeng_dcore_history_smoke",
    "neoeng_temporal_closure_tests",
}

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


def _arguments(row: dict) -> list[str]:
    args = row.get("arguments")
    if not isinstance(args, list) or not all(isinstance(x, str) for x in args):
        raise module.EvidenceError(f"command arguments are invalid: {row.get('name')!r}")
    return args


def _arg_after(args: list[str], key: str) -> str:
    try:
        idx = args.index(key)
    except ValueError as exc:
        raise module.EvidenceError(f"required command argument missing: {key}") from exc
    if idx + 1 >= len(args):
        raise module.EvidenceError(f"required command argument has no value: {key}")
    return args[idx + 1]


def _ends_with_dir(value: str, dirname: str) -> bool:
    normalized = value.replace("\\", "/").rstrip("/")
    return normalized.endswith("/" + dirname) or normalized == dirname


def check_ctest(evidence: Path, repo_root: Path) -> None:
    records = module.command_records(evidence)
    configure = module.require_command(records, "cmake-configure")
    configure_args = _arguments(configure)
    if "-DNEOENG_DCORE_BUILD_RESEARCH_TOOLS=OFF" not in configure_args:
        raise module.EvidenceError("supported-surface configure is not research-OFF")
    if "-DNEOENG_DCORE_BUILD_FULL_TOOLSET=OFF" not in configure_args:
        raise module.EvidenceError("supported-surface configure is not full-toolset-OFF")

    row = module.require_command(records, "ctest-dcore")
    ctest_args = _arguments(row)
    if _arg_after(ctest_args, "-L") != "dcore":
        raise module.EvidenceError("supported-surface CTest did not select label dcore")
    text = module.resolve_logged_file(evidence, row["stdout"]).read_text(encoding="utf-8", errors="replace")
    total, failed, names = parse_ctest_compat(text)

    historical_path = repo_root / NORMATIVE_CTEST
    if not historical_path.is_file():
        raise module.EvidenceError(f"normative CS015 CTest reference missing: {NORMATIVE_CTEST}")
    hist_total, hist_failed, hist_names = parse_ctest_compat(
        historical_path.read_text(encoding="utf-8", errors="replace")
    )
    if None in (total, failed, hist_total, hist_failed):
        raise module.EvidenceError("cannot parse supported/normative CTest summaries")
    if total != 54 or hist_total != 54 or failed != 0 or hist_failed != 0:
        raise module.EvidenceError(
            f"supported/normative CTest totals are not exact 54/0: local={total}/{failed} historical={hist_total}/{hist_failed}"
        )
    if names != hist_names:
        raise module.EvidenceError("supported-surface CTest inventory differs from normative CS015 54-test inventory")
    if EXPECTED_RESEARCH_TESTS & names:
        raise module.EvidenceError("research-only replay/history tests leaked into the normative supported-surface inventory")


def check_replay_rollback(evidence: Path, _: Path) -> None:
    records = module.command_records(evidence)
    configure = module.require_command(records, "research-cmake-configure")
    build = module.require_command(records, "research-cmake-build")
    ctest = module.require_command(records, "ctest-replay-rollback")

    configure_args = _arguments(configure)
    if "-DNEOENG_DCORE_BUILD_RESEARCH_TOOLS=ON" not in configure_args:
        raise module.EvidenceError("replay/history configure is not research-ON")
    if "-DNEOENG_DCORE_BUILD_FULL_TOOLSET=OFF" not in configure_args:
        raise module.EvidenceError("replay/history configure unexpectedly enables full toolset")
    research_build_dir = _arg_after(configure_args, "-B")
    if not _ends_with_dir(research_build_dir, "research-build"):
        raise module.EvidenceError("replay/history configure did not use isolated research-build directory")

    build_args = _arguments(build)
    if _arg_after(build_args, "--build") != research_build_dir:
        raise module.EvidenceError("research build command is not bound to research configure directory")
    try:
        target_index = build_args.index("--target")
    except ValueError as exc:
        raise module.EvidenceError("research build does not use explicit target allowlist") from exc
    tail = build_args[target_index + 1 :]
    if "neoeng_dcore_preclosure" not in tail or "neoeng_temporal_closure_tests" not in tail:
        raise module.EvidenceError("research build did not build both required replay/history targets")

    ctest_args = _arguments(ctest)
    if _arg_after(ctest_args, "--test-dir") != research_build_dir:
        raise module.EvidenceError("replay/history CTest is not bound to isolated research-build directory")
    regex = _arg_after(ctest_args, "-R")
    expected_regex = "^(neoeng_dcore_replay_smoke|neoeng_dcore_history_smoke|neoeng_temporal_closure_tests)$"
    if regex != expected_regex:
        raise module.EvidenceError(f"replay/history CTest regex is not exact anchored allowlist: {regex!r}")

    text = module.resolve_logged_file(evidence, ctest["stdout"]).read_text(encoding="utf-8", errors="replace")
    total, failed, names = parse_ctest_compat(text)
    if total != 3 or failed != 0 or names != EXPECTED_RESEARCH_TESTS:
        raise module.EvidenceError(
            f"replay/history surface must be exactly 3/0 and exact names; got total={total} failed={failed} names={sorted(names)}"
        )

    validation = module.load_json(evidence / "replay-rollback-validation.json")
    if validation.get("schema") != "neoeng.dlab.ev00-replay-rollback-validation.v1":
        raise module.EvidenceError("replay-rollback-validation schema mismatch")
    if validation.get("research_tools") is not True or validation.get("full_toolset") is not False:
        raise module.EvidenceError("replay-rollback-validation build-mode declaration mismatch")
    if set(validation.get("expected_tests", [])) != EXPECTED_RESEARCH_TESTS:
        raise module.EvidenceError("replay-rollback-validation expected inventory mismatch")
    if set(validation.get("observed_tests", [])) != EXPECTED_RESEARCH_TESTS:
        raise module.EvidenceError("replay-rollback-validation observed inventory mismatch")
    if validation.get("total_tests") != 3 or validation.get("failed_tests") != 0 or validation.get("exact_inventory") is not True:
        raise module.EvidenceError("replay-rollback-validation result is not exact 3/0 PASS")


def _git_commit_exists(repo_root: Path, sha: str) -> bool:
    if not module.SHA40.fullmatch(sha):
        return False
    return subprocess.run(
        ["git", "-C", str(repo_root), "cat-file", "-e", f"{sha}^{{commit}}"],
        capture_output=True,
    ).returncode == 0


def _git_is_ancestor(repo_root: Path, ancestor: str, descendant: str) -> bool:
    return subprocess.run(
        ["git", "-C", str(repo_root), "merge-base", "--is-ancestor", ancestor, descendant],
        capture_output=True,
    ).returncode == 0


def _path_exists_at_commit(repo_root: Path, commit: str, rel: str) -> bool:
    return subprocess.run(
        ["git", "-C", str(repo_root), "cat-file", "-e", f"{commit}:{rel}"],
        capture_output=True,
    ).returncode == 0


def check_historical_assurance(_: Path, repo_root: Path, historical_result: Path | None = None) -> None:
    if historical_result is None:
        historical_result = repo_root / "docs/changesets/017/HISTORICAL_ASSURANCE_RESULT.json"
    result = module.load_json(historical_result)
    if result.get("schema") != "neoeng.dlab.ev00-historical-assurance.v2" or result.get("overall_status") != "PASS":
        raise module.EvidenceError("historical assurance v2 overall status is not PASS")
    baseline = result.get("baseline_commit")
    if baseline != module.PRODUCT_SHA or not _git_commit_exists(repo_root, baseline):
        raise module.EvidenceError("historical assurance baseline binding mismatch")
    entries = result.get("entries")
    if not isinstance(entries, list):
        raise module.EvidenceError("historical assurance entries must be list")
    expected = {f"CS{i:03d}" for i in range(1, 16)}
    actual: set[str] = set()
    allowed_assessments = {"verified_integrity", "reproduced", "historical_only", "not_reproducible"}
    allowed_risks = {"unclassified", "critical", "high", "medium", "low"}
    for row in entries:
        if not isinstance(row, dict):
            raise module.EvidenceError("historical assurance entry is not object")
        cs = row.get("changeset")
        if not isinstance(cs, str) or cs in actual:
            raise module.EvidenceError(f"invalid/duplicate historical changeset: {cs!r}")
        actual.add(cs)
        if row.get("audit_status") != "PASS":
            raise module.EvidenceError(f"historical assurance audit not PASS: {cs}")
        if row.get("assessment_status") not in allowed_assessments:
            raise module.EvidenceError(f"historical assurance assessment invalid for {cs}")
        risk = row.get("risk_class")
        if risk not in allowed_risks:
            raise module.EvidenceError(f"historical assurance risk invalid for {cs}")
        rerun = row.get("rerun_status")
        if risk in {"critical", "high"} and row.get("reproducible") is True and rerun != "passed":
            raise module.EvidenceError(f"critical/high reproducible historical item lacks passed rerun: {cs}")
        commit = row.get("historical_commit")
        if not isinstance(commit, str) or not _git_commit_exists(repo_root, commit):
            raise module.EvidenceError(f"historical commit not verifiable for {cs}: {commit!r}")
        if not _git_is_ancestor(repo_root, commit, baseline):
            raise module.EvidenceError(f"historical commit is not in protected baseline ancestry for {cs}: {commit}")
        paths = row.get("evidence_paths")
        if not isinstance(paths, list) or not paths:
            raise module.EvidenceError(f"historical evidence paths missing for {cs}")
        for rel in paths:
            if not isinstance(rel, str) or not (repo_root / rel).is_file():
                raise module.EvidenceError(f"historical evidence path not found for {cs}: {rel!r}")
            if not _path_exists_at_commit(repo_root, commit, rel):
                raise module.EvidenceError(f"historical evidence path was not present at declared commit for {cs}: {rel}")
        provenance = row.get("provenance_kind")
        if provenance not in {"direct_changeset", "consolidated_validation", "accepted_baseline_closure"}:
            raise module.EvidenceError(f"historical provenance kind invalid for {cs}: {provenance!r}")
    if actual != expected:
        raise module.EvidenceError(f"historical assurance must cover CS001-CS015 exactly; got {sorted(actual)}")


def parser_self_test() -> None:
    modern = "Start 1: a\nStart 2: b\n100% tests passed, 0 tests failed out of 2\n"
    historical = "Start 1: a\nStart 2: b\n100% tests passed out of 2\n"
    ambiguous = "Start 1: a\nStart 2: b\n98% tests passed out of 2\n"
    if parse_ctest_compat(modern) != (2, 0, {"a", "b"}):
        raise SystemExit("R15 parser self-test failed: modern summary")
    if parse_ctest_compat(historical) != (2, 0, {"a", "b"}):
        raise SystemExit("R15 parser self-test failed: historical all-pass summary")
    total, failed, _ = parse_ctest_compat(ambiguous)
    if total is not None or failed is not None:
        raise SystemExit("R15 parser self-test failed: ambiguous historical summary accepted")


def research_contract_self_test() -> None:
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
            "expected_tests": sorted(EXPECTED_RESEARCH_TESTS),
            "observed_tests": sorted(EXPECTED_RESEARCH_TESTS),
            "total_tests": 3,
            "failed_tests": 0,
            "exact_inventory": True,
        }), encoding="utf-8")
        check_replay_rollback(evidence, Path(tmp))
        bad = json.loads((commands / "research-cmake-configure.json").read_text(encoding="utf-8"))
        bad["arguments"] = [x.replace("RESEARCH_TOOLS=ON", "RESEARCH_TOOLS=OFF") for x in bad["arguments"]]
        (commands / "research-cmake-configure.json").write_text(json.dumps(bad), encoding="utf-8")
        try:
            check_replay_rollback(evidence, Path(tmp))
        except module.EvidenceError:
            pass
        else:
            raise SystemExit("R15 research contract self-test failed: research-OFF evidence accepted")


module.parse_ctest = parse_ctest_compat
module.check_ctest = check_ctest
module.check_replay_rollback = check_replay_rollback
module.check_historical_assurance = check_historical_assurance
module.CHECKS["ctest"] = check_ctest
module.CHECKS["replay-rollback"] = check_replay_rollback
module.REQUIRED_COMMANDS.update({"research-cmake-configure", "research-cmake-build"})

if __name__ == "__main__":
    if "--self-test" in sys.argv:
        parser_self_test()
        research_contract_self_test()
    raise SystemExit(module.main())
