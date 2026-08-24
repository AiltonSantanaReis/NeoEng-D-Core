#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]

BASE = "f3629342f9db5fca75393a217f2d559493e13001"
BRANCH = "agent/cs000m-ev04-scope-authorization"

WORKFLOW = Path(
    ".github/workflows/cs000m-ev04-scope-authorization-validation.yml"
)
PLAN = Path("audit/validation/CS000M/VALIDATION_PLAN.json")
DESCRIPTOR = Path("audit/CURRENT_CHANGESET_VALIDATION.json")
SCOPE_LEDGER = Path("audit/STAGE_SCOPE_MAXIMA.json")
ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
REQS = Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json")
CHANGESET = Path("docs/changesets/000M/CHANGESET.md")
DECISION = Path("docs/records/evolution/DEV-0013.md")
SELF = Path("scripts/verify_cs000m_ev04_scope_authorization.py")

POLICY = Path("audit/CHANGESET_VALIDATION_POLICY.json")
TRANSITION = Path("audit/GOVERNANCE_TRANSITION_STATE.json")
SOURCE_INDEX = Path("audit/SOURCE_OF_TRUTH_INDEX.json")

CS000L_PLAN = Path("audit/validation/CS000L/VALIDATION_PLAN.json")
CS000L_RESULT = Path("audit/validation/CS000L/VALIDATION_RESULT.json")
CS000L_CHANGESET = Path("docs/changesets/000L/CHANGESET.md")
CS000L_EVIDENCE = Path(
    "docs/changesets/000L/evidence/EV03_ACCEPTANCE_MANIFEST.json"
)

PREPARATION = [
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

ALLOWED = list(PREPARATION)

FORBIDDEN = [
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

EXPECTED_EV04 = {
    "stage_id": "EV-04",
    "planned_changeset": "CS021",
    "status": "defined",
    "preparation_allowed_patterns": PREPARATION,
    "allowed_patterns": ALLOWED,
    "mandatory_forbidden_patterns": FORBIDDEN,
}

TRIGGER_SCOPE = {
    ".github/workflows/cs000m-ev04-scope-authorization-validation.yml",
    "audit/STAGE_SCOPE_MAXIMA.json",
    "audit/validation/CS000M/VALIDATION_PLAN.json",
    "docs/changesets/000M/CHANGESET.md",
    "docs/records/evolution/DEV-0013.md",
    "scripts/verify_cs000m_ev04_scope_authorization.py",
}

SOURCE_SCOPE = TRIGGER_SCOPE | {
    "MANIFEST.sha256",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
}

PROTECTED_BASE_FILES = {
    ".github/workflows/changeset-validation.yml",
    ".github/workflows/current-product-regression.yml",
    "CMakeLists.txt",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    "audit/EVOLUTION_INVARIANTS.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/SOURCE_OF_TRUTH_INDEX.json",
    "audit/validation/CS000L/VALIDATION_PLAN.json",
    "audit/validation/CS000L/VALIDATION_RESULT.json",
    "docs/changesets/000L/CHANGESET.md",
    "docs/changesets/000L/evidence/EV03_ACCEPTANCE_MANIFEST.json",
    "docs/contracts/GOLDEN_CORPUS_V1.md",
    "tests/golden/ev03/v1/corpus.json",
    "tests/golden/ev03/v1/manifest.json",
    "tests/golden/ev03/v1/world_initial.bin",
    "tests/golden/ev03/v1/world_after_transition.bin",
    "tests/golden/ev03/v1/world_after_rollback_replay.bin",
    "tests/golden/ev03/v1/evidence_envelope.bin",
}


def run(
    *args: str,
    binary: bool = False,
) -> subprocess.CompletedProcess:
    return subprocess.run(
        args,
        cwd=ROOT,
        text=not binary,
        capture_output=True,
        check=False,
    )


def load(path: Path) -> dict[str, Any]:
    value = json.loads(
        (ROOT / path).read_text(encoding="utf-8")
    )
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {path}")
    return value


def git_show_bytes(
    ref: str,
    path: str,
) -> bytes | None:
    proc = run(
        "git",
        "show",
        f"{ref}:{path}",
        binary=True,
    )
    return proc.stdout if proc.returncode == 0 else None


def base_json(path: Path) -> dict[str, Any]:
    raw = git_show_bytes(BASE, path.as_posix())
    if raw is None:
        raise ValueError(
            f"cannot read base path: {path}"
        )
    value = json.loads(raw.decode("utf-8"))
    if not isinstance(value, dict):
        raise ValueError(
            f"base JSON root must be object: {path}"
        )
    return value


def changed_paths() -> set[str]:
    result: set[str] = set()

    commands = [
        (
            "git",
            "diff",
            "--name-only",
            f"{BASE}...HEAD",
        ),
        (
            "git",
            "diff",
            "--name-only",
            "HEAD",
        ),
        (
            "git",
            "diff",
            "--cached",
            "--name-only",
        ),
        (
            "git",
            "ls-files",
            "--others",
            "--exclude-standard",
        ),
    ]

    for command in commands:
        proc = run(*command)
        if proc.returncode != 0:
            raise ValueError(
                proc.stderr.strip()
                or f"failed command: {' '.join(command)}"
            )
        result.update(
            line.strip()
            for line in proc.stdout.splitlines()
            if line.strip()
        )

    return result


def authority_errors() -> list[str]:
    errors: list[str] = []

    policy = load(POLICY)
    transition = load(TRANSITION)
    index = load(SOURCE_INDEX)

    if policy.get("schema") != (
        "neoeng.dcore.changeset-validation-policy.v1"
    ):
        errors.append("ChangeSet policy schema changed")

    required_true = [
        "test_inventory_frozen_before_execution",
        "exact_run_binding_required",
        "failure_preservation_required",
        "accepted_requires_validated",
        "all_required_tests_must_pass",
        "trusted_base_verifier_required_for_pr_acceptance",
        "historical_records_are_immutable",
        "release_is_separate_from_changeset_acceptance",
    ]

    for key in required_true:
        if policy.get(key) is not True:
            errors.append(
                f"ChangeSet policy invariant changed: {key}"
            )

    if policy.get(
        "candidate_verifier_is_authoritative"
    ) is not False:
        errors.append(
            "candidate verifier became authoritative"
        )

    authority = transition.get(
        "prospective_authority",
        {},
    )

    if authority.get("regime_id") != (
        "CHANGESET_VALIDATION"
    ):
        errors.append(
            "prospective ChangeSet regime mismatch"
        )

    precedence = index.get("precedence")

    if not isinstance(precedence, list):
        errors.append(
            "source-of-truth precedence missing"
        )
    else:
        if "audit/STAGE_SCOPE_MAXIMA.json" not in precedence:
            errors.append(
                "stage-scope ledger missing from precedence"
            )

        if "audit/EVOLUTION_ROADMAP.json" not in precedence:
            errors.append(
                "evolution roadmap missing from precedence"
            )

        if (
            "audit/STAGE_SCOPE_MAXIMA.json" in precedence
            and "audit/EVOLUTION_ROADMAP.json" in precedence
            and precedence.index(
                "audit/STAGE_SCOPE_MAXIMA.json"
            )
            > precedence.index(
                "audit/EVOLUTION_ROADMAP.json"
            )
        ):
            errors.append(
                "stage-scope ledger no longer precedes roadmap"
            )

    conflict = index.get("conflict_policy")

    if (
        not isinstance(conflict, str)
        or "conflict" not in conflict.lower()
        or "stop" not in conflict.lower()
    ):
        errors.append(
            "source-of-truth conflict STOP policy changed"
        )

    return errors


def expected_scope_document() -> dict[str, Any]:
    base = base_json(SCOPE_LEDGER)
    expected = copy.deepcopy(base)

    stages = expected.get("stages")
    undefined = expected.get("undefined_stages")

    if not isinstance(stages, list):
        raise ValueError(
            "base stage-scope stages is not list"
        )

    if not isinstance(undefined, list):
        raise ValueError(
            "base undefined_stages is not list"
        )

    if any(
        isinstance(row, dict)
        and row.get("stage_id") == "EV-04"
        for row in stages
    ):
        raise ValueError(
            "EV-04 unexpectedly defined at base"
        )

    if "EV-04" not in undefined:
        raise ValueError(
            "EV-04 unexpectedly absent from base undefined_stages"
        )

    stages.append(
        copy.deepcopy(EXPECTED_EV04)
    )

    expected["undefined_stages"] = [
        item
        for item in undefined
        if item != "EV-04"
    ]

    return expected


def reconciliation_errors() -> list[str]:
    current = load(SCOPE_LEDGER)
    expected = expected_scope_document()

    if current != expected:
        return [
            "STAGE_SCOPE_MAXIMA differs from exact "
            "single-EV04 reconciliation"
        ]

    return []


def scope_ledger_errors() -> list[str]:
    errors: list[str] = []

    doc = load(SCOPE_LEDGER)

    rows = [
        row
        for row in doc.get("stages", [])
        if isinstance(row, dict)
        and row.get("stage_id") == "EV-04"
    ]

    if len(rows) != 1:
        errors.append(
            f"EV-04 scope row count is {len(rows)}, expected 1"
        )
        return errors

    if rows[0] != EXPECTED_EV04:
        errors.append(
            "EV-04 scope row does not match frozen maximum"
        )

    undefined = doc.get("undefined_stages")

    if (
        not isinstance(undefined, list)
        or "EV-04" in undefined
    ):
        errors.append(
            "EV-04 remains undefined"
        )

    return errors


def predecessor_errors() -> list[str]:
    errors: list[str] = []

    result = load(CS000L_RESULT)

    if result.get("changeset") != "CS000L":
        errors.append(
            "current predecessor is not CS000L"
        )

    if result.get("validation_state") != "VALIDATED":
        errors.append(
            "CS000L validation state changed"
        )

    if result.get("acceptance_decision") != "ACCEPTED":
        errors.append(
            "CS000L acceptance changed"
        )

    for path in (
        CS000L_PLAN,
        CS000L_RESULT,
        CS000L_CHANGESET,
        CS000L_EVIDENCE,
    ):
        prior = git_show_bytes(
            BASE,
            path.as_posix(),
        )

        current = ROOT / path

        if prior is None:
            errors.append(
                f"cannot read predecessor at base: {path}"
            )
        elif not current.is_file():
            errors.append(
                f"predecessor path missing: {path}"
            )
        elif current.read_bytes() != prior:
            errors.append(
                f"predecessor bytes changed: {path}"
            )

    ancestry = run(
        "git",
        "merge-base",
        "--is-ancestor",
        BASE,
        "HEAD",
    )

    if ancestry.returncode != 0:
        errors.append(
            "protected CS000L integration is not ancestor"
        )

    return errors


def nonstart_errors() -> list[str]:
    errors: list[str] = []

    for path in (
        ROADMAP,
        REQS,
    ):
        prior = git_show_bytes(
            BASE,
            path.as_posix(),
        )

        current = ROOT / path

        if prior is None:
            errors.append(
                f"cannot read base lifecycle ledger: {path}"
            )
        elif current.read_bytes() != prior:
            errors.append(
                f"lifecycle ledger changed in CS000M: {path}"
            )

    roadmap = load(ROADMAP)

    if roadmap.get("current_stage") != "EV-03":
        errors.append(
            "current_stage must remain EV-03"
        )

    if roadmap.get("release_authorized") is not False:
        errors.append(
            "release authorization changed"
        )

    stages = {
        row.get("stage_id"): row
        for row in roadmap.get("stages", [])
        if isinstance(row, dict)
    }

    ev03 = stages.get("EV-03")
    ev04 = stages.get("EV-04")
    ev05 = stages.get("EV-05")

    if (
        not isinstance(ev03, dict)
        or ev03.get("status") != "accepted"
    ):
        errors.append(
            "EV-03 is not accepted"
        )

    if (
        not isinstance(ev04, dict)
        or ev04.get("status") != "not_started"
        or ev04.get("planned_changeset") != "CS021"
    ):
        errors.append(
            "EV-04 lifecycle changed prematurely"
        )

    if (
        not isinstance(ev05, dict)
        or ev05.get("status") != "not_started"
    ):
        errors.append(
            "EV-05 lifecycle changed"
        )

    reqs = load(REQS)

    rows = {
        row.get("requirement_id"): row
        for row in reqs.get("requirements", [])
        if isinstance(row, dict)
    }

    for rid in (
        "EVREQ-016",
        "EVREQ-017",
        "EVREQ-018",
    ):
        row = rows.get(rid)

        if not isinstance(row, dict):
            errors.append(
                f"{rid} missing"
            )
            continue

        if row.get("stage") != "EV-04":
            errors.append(
                f"{rid} stage changed"
            )

        if row.get("status") != "planned":
            errors.append(
                f"{rid} must remain planned"
            )

        if row.get("evidence") != []:
            errors.append(
                f"{rid} has premature evidence"
            )

    return errors


def descriptor_errors() -> list[str]:
    descriptor = load(DESCRIPTOR)

    expected = {
        "schema":
            "neoeng.dcore.current-changeset-validation.v1",
        "plan_path":
            "audit/validation/CS000M/VALIDATION_PLAN.json",
    }

    if descriptor != expected:
        return [
            "CS000M source descriptor must contain "
            "plan_path only and no result_path"
        ]

    return []


def scope_errors() -> list[str]:
    actual = changed_paths()

    if actual != SOURCE_SCOPE:
        missing = sorted(
            SOURCE_SCOPE - actual
        )
        extra = sorted(
            actual - SOURCE_SCOPE
        )

        errors = [
            "CS000M source scope is not exact"
        ]

        if missing:
            errors.append(
                "missing source paths: "
                + ", ".join(missing)
            )

        if extra:
            errors.append(
                "unexpected source paths: "
                + ", ".join(extra)
            )

        return errors

    return []


def non_effect_errors() -> list[str]:
    errors: list[str] = []

    for rel in sorted(PROTECTED_BASE_FILES):
        prior = git_show_bytes(
            BASE,
            rel,
        )

        current = ROOT / rel

        if prior is None:
            errors.append(
                f"cannot read protected base file: {rel}"
            )
        elif not current.is_file():
            errors.append(
                f"protected file missing: {rel}"
            )
        elif current.read_bytes() != prior:
            errors.append(
                f"protected product/history bytes changed: {rel}"
            )

    return errors


def self_test_errors() -> list[str]:
    errors: list[str] = []

    good = expected_scope_document()

    bad_undefined = copy.deepcopy(good)
    bad_undefined[
        "undefined_stages"
    ].append("EV-04")

    if bad_undefined == good:
        errors.append(
            "negative undefined-stage mutation ineffective"
        )

    bad_scope = copy.deepcopy(good)

    for row in bad_scope.get("stages", []):
        if (
            isinstance(row, dict)
            and row.get("stage_id") == "EV-04"
        ):
            row[
                "mandatory_forbidden_patterns"
            ] = [
                item
                for item in row[
                    "mandatory_forbidden_patterns"
                ]
                if item != "tests/golden/ev03/v1/**"
            ]

    if bad_scope == good:
        errors.append(
            "negative scope-weakening mutation ineffective"
        )

    actual = load(SCOPE_LEDGER)

    if actual == bad_undefined:
        errors.append(
            "undefined EV-04 would be accepted"
        )

    if actual == bad_scope:
        errors.append(
            "weakened golden boundary would be accepted"
        )

    return errors


def all_errors() -> list[str]:
    errors: list[str] = []

    for function in (
        authority_errors,
        reconciliation_errors,
        scope_ledger_errors,
        predecessor_errors,
        nonstart_errors,
        descriptor_errors,
        scope_errors,
        non_effect_errors,
    ):
        errors.extend(function())

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()

    group = parser.add_mutually_exclusive_group(
        required=True
    )

    group.add_argument(
        "--self-test",
        action="store_true",
    )
    group.add_argument(
        "--authority",
        action="store_true",
    )
    group.add_argument(
        "--reconciliation",
        action="store_true",
    )
    group.add_argument(
        "--scope-ledger",
        action="store_true",
    )
    group.add_argument(
        "--predecessor",
        action="store_true",
    )
    group.add_argument(
        "--nonstart",
        action="store_true",
    )
    group.add_argument(
        "--descriptor",
        action="store_true",
    )
    group.add_argument(
        "--scope",
        action="store_true",
    )
    group.add_argument(
        "--non-effects",
        action="store_true",
    )
    group.add_argument(
        "--all",
        action="store_true",
    )

    args = parser.parse_args()

    if args.self_test:
        label = "self-test"
        errors = self_test_errors()
    elif args.authority:
        label = "authority"
        errors = authority_errors()
    elif args.reconciliation:
        label = "reconciliation"
        errors = reconciliation_errors()
    elif args.scope_ledger:
        label = "scope-ledger"
        errors = scope_ledger_errors()
    elif args.predecessor:
        label = "predecessor"
        errors = predecessor_errors()
    elif args.nonstart:
        label = "nonstart"
        errors = nonstart_errors()
    elif args.descriptor:
        label = "descriptor"
        errors = descriptor_errors()
    elif args.scope:
        label = "scope"
        errors = scope_errors()
    elif args.non_effects:
        label = "non-effects"
        errors = non_effect_errors()
    else:
        label = "all"
        errors = all_errors()

    if errors:
        print(
            f"CS000M {label}: REJECT"
        )

        for error in errors:
            print(
                f"- {error}"
            )

        return 1

    print(
        f"CS000M {label}: PASS"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
