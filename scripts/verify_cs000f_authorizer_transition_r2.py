#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BASE = "4ad5e0629ef2c9a0ce9503b71af1c15fa9d21491"
ATTEMPT1 = "5c82691d9592c77894e855a504ea18a0245ede70"
ATTEMPT1_CAMPAIGN_RUN = 32545986480
ATTEMPT1_STALE_RUN = 32545986452

CANDIDATE_ALLOWED = {
    ".github/workflows/cs000e-r2-governance-transition-reconciliation.yml",
    ".github/workflows/cs000f-authorizer-transition-validation.yml",
    ".github/workflows/cs000f-r2-authorizer-transition-validation.yml",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
    "audit/validation/CS000F/ATTEMPT_001_NONACCEPTANCE.json",
    "audit/validation/CS000F/VALIDATION_PLAN.json",
    "audit/validation/CS000F/VALIDATION_PLAN_R2.json",
    "docs/changesets/000F/CHANGESET.md",
    "scripts/authorize_evolution_action.py",
    "scripts/authorize_evolution_action_legacy.py",
    "scripts/verify_cs000f_authorizer_transition.py",
    "scripts/verify_cs000f_authorizer_transition_r2.py",
}
CLOSURE_ALLOWED = CANDIDATE_ALLOWED | {"audit/validation/CS000F/VALIDATION_RESULT_R2.json"}


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
        errors.append("paths outside CS000F R2 scope: " + ", ".join(extra))
    missing = sorted(CANDIDATE_ALLOWED - paths)
    if missing:
        errors.append("required CS000F R2 paths missing: " + ", ".join(missing))
    for path in sorted(paths):
        if path == "CMakeLists.txt" or path.startswith(("src/", "include/", "tests/", "cmake/", "modules/", "apps/", "tools/")):
            errors.append("product/runtime path changed: " + path)
    return errors


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
            if token.endswith(":"):
                keys.add(token[:-1])
    return keys


def suffix_from_permissions(text: str) -> str:
    marker = "\npermissions:\n"
    pos = text.find(marker)
    if pos < 0:
        raise ValueError("permissions marker missing")
    return text[pos + 1:]


def git_show(ref: str, path: str) -> str:
    proc = run("git", "show", f"{ref}:{path}")
    if proc.returncode != 0:
        raise RuntimeError(f"cannot read {path} at {ref}: {proc.stderr.strip()}")
    return proc.stdout


def api_json(path: str) -> dict:
    token = os.environ.get("GITHUB_TOKEN", "")
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    if not token or not repo:
        raise RuntimeError("GITHUB_TOKEN/GITHUB_REPOSITORY required for live evidence")
    req = urllib.request.Request(
        f"https://api.github.com/repos/{repo}/{path}",
        headers={"Authorization": f"Bearer {token}", "Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28"},
    )
    with urllib.request.urlopen(req, timeout=20) as response:
        value = json.load(response)
    if not isinstance(value, dict):
        raise RuntimeError("GitHub API returned non-object")
    return value


def self_test_errors() -> list[str]:
    errors: list[str] = []
    if scope_errors(set(CANDIDATE_ALLOWED)):
        errors.append("valid R2 scope fixture rejected")
    bad = set(CANDIDATE_ALLOWED); bad.add("src/runtime.cpp")
    if not scope_errors(bad):
        errors.append("runtime scope fixture not rejected")
    if top_level_on_keys("name: x\n\non:\n  workflow_dispatch:\n\npermissions:\n  contents: read\n") != {"workflow_dispatch"}:
        errors.append("manual-only parser fixture failed")
    return errors


def attempt1_errors() -> list[str]:
    errors: list[str] = []
    record = json.loads((ROOT / "audit/validation/CS000F/ATTEMPT_001_NONACCEPTANCE.json").read_text(encoding="utf-8"))
    if record.get("source_sha") != ATTEMPT1 or record.get("acceptance_decision") != "NOT_ACCEPTED":
        errors.append("Attempt 1 nonacceptance record binding mismatch")
    if record.get("campaign", {}).get("run_id") != ATTEMPT1_CAMPAIGN_RUN or record.get("campaign", {}).get("conclusion") != "success":
        errors.append("Attempt 1 campaign record mismatch")
    stale = record.get("post_run_control_plane_observation", {})
    if stale.get("run_id") != ATTEMPT1_STALE_RUN or stale.get("conclusion") != "failure":
        errors.append("Attempt 1 stale-workflow failure record mismatch")

    campaign = api_json(f"actions/runs/{ATTEMPT1_CAMPAIGN_RUN}")
    if campaign.get("head_sha") != ATTEMPT1 or campaign.get("event") != "pull_request" or campaign.get("conclusion") != "success":
        errors.append("live Attempt 1 campaign evidence mismatch")
    if campaign.get("path") != ".github/workflows/cs000f-authorizer-transition-validation.yml":
        errors.append("live Attempt 1 campaign workflow mismatch")

    stale_run = api_json(f"actions/runs/{ATTEMPT1_STALE_RUN}")
    if stale_run.get("head_sha") != ATTEMPT1 or stale_run.get("event") != "pull_request" or stale_run.get("conclusion") != "failure":
        errors.append("live stale-workflow failure evidence mismatch")
    if stale_run.get("path") != ".github/workflows/cs000e-r2-governance-transition-reconciliation.yml":
        errors.append("live stale-workflow path mismatch")
    return errors


def retirement_errors() -> list[str]:
    errors: list[str] = []
    targets = [
        (".github/workflows/cs000e-r2-governance-transition-reconciliation.yml", BASE),
        (".github/workflows/cs000f-authorizer-transition-validation.yml", ATTEMPT1),
    ]
    for path, reference in targets:
        current = (ROOT / path).read_text(encoding="utf-8")
        if top_level_on_keys(current) != {"workflow_dispatch"}:
            errors.append(f"{path}: automatic trigger remains")
        prior = git_show(reference, path)
        if suffix_from_permissions(prior) != suffix_from_permissions(current):
            errors.append(f"{path}: body changed from permissions onward")
    return errors


def behavior_errors() -> list[str]:
    errors: list[str] = []
    selftest = run(sys.executable, "scripts/authorize_evolution_action.py", "--self-test")
    if selftest.returncode != 0:
        errors.append("canonical authorizer self-test failed: " + (selftest.stdout + selftest.stderr).strip())
    legacy = run(sys.executable, "scripts/authorize_evolution_action_legacy.py", "--action", "preflight", "--changeset", "CS017", "--stage", "EV-00")
    if legacy.returncode == 0 or "CS016E" not in (legacy.stdout + legacy.stderr):
        errors.append("legacy authorizer no longer demonstrates CS016E blocker")
    current = run(sys.executable, "scripts/authorize_evolution_action.py", "--action", "preflight", "--changeset", "CS017", "--stage", "EV-00")
    if current.returncode != 0:
        errors.append("canonical authorizer rejected EV-00 preflight: " + (current.stdout + current.stderr).strip())
    else:
        payload = json.loads(current.stdout)
        if payload.get("authorized") is not True or payload.get("decision") != "AUTHORIZED":
            errors.append("canonical authorizer did not authorize EV-00 preflight")
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
        print(f"CS000F R2 AUTHORIZER TRANSITION: REJECT — {label}")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"CS000F R2 AUTHORIZER TRANSITION: PASS — {label}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--attempt1", action="store_true")
    parser.add_argument("--retirement", action="store_true")
    parser.add_argument("--behavior", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--non-effects", action="store_true")
    args = parser.parse_args()
    selected = sum((args.self_test, args.attempt1, args.retirement, args.behavior, args.scope, args.non_effects))
    if selected != 1:
        parser.error("select exactly one check")
    try:
        if args.self_test:
            return emit("verifier-self-test", self_test_errors())
        if args.attempt1:
            return emit("attempt1-history", attempt1_errors())
        if args.retirement:
            return emit("workflow-retirement", retirement_errors())
        if args.behavior:
            return emit("authorizer-behavior", behavior_errors())
        if args.scope:
            return emit("controlled-scope", scope_errors(changed_paths()))
        return emit("non-effects", non_effect_errors())
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError) as exc:
        return emit("exception", [str(exc)])


if __name__ == "__main__":
    raise SystemExit(main())
