#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
BASE = "bf3051abdb084273540e6caeb72329eafa0a2eea"

WORKFLOW = ".github/workflows/cs019-ev02-fundamental-contract-hardening-validation.yml"
PLAN = "audit/validation/CS019/VALIDATION_PLAN.json"

CONTROL = {
    WORKFLOW,
    "MANIFEST.sha256",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    PLAN,
    "docs/changesets/019/CHANGESET.md",
    "scripts/verify_cs019_ev02_fundamental_contracts.py",
}

SEMANTIC = {
    "docs/contracts/FUNDAMENTAL_TRANSITION_V1.md",
    "include/neoeng/core/simulation.hpp",
    "src/simulation.cpp",
    "src/rollback.cpp",
    "tests/test_main.cpp",
    "tests/numeric_closure_tests.cpp",
    "modules/host_sdk/tests/host_sdk_tests.cpp",
}

SOURCE_SCOPE = CONTROL | SEMANTIC

REFERENCES = (
    "include/neoeng/core/types.hpp",
    "include/neoeng/core/fixed.hpp",
    "include/neoeng/core/numeric_contract.hpp",
    "src/numeric_contract.cpp",
    "modules/host_sdk/include/neoeng/dcore_host.h",
    "modules/host_sdk/src/dcore_host.cpp",
    "modules/host_sdk/CMakeLists.txt",
    "docs/contracts/NUMERIC_CLOSURE_V1.md",
    "docs/contracts/TEMPORAL_CLOSURE_V1.md",
    "docs/contracts/HOST_SDK_C_ABI_V1.md",
    "CMakeLists.txt",
)

EXPECTED_TESTS = (
    ("cs019.verifier-self-test", "CS019 validation", "CS019 verifier negative self-test"),
    ("cs019.authority", "CS019 validation", "Verify prospective governance authority"),
    ("cs019.planning-ledger", "CS019 validation", "Verify EV-02 planning ledger"),
    ("cs019.scope", "CS019 validation", "Verify exact CS019 source scope"),
    ("cs019.references", "CS019 validation", "Verify frozen reference surfaces"),
    ("cs019.workflow-contract", "CS019 validation", "Verify CS019 workflow contract"),
    ("cs019.non-effects", "CS019 validation", "Verify CS019 non-effects"),
    ("evolution.plan", "CS019 validation", "Verify evolution ledger"),
    ("changeset.policy-self-test", "CS019 validation", "Verify ChangeSet policy self-test"),
    ("changeset.plan-structure", "CS019 validation", "Verify ChangeSet validation plan"),
    ("repository.manifest", "CS019 validation", "Verify tracked-file manifest"),
    ("product.configure", "CS019 validation", "Configure primary product regression"),
    ("product.build", "CS019 validation", "Build primary product regression"),
    ("cs019.fundamental-core", "CS019 validation", "Run fundamental core tests"),
    ("product.smoke", "CS019 validation", "Run product smoke regression"),
    ("cs019.gcc-configure", "CS019 cross-compiler negative corpus", "Configure GCC fundamental corpus"),
    ("cs019.gcc-build", "CS019 cross-compiler negative corpus", "Build GCC fundamental corpus"),
    ("cs019.gcc-negative-corpus", "CS019 cross-compiler negative corpus", "Run GCC fundamental rejection corpus"),
    ("cs019.clang-configure", "CS019 cross-compiler negative corpus", "Configure Clang fundamental corpus"),
    ("cs019.clang-build", "CS019 cross-compiler negative corpus", "Build Clang fundamental corpus"),
    ("cs019.clang-negative-corpus", "CS019 cross-compiler negative corpus", "Run Clang fundamental rejection corpus"),
    ("cs019.cross-compiler-equivalence", "CS019 cross-compiler negative corpus", "Compare cross-compiler rejection corpus"),
)

def run(*args: str, binary: bool = False):
    return subprocess.run(
        args,
        cwd=ROOT,
        capture_output=True,
        text=not binary,
        check=False,
    )

def load_json(rel: str) -> dict[str, Any]:
    value = json.loads((ROOT / rel).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {rel}")
    return value

def changed_paths() -> set[str]:
    proc = run("git", "diff", "--name-only", BASE, "--")
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "git diff failed")
    return {
        line.strip()
        for line in proc.stdout.splitlines()
        if line.strip()
    }

def scope_errors(actual: set[str], expected: set[str]) -> list[str]:
    errors: list[str] = []
    extra = sorted(actual - expected)
    missing = sorted(expected - actual)
    if extra:
        errors.append("paths outside exact scope: " + ", ".join(extra))
    if missing:
        errors.append("required scope paths missing: " + ", ".join(missing))
    return errors

def authority_errors() -> list[str]:
    errors: list[str] = []

    transition = load_json("audit/GOVERNANCE_TRANSITION_STATE.json")
    policy = load_json("audit/CHANGESET_VALIDATION_POLICY.json")
    index = load_json("audit/SOURCE_OF_TRUTH_INDEX.json")
    maxima = load_json("audit/STAGE_SCOPE_MAXIMA.json")

    prospective = transition.get("prospective_authority", {})
    legacy = transition.get("legacy_cs016e", {})

    if transition.get("schema") != "neoeng.dcore.governance-transition-state.v1":
        errors.append("transition schema mismatch")
    if transition.get("changeset") != "CS000E":
        errors.append("transition changeset mismatch")
    if prospective.get("regime_id") != "CHANGESET_VALIDATION":
        errors.append("prospective regime mismatch")
    if prospective.get("policy") != "audit/CHANGESET_VALIDATION_POLICY.json":
        errors.append("prospective policy binding mismatch")
    if prospective.get("verifier") != "scripts/verify_changeset_validation.py":
        errors.append("prospective verifier binding mismatch")

    if legacy.get("status") != "SUPERSEDED_UNACCEPTED":
        errors.append("CS016E historical status mismatch")
    if legacy.get("accepted") is not False:
        errors.append("CS016E was retroactively accepted")
    if legacy.get("accepted_source_commit") is not None:
        errors.append("CS016E accepted source was invented")
    if legacy.get("evidence_manifest") is not None:
        errors.append("CS016E evidence was invented")

    if policy.get("schema") != "neoeng.dcore.changeset-validation-policy.v1":
        errors.append("ChangeSet policy schema mismatch")
    if policy.get("policy_id") != "NEOENG-DCORE-CHANGESET-VALIDATION-001":
        errors.append("ChangeSet policy identity mismatch")

    precedence = index.get("precedence")
    if not isinstance(precedence, list):
        errors.append("source-of-truth precedence missing")
    else:
        transition_path = "audit/GOVERNANCE_TRANSITION_STATE.json"
        scope_path = "audit/STAGE_SCOPE_MAXIMA.json"
        if transition_path not in precedence or scope_path not in precedence:
            errors.append("authority paths absent from precedence")
        elif precedence.index(transition_path) >= precedence.index(scope_path):
            errors.append("transition does not precede legacy scope ledger")

    if "EV-02" not in maxima.get("undefined_stages", []):
        errors.append("historical EV-02 legacy scope entry changed")

    return errors

def planning_errors() -> list[str]:
    errors: list[str] = []

    roadmap = load_json("audit/EVOLUTION_ROADMAP.json")
    requirements = load_json("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json")
    descriptor = load_json("audit/CURRENT_CHANGESET_VALIDATION.json")

    if roadmap.get("current_stage") != "EV-02":
        errors.append("current_stage must be EV-02")
    if roadmap.get("release_authorized") is not False:
        errors.append("release_authorized changed")

    stages = {
        row.get("stage_id"): row
        for row in roadmap.get("stages", [])
        if isinstance(row, dict)
    }

    ev01 = stages.get("EV-01")
    ev02 = stages.get("EV-02")
    ev03 = stages.get("EV-03")

    if not isinstance(ev01, dict) or ev01.get("status") != "accepted":
        errors.append("EV-01 is not accepted")
    if not isinstance(ev02, dict) or ev02.get("status") != "in_progress":
        errors.append("EV-02 is not in_progress")
    else:
        if ev02.get("depends_on") != ["EV-01"]:
            errors.append("EV-02 dependency mismatch")
        if ev02.get("planned_changeset") != "CS019":
            errors.append("EV-02 planned ChangeSet mismatch")
        for key in ("accepted_commit", "evidence_manifest", "decision_record"):
            if ev02.get(key) is not None:
                errors.append(f"EV-02 premature acceptance binding: {key}")

    if not isinstance(ev03, dict) or ev03.get("status") != "not_started":
        errors.append("EV-03 must remain not_started")

    in_progress = [
        row.get("stage_id")
        for row in roadmap.get("stages", [])
        if isinstance(row, dict) and row.get("status") == "in_progress"
    ]
    if in_progress != ["EV-02"]:
        errors.append(f"exact in_progress stage mismatch: {in_progress!r}")

    expected_ids = {"EVREQ-009", "EVREQ-010", "EVREQ-011", "EVREQ-012"}
    actual_ids: set[str] = set()

    for row in requirements.get("requirements", []):
        if not isinstance(row, dict) or row.get("stage") != "EV-02":
            continue
        rid = row.get("requirement_id")
        if isinstance(rid, str):
            actual_ids.add(rid)
        if row.get("status") != "in_progress":
            errors.append(f"{rid} is not in_progress")
        if row.get("evidence") != []:
            errors.append(f"{rid} has premature evidence")

    if actual_ids != expected_ids:
        errors.append(
            "EV-02 requirement identity mismatch: "
            + ", ".join(sorted(actual_ids))
        )

    expected_descriptor = {
        "schema": "neoeng.dcore.current-changeset-validation.v1",
        "plan_path": PLAN,
    }
    if descriptor != expected_descriptor:
        errors.append("descriptor is not exact CS019 PLAN_ONLY")

    return errors

def reference_errors() -> list[str]:
    errors: list[str] = []
    for rel in REFERENCES:
        prior = run("git", "show", f"{BASE}:{rel}", binary=True)
        current = ROOT / rel
        if prior.returncode != 0:
            errors.append(f"cannot read reference at base: {rel}")
            continue
        if not current.is_file():
            errors.append(f"reference missing: {rel}")
            continue
        if prior.stdout != current.read_bytes():
            errors.append(f"read-only reference changed: {rel}")
    return errors

def workflow_errors() -> list[str]:
    errors: list[str] = []

    workflow_path = ROOT / WORKFLOW
    if not workflow_path.is_file():
        return ["CS019 workflow missing"]

    text = workflow_path.read_text(encoding="utf-8")

    required_tokens = [
        "name: CS019 EV-02 fundamental contract hardening",
        "pull_request:",
        "branches: [main]",
        "permissions:",
        "contents: read",
        "actions/checkout@11d5960a326750d5838078e36cf38b85af677262",
        "ref: ${{ github.event.pull_request.head.sha }}",
        "name: CS019 validation",
        "name: CS019 cross-compiler negative corpus",
    ]

    for token in required_tokens:
        if token not in text:
            errors.append(f"workflow token missing: {token}")

    if "workflow_dispatch:" in text:
        errors.append("CS019 qualifying workflow must not expose manual dispatch")

    for rel in sorted(SOURCE_SCOPE):
        token = f'- "{rel}"'
        if token not in text:
            errors.append(f"workflow path trigger missing: {rel}")

    for _, _, step in EXPECTED_TESTS:
        if f"- name: {step}" not in text:
            errors.append(f"required workflow step missing: {step}")

    plan = load_json(PLAN)
    tests = plan.get("required_tests")
    if not isinstance(tests, list):
        return errors + ["plan required_tests missing"]

    actual = tuple(
        (
            row.get("test_id"),
            row.get("evidence_job"),
            row.get("evidence_step"),
        )
        for row in tests
        if isinstance(row, dict)
    )

    if actual != EXPECTED_TESTS:
        errors.append("plan required_tests inventory differs from frozen workflow contract")

    return errors

def non_effect_errors() -> list[str]:
    errors: list[str] = []

    roadmap = load_json("audit/EVOLUTION_ROADMAP.json")
    if roadmap.get("release_authorized") is not False:
        errors.append("release became authorized")

    stages = {
        row.get("stage_id"): row
        for row in roadmap.get("stages", [])
        if isinstance(row, dict)
    }
    ev03 = stages.get("EV-03")
    if not isinstance(ev03, dict) or ev03.get("status") != "not_started":
        errors.append("EV-03 was started")

    result = ROOT / "audit/validation/CS019/VALIDATION_RESULT.json"
    if result.exists():
        errors.append("CS019 result exists before qualifying execution")

    errors.extend(reference_errors())
    return errors

def self_test() -> list[str]:
    failures: list[str] = []

    if scope_errors(set(CONTROL), set(CONTROL)):
        failures.append("valid plan scope fixture rejected")

    extra = set(CONTROL)
    extra.add("src/forbidden.cpp")
    if not scope_errors(extra, set(CONTROL)):
        failures.append("extra plan path fixture accepted")

    missing = set(CONTROL)
    missing.remove(WORKFLOW)
    if not scope_errors(missing, set(CONTROL)):
        failures.append("missing plan path fixture accepted")

    if scope_errors(set(SOURCE_SCOPE), set(SOURCE_SCOPE)):
        failures.append("valid source scope fixture rejected")

    source_extra = set(SOURCE_SCOPE)
    source_extra.add("CMakeLists.txt")
    if not scope_errors(source_extra, set(SOURCE_SCOPE)):
        failures.append("source scope expansion fixture accepted")

    return failures

def emit(label: str, errors: list[str]) -> int:
    if errors:
        print(f"CS019 FUNDAMENTAL CONTRACT VERIFIER: REJECT — {label}")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"CS019 FUNDAMENTAL CONTRACT VERIFIER: PASS — {label}")
    return 0

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--authority", action="store_true")
    parser.add_argument("--planning", action="store_true")
    parser.add_argument("--plan-scope", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--references", action="store_true")
    parser.add_argument("--workflow", action="store_true")
    parser.add_argument("--non-effects", action="store_true")
    args = parser.parse_args()

    selected = sum(
        (
            args.self_test,
            args.authority,
            args.planning,
            args.plan_scope,
            args.scope,
            args.references,
            args.workflow,
            args.non_effects,
        )
    )
    if selected != 1:
        parser.error("select exactly one verification mode")

    try:
        if args.self_test:
            return emit("self-test", self_test())
        if args.authority:
            return emit("prospective-authority", authority_errors())
        if args.planning:
            return emit("planning-ledger", planning_errors())
        if args.plan_scope:
            return emit(
                "plan-only-scope",
                scope_errors(changed_paths(), set(CONTROL)),
            )
        if args.scope:
            return emit(
                "implemented-source-scope",
                scope_errors(changed_paths(), set(SOURCE_SCOPE)),
            )
        if args.references:
            return emit("frozen-references", reference_errors())
        if args.workflow:
            return emit("workflow-contract", workflow_errors())
        return emit("non-effects", non_effect_errors())
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        return emit("exception", [str(exc)])

if __name__ == "__main__":
    raise SystemExit(main())
