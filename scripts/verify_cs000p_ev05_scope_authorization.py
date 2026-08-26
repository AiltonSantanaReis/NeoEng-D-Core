#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]

BASE = "ca3746d7af4476387d269e2dfbe1e9677bd670cb"
BASE_TREE = "dda63e5b73873fffb7758a18c0b034dbeb9d916d"
BASE_PARENTS = [
    "08ae1545b68d99575e0ddadf76398055c84bb84e",
    "9410aa252700052e6d1a04491cfbd514523b44a9",
]

WORKFLOW = Path(
    ".github/workflows/cs000p-ev05-scope-authorization-validation.yml"
)
PLAN = Path("audit/validation/CS000P/VALIDATION_PLAN.json")
RESULT = Path("audit/validation/CS000P/VALIDATION_RESULT.json")
DESCRIPTOR = Path("audit/CURRENT_CHANGESET_VALIDATION.json")
SCOPE_LEDGER = Path("audit/STAGE_SCOPE_MAXIMA.json")
ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
REQS = Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json")
CHANGESET = Path("docs/changesets/000P/CHANGESET.md")
DECISION = Path("docs/records/evolution/DEV-0016.md")
SELF = Path("scripts/verify_cs000p_ev05_scope_authorization.py")

POLICY = Path("audit/CHANGESET_VALIDATION_POLICY.json")
TRANSITION = Path("audit/GOVERNANCE_TRANSITION_STATE.json")
SOURCE_INDEX = Path("audit/SOURCE_OF_TRUTH_INDEX.json")

CS000O_PLAN = Path("audit/validation/CS000O/VALIDATION_PLAN.json")
CS000O_RESULT = Path("audit/validation/CS000O/VALIDATION_RESULT.json")
CS000O_CHANGESET = Path("docs/changesets/000O/CHANGESET.md")
CS000O_EVIDENCE = Path(
    "docs/changesets/000O/evidence/EV04_ACCEPTANCE_MANIFEST.json"
)
CS000O_DECISION = Path("docs/records/evolution/DEV-0015.md")

PREPARATION = [
    ".github/workflows/cs022-ev05-semantic-fuzz-corruption-validation.yml",
    "CMakeLists.txt",
    "MANIFEST.sha256",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/validation/CS022/**",
    "docs/changesets/022/**",
    "scripts/verify_cs022_ev05_semantic_fuzz_corruption.py",
    "tests/semantic_fuzz_corruption_tests.cpp",
]

ALLOWED = list(PREPARATION)

FORBIDDEN = [
    "src/**",
    "include/**",
    "modules/**",
    "apps/**",
    "fuzz/**",
    "cmake/**",
    "docs/governance/**",
    "docs/contracts/**",
    "audit/SOURCE_OF_TRUTH_INDEX.json",
    "audit/STAGE_SCOPE_MAXIMA.json",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/EVOLUTION_INVARIANTS.json",
    "audit/GOVERNANCE_ROOT_OF_TRUST.json",
    "audit/GOVERNANCE_ACCEPTANCE_CHAIN.json",
    "audit/REPOSITORY_PROTECTION_POLICY.json",
    "audit/PRODUCT_CLAIMS_LEDGER.json",
    "audit/RELEASE_ASSURANCE_POLICY.json",
    "audit/FINAL_ACCEPTANCE_POLICY.json",
    ".github/workflows/changeset-validation.yml",
    ".github/workflows/current-product-regression.yml",
    ".github/workflows/evolution-governance.yml",
    ".github/workflows/governance-root.yml",
    "scripts/generate_manifest.py",
    "scripts/verify_changeset_validation.py",
    "scripts/verify_evolution_plan.py",
    "scripts/verify_build_ci_governance.py",
    "tests/golden/ev03/v1/**",
    "tests/golden_corpus_tests.cpp",
    "tests/property_model_tests.cpp",
    ".github/workflows/cs021-ev04-property-model-testing-validation.yml",
    "scripts/verify_cs021_ev04_property_model_testing.py",
    "audit/validation/CS021/**",
    "docs/changesets/021/**",
    ".github/workflows/cs000o-ev04-ledger-closure-validation.yml",
    "scripts/verify_cs000o_ev04_ledger_closure.py",
    "audit/validation/CS000O/**",
    "docs/changesets/000O/**",
    "docs/records/evolution/DEV-0015.md",
]

EXPECTED_EV05 = {
    "stage_id": "EV-05",
    "planned_changeset": "CS022",
    "status": "defined",
    "preparation_allowed_patterns": PREPARATION,
    "allowed_patterns": ALLOWED,
    "mandatory_forbidden_patterns": FORBIDDEN,
}

TRIGGER_SCOPE = {
    ".github/workflows/cs000p-ev05-scope-authorization-validation.yml",
    "audit/STAGE_SCOPE_MAXIMA.json",
    "audit/validation/CS000P/VALIDATION_PLAN.json",
    "docs/changesets/000P/CHANGESET.md",
    "docs/records/evolution/DEV-0016.md",
    "scripts/verify_cs000p_ev05_scope_authorization.py",
}

SOURCE_SCOPE = TRIGGER_SCOPE | {
    "MANIFEST.sha256",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
}

EXPECTED_TEST_IDS = {
    "cs000p.verifier-self-test",
    "cs000p.authority",
    "cs000p.reconciliation",
    "cs000p.scope-ledger",
    "cs000p.predecessor",
    "cs000p.nonstart",
    "cs000p.descriptor",
    "cs000p.scope",
    "cs000p.non-effects",
    "evolution.plan",
    "repository.manifest",
    "changeset.plan-structure",
}

PROTECTED_BASE_FILES = {
    ".github/workflows/changeset-validation.yml",
    ".github/workflows/current-product-regression.yml",
    ".github/workflows/cs000o-ev04-ledger-closure-validation.yml",
    "CMakeLists.txt",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    "audit/EVOLUTION_INVARIANTS.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/SOURCE_OF_TRUTH_INDEX.json",
    "audit/validation/CS000O/VALIDATION_PLAN.json",
    "audit/validation/CS000O/VALIDATION_RESULT.json",
    "docs/changesets/000O/CHANGESET.md",
    "docs/changesets/000O/evidence/EV04_ACCEPTANCE_MANIFEST.json",
    "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md",
    "docs/records/evolution/DEV-0015.md",
    "scripts/generate_manifest.py",
    "scripts/verify_changeset_validation.py",
    "scripts/verify_cs000o_ev04_ledger_closure.py",
    "scripts/verify_evolution_plan.py",
    "tests/golden_corpus_tests.cpp",
    "tests/property_model_tests.cpp",
    "tests/golden/ev03/v1/corpus.json",
    "tests/golden/ev03/v1/evidence_envelope.bin",
    "tests/golden/ev03/v1/manifest.json",
    "tests/golden/ev03/v1/world_after_rollback_replay.bin",
    "tests/golden/ev03/v1/world_after_transition.bin",
    "tests/golden/ev03/v1/world_initial.bin",
}


def run(*args: str, binary: bool = False) -> subprocess.CompletedProcess:
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


def git_show_bytes(ref: str, path: str) -> bytes | None:
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
        raise ValueError(f"cannot read base path: {path}")
    value = json.loads(raw.decode("utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"base JSON root must be object: {path}")
    return value


def changed_paths() -> set[str]:
    result: set[str] = set()

    commands = [
        ("git", "diff", "--name-only", f"{BASE}...HEAD"),
        ("git", "diff", "--name-only", "HEAD"),
        ("git", "diff", "--cached", "--name-only"),
        ("git", "ls-files", "--others", "--exclude-standard"),
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


def expected_scope_document() -> dict[str, Any]:
    base = base_json(SCOPE_LEDGER)
    expected = copy.deepcopy(base)

    stages = expected.get("stages")
    undefined = expected.get("undefined_stages")

    if not isinstance(stages, list):
        raise ValueError("base stage-scope stages is not list")

    if not isinstance(undefined, list):
        raise ValueError("base undefined_stages is not list")

    if any(
        isinstance(row, dict)
        and row.get("stage_id") == "EV-05"
        for row in stages
    ):
        raise ValueError("EV-05 unexpectedly defined at base")

    if undefined.count("EV-05") != 1:
        raise ValueError(
            "EV-05 must occur exactly once in base undefined_stages"
        )

    stages.append(copy.deepcopy(EXPECTED_EV05))

    expected["undefined_stages"] = [
        item
        for item in undefined
        if item != "EV-05"
    ]

    return expected


def scope_document_errors(
    document: dict[str, Any],
) -> list[str]:
    expected = expected_scope_document()

    if document != expected:
        return [
            "STAGE_SCOPE_MAXIMA differs from exact "
            "single-EV05 reconciliation"
        ]

    return []


def authority_errors() -> list[str]:
    errors: list[str] = []

    policy = load(POLICY)
    transition = load(TRANSITION)
    index = load(SOURCE_INDEX)
    scope = base_json(SCOPE_LEDGER)

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

    if authority.get("regime_id") != "CHANGESET_VALIDATION":
        errors.append(
            "prospective ChangeSet regime mismatch"
        )

    precedence = index.get("precedence")

    if not isinstance(precedence, list):
        errors.append("source-of-truth precedence missing")
    else:
        required = [
            "audit/STAGE_SCOPE_MAXIMA.json",
            "audit/EVOLUTION_ROADMAP.json",
        ]
        for path in required:
            if path not in precedence:
                errors.append(
                    f"source-of-truth path missing: {path}"
                )

        if all(path in precedence for path in required):
            if (
                precedence.index(
                    "audit/STAGE_SCOPE_MAXIMA.json"
                )
                >
                precedence.index(
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

    if scope.get(
        "default_policy"
    ) != "undefined_stage_scope_is_REJECT":
        errors.append(
            "undefined-stage default rejection policy changed"
        )

    if scope.get(
        "scope_patterns_must_be_literal_members_of_maximum"
    ) is not True:
        errors.append(
            "literal maximum membership policy changed"
        )

    if scope.get(
        "rule"
    ) != (
        "A stage in undefined_stages cannot be prepared, "
        "started or operated until an accepted prior governance "
        "amendment defines its maximum scope."
    ):
        errors.append(
            "undefined-stage governance rule changed"
        )

    return errors


def reconciliation_errors() -> list[str]:
    return scope_document_errors(load(SCOPE_LEDGER))


def scope_ledger_errors() -> list[str]:
    errors: list[str] = []

    document = load(SCOPE_LEDGER)

    rows = [
        row
        for row in document.get("stages", [])
        if isinstance(row, dict)
        and row.get("stage_id") == "EV-05"
    ]

    if len(rows) != 1:
        errors.append(
            f"EV-05 scope row count is {len(rows)}, expected 1"
        )
        return errors

    if rows[0] != EXPECTED_EV05:
        errors.append(
            "EV-05 scope row does not match frozen maximum"
        )

    base = base_json(SCOPE_LEDGER)
    base_undefined = base.get("undefined_stages")

    if not isinstance(base_undefined, list):
        errors.append(
            "base undefined_stages is not list"
        )
        return errors

    expected_undefined = [
        item
        for item in base_undefined
        if item != "EV-05"
    ]

    if document.get(
        "undefined_stages"
    ) != expected_undefined:
        errors.append(
            "undefined_stages changed beyond EV-05 removal"
        )

    return errors


def predecessor_errors() -> list[str]:
    errors: list[str] = []

    tree = run(
        "git",
        "rev-parse",
        f"{BASE}^{{tree}}",
    )

    if (
        tree.returncode != 0
        or tree.stdout.strip() != BASE_TREE
    ):
        errors.append(
            "protected CS000O merge tree identity mismatch"
        )

    parents = run(
        "git",
        "show",
        "-s",
        "--format=%P",
        BASE,
    )

    if parents.returncode != 0:
        errors.append(
            "cannot read protected CS000O merge parents"
        )
    elif parents.stdout.strip().split() != BASE_PARENTS:
        errors.append(
            "protected CS000O merge parent identity mismatch"
        )

    result = load(CS000O_RESULT)

    if result.get("changeset") != "CS000O":
        errors.append("accepted predecessor is not CS000O")

    if result.get("validation_state") != "VALIDATED":
        errors.append(
            "CS000O validation state changed"
        )

    if result.get(
        "acceptance_decision"
    ) != "ACCEPTED":
        errors.append(
            "CS000O acceptance changed"
        )

    if result.get(
        "source_sha"
    ) != "9913352ab88a14b444a857377e9bcd2bc4dccde6":
        errors.append(
            "CS000O qualified source identity changed"
        )

    if result.get("run_id") != 32913944112:
        errors.append(
            "CS000O qualifying run identity changed"
        )

    for path in (
        CS000O_PLAN,
        CS000O_RESULT,
        CS000O_CHANGESET,
        CS000O_EVIDENCE,
        CS000O_DECISION,
    ):
        prior = git_show_bytes(
            BASE,
            path.as_posix(),
        )

        current = ROOT / path

        if prior is None:
            errors.append(
                f"cannot read accepted predecessor at base: {path}"
            )
        elif not current.is_file():
            errors.append(
                f"accepted predecessor path missing: {path}"
            )
        elif current.read_bytes() != prior:
            errors.append(
                f"accepted predecessor bytes changed: {path}"
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
            "protected CS000O integration is not ancestor of HEAD"
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
                f"lifecycle ledger changed in CS000P: {path}"
            )

    roadmap = load(ROADMAP)

    if roadmap.get("current_stage") != "EV-04":
        errors.append(
            "current_stage must remain EV-04"
        )

    if roadmap.get(
        "release_authorized"
    ) is not False:
        errors.append(
            "release authorization changed"
        )

    stages = {
        row.get("stage_id"): row
        for row in roadmap.get("stages", [])
        if isinstance(row, dict)
    }

    ev04 = stages.get("EV-04")
    ev05 = stages.get("EV-05")

    if (
        not isinstance(ev04, dict)
        or ev04.get("status") != "accepted"
        or ev04.get("planned_changeset") != "CS021"
        or ev04.get("accepted_commit")
        != "d75fb80e7aa304576060339c31ff87fdb9dae206"
    ):
        errors.append(
            "EV-04 accepted lifecycle identity changed"
        )

    if (
        not isinstance(ev05, dict)
        or ev05.get("status") != "not_started"
        or ev05.get("planned_changeset") != "CS022"
        or ev05.get("accepted_commit") is not None
        or ev05.get("evidence_manifest") is not None
        or ev05.get("decision_record") is not None
    ):
        errors.append(
            "EV-05 lifecycle changed prematurely"
        )

    reqs = load(REQS)

    rows = {
        row.get("requirement_id"): row
        for row in reqs.get("requirements", [])
        if isinstance(row, dict)
    }

    for requirement_id in (
        "EVREQ-019",
        "EVREQ-020",
        "EVREQ-021",
    ):
        row = rows.get(requirement_id)

        if (
            not isinstance(row, dict)
            or row.get("stage") != "EV-05"
            or row.get("status") != "planned"
            or row.get("evidence") != []
        ):
            errors.append(
                f"{requirement_id} changed prematurely"
            )

    return errors


def descriptor_errors() -> list[str]:
    errors: list[str] = []

    descriptor = load(DESCRIPTOR)

    expected = {
        "schema":
            "neoeng.dcore.current-changeset-validation.v1",
        "plan_path":
            "audit/validation/CS000P/VALIDATION_PLAN.json",
    }

    if descriptor != expected:
        errors.append(
            "CS000P source descriptor is not exact plan-only form"
        )

    if (ROOT / RESULT).exists():
        errors.append(
            "CS000P VALIDATION_RESULT must be absent in source"
        )

    return errors


def scope_errors() -> list[str]:
    errors: list[str] = []

    actual = changed_paths()

    if actual != SOURCE_SCOPE:
        errors.append(
            "CS000P changed paths differ from exact 8-path source scope: "
            + ", ".join(sorted(actual))
        )

    count = run(
        "git",
        "rev-list",
        "--count",
        f"{BASE}..HEAD",
    )

    if count.returncode != 0:
        errors.append(
            "cannot determine source commit cardinality"
        )
    else:
        try:
            commits = int(count.stdout.strip())
        except ValueError:
            errors.append(
                "source commit cardinality is not integer"
            )
        else:
            if commits not in (0, 1):
                errors.append(
                    f"CS000P source must be 0 or 1 commits above "
                    f"base during preparation/qualification, got {commits}"
                )

            if commits == 1:
                commit_paths = run(
                    "git",
                    "diff",
                    "--name-only",
                    f"{BASE}..HEAD",
                )

                if commit_paths.returncode != 0:
                    errors.append(
                        "cannot inspect committed source paths"
                    )
                else:
                    committed = {
                        line.strip()
                        for line in commit_paths.stdout.splitlines()
                        if line.strip()
                    }

                    if committed != SOURCE_SCOPE:
                        errors.append(
                            "committed CS000P source geometry mismatch"
                        )

    workflow = (
        ROOT / WORKFLOW
    ).read_text(encoding="utf-8")

    trigger_lines = set(
        re.findall(
            r"^\s+- '([^']+)'\s*$",
            workflow,
            flags=re.MULTILINE,
        )
    )

    if trigger_lines != TRIGGER_SCOPE:
        errors.append(
            "CS000P workflow trigger path set mismatch"
        )

    if (
        "agent/cs000p-ev05-scope-authorization"
        not in workflow
    ):
        errors.append(
            "CS000P workflow branch mismatch"
        )

    plan = load(PLAN)

    tests = plan.get("required_tests")

    if not isinstance(tests, list):
        errors.append(
            "CS000P required_tests is not list"
        )
    else:
        test_ids = {
            row.get("test_id")
            for row in tests
            if isinstance(row, dict)
        }

        if (
            len(tests) != 12
            or test_ids != EXPECTED_TEST_IDS
        ):
            errors.append(
                "CS000P frozen 12-test inventory mismatch"
            )

    return errors


def non_effect_errors() -> list[str]:
    errors: list[str] = []

    for relative in sorted(PROTECTED_BASE_FILES):
        prior = git_show_bytes(BASE, relative)
        current = ROOT / relative

        if prior is None:
            errors.append(
                f"cannot read protected base path: {relative}"
            )
        elif not current.is_file():
            errors.append(
                f"protected path missing: {relative}"
            )
        elif current.read_bytes() != prior:
            errors.append(
                f"protected path changed: {relative}"
            )

    actual = changed_paths()

    forbidden_prefixes = (
        "src/",
        "include/",
        "modules/",
        "apps/",
        "fuzz/",
        "cmake/",
        "docs/governance/",
        "docs/contracts/",
        "tests/golden/ev03/v1/",
    )

    for path in sorted(actual):
        if path.startswith(forbidden_prefixes):
            errors.append(
                f"forbidden product/history path changed: {path}"
            )

    if "CMakeLists.txt" in actual:
        errors.append(
            "CS000P must not change CMakeLists.txt"
        )

    if "tests/property_model_tests.cpp" in actual:
        errors.append(
            "CS000P must not change EV-04 property/model test"
        )

    return errors


def self_test_errors() -> list[str]:
    failures: list[str] = []

    expected = expected_scope_document()

    if scope_document_errors(expected):
        failures.append(
            "valid exact EV-05 reconciliation rejected"
        )

    still_undefined = copy.deepcopy(expected)
    still_undefined["undefined_stages"].append("EV-05")

    if not scope_document_errors(still_undefined):
        failures.append(
            "EV-05 remaining undefined was not rejected"
        )

    widened = copy.deepcopy(expected)
    ev05 = next(
        row
        for row in widened["stages"]
        if row.get("stage_id") == "EV-05"
    )
    ev05["allowed_patterns"].append("src/**")

    if not scope_document_errors(widened):
        failures.append(
            "unauthorized EV-05 scope widening was not rejected"
        )

    other_stage_removed = copy.deepcopy(expected)
    other_stage_removed["undefined_stages"] = [
        item
        for item in other_stage_removed["undefined_stages"]
        if item != "EV-06"
    ]

    if not scope_document_errors(other_stage_removed):
        failures.append(
            "unrelated undefined-stage mutation was not rejected"
        )

    return failures


def emit(
    mode: str,
    errors: list[str],
) -> int:
    if errors:
        print(f"CS000P VERIFICATION: REJECT [{mode}]")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"CS000P VERIFICATION: ACCEPT [{mode}]")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--self-test",
        action="store_true",
    )
    parser.add_argument(
        "--authority",
        action="store_true",
    )
    parser.add_argument(
        "--reconciliation",
        action="store_true",
    )
    parser.add_argument(
        "--scope-ledger",
        action="store_true",
    )
    parser.add_argument(
        "--predecessor",
        action="store_true",
    )
    parser.add_argument(
        "--nonstart",
        action="store_true",
    )
    parser.add_argument(
        "--descriptor",
        action="store_true",
    )
    parser.add_argument(
        "--scope",
        action="store_true",
    )
    parser.add_argument(
        "--non-effects",
        action="store_true",
    )

    args = parser.parse_args()

    selected = [
        args.self_test,
        args.authority,
        args.reconciliation,
        args.scope_ledger,
        args.predecessor,
        args.nonstart,
        args.descriptor,
        args.scope,
        args.non_effects,
    ]

    if sum(bool(item) for item in selected) > 1:
        return emit(
            "argument",
            ["select at most one verification mode"],
        )

    try:
        if args.self_test:
            return emit(
                "self-test",
                self_test_errors(),
            )

        if args.authority:
            return emit(
                "authority",
                authority_errors(),
            )

        if args.reconciliation:
            return emit(
                "reconciliation",
                reconciliation_errors(),
            )

        if args.scope_ledger:
            return emit(
                "scope-ledger",
                scope_ledger_errors(),
            )

        if args.predecessor:
            return emit(
                "predecessor",
                predecessor_errors(),
            )

        if args.nonstart:
            return emit(
                "nonstart",
                nonstart_errors(),
            )

        if args.descriptor:
            return emit(
                "descriptor",
                descriptor_errors(),
            )

        if args.scope:
            return emit(
                "scope",
                scope_errors(),
            )

        if args.non_effects:
            return emit(
                "non-effects",
                non_effect_errors(),
            )

        errors: list[str] = []
        errors.extend(authority_errors())
        errors.extend(reconciliation_errors())
        errors.extend(scope_ledger_errors())
        errors.extend(predecessor_errors())
        errors.extend(nonstart_errors())
        errors.extend(descriptor_errors())
        errors.extend(scope_errors())
        errors.extend(non_effect_errors())

        return emit("all", errors)

    except Exception as error:
        return emit(
            "exception",
            [f"{type(error).__name__}: {error}"],
        )


if __name__ == "__main__":
    raise SystemExit(main())
