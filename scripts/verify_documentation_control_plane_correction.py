#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import subprocess
import unicodedata
from pathlib import Path
from typing import Any

PLAN_PATH = Path("audit/validation/CS000C/VALIDATION_PLAN.json")
FAILED_ATTEMPT_PATH = Path("audit/validation/CS000B/FAILED_ATTEMPT_001.json")
FAILED_SHA = "49e9edd36a4a7bae75b832231b55e360f7c41ae2"
SUPERSEDED_WORKFLOWS = (
    ".github/workflows/cs000a-legacy-retirement-validation.yml",
    ".github/workflows/cs000b-documentation-modernization-validation.yml",
)


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


def fold(text: str) -> str:
    normalized = unicodedata.normalize("NFKD", text)
    return "".join(ch for ch in normalized if not unicodedata.combining(ch)).lower()


def release_not_authorized(text: str) -> bool:
    value = fold(text)
    negative_patterns = (
        r"\bsem\s+autorizacao\s+de\s+nova\s+release\b",
        r"\bnova\s+release\s+nao\s+(?:esta\s+)?autorizada\b",
        r"\bnenhuma\s+nova\s+release\s+autorizada\b",
        r"\bnova\s+release\s+autorizada\s*\|\s*false\b",
        r"\brelease_authorized\s*[=:]\s*false\b",
    )
    if re.search(r"\brelease_authorized\s*[=:]\s*true\b", value):
        return False
    if re.search(r"\bnova\s+release\s+esta\s+autorizada\b", value):
        return False
    return any(re.search(pattern, value) for pattern in negative_patterns)


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


def check_failed_attempt_preservation(root: Path, plan: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    record = read_json(root / FAILED_ATTEMPT_PATH)
    expected = {
        "schema": "neoeng.dcore.failed-changeset-attempt.v1",
        "changeset": "CS000B",
        "source_sha": FAILED_SHA,
        "source_tree": "73d40076a1e2a3e57ae00c850b41ad2d891c79cc",
        "run_id": 32532596759,
        "run_attempt": 1,
        "workflow_path": ".github/workflows/cs000b-documentation-modernization-validation.yml",
        "conclusion": "failure",
        "failed_step": "Verify roadmap and release status binding",
        "classification": "FAILED_INITIAL_CS000B_DOCUMENTATION_VALIDATION_CANDIDATE",
        "rerun_allowed": False,
    }
    for key, value in expected.items():
        if record.get(key) != value:
            errors.append(f"failed attempt record mismatch: {key}")

    immutable = list(plan.get("documentation_paths_preserved_from_failed_candidate", []))
    immutable += [
        "scripts/verify_documentation_modernization.py",
        "audit/validation/CS000B/VALIDATION_PLAN.json",
        "docs/changesets/000B/CHANGESET.md",
        "docs/maintenance/DCORE_DOCUMENTATION_MODERNIZATION_PREPARATION.json",
    ]
    for rel in immutable:
        prior = git_show(root, FAILED_SHA, rel)
        current = root / rel
        if prior is None:
            errors.append(f"cannot read failed-candidate path: {rel}")
        elif not current.is_file():
            errors.append(f"failed-candidate path missing now: {rel}")
        elif current.read_bytes() != prior:
            errors.append(f"failed-candidate bytes changed: {rel}")

    if (root / "audit/validation/CS000B/VALIDATION_RESULT.json").exists():
        errors.append("CS000B VALIDATION_RESULT must remain absent after failed attempt")
    return errors


def check_scope(root: Path, plan: dict[str, Any]) -> list[str]:
    proc = git(root, "diff", "--name-only", f"{FAILED_SHA}...HEAD")
    if proc.returncode != 0:
        return [proc.stderr.strip() or "git diff failed"]
    actual = {line.strip() for line in proc.stdout.splitlines() if line.strip()}
    expected = set(plan.get("correction_delta_from_failed_candidate", []))
    errors: list[str] = []
    extra = sorted(actual - expected)
    missing = sorted(expected - actual)
    if extra:
        errors.append("paths outside frozen correction delta: " + ", ".join(extra))
    if missing:
        errors.append("required correction paths missing: " + ", ".join(missing))
    return errors


def check_status_semantics(root: Path, plan: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    roadmap = read_json(root / "audit/EVOLUTION_ROADMAP.json")
    expected = plan["status_expectations"]
    for key in ("project_version", "program_id", "program_state", "current_stage", "release_authorized"):
        if roadmap.get(key) != expected[key]:
            errors.append(f"roadmap {key} differs from frozen expectation")

    baseline = roadmap.get("baseline", {})
    if baseline.get("release_tag") != expected["release_tag"]:
        errors.append("roadmap release_tag differs from frozen expectation")
    if baseline.get("release_commit") != expected["release_commit"]:
        errors.append("roadmap release_commit differs from frozen expectation")

    stage = next(
        (item for item in roadmap.get("stages", []) if item.get("stage_id") == expected["current_stage"]),
        None,
    )
    if not isinstance(stage, dict):
        errors.append("current stage missing from roadmap")
    else:
        if stage.get("status") != expected["current_stage_status"]:
            errors.append("current stage status differs from frozen expectation")
        if stage.get("planned_changeset") != expected["planned_changeset"]:
            errors.append("planned ChangeSet differs from frozen expectation")

    required_tokens = (
        expected["project_version"],
        expected["release_tag"],
        expected["release_commit"],
        expected["program_id"],
        expected["current_stage"],
        expected["planned_changeset"],
    )
    for rel in ("README.md", "ABOUT.md", "docs/PROJECT_STATUS.md"):
        text = (root / rel).read_text(encoding="utf-8")
        for token in required_tokens:
            if token not in text:
                errors.append(f"{rel}: missing status identity {token}")
        if not release_not_authorized(text):
            errors.append(f"{rel}: does not unambiguously state that a new release is not authorized")
    return errors


def check_superseded_workflow_applicability(root: Path) -> list[str]:
    errors: list[str] = []
    for rel in SUPERSEDED_WORKFLOWS:
        current = (root / rel).read_text(encoding="utf-8")
        try:
            keys = on_keys(current)
        except ValueError as exc:
            errors.append(f"{rel}: {exc}")
            continue
        if keys != {"workflow_dispatch"}:
            errors.append(f"{rel}: must be manual-only, found triggers: {sorted(keys)}")

        prior_bytes = git_show(root, FAILED_SHA, rel)
        if prior_bytes is None:
            errors.append(f"{rel}: cannot read failed-candidate bytes")
            continue
        prior = prior_bytes.decode("utf-8")
        if prior.splitlines()[0] != current.splitlines()[0]:
            errors.append(f"{rel}: workflow name changed")
        try:
            if preserved_suffix(prior) != preserved_suffix(current):
                errors.append(f"{rel}: workflow body changed outside trigger retirement")
        except ValueError as exc:
            errors.append(f"{rel}: {exc}")
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
    accepted = (
        "O roadmap segue sem autorização de nova release.",
        "A nova release não está autorizada.",
        "Nenhuma nova release autorizada.",
        "| Nova release autorizada | false |",
    )
    for fixture in accepted:
        if not release_not_authorized(fixture):
            errors.append(f"negative release fixture rejected: {fixture}")
    if release_not_authorized("A nova release está autorizada."):
        errors.append("positive release authorization fixture was accepted")

    manual = "name: x\n\non:\n  workflow_dispatch:\n\npermissions:\n  contents: read\n"
    automatic = "name: x\n\non:\n  pull_request:\n    branches: [main]\n\npermissions:\n  contents: read\n"
    if on_keys(manual) != {"workflow_dispatch"}:
        errors.append("manual workflow fixture rejected")
    if on_keys(automatic) == {"workflow_dispatch"}:
        errors.append("automatic workflow fixture not rejected")
    return errors


def emit(errors: list[str], label: str) -> int:
    if errors:
        print(f"DCORE DOCUMENTATION CONTROL-PLANE CORRECTION: REJECT — {label}")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"DCORE DOCUMENTATION CONTROL-PLANE CORRECTION: PASS — {label}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=".")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--failed-attempt", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--status", action="store_true")
    parser.add_argument("--legacy-applicability", action="store_true")
    parser.add_argument("--project-boundary", action="store_true")
    args = parser.parse_args()

    root = Path(args.root).resolve()
    if args.self_test:
        return emit(self_test(), "verifier-self-test")
    plan = read_json(root / PLAN_PATH)
    if args.failed_attempt:
        return emit(check_failed_attempt_preservation(root, plan), "failed-attempt-preservation")
    if args.scope:
        return emit(check_scope(root, plan), "correction-scope")
    if args.status:
        return emit(check_status_semantics(root, plan), "status-semantics")
    if args.legacy_applicability:
        return emit(check_superseded_workflow_applicability(root), "legacy-applicability")
    if args.project_boundary:
        return emit(check_project_boundary(root), "project-boundary")
    parser.error("select one verification mode")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
