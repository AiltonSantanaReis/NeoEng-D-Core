#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable

ROOT = Path(__file__).resolve().parents[1]
BASE = "9e5c00faa4db0868da48913b8ffa24e0f64972e2"
BRANCH = "agent/cs021-ev04-property-model-testing"
WORKFLOW = Path(".github/workflows/cs021-ev04-property-model-testing-validation.yml")
PLAN = Path("audit/validation/CS021/VALIDATION_PLAN.json")
CHANGESET = Path("docs/changesets/021/CHANGESET.md")
TEST_SOURCE = Path("tests/property_model_tests.cpp")
CURRENT = Path("audit/CURRENT_CHANGESET_VALIDATION.json")
ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
TRACE = Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json")
STAGE_SCOPE = Path("audit/STAGE_SCOPE_MAXIMA.json")
TRANSITION = Path("audit/GOVERNANCE_TRANSITION_STATE.json")
CS000M_RESULT = Path("audit/validation/CS000M/VALIDATION_RESULT.json")
CS000L_RESULT = Path("audit/validation/CS000L/VALIDATION_RESULT.json")
CS020_RESULT = Path("audit/validation/CS020/VALIDATION_RESULT.json")
RESULT = Path("audit/validation/CS021/VALIDATION_RESULT.json")

SOURCE_SCOPE = {
    ".github/workflows/cs021-ev04-property-model-testing-validation.yml",
    "CMakeLists.txt",
    "MANIFEST.sha256",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/validation/CS021/VALIDATION_PLAN.json",
    "docs/changesets/021/CHANGESET.md",
    "scripts/verify_cs021_ev04_property_model_testing.py",
    "tests/property_model_tests.cpp",
}

TRIGGER_SCOPE = {
    ".github/workflows/cs021-ev04-property-model-testing-validation.yml",
    "CMakeLists.txt",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/validation/CS021/VALIDATION_PLAN.json",
    "docs/changesets/021/CHANGESET.md",
    "scripts/verify_cs021_ev04_property_model_testing.py",
    "tests/property_model_tests.cpp",
}

EXPECTED_ALLOWED = [
    ".github/workflows/cs021-ev04-property-model-testing-validation.yml",
    "CMakeLists.txt",
    "MANIFEST.sha256",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/validation/CS021/**",
    "docs/changesets/021/**",
    "scripts/verify_cs021_ev04_property_model_testing.py",
    "tests/property_model_tests.cpp",
]

EXPECTED_FORBIDDEN = [
    "src/**",
    "include/**",
    "modules/**",
    "apps/**",
    "cmake/**",
    "docs/governance/**",
    "audit/SOURCE_OF_TRUTH_INDEX.json",
    "audit/STAGE_SCOPE_MAXIMA.json",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/EVOLUTION_INVARIANTS.json",
    ".github/workflows/changeset-validation.yml",
    ".github/workflows/current-product-regression.yml",
    "scripts/generate_manifest.py",
    "scripts/verify_changeset_validation.py",
    "scripts/verify_evolution_plan.py",
    "tests/golden/ev03/v1/**",
    "docs/contracts/GOLDEN_CORPUS_V1.md",
    "audit/validation/CS000L/**",
    "docs/changesets/000L/**",
]

GOLDEN_FILES = [
    "docs/contracts/GOLDEN_CORPUS_V1.md",
    "tests/golden/ev03/v1/corpus.json",
    "tests/golden/ev03/v1/evidence_envelope.bin",
    "tests/golden/ev03/v1/manifest.json",
    "tests/golden/ev03/v1/world_after_rollback_replay.bin",
    "tests/golden/ev03/v1/world_after_transition.bin",
    "tests/golden/ev03/v1/world_initial.bin",
]

RUNTIME_REFERENCE_FILES = [
    "include/neoeng/core/fixed.hpp",
    "include/neoeng/core/hash.hpp",
    "include/neoeng/core/rollback.hpp",
    "include/neoeng/core/simulation.hpp",
    "include/neoeng/core/snapshot_store.hpp",
    "include/neoeng/core/types.hpp",
    "src/hash.cpp",
    "src/rollback.cpp",
    "src/simulation.cpp",
    "src/snapshot_store.cpp",
    "tests/golden_corpus_tests.cpp",
]

FORBIDDEN_PREFIXES = (
    "src/",
    "include/",
    "modules/",
    "apps/",
    "cmake/",
    "docs/governance/",
    "tests/golden/ev03/v1/",
)

FORBIDDEN_EXACT = {
    "audit/SOURCE_OF_TRUTH_INDEX.json",
    "audit/STAGE_SCOPE_MAXIMA.json",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/EVOLUTION_INVARIANTS.json",
    ".github/workflows/changeset-validation.yml",
    ".github/workflows/current-product-regression.yml",
    "scripts/generate_manifest.py",
    "scripts/verify_changeset_validation.py",
    "scripts/verify_evolution_plan.py",
    "docs/contracts/GOLDEN_CORPUS_V1.md",
}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read_json(path: Path) -> Any:
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


def git(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    completed = subprocess.run(
        ["git", *args],
        cwd=ROOT,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if check and completed.returncode != 0:
        raise AssertionError(
            f"git {' '.join(args)} failed: {completed.stderr.strip()}"
        )
    return completed


def git_output(*args: str) -> str:
    return git(*args).stdout.strip()


def paths_between_base_and_head() -> set[str]:
    text = git_output("diff", "--name-only", f"{BASE}..HEAD")
    return {line for line in text.splitlines() if line}


def working_tracked_paths() -> set[str]:
    paths: set[str] = set()
    for args in (
        ("diff", "--name-only"),
        ("diff", "--cached", "--name-only"),
    ):
        text = git_output(*args)
        paths.update(line for line in text.splitlines() if line)
    return paths


def require_exact_set(actual: Iterable[str], expected: set[str], label: str) -> None:
    actual_set = set(actual)
    require(
        actual_set == expected,
        f"{label} mismatch: expected={sorted(expected)!r} actual={sorted(actual_set)!r}",
    )


def stage_by_id(roadmap: dict[str, Any], stage_id: str) -> dict[str, Any]:
    matches = [item for item in roadmap["stages"] if item["stage_id"] == stage_id]
    require(len(matches) == 1, f"stage cardinality mismatch: {stage_id}")
    return matches[0]


def requirement_by_id(trace: dict[str, Any], requirement_id: str) -> dict[str, Any]:
    matches = [
        item for item in trace["requirements"]
        if item["requirement_id"] == requirement_id
    ]
    require(len(matches) == 1, f"requirement cardinality mismatch: {requirement_id}")
    return matches[0]


def scope_row() -> dict[str, Any]:
    scope = read_json(STAGE_SCOPE)
    matches = [item for item in scope["stages"] if item["stage_id"] == "EV-04"]
    require(len(matches) == 1, "EV-04 stage-scope row cardinality mismatch")
    return matches[0]


def require_paths_unchanged(paths: list[str], label: str) -> None:
    head = git_output("rev-parse", "HEAD")
    if head != BASE:
        completed = git("diff", "--quiet", BASE, "HEAD", "--", *paths, check=False)
        require(completed.returncode == 0, f"{label} changed in committed candidate")

    completed = git("diff", "--quiet", "--", *paths, check=False)
    require(completed.returncode == 0, f"{label} changed in worktree")

    completed = git("diff", "--cached", "--quiet", "--", *paths, check=False)
    require(completed.returncode == 0, f"{label} changed in index")


def verify_self_test() -> None:
    caught = False
    try:
        require_exact_set({"a"}, {"a", "b"}, "negative sentinel")
    except AssertionError:
        caught = True
    require(caught, "negative exact-set self-test did not reject mismatch")

    caught = False
    try:
        require(False, "negative require sentinel")
    except AssertionError:
        caught = True
    require(caught, "negative require self-test did not reject false condition")

    print("CS021 verifier self-test: PASS")


def verify_authority() -> None:
    transition = read_json(TRANSITION)
    authority = transition["prospective_authority"]
    require(authority["regime_id"] == "CHANGESET_VALIDATION", "wrong prospective regime")
    require(
        authority["required_branch_check"] == "Trusted ChangeSet validation gate",
        "wrong trusted branch check",
    )
    require(authority["required_branch_check_app_id"] == 15368, "wrong trusted app id")

    scope = read_json(STAGE_SCOPE)
    require(scope["default_policy"] == "undefined_stage_scope_is_REJECT", "wrong scope default")
    require(scope["scope_patterns_must_be_literal_members_of_maximum"] is True, "scope literal rule disabled")
    require("EV-04" not in scope["undefined_stages"], "EV-04 remains undefined")

    row = scope_row()
    require(row["planned_changeset"] == "CS021", "EV-04 planned changeset mismatch")
    require(row["status"] == "defined", "EV-04 maximum not defined")
    require(row["preparation_allowed_patterns"] == EXPECTED_ALLOWED, "EV-04 preparation maximum mismatch")
    require(row["allowed_patterns"] == EXPECTED_ALLOWED, "EV-04 allowed maximum mismatch")
    require(row["mandatory_forbidden_patterns"] == EXPECTED_FORBIDDEN, "EV-04 forbidden maximum mismatch")

    cs000m = read_json(CS000M_RESULT)
    require(cs000m["validation_state"] == "VALIDATED", "CS000M not validated")
    require(cs000m["acceptance_decision"] == "ACCEPTED", "CS000M not accepted")
    require(cs000m["effects"]["stage_scope"] == "EV04_DEFINED", "CS000M did not define EV-04")

    require_paths_unchanged(
        [
            "audit/STAGE_SCOPE_MAXIMA.json",
            "audit/SOURCE_OF_TRUTH_INDEX.json",
            "audit/GOVERNANCE_TRANSITION_STATE.json",
            "audit/CHANGESET_VALIDATION_POLICY.json",
        ],
        "governance authority",
    )

    print("CS021 authority: PASS")


def verify_planning() -> None:
    roadmap = read_json(ROADMAP)
    trace = read_json(TRACE)

    require(roadmap["program_state"] == "active", "program not active")
    require(roadmap["current_stage"] == "EV-04", "current_stage is not EV-04")
    require(roadmap["release_authorized"] is False, "release unexpectedly authorized")

    ev03 = stage_by_id(roadmap, "EV-03")
    ev04 = stage_by_id(roadmap, "EV-04")
    ev05 = stage_by_id(roadmap, "EV-05")

    require(ev03["status"] == "accepted", "EV-03 predecessor not accepted")
    require(ev04["status"] == "in_progress", "EV-04 not in progress")
    require(ev04["planned_changeset"] == "CS021", "EV-04 changeset mismatch")
    require(ev04["accepted_commit"] is None, "EV-04 accepted commit set prematurely")
    require(ev04["evidence_manifest"] is None, "EV-04 evidence set prematurely")
    require(ev04["decision_record"] is None, "EV-04 decision set prematurely")
    require(ev05["status"] == "not_started", "EV-05 started prematurely")

    for requirement_id in ("EVREQ-013", "EVREQ-014", "EVREQ-015"):
        requirement = requirement_by_id(trace, requirement_id)
        require(requirement["status"] == "verified", f"{requirement_id} predecessor not verified")

    for requirement_id in ("EVREQ-016", "EVREQ-017", "EVREQ-018"):
        requirement = requirement_by_id(trace, requirement_id)
        require(requirement["status"] == "in_progress", f"{requirement_id} not in progress")
        require(requirement["evidence"] == [], f"{requirement_id} evidence set before qualification")

    for requirement_id in ("EVREQ-019", "EVREQ-020", "EVREQ-021"):
        requirement = requirement_by_id(trace, requirement_id)
        require(requirement["status"] == "planned", f"{requirement_id} advanced prematurely")
        require(requirement["evidence"] == [], f"{requirement_id} evidence set prematurely")

    print("CS021 planning ledger: PASS")


def verify_predecessor() -> None:
    roadmap = read_json(ROADMAP)
    ev03 = stage_by_id(roadmap, "EV-03")
    require(ev03["status"] == "accepted", "EV-03 not accepted")
    require(
        ev03["accepted_commit"] == "0adf4721ebefb77723e2c59ee042f35fb291854a",
        "EV-03 accepted commit changed",
    )

    cs020 = read_json(CS020_RESULT)
    require(cs020["validation_state"] == "VALIDATED", "CS020 validation lost")
    require(cs020["acceptance_decision"] == "ACCEPTED", "CS020 acceptance lost")

    cs000l = read_json(CS000L_RESULT)
    require(cs000l["validation_state"] == "VALIDATED", "CS000L validation lost")
    require(cs000l["acceptance_decision"] == "ACCEPTED", "CS000L acceptance lost")

    cs000m = read_json(CS000M_RESULT)
    require(cs000m["validation_state"] == "VALIDATED", "CS000M validation lost")
    require(cs000m["acceptance_decision"] == "ACCEPTED", "CS000M acceptance lost")

    print("CS021 predecessor preservation: PASS")


def verify_scope() -> None:
    head = git_output("rev-parse", "HEAD")
    require(head != BASE, "scope verification requires committed source candidate")
    require(git_output("rev-list", "--count", f"{BASE}..HEAD") == "1", "source candidate must be exactly one commit")

    topology = git_output("rev-list", "--parents", "-n", "1", "HEAD").split()
    require(len(topology) == 2, "source candidate parent cardinality mismatch")
    require(topology[0] == head, "source candidate topology identity mismatch")
    require(topology[1] == BASE, "source candidate parent is not exact base")

    github_ref = os.environ.get("GITHUB_REF")
    if github_ref:
        require(github_ref == f"refs/heads/{BRANCH}", "workflow executed on wrong branch")

    require_exact_set(paths_between_base_and_head(), SOURCE_SCOPE, "CS021 source scope")
    require(not (ROOT / RESULT).exists(), "validation result exists before qualification")

    print("CS021 exact source scope: PASS")


def verify_golden() -> None:
    require_paths_unchanged(GOLDEN_FILES, "EV-03 golden corpus")
    require_paths_unchanged(RUNTIME_REFERENCE_FILES, "runtime/reference surfaces")
    print("CS021 golden/runtime preservation: PASS")


def verify_descriptor() -> None:
    descriptor = read_json(CURRENT)
    require(
        descriptor == {
            "schema": "neoeng.dcore.current-changeset-validation.v1",
            "plan_path": "audit/validation/CS021/VALIDATION_PLAN.json",
        },
        "CS021 source descriptor is not exact plan-only geometry",
    )
    require(not (ROOT / RESULT).exists(), "CS021 result exists in source phase")
    print("CS021 descriptor: PASS")


def workflow_trigger_paths() -> set[str]:
    text = (ROOT / WORKFLOW).read_text(encoding="utf-8")
    match = re.search(r"    paths:\n((?:      - .+\n)+)", text)
    require(match is not None, "workflow paths block missing")
    paths: set[str] = set()
    for line in match.group(1).splitlines():
        value = line.split("-", 1)[1].strip().strip("'\"")
        paths.add(value)
    return paths


def verify_registration() -> None:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    test_source = (ROOT / TEST_SOURCE).read_text(encoding="utf-8")
    workflow = (ROOT / WORKFLOW).read_text(encoding="utf-8")
    plan = read_json(PLAN)

    for snippet in (
        "add_executable(neoeng_property_model_tests tests/property_model_tests.cpp)",
        "target_link_libraries(neoeng_property_model_tests PRIVATE neoeng_dcore)",
        "add_test(NAME neoeng_property_model_tests COMMAND neoeng_property_model_tests)",
        'PROPERTIES LABELS "smoke;dcore;ev04;property-model"',
    ):
        require(cmake.count(snippet) == 1, f"CMake registration mismatch: {snippet}")

    required_tokens = (
        "0xC5021E0400000001ULL",
        "reference_step",
        "run_overflow_sensitive_order_property",
        "SnapshotStrategy::FullCopy",
        "SnapshotStrategy::DeltaLog",
        "SnapshotStrategy::PagedCopyOnWrite",
        "SnapshotStrategy::PersistentChunkTree",
        "SnapshotStrategy::ComponentSoA",
        "SnapshotStrategy::HybridAdaptive",
        "serialize=covered",
        "deserialize=not_applicable",
    )
    for token in required_tokens:
        require(token in test_source, f"property/model test token missing: {token}")

    lowered = test_source.lower()
    for forbidden_dependency in ("rapidcheck", "catch2", "hypothesis", "boost/test"):
        require(forbidden_dependency not in lowered, f"unexpected external property framework: {forbidden_dependency}")

    require(f"      - {BRANCH}" in workflow, "workflow branch trigger mismatch")
    require_exact_set(workflow_trigger_paths(), TRIGGER_SCOPE, "workflow trigger scope")
    require("MANIFEST.sha256" not in workflow_trigger_paths(), "manifest must not trigger qualification")
    require("audit/CURRENT_CHANGESET_VALIDATION.json" not in workflow_trigger_paths(), "descriptor must not trigger qualification")
    require("audit/validation/CS021/VALIDATION_RESULT.json" not in workflow_trigger_paths(), "result must not trigger qualification")

    require(plan["changeset"] == "CS021", "validation plan changeset mismatch")
    require(plan["base_sha"] == BASE, "validation plan base mismatch")
    require(plan["execution_workflow"] == WORKFLOW.as_posix(), "validation plan workflow mismatch")
    require(len(plan["required_tests"]) == 26, "validation plan must contain exactly 26 tests")
    require(len(plan["frozen_files"]) == 44, "validation plan must freeze exactly 44 paths")
    require(plan["acceptance_requires_all_required_pass"] is True, "all-pass acceptance rule disabled")
    require(plan["allow_test_removal_after_execution"] is False, "test-removal prohibition disabled")

    print("CS021 campaign registration: PASS")


def verify_non_effects() -> None:
    roadmap = read_json(ROADMAP)
    require(roadmap["release_authorized"] is False, "release authorized by CS021")
    require(stage_by_id(roadmap, "EV-05")["status"] == "not_started", "EV-05 started by CS021")

    head = git_output("rev-parse", "HEAD")
    changed = paths_between_base_and_head() if head != BASE else working_tracked_paths()
    for path in changed:
        require(not path.startswith(FORBIDDEN_PREFIXES), f"forbidden path changed: {path}")
        require(path not in FORBIDDEN_EXACT, f"forbidden exact path changed: {path}")

    require_paths_unchanged(
        [
            "audit/STAGE_SCOPE_MAXIMA.json",
            "audit/SOURCE_OF_TRUTH_INDEX.json",
            "audit/CHANGESET_VALIDATION_POLICY.json",
            "audit/GOVERNANCE_TRANSITION_STATE.json",
            "audit/EVOLUTION_INVARIANTS.json",
            ".github/workflows/changeset-validation.yml",
            ".github/workflows/current-product-regression.yml",
            "scripts/generate_manifest.py",
            "scripts/verify_changeset_validation.py",
            "scripts/verify_evolution_plan.py",
        ],
        "governance/product protected surfaces",
    )

    print("CS021 non-effects: PASS")


def verify_all() -> None:
    verify_authority()
    verify_planning()
    verify_predecessor()
    verify_scope()
    verify_golden()
    verify_descriptor()
    verify_non_effects()
    verify_registration()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--authority", action="store_true")
    parser.add_argument("--planning", action="store_true")
    parser.add_argument("--predecessor", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--golden", action="store_true")
    parser.add_argument("--descriptor", action="store_true")
    parser.add_argument("--non-effects", action="store_true")
    parser.add_argument("--registration", action="store_true")
    parser.add_argument("--all", action="store_true")
    args = parser.parse_args()

    selected = [
        args.self_test,
        args.authority,
        args.planning,
        args.predecessor,
        args.scope,
        args.golden,
        args.descriptor,
        args.non_effects,
        args.registration,
        args.all,
    ]
    if sum(bool(value) for value in selected) != 1:
        parser.error("select exactly one verification mode")

    try:
        if args.self_test:
            verify_self_test()
        elif args.authority:
            verify_authority()
        elif args.planning:
            verify_planning()
        elif args.predecessor:
            verify_predecessor()
        elif args.scope:
            verify_scope()
        elif args.golden:
            verify_golden()
        elif args.descriptor:
            verify_descriptor()
        elif args.non_effects:
            verify_non_effects()
        elif args.registration:
            verify_registration()
        elif args.all:
            verify_all()
    except (AssertionError, KeyError, json.JSONDecodeError, OSError) as error:
        print(f"CS021 verification: FAIL: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
