#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = "4ad5e0629ef2c9a0ce9503b71af1c15fa9d21491"
LEGACY_BLOB = "1db08cda94d0f774d6296b8c9dcf87bc956c5a56"
CANDIDATE_ALLOWED = {
    ".github/workflows/cs000f-authorizer-transition-validation.yml",
    "scripts/authorize_evolution_action.py",
    "scripts/authorize_evolution_action_legacy.py",
    "scripts/verify_cs000f_authorizer_transition.py",
    "audit/validation/CS000F/VALIDATION_PLAN.json",
    "docs/changesets/000F/CHANGESET.md",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
}
CLOSURE_ALLOWED = CANDIDATE_ALLOWED | {"audit/validation/CS000F/VALIDATION_RESULT.json"}


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, check=False)


def changed_paths() -> set[str]:
    proc = run("git", "diff", "--name-only", f"{BASE}...HEAD")
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "git diff failed")
    return {line.strip() for line in proc.stdout.splitlines() if line.strip()}


def scope_errors(paths: set[str]) -> list[str]:
    errors: list[str] = []
    extra = sorted(paths - CLOSURE_ALLOWED)
    if extra:
        errors.append("paths outside CS000F scope: " + ", ".join(extra))
    required = CANDIDATE_ALLOWED
    missing = sorted(required - paths)
    if missing:
        errors.append("required CS000F candidate paths missing: " + ", ".join(missing))
    result_present = "audit/validation/CS000F/VALIDATION_RESULT.json" in paths
    if result_present and "audit/CURRENT_CHANGESET_VALIDATION.json" not in paths:
        errors.append("CS000F result requires active descriptor")
    forbidden_prefixes = ("src/", "include/", "tests/", "cmake/", "modules/", "apps/", "tools/")
    for path in sorted(paths):
        if path.startswith(forbidden_prefixes) or path == "CMakeLists.txt":
            errors.append("product/runtime path changed: " + path)
    return errors


def self_test() -> list[str]:
    failures: list[str] = []
    good = set(CANDIDATE_ALLOWED)
    if scope_errors(good):
        failures.append("candidate scope fixture rejected")
    bad = set(good); bad.add("src/core.cpp")
    if not scope_errors(bad):
        failures.append("runtime path fixture was not rejected")
    missing = set(good); missing.remove("scripts/authorize_evolution_action_legacy.py")
    if not scope_errors(missing):
        failures.append("legacy-preservation omission was not rejected")
    return failures


def behavior_errors() -> list[str]:
    errors: list[str] = []
    legacy_hash = run("git", "hash-object", "scripts/authorize_evolution_action_legacy.py")
    if legacy_hash.returncode != 0 or legacy_hash.stdout.strip() != LEGACY_BLOB:
        errors.append("legacy authorizer bytes differ from pre-CS000F blob")

    legacy = run(sys.executable, "scripts/authorize_evolution_action_legacy.py", "--action", "preflight", "--changeset", "CS017", "--stage", "EV-00")
    if legacy.returncode == 0:
        errors.append("legacy authorizer unexpectedly authorizes EV-00 preflight")
    if "CS016E" not in (legacy.stdout + legacy.stderr):
        errors.append("legacy rejection does not identify CS016E blocker")

    current = run(sys.executable, "scripts/authorize_evolution_action.py", "--action", "preflight", "--changeset", "CS017", "--stage", "EV-00")
    if current.returncode != 0:
        errors.append("canonical authorizer rejected reconciled EV-00 preflight: " + (current.stdout + current.stderr).strip())
    else:
        try:
            payload = json.loads(current.stdout)
        except json.JSONDecodeError:
            errors.append("canonical authorizer did not emit JSON")
        else:
            if payload.get("authorized") is not True or payload.get("decision") != "AUTHORIZED":
                errors.append("canonical authorizer did not authorize reconciled EV-00 preflight")

    amendments = json.loads((ROOT / "audit/EVOLUTION_AMENDMENTS.json").read_text(encoding="utf-8"))
    row = next((r for r in amendments.get("amendments", []) if r.get("changeset") == "CS016E"), None)
    if not isinstance(row, dict):
        errors.append("CS016E amendment row missing")
    else:
        if row.get("status") != "superseded":
            errors.append("CS016E persistent status changed from superseded")
        if row.get("accepted_source_commit") is not None or row.get("evidence_manifest") is not None:
            errors.append("CS016E was retroactively accepted")
        if row.get("superseded_by") != "CS000E":
            errors.append("CS016E supersession binding missing")

    transition = json.loads((ROOT / "audit/GOVERNANCE_TRANSITION_STATE.json").read_text(encoding="utf-8"))
    legacy_state = transition.get("legacy_cs016e", {})
    if legacy_state.get("status") != "SUPERSEDED_UNACCEPTED" or legacy_state.get("accepted") is not False:
        errors.append("transition state no longer preserves CS016E as superseded/unaccepted")
    return errors


def non_effect_errors() -> list[str]:
    errors: list[str] = []
    protected = [
        "audit/EVOLUTION_ROADMAP.json",
        "audit/EVOLUTION_AMENDMENTS.json",
        "audit/GOVERNANCE_TRANSITION_STATE.json",
        "audit/STAGE_SCOPE_MAXIMA.json",
        "audit/GOVERNANCE_ROOT_OF_TRUST.json",
        "audit/GOVERNANCE_ACCEPTANCE_CHAIN.json",
    ]
    proc = run("git", "diff", "--name-only", BASE, "--", *protected)
    if proc.returncode != 0:
        errors.append("cannot verify protected governance bytes")
    elif proc.stdout.strip():
        errors.append("protected governance state changed: " + ", ".join(proc.stdout.splitlines()))
    return errors


def emit(label: str, errors: list[str]) -> int:
    if errors:
        print(f"CS000F AUTHORIZER TRANSITION: REJECT — {label}")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"CS000F AUTHORIZER TRANSITION: PASS — {label}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--behavior", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--non-effects", action="store_true")
    args = parser.parse_args()
    selected = sum((args.self_test, args.behavior, args.scope, args.non_effects))
    if selected != 1:
        parser.error("select exactly one check")
    try:
        if args.self_test:
            return emit("verifier-self-test", self_test())
        if args.behavior:
            return emit("authorizer-behavior", behavior_errors())
        if args.scope:
            return emit("controlled-scope", scope_errors(changed_paths()))
        return emit("non-effects", non_effect_errors())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        return emit("exception", [str(exc)])


if __name__ == "__main__":
    raise SystemExit(main())
