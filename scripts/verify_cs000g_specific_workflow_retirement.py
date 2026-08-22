#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = "b8598b73245ee0b31cc40ccfa60a1baed5142194"
TARGET = ".github/workflows/cs000f-r2-authorizer-transition-validation.yml"
SELF = ".github/workflows/cs000g-specific-workflow-retirement-validation.yml"
EXPECTED_TRIGGER_PATHS = {
    TARGET,
    SELF,
    "scripts/verify_cs000g_specific_workflow_retirement.py",
    "audit/validation/CS000G/VALIDATION_PLAN.json",
    "docs/changesets/000G/CHANGESET.md",
}
CANDIDATE_ALLOWED = EXPECTED_TRIGGER_PATHS | {"audit/CURRENT_CHANGESET_VALIDATION.json"}
CLOSURE_ALLOWED = CANDIDATE_ALLOWED | {"audit/validation/CS000G/VALIDATION_RESULT.json"}


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, check=False)


def git_show(ref: str, path: str) -> str:
    proc = run("git", "show", f"{ref}:{path}")
    if proc.returncode != 0:
        raise RuntimeError(f"cannot read {path} at {ref}: {proc.stderr.strip()}")
    return proc.stdout


def changed_paths() -> set[str]:
    proc = run("git", "diff", "--name-only", f"{BASE}...HEAD")
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "git diff failed")
    return {line.strip() for line in proc.stdout.splitlines() if line.strip()}


def top_level_on_keys(text: str) -> set[str]:
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
            if token.endswith(":" ):
                keys.add(token[:-1])
    return keys


def suffix_from_permissions(text: str) -> str:
    marker = "\npermissions:\n"
    pos = text.find(marker)
    if pos < 0:
        raise ValueError("permissions marker missing")
    return text[pos + 1:]


def trigger_paths(text: str) -> set[str]:
    lines = text.splitlines()
    in_paths = False
    values: set[str] = set()
    for line in lines:
        if line == "    paths:":
            in_paths = True
            continue
        if in_paths:
            if line.startswith("      - "):
                value = line.split("- ", 1)[1].strip().strip("'\"")
                values.add(value)
                continue
            if line.strip() and not line.startswith("      "):
                break
    return values


def self_test_errors() -> list[str]:
    errors: list[str] = []
    manual = "name: x\n\non:\n  workflow_dispatch:\n\npermissions:\n  contents: read\n"
    if top_level_on_keys(manual) != {"workflow_dispatch"}:
        errors.append("manual-only parser fixture failed")
    scoped = "name: x\n\non:\n  pull_request:\n    branches: [main]\n    paths:\n      - 'a'\n      - 'b'\n\npermissions:\n  contents: read\n"
    if trigger_paths(scoped) != {"a", "b"}:
        errors.append("path-scope parser fixture failed")
    good = set(CANDIDATE_ALLOWED)
    if good - CLOSURE_ALLOWED:
        errors.append("candidate scope fixture invalid")
    bad = set(good); bad.add("src/core.cpp")
    if not (bad - CLOSURE_ALLOWED):
        errors.append("extra-path fixture was not rejected")
    return errors


def retirement_errors() -> list[str]:
    errors: list[str] = []
    prior = git_show(BASE, TARGET)
    current = (ROOT / TARGET).read_text(encoding="utf-8")
    if top_level_on_keys(current) != {"workflow_dispatch"}:
        errors.append("CS000F R2 workflow is not workflow_dispatch-only")
    if suffix_from_permissions(prior) != suffix_from_permissions(current):
        errors.append("CS000F R2 workflow body changed from permissions onward")
    return errors


def applicability_errors() -> list[str]:
    errors: list[str] = []
    text = (ROOT / SELF).read_text(encoding="utf-8")
    if top_level_on_keys(text) != {"pull_request"}:
        errors.append("CS000G workflow must use only pull_request automatic applicability")
    paths = trigger_paths(text)
    if paths != EXPECTED_TRIGGER_PATHS:
        errors.append("CS000G workflow paths differ from exact bounded set")
    forbidden_future = {
        "audit/CURRENT_CHANGESET_VALIDATION.json",
        "audit/validation/CS000G/VALIDATION_RESULT.json",
        "docs/changesets/017/**",
        "audit/EVOLUTION_ROADMAP.json",
        ".github/workflows/ev00-dlab.yml",
    }
    overlap = sorted(paths & forbidden_future)
    if overlap:
        errors.append("CS000G workflow would trigger on closure/future EV-00 paths: " + ", ".join(overlap))
    return errors


def scope_errors() -> list[str]:
    paths = changed_paths()
    errors: list[str] = []
    extra = sorted(paths - CLOSURE_ALLOWED)
    if extra:
        errors.append("paths outside CS000G scope: " + ", ".join(extra))
    missing = sorted(CANDIDATE_ALLOWED - paths)
    if missing:
        errors.append("required CS000G candidate paths missing: " + ", ".join(missing))
    for path in sorted(paths):
        if path == "CMakeLists.txt" or path.startswith(("src/", "include/", "tests/", "cmake/", "modules/", "apps/", "tools/")):
            errors.append("product/runtime path changed: " + path)
    return errors


def non_effect_errors() -> list[str]:
    protected = [
        "audit/EVOLUTION_ROADMAP.json",
        "audit/EVOLUTION_AMENDMENTS.json",
        "audit/GOVERNANCE_TRANSITION_STATE.json",
        "audit/STAGE_SCOPE_MAXIMA.json",
        "audit/GOVERNANCE_ROOT_OF_TRUST.json",
        "audit/GOVERNANCE_ACCEPTANCE_CHAIN.json",
        "audit/validation/CS000F/VALIDATION_RESULT_R2.json",
        "scripts/authorize_evolution_action.py",
        "scripts/authorize_evolution_action_legacy.py",
    ]
    proc = run("git", "diff", "--name-only", BASE, "--", *protected)
    if proc.returncode != 0:
        return ["cannot verify protected governance/product bytes"]
    if proc.stdout.strip():
        return ["protected state changed: " + ", ".join(proc.stdout.splitlines())]
    return []


def emit(label: str, errors: list[str]) -> int:
    if errors:
        print(f"CS000G SPECIFIC WORKFLOW RETIREMENT: REJECT — {label}")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"CS000G SPECIFIC WORKFLOW RETIREMENT: PASS — {label}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--retirement", action="store_true")
    parser.add_argument("--applicability", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--non-effects", action="store_true")
    args = parser.parse_args()
    selected = sum((args.self_test, args.retirement, args.applicability, args.scope, args.non_effects))
    if selected != 1:
        parser.error("select exactly one check")
    try:
        if args.self_test:
            return emit("verifier-self-test", self_test_errors())
        if args.retirement:
            return emit("cs000f-r2-retirement", retirement_errors())
        if args.applicability:
            return emit("bounded-future-applicability", applicability_errors())
        if args.scope:
            return emit("controlled-scope", scope_errors())
        return emit("non-effects", non_effect_errors())
    except (OSError, RuntimeError, ValueError) as exc:
        return emit("exception", [str(exc)])


if __name__ == "__main__":
    raise SystemExit(main())
