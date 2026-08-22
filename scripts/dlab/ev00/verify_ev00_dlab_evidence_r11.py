#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Any

PRODUCT_SHA = "e3fff973554a2e56b8bd7afdc1132f75f3ec337c"
BASELINE_TAG = "v1.14.1"
CHANGESET = "CS017"
STAGE = "EV-00"
SHA40 = re.compile(r"^[0-9a-f]{40}$")
HISTORICAL_CTEST = Path("docs/changesets/015/evidence/windows-x86_64-clang-20260810/raw/ctest-output.txt")
REQUIRED_COMMANDS = {
    "cmake-configure",
    "cmake-build",
    "ctest-dcore",
    "determinism-1",
    "determinism-2",
    "ctest-host-sdk",
    "ctest-replay-rollback",
    "state-evidence-probe",
    "support-bundle-probe",
    "release-gate",
}


class EvidenceError(ValueError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise EvidenceError(f"missing file: {path}") from exc
    except json.JSONDecodeError as exc:
        raise EvidenceError(f"invalid JSON {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise EvidenceError(f"JSON root must be object: {path}")
    return data


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_records(evidence: Path) -> dict[str, dict[str, Any]]:
    root = evidence / "raw" / "commands"
    if not root.is_dir():
        raise EvidenceError(f"missing command record directory: {root}")
    records: dict[str, dict[str, Any]] = {}
    for path in sorted(root.glob("*.json")):
        row = load_json(path)
        name = row.get("name")
        if not isinstance(name, str) or not name:
            raise EvidenceError(f"command record missing name: {path}")
        if name in records:
            raise EvidenceError(f"duplicate command record: {name}")
        records[name] = row
    return records


def require_command(records: dict[str, dict[str, Any]], name: str) -> dict[str, Any]:
    row = records.get(name)
    if row is None:
        raise EvidenceError(f"required command missing: {name}")
    if row.get("exit_code") != 0 or row.get("classification") != "PASS":
        raise EvidenceError(
            f"required command not PASS: {name} exit={row.get('exit_code')} class={row.get('classification')}"
        )
    for key in ("stdout", "stderr", "working_directory", "started_at_utc", "finished_at_utc"):
        if not isinstance(row.get(key), str) or not row.get(key):
            raise EvidenceError(f"command {name} missing {key}")
    return row


def resolve_logged_file(evidence: Path, relative: str) -> Path:
    path = (evidence / relative).resolve()
    try:
        path.relative_to(evidence.resolve())
    except ValueError as exc:
        raise EvidenceError(f"log path escapes evidence root: {relative}") from exc
    if not path.is_file():
        raise EvidenceError(f"referenced log missing: {relative}")
    return path


def parse_ctest(text: str) -> tuple[int | None, int | None, set[str]]:
    total = None
    failed = None
    m = re.search(r"(\d+)% tests passed,\s+(\d+) tests failed out of (\d+)", text)
    if m:
        failed = int(m.group(2))
        total = int(m.group(3))
    names = set(re.findall(r"Test\s+#?\d+:\s+([A-Za-z0-9_.-]+)", text))
    if not names:
        names = set(re.findall(r"Start\s+\d+:\s+([A-Za-z0-9_.-]+)", text))
    return total, failed, names


def check_source(evidence: Path, _: Path) -> None:
    identity = load_json(evidence / "run-identity.json")
    expected = {
        "schema": "neoeng.dlab.ev00-run-identity.v1",
        "stage": STAGE,
        "changeset": CHANGESET,
        "baseline_tag": BASELINE_TAG,
        "product_sha": PRODUCT_SHA,
    }
    for key, value in expected.items():
        if identity.get(key) != value:
            raise EvidenceError(f"run identity mismatch {key}: {identity.get(key)!r} != {value!r}")
    harness = identity.get("harness_sha")
    if not isinstance(harness, str) or not SHA40.fullmatch(harness):
        raise EvidenceError("run identity harness_sha must be exact SHA")
    if identity.get("source_head") != PRODUCT_SHA:
        raise EvidenceError("source worktree head is not the protected v1.14.1 commit")
    if identity.get("source_dirty") is not False:
        raise EvidenceError("source worktree was dirty")


def check_environment(evidence: Path, _: Path) -> None:
    env = load_json(evidence / "environment.json")
    if env.get("schema") != "neoeng.dlab.ev00-environment.v1":
        raise EvidenceError("environment schema mismatch")
    if env.get("os_family") != "Windows":
        raise EvidenceError(f"qualifying EV-00 environment must be Windows, got {env.get('os_family')!r}")
    if env.get("physical_host") is not True:
        raise EvidenceError("environment does not declare physical_host=true")
    tools = env.get("tools")
    if not isinstance(tools, dict):
        raise EvidenceError("environment tools must be object")
    for name in ("git", "python", "cmake", "ctest", "ninja", "clang-cl"):
        value = tools.get(name)
        if not isinstance(value, str) or not value.strip():
            raise EvidenceError(f"environment missing tool identity: {name}")


def check_fresh_build(evidence: Path, _: Path) -> None:
    identity = load_json(evidence / "run-identity.json")
    if identity.get("workspace_fresh") is not True:
        raise EvidenceError("run does not prove workspace_fresh=true")
    if identity.get("preexisting_build_used") is not False:
        raise EvidenceError("preexisting build was used")
    records = command_records(evidence)
    require_command(records, "cmake-configure")
    require_command(records, "cmake-build")


def check_ctest(evidence: Path, _: Path) -> None:
    records = command_records(evidence)
    row = require_command(records, "ctest-dcore")
    text = resolve_logged_file(evidence, row["stdout"]).read_text(encoding="utf-8", errors="replace")
    total, failed, names = parse_ctest(text)
    if total is None or failed is None or failed != 0 or total <= 0:
        raise EvidenceError("supported CTest surface is not an unambiguous 100% PASS")
    required_names = {
        "neoeng_tests",
        "neoeng_state_evidence_tests",
        "neoeng_support_bundle_probe",
        "neoeng_dcore_replay_smoke",
        "neoeng_dcore_history_smoke",
        "neoeng_host_sdk_tests",
    }
    missing = sorted(required_names - names)
    if missing:
        raise EvidenceError("required CTest members missing: " + ", ".join(missing))


def check_determinism(evidence: Path, _: Path) -> None:
    records = command_records(evidence)
    first = require_command(records, "determinism-1")
    second = require_command(records, "determinism-2")
    first_path = resolve_logged_file(evidence, first["stdout"])
    second_path = resolve_logged_file(evidence, second["stdout"])
    if first_path.stat().st_size == 0 or second_path.stat().st_size == 0:
        raise EvidenceError("determinism probe output is empty")
    if sha256(first_path) != sha256(second_path):
        raise EvidenceError("determinism probe repeated outputs differ")


def check_host_sdk(evidence: Path, _: Path) -> None:
    records = command_records(evidence)
    row = require_command(records, "ctest-host-sdk")
    text = resolve_logged_file(evidence, row["stdout"]).read_text(encoding="utf-8", errors="replace")
    total, failed, names = parse_ctest(text)
    required = {"neoeng_host_sdk_install_consumer", "neoeng_host_sdk_tests", "neoeng_host_sdk_c_header_test", "neoeng_host_sdk_reference"}
    if total is None or failed != 0 or not required.issubset(names):
        raise EvidenceError("Host SDK boundary did not pass the complete expected surface")


def check_replay_rollback(evidence: Path, _: Path) -> None:
    records = command_records(evidence)
    row = require_command(records, "ctest-replay-rollback")
    text = resolve_logged_file(evidence, row["stdout"]).read_text(encoding="utf-8", errors="replace")
    total, failed, names = parse_ctest(text)
    required = {"neoeng_dcore_replay_smoke", "neoeng_dcore_history_smoke", "neoeng_temporal_closure_tests"}
    if total is None or failed != 0 or not required.issubset(names):
        raise EvidenceError("replay/rollback surface did not pass all expected tests")


def check_state_evidence(evidence: Path, _: Path) -> None:
    require_command(command_records(evidence), "state-evidence-probe")


def check_support_bundle(evidence: Path, _: Path) -> None:
    require_command(command_records(evidence), "support-bundle-probe")


def check_release_gate(evidence: Path, _: Path) -> None:
    records = command_records(evidence)
    row = require_command(records, "release-gate")
    text = resolve_logged_file(evidence, row["stdout"]).read_text(encoding="utf-8", errors="replace")
    total, failed, _ = parse_ctest(text)
    if total is None or failed != 0 or total <= 0:
        raise EvidenceError("baseline release-gate revalidation did not pass")


def check_manifest(evidence: Path, _: Path) -> None:
    manifest = load_json(evidence / "evidence-manifest.json")
    if manifest.get("schema") != "neoeng.dlab.evidence-manifest.v1" or manifest.get("algorithm") != "sha256":
        raise EvidenceError("evidence manifest schema/algorithm mismatch")
    rows = manifest.get("files")
    if not isinstance(rows, list) or not rows:
        raise EvidenceError("evidence manifest files must be non-empty list")
    seen: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            raise EvidenceError("manifest row is not object")
        rel = row.get("path")
        digest = row.get("sha256")
        size = row.get("size")
        if not isinstance(rel, str) or not rel or rel == "evidence-manifest.json" or rel in seen:
            raise EvidenceError(f"invalid/duplicate manifest path: {rel!r}")
        seen.add(rel)
        target = resolve_logged_file(evidence, rel)
        if not isinstance(digest, str) or not re.fullmatch(r"[0-9a-f]{64}", digest):
            raise EvidenceError(f"invalid manifest hash: {rel}")
        if target.stat().st_size != size or sha256(target) != digest:
            raise EvidenceError(f"manifest mismatch: {rel}")
    actual = {
        path.relative_to(evidence).as_posix()
        for path in evidence.rglob("*")
        if path.is_file() and path.name != "evidence-manifest.json"
    }
    if seen != actual:
        missing = sorted(actual - seen)
        stale = sorted(seen - actual)
        raise EvidenceError(f"manifest set mismatch missing={missing} stale={stale}")


def check_historical_comparison(evidence: Path, repo_root: Path) -> None:
    records = command_records(evidence)
    local_row = require_command(records, "ctest-dcore")
    local_text = resolve_logged_file(evidence, local_row["stdout"]).read_text(encoding="utf-8", errors="replace")
    historical_path = repo_root / HISTORICAL_CTEST
    if not historical_path.is_file():
        raise EvidenceError(f"historical CTest reference missing: {HISTORICAL_CTEST}")
    historical_text = historical_path.read_text(encoding="utf-8", errors="replace")
    local_total, local_failed, local_names = parse_ctest(local_text)
    hist_total, hist_failed, hist_names = parse_ctest(historical_text)
    if None in (local_total, local_failed, hist_total, hist_failed):
        raise EvidenceError("cannot parse local/historical CTest summaries")
    if local_failed != 0 or hist_failed != 0:
        raise EvidenceError("local or historical CTest reference contains failures")
    if local_total != hist_total or local_names != hist_names:
        raise EvidenceError(
            f"CTest inventory differs from accepted historical reference: local={local_total} historical={hist_total}"
        )
    comparison = load_json(evidence / "historical-comparison.json")
    if comparison.get("inventory_equal") is not True or comparison.get("local_total_tests") != local_total or comparison.get("historical_total_tests") != hist_total:
        raise EvidenceError("historical-comparison.json does not match independently parsed evidence")


def git_commit_exists(repo_root: Path, sha: str) -> bool:
    if not SHA40.fullmatch(sha):
        return False
    proc = subprocess.run(["git", "-C", str(repo_root), "cat-file", "-e", f"{sha}^{{commit}}"], capture_output=True)
    return proc.returncode == 0


def check_historical_assurance(_: Path, repo_root: Path, historical_result: Path | None = None) -> None:
    if historical_result is None:
        historical_result = repo_root / "docs/changesets/017/HISTORICAL_ASSURANCE_RESULT.json"
    result = load_json(historical_result)
    if result.get("schema") != "neoeng.dlab.ev00-historical-assurance.v1" or result.get("overall_status") != "PASS":
        raise EvidenceError("historical assurance overall status is not PASS")
    entries = result.get("entries")
    if not isinstance(entries, list):
        raise EvidenceError("historical assurance entries must be list")
    expected = {f"CS{i:03d}" for i in range(1, 16)}
    actual: set[str] = set()
    for row in entries:
        if not isinstance(row, dict):
            raise EvidenceError("historical assurance entry is not object")
        cs = row.get("changeset")
        if not isinstance(cs, str) or cs in actual:
            raise EvidenceError(f"invalid/duplicate historical changeset: {cs!r}")
        actual.add(cs)
        commit = row.get("historical_commit")
        if not isinstance(commit, str) or not git_commit_exists(repo_root, commit):
            raise EvidenceError(f"historical commit not verifiable for {cs}: {commit!r}")
        paths = row.get("evidence_paths")
        if not isinstance(paths, list) or not paths:
            raise EvidenceError(f"historical evidence paths missing for {cs}")
        for rel in paths:
            if not isinstance(rel, str) or not (repo_root / rel).is_file():
                raise EvidenceError(f"historical evidence path not found for {cs}: {rel!r}")
        if row.get("status") != "PASS":
            raise EvidenceError(f"historical assurance entry not PASS: {cs}")
    if actual != expected:
        raise EvidenceError(f"historical assurance must cover CS001-CS015 exactly; got {sorted(actual)}")


def check_terminal(evidence: Path, _: Path) -> None:
    terminal = load_json(evidence / "terminal-state.json")
    if terminal.get("schema") != "neoeng.dlab.ev00-terminal-state.v1" or terminal.get("state") != "PASSED":
        raise EvidenceError(f"local D-Lab terminal state is not PASSED: {terminal.get('state')!r}")
    records = command_records(evidence)
    missing = sorted(REQUIRED_COMMANDS - set(records))
    if missing:
        raise EvidenceError("terminal run missing commands: " + ", ".join(missing))
    for name in sorted(REQUIRED_COMMANDS):
        require_command(records, name)


CHECKS = {
    "source": check_source,
    "environment": check_environment,
    "fresh-build": check_fresh_build,
    "ctest": check_ctest,
    "determinism": check_determinism,
    "host-sdk": check_host_sdk,
    "replay-rollback": check_replay_rollback,
    "state-evidence": check_state_evidence,
    "support-bundle": check_support_bundle,
    "release-gate": check_release_gate,
    "manifest": check_manifest,
    "historical-comparison": check_historical_comparison,
    "terminal": check_terminal,
}


def self_test() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        evidence = root / "evidence"
        (evidence / "raw" / "commands").mkdir(parents=True)
        bad = {
            "schema": "neoeng.dlab.ev00-run-identity.v1",
            "stage": STAGE,
            "changeset": CHANGESET,
            "baseline_tag": BASELINE_TAG,
            "product_sha": "0" * 40,
            "harness_sha": "a" * 40,
            "source_head": "0" * 40,
            "source_dirty": False,
        }
        (evidence / "run-identity.json").write_text(json.dumps(bad), encoding="utf-8")
        try:
            check_source(evidence, root)
        except EvidenceError:
            pass
        else:
            raise EvidenceError("self-test failed: wrong product SHA was accepted")
        (evidence / "evidence-manifest.json").write_text(
            json.dumps({"schema": "neoeng.dlab.evidence-manifest.v1", "algorithm": "sha256", "files": []}),
            encoding="utf-8",
        )
        try:
            check_manifest(evidence, root)
        except EvidenceError:
            pass
        else:
            raise EvidenceError("self-test failed: empty manifest was accepted")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--evidence", type=Path)
    parser.add_argument("--repo-root", type=Path, default=Path("."))
    parser.add_argument("--historical-result", type=Path)
    parser.add_argument("--check", choices=[*CHECKS.keys(), "historical-assurance", "all"], default="all")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
            print("EV00 EVIDENCE VERIFIER SELF-TEST: PASS")
            return 0
        if args.evidence is None:
            raise EvidenceError("--evidence is required")
        evidence = args.evidence.resolve()
        repo_root = args.repo_root.resolve()
        selected = list(CHECKS) if args.check == "all" else [args.check]
        if args.check in {"historical-assurance", "all"}:
            if args.check == "historical-assurance":
                selected = []
            check_historical_assurance(evidence, repo_root, args.historical_result.resolve() if args.historical_result else None)
            print("historical-assurance: PASS")
        for name in selected:
            CHECKS[name](evidence, repo_root)
            print(f"{name}: PASS")
    except EvidenceError as exc:
        print(f"EV00 EVIDENCE VERIFICATION: REJECT: {exc}")
        return 1
    print("EV00 EVIDENCE VERIFICATION: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
