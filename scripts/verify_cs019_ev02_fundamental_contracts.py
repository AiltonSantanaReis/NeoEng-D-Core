#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]

BASE = "bf3051abdb084273540e6caeb72329eafa0a2eea"
BRANCH = "agent/cs019-ev02-fundamental-contract-hardening"

R1_PLAN_COMMIT = "4b7e2470006486f07c0e027da79e5dd23e13b2f6"
R1_SOURCE = "9a131f35ed3d8d7d4989b23e6382f5eaf8c8f121"
R1_RUN_ID = 32641132546
R1_PLAN_HASH = "ccffe8a55cabe86f5accb854410e652c7adc33fe36c3f8ea7b17961309f64ef9"

WORKFLOW = ".github/workflows/cs019-ev02-fundamental-contract-hardening-validation.yml"
MANIFEST = "MANIFEST.sha256"
DESCRIPTOR = "audit/CURRENT_CHANGESET_VALIDATION.json"
PLAN_R1 = "audit/validation/CS019/VALIDATION_PLAN.json"
ATTEMPT_R1 = "audit/validation/CS019/ATTEMPT_001_NONACCEPTANCE.json"
PLAN_R2 = "audit/validation/CS019/VALIDATION_PLAN_R2.json"
CHANGESET_R1 = "docs/changesets/019/CHANGESET.md"
CHANGESET_R2 = "docs/changesets/019/CHANGESET_R2.md"
DEVIATION = "docs/records/evolution/DEV-0009.md"

R1_SOURCE_SCOPE = {
    WORKFLOW,
    MANIFEST,
    DESCRIPTOR,
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    PLAN_R1,
    CHANGESET_R1,
    "docs/contracts/FUNDAMENTAL_TRANSITION_V1.md",
    "include/neoeng/core/simulation.hpp",
    "modules/host_sdk/tests/host_sdk_tests.cpp",
    "scripts/verify_cs019_ev02_fundamental_contracts.py",
    "src/rollback.cpp",
    "src/simulation.cpp",
    "tests/numeric_closure_tests.cpp",
    "tests/test_main.cpp",
}

R2_NEW = {
    ATTEMPT_R1,
    PLAN_R2,
    CHANGESET_R2,
    DEVIATION,
}

R2_SOURCE_SCOPE = R1_SOURCE_SCOPE | R2_NEW

R2_COMMIT_SCOPE = {
    WORKFLOW,
    MANIFEST,
    DESCRIPTOR,
    ATTEMPT_R1,
    PLAN_R2,
    CHANGESET_R2,
    DEVIATION,
    "scripts/verify_cs019_ev02_fundamental_contracts.py",
}

TRIGGER_SCOPE = R2_SOURCE_SCOPE - {
    MANIFEST,
    DESCRIPTOR,
}

PRODUCT_R1 = {
    "docs/contracts/FUNDAMENTAL_TRANSITION_V1.md",
    "include/neoeng/core/simulation.hpp",
    "src/simulation.cpp",
    "src/rollback.cpp",
    "tests/test_main.cpp",
    "tests/numeric_closure_tests.cpp",
    "modules/host_sdk/tests/host_sdk_tests.cpp",
}

READ_ONLY = {
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
}

EXPECTED_TESTS = (
    ("cs019.r2.verifier-self-test", "CS019 R2 validation",
     "CS019 R2 verifier negative self-test"),
    ("cs019.r2.authority", "CS019 R2 validation",
     "Verify prospective governance authority"),
    ("cs019.r2.planning-ledger", "CS019 R2 validation",
     "Verify EV-02 planning ledger"),
    ("cs019.r1-preservation", "CS019 R2 validation",
     "Verify CS019 R1 nonacceptance preservation"),
    ("cs019.r2.scope", "CS019 R2 validation",
     "Verify exact CS019 R2 source scope"),
    ("cs019.r2.references", "CS019 R2 validation",
     "Verify frozen R1 product and reference surfaces"),
    ("cs019.r2.workflow-contract", "CS019 R2 validation",
     "Verify CS019 R2 workflow contract"),
    ("cs019.r2.non-effects", "CS019 R2 validation",
     "Verify CS019 R2 non-effects"),
    ("build-ci.self-test", "CS019 R2 validation",
     "Build CI governance self-test"),
    ("build-ci.workflow-classification", "CS019 R2 validation",
     "Verify workflow classification"),
    ("build-ci.action-pinning", "CS019 R2 validation",
     "Verify critical action pinning"),
    ("build-ci.cmake-options", "CS019 R2 validation",
     "Verify current CMake options"),
    ("build-ci.regression-contract", "CS019 R2 validation",
     "Verify permanent regression contract"),
    ("build-ci.historical-boundary", "CS019 R2 validation",
     "Verify historical workflow boundary"),
    ("evolution.plan", "CS019 R2 validation",
     "Verify evolution ledger"),
    ("evolution.self-test", "CS019 R2 validation",
     "Verify evolution verifier self-test"),
    ("changeset.policy-self-test", "CS019 R2 validation",
     "Verify ChangeSet policy self-test"),
    ("changeset.r2-plan-structure", "CS019 R2 validation",
     "Verify ChangeSet validation R2 plan"),
    ("repository.manifest", "CS019 R2 validation",
     "Verify tracked-file manifest"),
    ("product.configure", "CS019 R2 validation",
     "Configure primary product regression"),
    ("product.build", "CS019 R2 validation",
     "Build primary product regression"),
    ("cs019.fundamental-core", "CS019 R2 validation",
     "Run fundamental core tests"),
    ("product.smoke", "CS019 R2 validation",
     "Run product smoke regression"),
    ("cs019.gcc-configure", "CS019 R2 cross-compiler negative corpus",
     "Configure GCC fundamental corpus"),
    ("cs019.gcc-build", "CS019 R2 cross-compiler negative corpus",
     "Build GCC fundamental corpus"),
    ("cs019.gcc-negative-corpus", "CS019 R2 cross-compiler negative corpus",
     "Run GCC fundamental rejection corpus"),
    ("cs019.clang-configure", "CS019 R2 cross-compiler negative corpus",
     "Configure Clang fundamental corpus"),
    ("cs019.clang-build", "CS019 R2 cross-compiler negative corpus",
     "Build Clang fundamental corpus"),
    ("cs019.clang-negative-corpus", "CS019 R2 cross-compiler negative corpus",
     "Run Clang fundamental rejection corpus"),
    ("cs019.cross-compiler-equivalence",
     "CS019 R2 cross-compiler negative corpus",
     "Compare cross-compiler rejection corpus"),
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


def git_show(ref: str, rel: str) -> bytes | None:
    proc = run("git", "show", f"{ref}:{rel}", binary=True)
    return proc.stdout if proc.returncode == 0 else None


def changed_paths(ref: str) -> set[str]:
    proc = run("git", "diff", "--name-only", ref, "--")
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

    prospective = transition.get("prospective_authority", {})
    legacy = transition.get("legacy_cs016e", {})

    if prospective.get("regime_id") != "CHANGESET_VALIDATION":
        errors.append("prospective regime mismatch")

    if prospective.get("policy") != "audit/CHANGESET_VALIDATION_POLICY.json":
        errors.append("prospective policy binding mismatch")

    if prospective.get("verifier") != "scripts/verify_changeset_validation.py":
        errors.append("prospective verifier binding mismatch")

    if legacy.get("status") != "SUPERSEDED_UNACCEPTED":
        errors.append("legacy CS016E status mismatch")

    if legacy.get("accepted") is not False:
        errors.append("legacy CS016E was retroactively accepted")

    if policy.get("policy_id") != "NEOENG-DCORE-CHANGESET-VALIDATION-001":
        errors.append("ChangeSet validation policy identity mismatch")

    if policy.get("failure_preservation_required") is not True:
        errors.append("failure preservation is no longer required")

    if policy.get("allow_test_removal_after_execution") is not False:
        errors.append("test removal became allowed")

    return errors


def planning_errors() -> list[str]:
    errors: list[str] = []

    descriptor = load_json(DESCRIPTOR)

    expected_descriptor = {
        "schema": "neoeng.dcore.current-changeset-validation.v1",
        "plan_path": PLAN_R2,
    }

    if descriptor != expected_descriptor:
        errors.append("descriptor is not exact CS019 R2 PLAN_ONLY")

    roadmap = load_json("audit/EVOLUTION_ROADMAP.json")
    requirements = load_json(
        "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json"
    )

    if roadmap.get("current_stage") != "EV-02":
        errors.append("current_stage must remain EV-02")

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
    elif ev02.get("planned_changeset") != "CS019":
        errors.append("EV-02 planned ChangeSet changed")

    if not isinstance(ev03, dict) or ev03.get("status") != "not_started":
        errors.append("EV-03 must remain not_started")

    target = {
        "EVREQ-009",
        "EVREQ-010",
        "EVREQ-011",
        "EVREQ-012",
    }

    found: set[str] = set()

    for row in requirements.get("requirements", []):
        if not isinstance(row, dict):
            continue
        rid = row.get("requirement_id")
        if rid not in target:
            continue

        found.add(rid)

        if row.get("status") != "in_progress":
            errors.append(f"{rid} must remain in_progress")

        if row.get("evidence") != []:
            errors.append(f"{rid} has premature evidence")

    if found != target:
        errors.append("EV-02 requirement identity changed")

    return errors


def r1_preservation_errors() -> list[str]:
    errors: list[str] = []

    plan_bytes = (ROOT / PLAN_R1).read_bytes()

    if hashlib.sha256(plan_bytes).hexdigest() != R1_PLAN_HASH:
        errors.append("R1 plan bytes changed")

    attempt = load_json(ATTEMPT_R1)

    expected = {
        "schema":
            "neoeng.dcore.changeset-validation-attempt-record.v1",
        "changeset": "CS019",
        "attempt": 1,
        "plan_path": PLAN_R1,
        "plan_commit": R1_PLAN_COMMIT,
        "source_sha": R1_SOURCE,
        "workflow_path": WORKFLOW,
        "execution_attempted": True,
        "workflow_run_created": True,
        "run_id": R1_RUN_ID,
        "run_attempt": 1,
        "workflow_conclusion": "success",
        "required_tests_executed": 22,
        "required_tests_total": 22,
        "required_tests_passed": 22,
        "validation_state": "BLOCKED",
        "acceptance_decision": "NOT_ACCEPTED",
        "classification":
            "POST_EXECUTION_PERMANENT_GOVERNANCE_REGRESSION",
        "rerun_attempt1": False,
        "next_plan": PLAN_R2,
    }

    for key, value in expected.items():
        if attempt.get(key) != value:
            errors.append(f"R1 attempt record mismatch: {key}")

    observations = {
        row.get("run_id"): row
        for row in attempt.get("parallel_observations", [])
        if isinstance(row, dict)
    }

    permanent = observations.get(32641132506)
    stale = observations.get(32641132555)
    generic = observations.get(32641132539)

    if not isinstance(permanent, dict):
        errors.append("permanent failed run not preserved")
    else:
        if permanent.get("conclusion") != "failure":
            errors.append("permanent failed run conclusion changed")
        if permanent.get("failed_step") != "Verify workflow classification":
            errors.append("permanent failed step changed")

    if not isinstance(stale, dict):
        errors.append("CS000J stale-scope run not preserved")
    elif stale.get("classification") != "NOT_APPLICABLE_STALE_SCOPE":
        errors.append("CS000J stale-scope classification changed")

    if not isinstance(generic, dict):
        errors.append("generic ChangeSet run not preserved")
    elif generic.get("conclusion") != "success":
        errors.append("generic ChangeSet run conclusion changed")

    ancestry = run(
        "git",
        "merge-base",
        "--is-ancestor",
        R1_SOURCE,
        "HEAD",
    )

    if ancestry.returncode != 0:
        errors.append("R1 source is not ancestor of current HEAD")

    for rel in PRODUCT_R1 | {CHANGESET_R1}:
        prior = git_show(R1_SOURCE, rel)
        current = ROOT / rel

        if prior is None:
            errors.append(f"cannot read R1 preserved path: {rel}")
            continue

        if not current.is_file():
            errors.append(f"R1 preserved path missing: {rel}")
            continue

        if current.read_bytes() != prior:
            errors.append(f"R1 preserved bytes changed: {rel}")

    r1_workflow = git_show(R1_SOURCE, WORKFLOW)

    if r1_workflow is None:
        errors.append("cannot read R1 workflow")
    else:
        text = r1_workflow.decode("utf-8")

        if "pull_request:" not in text:
            errors.append("R1 pull_request trigger history changed")

        if "ref: ${{ github.event.pull_request.head.sha }}" not in text:
            errors.append("R1 exact-head checkout history changed")

    return errors


def reference_errors() -> list[str]:
    errors: list[str] = []

    for rel in PRODUCT_R1 | READ_ONLY:
        prior = git_show(R1_SOURCE, rel)
        current = ROOT / rel

        if prior is None:
            errors.append(f"cannot read path at R1 source: {rel}")
            continue

        if not current.is_file():
            errors.append(f"frozen path missing: {rel}")
            continue

        if current.read_bytes() != prior:
            errors.append(f"path changed after R1 source: {rel}")

    return errors


def workflow_errors() -> list[str]:
    errors: list[str] = []

    text = (ROOT / WORKFLOW).read_text(encoding="utf-8")

    required_tokens = (
        "name: CS019 EV-02 fundamental contract hardening R2",
        "  push:",
        f"branches: [{BRANCH}]",
        "name: CS019 R2 validation",
        "name: CS019 R2 cross-compiler negative corpus",
        "ref: ${{ github.sha }}",
        "actions/checkout@11d5960a326750d5838078e36cf38b85af677262",
    )

    for token in required_tokens:
        if token not in text:
            errors.append(f"R2 workflow token missing: {token}")

    if "pull_request:" in text:
        errors.append("R2 workflow must not use pull_request")

    if "workflow_dispatch:" in text:
        errors.append("R2 workflow must not expose manual dispatch")

    for rel in sorted(TRIGGER_SCOPE):
        if f'- "{rel}"' not in text:
            errors.append(f"R2 trigger path missing: {rel}")

    for rel in (MANIFEST, DESCRIPTOR):
        if f'- "{rel}"' in text:
            errors.append(
                f"lifecycle-mutable result path must not retrigger R2: {rel}"
            )

    plan = load_json(PLAN_R2)
    tests = plan.get("required_tests")

    if not isinstance(tests, list):
        return errors + ["R2 required test inventory missing"]

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
        errors.append("R2 required test inventory/order mismatch")

    for _, _, step in EXPECTED_TESTS:
        if f"- name: {step}" not in text:
            errors.append(f"R2 workflow step missing: {step}")

    return errors


def non_effect_errors() -> list[str]:
    errors: list[str] = []

    if (ROOT / "audit/validation/CS019/VALIDATION_RESULT.json").exists():
        errors.append("CS019 result exists before R2 qualification")

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

    errors.extend(reference_errors())

    return errors


def self_test() -> list[str]:
    failures: list[str] = []

    if scope_errors(set(R2_COMMIT_SCOPE), set(R2_COMMIT_SCOPE)):
        failures.append("valid R2 commit-scope fixture rejected")

    bad = set(R2_COMMIT_SCOPE)
    bad.add("src/forbidden.cpp")

    if not scope_errors(bad, set(R2_COMMIT_SCOPE)):
        failures.append("extra R2 commit path fixture accepted")

    if scope_errors(set(R2_SOURCE_SCOPE), set(R2_SOURCE_SCOPE)):
        failures.append("valid R2 cumulative-scope fixture rejected")

    if MANIFEST in TRIGGER_SCOPE:
        failures.append("MANIFEST incorrectly retriggers R2")

    if DESCRIPTOR in TRIGGER_SCOPE:
        failures.append("descriptor incorrectly retriggers R2")

    if len(EXPECTED_TESTS) != 30:
        failures.append("R2 test inventory cardinality changed")

    return failures


def emit(label: str, errors: list[str]) -> int:
    if errors:
        print(f"CS019 R2 VERIFIER: REJECT — {label}")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"CS019 R2 VERIFIER: PASS — {label}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--authority", action="store_true")
    parser.add_argument("--planning", action="store_true")
    parser.add_argument("--r1-preservation", action="store_true")
    parser.add_argument("--r2-plan-scope", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--references", action="store_true")
    parser.add_argument("--workflow", action="store_true")
    parser.add_argument("--non-effects", action="store_true")

    args = parser.parse_args()

    selected = (
        args.self_test,
        args.authority,
        args.planning,
        args.r1_preservation,
        args.r2_plan_scope,
        args.scope,
        args.references,
        args.workflow,
        args.non_effects,
    )

    if sum(selected) != 1:
        parser.error("select exactly one verification mode")

    try:
        if args.self_test:
            return emit("self-test", self_test())

        if args.authority:
            return emit("prospective-authority", authority_errors())

        if args.planning:
            return emit("planning-ledger", planning_errors())

        if args.r1_preservation:
            return emit(
                "r1-nonacceptance-preservation",
                r1_preservation_errors(),
            )

        if args.r2_plan_scope:
            return emit(
                "r2-corrective-commit-scope",
                scope_errors(
                    changed_paths(R1_SOURCE),
                    set(R2_COMMIT_SCOPE),
                ),
            )

        if args.scope:
            return emit(
                "r2-cumulative-source-scope",
                scope_errors(
                    changed_paths(BASE),
                    set(R2_SOURCE_SCOPE),
                ),
            )

        if args.references:
            return emit(
                "r1-product-reference-preservation",
                reference_errors(),
            )

        if args.workflow:
            return emit(
                "r2-workflow-contract",
                workflow_errors(),
            )

        return emit(
            "r2-non-effects",
            non_effect_errors(),
        )

    except (
        OSError,
        ValueError,
        RuntimeError,
        json.JSONDecodeError,
    ) as exc:
        return emit("exception", [str(exc)])


if __name__ == "__main__":
    raise SystemExit(main())
