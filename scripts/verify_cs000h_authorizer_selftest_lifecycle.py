#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ast
import json
import os
import subprocess
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
BASE = "ed3661ee3aad366d639d1a3de5934e53c507c135"
OLD_WRAPPER_BLOB = "2b2969d0a173fbd71bb1ec12f739998f02278715"
NEW_WRAPPER_BLOB = "0316e2659e3284cbf2a45f816c0f2e958bd3674e"
OLD_WRAPPER = "scripts/authorize_evolution_action_cs000f.py"
CANONICAL_WRAPPER = "scripts/authorize_evolution_action.py"
FAILURE_RECORD = "audit/validation/CS000H/R8_PREPARATION_FAILURE.json"
SELF_WORKFLOW = ".github/workflows/cs000h-authorizer-selftest-lifecycle-validation.yml"
EXPECTED_TRIGGER_PATHS = {'scripts/authorize_evolution_action_cs000f.py', 'scripts/authorize_evolution_action.py', 'audit/validation/CS000H/VALIDATION_PLAN.json', 'scripts/verify_cs000h_authorizer_selftest_lifecycle.py', 'audit/validation/CS000H/R8_PREPARATION_FAILURE.json', 'docs/changesets/000H/CHANGESET.md', '.github/workflows/cs000h-authorizer-selftest-lifecycle-validation.yml'}
CANDIDATE_ALLOWED = EXPECTED_TRIGGER_PATHS | {"audit/CURRENT_CHANGESET_VALIDATION.json"}
CLOSURE_ALLOWED = CANDIDATE_ALLOWED | {"audit/validation/CS000H/VALIDATION_RESULT.json"}
R8_RUN_ID = 32546849264
R8_SOURCE_SHA = "5697828d499051f9fc397c99dd6f43cc9a021318"
R8_JOB_ID = 96966722299
R8_WORKFLOW = ".github/workflows/cs017-r8-preparation-preflight.yml"


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True, check=False)


def blob(path: str) -> str:
    proc = run("git", "hash-object", path)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or f"cannot hash {path}")
    return proc.stdout.strip()


def changed_paths() -> set[str]:
    proc = run("git", "diff", "--name-only", f"{BASE}...HEAD")
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "git diff failed")
    return {line.strip() for line in proc.stdout.splitlines() if line.strip()}


def trigger_paths(text: str) -> set[str]:
    values: set[str] = set()
    in_paths = False
    for line in text.splitlines():
        if line == "    paths:":
            in_paths = True
            continue
        if in_paths:
            if line.startswith("      - "):
                values.add(line.split("- ", 1)[1].strip().strip("'\""))
                continue
            if line.strip() and not line.startswith("      "):
                break
    return values


def top_level_on_keys(text: str) -> set[str]:
    lines = text.splitlines()
    try:
        start = lines.index("on:") + 1
    except ValueError as exc:
        raise ValueError("missing top-level on block") from exc
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


def load_json(path: str) -> dict[str, Any]:
    value = json.loads((ROOT / path).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {path}")
    return value


def function_ast(text: str, name: str) -> str:
    module = ast.parse(text)
    for node in module.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.name == name:
            return ast.dump(node, include_attributes=False)
    raise ValueError(f"function not found: {name}")


def api_json(url: str) -> dict[str, Any]:
    token = os.environ.get("GITHUB_TOKEN", "")
    if not token:
        raise ValueError("GITHUB_TOKEN is required for live history verification")
    request = urllib.request.Request(
        url,
        headers={
            "Authorization": f"Bearer {token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "neoeng-cs000h-verifier",
        },
    )
    with urllib.request.urlopen(request, timeout=30) as response:
        value = json.loads(response.read().decode("utf-8"))
    if not isinstance(value, dict):
        raise ValueError("GitHub API response must be object")
    return value


def self_test_errors() -> list[str]:
    errors: list[str] = []
    if "src/core.cpp" in CLOSURE_ALLOWED:
        errors.append("scope negative fixture failed")
    if CANONICAL_WRAPPER not in EXPECTED_TRIGGER_PATHS or OLD_WRAPPER not in EXPECTED_TRIGGER_PATHS:
        errors.append("wrapper trigger fixture failed")
    fixture = "name: x\n\non:\n  pull_request:\n    branches: [main]\n    paths:\n      - 'a'\n      - 'b'\n\n"
    if trigger_paths(fixture) != {"a", "b"}:
        errors.append("workflow path parser fixture failed")
    manual = "name: x\n\non:\n  workflow_dispatch:\n"
    if top_level_on_keys(manual) != {"workflow_dispatch"}:
        errors.append("workflow event parser fixture failed")
    record = {
        "run_id": R8_RUN_ID,
        "run_attempt": 1,
        "source_sha": R8_SOURCE_SHA,
        "failed_step": "Canonical evolution authorizer self-test",
        "product_campaign_executed": False,
    }
    bad = dict(record)
    bad["product_campaign_executed"] = True
    if bad["product_campaign_executed"] is False:
        errors.append("failure-record negative fixture failed")
    return errors


def preservation_errors() -> list[str]:
    errors: list[str] = []
    if blob(OLD_WRAPPER) != OLD_WRAPPER_BLOB:
        errors.append("preserved CS000F wrapper blob mismatch")
    if blob(CANONICAL_WRAPPER) != NEW_WRAPPER_BLOB:
        errors.append("canonical corrected wrapper blob mismatch")
    old = (ROOT / OLD_WRAPPER).read_text(encoding="utf-8")
    new = (ROOT / CANONICAL_WRAPPER).read_text(encoding="utf-8")
    for name in (
        "_load_transition",
        "_cs016e_supersession_is_authoritative",
        "effective_amendments",
        "authorize_state",
        "main",
    ):
        if function_ast(old, name) != function_ast(new, name):
            errors.append(f"authorization semantics changed outside self-test: {name}")
    if "_roadmap_fixture" not in {n.name for n in ast.parse(new).body if isinstance(n, ast.FunctionDef)}:
        errors.append("explicit lifecycle fixture helper missing")
    return errors


def history_errors() -> list[str]:
    errors: list[str] = []
    record = load_json(FAILURE_RECORD)
    expected = {
        "schema": "neoeng.dcore.cs000h-triggering-failure.v1",
        "changeset": "CS000H",
        "triggering_changeset": "CS017",
        "triggering_revision": "R8",
        "classification": "FAILED_NONQUALIFYING_PREPARATION_PREFLIGHT",
        "run_id": R8_RUN_ID,
        "run_attempt": 1,
        "event": "pull_request",
        "workflow_path": R8_WORKFLOW,
        "head_branch": "agent/cs017-ev00-baseline-certification-r8",
        "source_sha": R8_SOURCE_SHA,
        "source_tree": "55c7226c0e4cafc48fd8dacb899f5217587c3e00",
        "job_id": R8_JOB_ID,
        "failed_step": "Canonical evolution authorizer self-test",
        "failed_step_conclusion": "failure",
        "product_campaign_executed": False,
        "stage_operation_executed": False,
        "harness_published": False,
        "local_qualify_executed": False,
        "main_modified_by_r8": False,
        "reusable_as_qualification": False,
        "reclassification_prohibited": True,
    }
    for key, value in expected.items():
        if record.get(key) != value:
            errors.append(f"failure record mismatch: {key}")
    downstream = record.get("downstream_steps")
    expected_downstream = {
        "Authorize EV-00 start state": "skipped",
        "Pre-authorize exact R8 harness publication paths": "skipped",
        "Verify combined stage and ChangeSet control-plane scope": "skipped",
    }
    if downstream != expected_downstream:
        errors.append("failure record downstream-step preservation mismatch")

    repository = os.environ.get("GITHUB_REPOSITORY", "AiltonSantanaReis/NeoEng-D-Core")
    run_doc = api_json(f"https://api.github.com/repos/{repository}/actions/runs/{R8_RUN_ID}")
    live = {
        "id": run_doc.get("id"),
        "run_attempt": run_doc.get("run_attempt"),
        "event": run_doc.get("event"),
        "conclusion": run_doc.get("conclusion"),
        "head_sha": run_doc.get("head_sha"),
        "head_branch": run_doc.get("head_branch"),
        "path": run_doc.get("path"),
    }
    expected_live = {
        "id": R8_RUN_ID,
        "run_attempt": 1,
        "event": "pull_request",
        "conclusion": "failure",
        "head_sha": R8_SOURCE_SHA,
        "head_branch": "agent/cs017-ev00-baseline-certification-r8",
        "path": R8_WORKFLOW,
    }
    if live != expected_live:
        errors.append("live R8 workflow-run binding mismatch")

    jobs = api_json(f"https://api.github.com/repos/{repository}/actions/runs/{R8_RUN_ID}/jobs?per_page=100").get("jobs")
    if not isinstance(jobs, list):
        errors.append("live R8 jobs payload missing")
        return errors
    job = next((row for row in jobs if isinstance(row, dict) and row.get("id") == R8_JOB_ID), None)
    if not isinstance(job, dict):
        errors.append("live R8 failed job not found")
        return errors
    steps = {str(s.get("name")): s.get("conclusion") for s in job.get("steps", []) if isinstance(s, dict)}
    expected_steps = {
        "Verify preparation state and exact current ACTION_SCOPE": "success",
        "Canonical evolution authorizer self-test": "failure",
        "Authorize EV-00 start state": "skipped",
        "Pre-authorize exact R8 harness publication paths": "skipped",
        "Verify combined stage and ChangeSet control-plane scope": "skipped",
    }
    for name, conclusion in expected_steps.items():
        if steps.get(name) != conclusion:
            errors.append(f"live R8 step mismatch: {name}={steps.get(name)!r}")
    return errors


def behavior_errors() -> list[str]:
    errors: list[str] = []
    sys.path.insert(0, str(ROOT / "scripts"))
    import authorize_evolution_action as current
    import authorize_evolution_action_cs000f as prior

    roadmap = current.legacy.load_json(ROOT / current.legacy.ROADMAP)
    amendments = current.legacy.load_json(ROOT / current.legacy.AMENDMENTS)
    policy = current.legacy.load_json(ROOT / current.legacy.POLICY)
    transition = current._load_transition(ROOT)

    def fixture(status: str) -> dict[str, Any]:
        return current._roadmap_fixture(roadmap, status)

    cases = [
        ("preflight-not-started", "preflight", fixture("not_started"), []),
        ("preflight-in-progress", "preflight", fixture("in_progress"), []),
    ]
    for label, action, road, paths in cases:
        cur = current.authorize_state(
            root=ROOT, roadmap=road, amendments=amendments, policy=policy,
            action=action, changeset="CS017", stage="EV-00", paths=paths,
            transition_override=transition)
        old = prior.authorize_state(
            root=ROOT, roadmap=road, amendments=amendments, policy=policy,
            action=action, changeset="CS017", stage="EV-00", paths=paths,
            transition_override=transition)
        if cur != old:
            errors.append(f"runtime authorization result changed: {label}")
    if not current.authorize_state(
        root=ROOT, roadmap=fixture("not_started"), amendments=amendments, policy=policy,
        action="preflight", changeset="CS017", stage="EV-00", paths=[],
        transition_override=transition).get("authorized"):
        errors.append("valid CS016E supersession no longer resolves not_started preflight")
    lifecycle = current.authorize_state(
        root=ROOT, roadmap=fixture("in_progress"), amendments=amendments, policy=policy,
        action="preflight", changeset="CS017", stage="EV-00", paths=[],
        transition_override=transition)
    reasons = lifecycle.get("reasons") if isinstance(lifecycle.get("reasons"), list) else []
    if lifecycle.get("authorized") or not any("preflight requires not_started" in str(x) for x in reasons):
        errors.append("in_progress preflight lifecycle rejection is not preserved")
    if any("required amendments not accepted" in str(x) for x in reasons):
        errors.append("in_progress fixture incorrectly regressed to CS016E blocker")

    proc = run("python3", "scripts/authorize_evolution_action.py", "--self-test")
    if proc.returncode != 0:
        errors.append("canonical lifecycle-independent self-test failed: " + (proc.stdout + proc.stderr).strip())
    return errors


def applicability_errors() -> list[str]:
    errors: list[str] = []
    text = (ROOT / SELF_WORKFLOW).read_text(encoding="utf-8")
    if top_level_on_keys(text) != {"pull_request"}:
        errors.append("CS000H workflow must use only pull_request automatic applicability")
    paths = trigger_paths(text)
    if paths != EXPECTED_TRIGGER_PATHS:
        errors.append("CS000H workflow paths differ from exact bounded set")
    forbidden_future = {
        "audit/CURRENT_CHANGESET_VALIDATION.json",
        "audit/validation/CS000H/VALIDATION_RESULT.json",
        "docs/changesets/017/**",
        "audit/EVOLUTION_ROADMAP.json",
        ".github/workflows/ev00-dlab.yml",
    }
    overlap = sorted(paths & forbidden_future)
    if overlap:
        errors.append("CS000H workflow would trigger on closure/future EV-00 paths: " + ", ".join(overlap))
    return errors


def scope_errors() -> list[str]:
    paths = changed_paths()
    errors: list[str] = []
    extra = sorted(paths - CLOSURE_ALLOWED)
    if extra:
        errors.append("paths outside CS000H scope: " + ", ".join(extra))
    missing = sorted(CANDIDATE_ALLOWED - paths)
    if missing:
        errors.append("required CS000H candidate paths missing: " + ", ".join(missing))
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
        "audit/validation/CS000G/VALIDATION_RESULT.json",
        "scripts/authorize_evolution_action_legacy.py",
    ]
    proc = run("git", "diff", "--name-only", BASE, "--", *protected)
    errors: list[str] = []
    if proc.returncode != 0:
        errors.append("cannot verify protected governance/product bytes")
    elif proc.stdout.strip():
        errors.append("protected state changed: " + ", ".join(proc.stdout.splitlines()))
    roadmap = load_json("audit/EVOLUTION_ROADMAP.json")
    stages = roadmap.get("stages")
    ev00 = next((x for x in stages if isinstance(x, dict) and x.get("stage_id") == "EV-00"), None) if isinstance(stages, list) else None
    if not isinstance(ev00, dict) or ev00.get("status") != "not_started":
        errors.append("EV-00 must remain not_started in CS000H")
    if roadmap.get("release_authorized") is not False:
        errors.append("release_authorized changed")
    return errors


def emit(label: str, errors: list[str]) -> int:
    if errors:
        print(f"CS000H AUTHORIZER SELF-TEST LIFECYCLE: REJECT — {label}")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"CS000H AUTHORIZER SELF-TEST LIFECYCLE: PASS — {label}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--preservation", action="store_true")
    parser.add_argument("--history", action="store_true")
    parser.add_argument("--behavior", action="store_true")
    parser.add_argument("--applicability", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--non-effects", action="store_true")
    args = parser.parse_args()
    selected = sum((
        args.self_test, args.preservation, args.history, args.behavior,
        args.applicability, args.scope, args.non_effects,
    ))
    if selected != 1:
        parser.error("select exactly one check")
    try:
        if args.self_test:
            return emit("verifier-self-test", self_test_errors())
        if args.preservation:
            return emit("wrapper-preservation", preservation_errors())
        if args.history:
            return emit("r8-failure-history", history_errors())
        if args.behavior:
            return emit("authorizer-behavior", behavior_errors())
        if args.applicability:
            return emit("bounded-future-applicability", applicability_errors())
        if args.scope:
            return emit("controlled-scope", scope_errors())
        return emit("non-effects", non_effect_errors())
    except (OSError, RuntimeError, ValueError, urllib.error.URLError) as exc:
        return emit("exception", [str(exc)])


if __name__ == "__main__":
    raise SystemExit(main())
