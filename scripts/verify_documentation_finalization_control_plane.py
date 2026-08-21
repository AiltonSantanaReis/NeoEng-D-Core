#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import Any

PLAN_PATH = Path("audit/validation/CS000D/VALIDATION_PLAN.json")
CLOSURE_FAILURE_PATH = Path("audit/validation/CS000C/FAILED_CLOSURE_ATTEMPT_001.json")
CS000C_PLAN_PATH = Path("audit/validation/CS000C/VALIDATION_PLAN.json")
CS000C_RESULT_PATH = Path("audit/validation/CS000C/VALIDATION_RESULT.json")
CS000C_VERIFIER_PATH = Path("scripts/verify_documentation_control_plane_correction.py")
CS000C_WORKFLOW_PATH = Path(".github/workflows/cs000c-documentation-control-plane-correction.yml")
CURRENT_DESCRIPTOR_PATH = Path("audit/CURRENT_CHANGESET_VALIDATION.json")
CS000D_RESULT_PATH = Path("audit/validation/CS000D/VALIDATION_RESULT.json")
REFERENCE_SHA = "9bc97328d9701a08fa9552893728c7e63788b710"


def git(root: Path, *args: str, binary: bool = False):
    return subprocess.run(
        ["git", "-C", str(root), *args],
        text=not binary,
        capture_output=True,
        check=False,
    )


def git_show(root: Path, commit: str, rel: str) -> bytes | None:
    proc = git(root, "show", f"{commit}:{rel}", binary=True)
    return proc.stdout if proc.returncode == 0 else None


def read_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {path}")
    return value


def on_keys(text: str) -> set[str]:
    lines = text.splitlines()
    try:
        start = lines.index("on:") + 1
    except ValueError as exc:
        raise ValueError("missing top-level on: block") from exc
    keys: set[str] = set()
    for line in lines[start:]:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if not line.startswith(" "):
            break
        if line.startswith("  ") and not line.startswith("    "):
            token = line.strip()
            if token.endswith(":"):
                keys.add(token[:-1])
    return keys


def preserved_suffix(text: str, marker: str = "\npermissions:\n") -> str:
    pos = text.find(marker)
    if pos < 0:
        raise ValueError("permissions marker not found")
    return text[pos + 1:]


def expected_scope(plan: dict[str, Any], closure_bound: bool) -> set[str]:
    expected = set(plan.get("correction_delta_from_closure_failure", []))
    if closure_bound:
        expected.update(plan.get("controlled_closure_artifact_paths", []))
    return expected


def descriptor_is_cs000d(root: Path) -> bool:
    if not CURRENT_DESCRIPTOR_PATH.is_absolute():
        path = root / CURRENT_DESCRIPTOR_PATH
    else:
        path = CURRENT_DESCRIPTOR_PATH
    if not path.is_file():
        return False
    value = read_json(path)
    return (
        value.get("schema") == "neoeng.dcore.current-changeset-validation.v1"
        and value.get("plan_path") == PLAN_PATH.as_posix()
        and value.get("result_path") == CS000D_RESULT_PATH.as_posix()
    )


def check_history(root: Path, plan: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    failure = read_json(root / CLOSURE_FAILURE_PATH)
    expected_failure = {
        "schema": "neoeng.dcore.failed-changeset-attempt.v1",
        "changeset": "CS000C",
        "source_sha": REFERENCE_SHA,
        "source_tree": "c2b32cb95d609dc3cec900bb36dbf937bdf2d979",
        "run_id": 32536496359,
        "run_attempt": 1,
        "event": "pull_request",
        "workflow_path": ".github/workflows/cs000c-documentation-control-plane-correction.yml",
        "conclusion": "failure",
        "failed_step": "Verify correction-only delta",
        "classification": "FAILED_POST_ACCEPTANCE_CLOSURE_APPLICABILITY_ATTEMPT",
        "qualifying_source_sha": "87d8749cf81964afd4b1e5b35f1bdffbba7851f2",
        "qualifying_run_id": 32535065912,
        "qualifying_run_attempt": 1,
        "qualifying_validation_state": "VALIDATED",
        "qualifying_acceptance_decision": "ACCEPTED",
        "rerun_allowed": False,
        "qualification_effect": "NONE",
        "product_runtime_abi_effect": "NONE",
        "evolution_stage_effect": "NONE",
        "release_effect": "NONE",
    }
    for key, value in expected_failure.items():
        if failure.get(key) != value:
            errors.append(f"closure failure record mismatch: {key}")

    result = read_json(root / CS000C_RESULT_PATH)
    expected_result = {
        "changeset": "CS000C",
        "plan_commit": "87d8749cf81964afd4b1e5b35f1bdffbba7851f2",
        "source_sha": "87d8749cf81964afd4b1e5b35f1bdffbba7851f2",
        "run_id": 32535065912,
        "run_attempt": 1,
        "validation_state": "VALIDATED",
        "acceptance_decision": "ACCEPTED",
    }
    for key, value in expected_result.items():
        if result.get(key) != value:
            errors.append(f"CS000C accepted result mismatch: {key}")

    for rel in (CS000C_PLAN_PATH, CS000C_RESULT_PATH, CS000C_VERIFIER_PATH):
        prior = git_show(root, REFERENCE_SHA, rel.as_posix())
        current = root / rel
        if prior is None:
            errors.append(f"cannot read CS000C historical path at reference SHA: {rel}")
        elif not current.is_file():
            errors.append(f"CS000C historical path missing: {rel}")
        elif current.read_bytes() != prior:
            errors.append(f"CS000C historical bytes changed: {rel}")

    if plan.get("reference_closure_sha") != REFERENCE_SHA:
        errors.append("plan reference_closure_sha mismatch")
    if plan.get("preserved_cs000c_closure_failure_run_id") != 32536496359:
        errors.append("plan closure failure run binding mismatch")
    return errors


def check_scope(root: Path, plan: dict[str, Any]) -> list[str]:
    proc = git(root, "diff", "--name-only", f"{REFERENCE_SHA}...HEAD")
    if proc.returncode != 0:
        return [proc.stderr.strip() or "git diff failed"]
    actual = {line.strip() for line in proc.stdout.splitlines() if line.strip()}

    result_exists = (root / CS000D_RESULT_PATH).is_file()
    descriptor_bound = descriptor_is_cs000d(root)
    errors: list[str] = []
    if result_exists != descriptor_bound:
        errors.append("CS000D closure artifacts must appear together as result plus active descriptor")

    closure_bound = result_exists and descriptor_bound
    expected = expected_scope(plan, closure_bound)
    extra = sorted(actual - expected)
    missing = sorted(expected - actual)
    if extra:
        errors.append("paths outside controlled CS000D delta: " + ", ".join(extra))
    if missing:
        errors.append("required CS000D paths missing: " + ", ".join(missing))
    return errors


def check_cs000c_retired(root: Path) -> list[str]:
    errors: list[str] = []
    current = (root / CS000C_WORKFLOW_PATH).read_text(encoding="utf-8")
    try:
        keys = on_keys(current)
    except ValueError as exc:
        return [str(exc)]
    if keys != {"workflow_dispatch"}:
        errors.append(f"CS000C must be manual-only, found triggers: {sorted(keys)}")

    prior_bytes = git_show(root, REFERENCE_SHA, CS000C_WORKFLOW_PATH.as_posix())
    if prior_bytes is None:
        return errors + ["cannot read CS000C workflow at reference SHA"]
    prior = prior_bytes.decode("utf-8")
    try:
        prior_keys = on_keys(prior)
        if prior_keys != {"pull_request", "workflow_dispatch"}:
            errors.append(f"reference CS000C trigger set unexpected: {sorted(prior_keys)}")
        if preserved_suffix(prior) != preserved_suffix(current):
            errors.append("CS000C workflow body changed outside trigger retirement")
    except ValueError as exc:
        errors.append(str(exc))
    return errors


def check_documentation(root: Path, plan: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    for rel in plan.get("documentation_paths_preserved_from_closure_failure", []):
        prior = git_show(root, REFERENCE_SHA, rel)
        current = root / rel
        if prior is None:
            errors.append(f"cannot read documentation at reference SHA: {rel}")
        elif not current.is_file():
            errors.append(f"documentation path missing: {rel}")
        elif current.read_bytes() != prior:
            errors.append(f"documentation bytes changed after accepted CS000C: {rel}")
    return errors


def check_project_boundary(root: Path) -> list[str]:
    errors: list[str] = []
    required = {
        "README.md": ("D-Core", "D-Lab", "produto", "extern"),
        "ABOUT.md": ("D-Core", "D-Lab", "produto", "extern"),
        "docs/PROJECT_STATUS.md": ("D-Core", "D-Lab", "produto", "extern"),
        "docs/README.md": ("D-Core", "D-Lab"),
    }
    for rel, tokens in required.items():
        lowered = (root / rel).read_text(encoding="utf-8").lower()
        for token in tokens:
            if token.lower() not in lowered:
                errors.append(f"{rel}: missing project-boundary token {token!r}")
    return errors


def self_test() -> list[str]:
    errors: list[str] = []
    manual = "name: x\n\non:\n  workflow_dispatch:\n\npermissions:\n  contents: read\n"
    automatic = "name: x\n\non:\n  pull_request:\n    branches: [main]\n  workflow_dispatch:\n\npermissions:\n  contents: read\n"
    if on_keys(manual) != {"workflow_dispatch"}:
        errors.append("manual trigger fixture rejected")
    if on_keys(automatic) != {"pull_request", "workflow_dispatch"}:
        errors.append("automatic trigger fixture rejected")

    fixture_plan = {
        "correction_delta_from_closure_failure": ["a", "b"],
        "controlled_closure_artifact_paths": ["c", "d"],
    }
    if expected_scope(fixture_plan, False) != {"a", "b"}:
        errors.append("candidate-scope fixture rejected")
    if expected_scope(fixture_plan, True) != {"a", "b", "c", "d"}:
        errors.append("closure-scope fixture rejected")
    return errors


def emit(errors: list[str], label: str) -> int:
    if errors:
        print(f"DCORE DOCUMENTATION FINALIZATION CONTROL-PLANE: REJECT — {label}")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"DCORE DOCUMENTATION FINALIZATION CONTROL-PLANE: PASS — {label}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--history", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--cs000c-retired", action="store_true")
    parser.add_argument("--documentation", action="store_true")
    parser.add_argument("--project-boundary", action="store_true")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    if args.self_test:
        return emit(self_test(), "verifier-self-test")
    plan = read_json(root / PLAN_PATH)
    if args.history:
        return emit(check_history(root, plan), "history-preservation")
    if args.scope:
        return emit(check_scope(root, plan), "controlled-scope")
    if args.cs000c_retired:
        return emit(check_cs000c_retired(root), "cs000c-retirement")
    if args.documentation:
        return emit(check_documentation(root, plan), "documentation-preservation")
    if args.project_boundary:
        return emit(check_project_boundary(root), "project-boundary")
    parser.error("select one verification mode")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
