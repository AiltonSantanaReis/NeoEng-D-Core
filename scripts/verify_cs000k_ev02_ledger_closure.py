#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import subprocess
import urllib.request
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]

REPO = "AiltonSantanaReis/NeoEng-D-Core"

BASE = "9e35c25fd4c618d4707067f8760712c549f01a3e"
PROTECTED_BASE = "bf3051abdb084273540e6caeb72329eafa0a2eea"

CS019_SOURCE = "49b0ea1c9ed006503957331d6dd037f51e55745d"
CS019_BINDING = "c30a8fdd5ca20d5b2b2473e858e5adc53904c345"
ACCIDENTAL = "ca4a7d0397660e7955d22899ec338b67442b6c52"
RECOVERY = "16e18cd53af9bbc8b4791f2c0701f91990230809"
EXPECTED_TREE = "eaef6588e3c591b37e66dce595e7b1ac25dcd5e9"

CS019_QUALIFYING_RUN = 32644940394
CS019_TRUSTED_RUN = 32647449825
CS019_DIAGNOSTIC_RUN = 32647451020
CS019_PR_REGRESSION_RUN = 32647451062
CS000J_STALE_RUN = 32647451040
MAIN_CHANGESET_RUN = 32650927099
POST_MERGE_REGRESSION_RUN = 32650927094

TRUSTED_APP_ID = 15368
PR_NUMBER = 54

ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
REQS = Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json")
TRANSITION = Path("audit/GOVERNANCE_TRANSITION_STATE.json")
POLICY = Path("audit/CHANGESET_VALIDATION_POLICY.json")

OLD_WORKFLOW = Path(
    ".github/workflows/cs000j-ev01-ledger-closure-validation.yml"
)

SELF_WORKFLOW = Path(
    ".github/workflows/cs000k-ev02-ledger-closure-validation.yml"
)

PLAN = Path("audit/validation/CS000K/VALIDATION_PLAN.json")
RESULT = Path("audit/validation/CS000K/VALIDATION_RESULT.json")
DESCRIPTOR = Path("audit/CURRENT_CHANGESET_VALIDATION.json")

CHANGESET = Path("docs/changesets/000K/CHANGESET.md")
EVIDENCE = Path(
    "docs/changesets/000K/evidence/EV02_ACCEPTANCE_MANIFEST.json"
)
DECISION = Path("docs/records/evolution/DEV-0010.md")
GLOBAL_MANIFEST = Path("MANIFEST.sha256")

EXPECTED_TRIGGER_PATHS = {
    ".github/workflows/cs000j-ev01-ledger-closure-validation.yml",
    ".github/workflows/cs000k-ev02-ledger-closure-validation.yml",
    "scripts/verify_cs000k_ev02_ledger_closure.py",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/validation/CS000K/VALIDATION_PLAN.json",
    "docs/changesets/000K/CHANGESET.md",
    "docs/changesets/000K/evidence/EV02_ACCEPTANCE_MANIFEST.json",
    "docs/records/evolution/DEV-0010.md",
}

SOURCE_REQUIRED = EXPECTED_TRIGGER_PATHS | {
    "audit/CURRENT_CHANGESET_VALIDATION.json",
    "MANIFEST.sha256",
}

CLOSURE_ALLOWED = SOURCE_REQUIRED | {
    "audit/validation/CS000K/VALIDATION_RESULT.json",
}

EVIDENCE_FILES = [
    "audit/validation/CS019/VALIDATION_PLAN_R2.json",
    "audit/validation/CS019/VALIDATION_RESULT_R2.json",
    "audit/validation/CS019/ATTEMPT_001_NONACCEPTANCE.json",
    "docs/changesets/019/CHANGESET.md",
    "docs/changesets/019/CHANGESET_R2.md",
    "docs/contracts/FUNDAMENTAL_TRANSITION_V1.md",
    "docs/records/evolution/DEV-0009.md",
    ".github/workflows/cs019-ev02-fundamental-contract-hardening-validation.yml",
    ".github/workflows/current-product-regression.yml",
    "scripts/verify_cs019_ev02_fundamental_contracts.py",
    "scripts/verify_build_ci_governance.py",
    "tests/test_main.cpp",
    "tests/numeric_closure_tests.cpp",
    "modules/host_sdk/tests/host_sdk_tests.cpp",
    "include/neoeng/core/simulation.hpp",
    "src/simulation.cpp",
    "src/rollback.cpp",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    ".github/workflows/changeset-validation.yml",
]

REQ_EVIDENCE = {
    "EVREQ-009": [
        "docs/contracts/FUNDAMENTAL_TRANSITION_V1.md",
        "tests/test_main.cpp",
        "audit/validation/CS019/VALIDATION_RESULT_R2.json",
        EVIDENCE.as_posix(),
    ],
    "EVREQ-010": [
        "docs/contracts/FUNDAMENTAL_TRANSITION_V1.md",
        "tests/test_main.cpp",
        "modules/host_sdk/tests/host_sdk_tests.cpp",
        "audit/validation/CS019/VALIDATION_RESULT_R2.json",
        EVIDENCE.as_posix(),
    ],
    "EVREQ-011": [
        "docs/contracts/FUNDAMENTAL_TRANSITION_V1.md",
        "tests/numeric_closure_tests.cpp",
        "audit/validation/CS019/VALIDATION_RESULT_R2.json",
        EVIDENCE.as_posix(),
    ],
    "EVREQ-012": [
        ".github/workflows/cs019-ev02-fundamental-contract-hardening-validation.yml",
        "audit/validation/CS019/VALIDATION_RESULT_R2.json",
        "docs/contracts/FUNDAMENTAL_TRANSITION_V1.md",
        EVIDENCE.as_posix(),
    ],
}

EXTERNAL_EVIDENCE = {
    "cs019_qualifying_run": {
        "run_id": CS019_QUALIFYING_RUN,
        "run_attempt": 1,
        "source_sha": CS019_SOURCE,
        "workflow_path":
            ".github/workflows/"
            "cs019-ev02-fundamental-contract-hardening-validation.yml",
        "conclusion": "success",
    },
    "pr54_recovery_trusted_gate": {
        "run_id": CS019_TRUSTED_RUN,
        "run_attempt": 1,
        "head_sha": RECOVERY,
        "workflow_path":
            ".github/workflows/changeset-validation.yml",
        "conclusion": "success",
    },
    "pr54_candidate_diagnostic": {
        "run_id": CS019_DIAGNOSTIC_RUN,
        "run_attempt": 1,
        "head_sha": RECOVERY,
        "workflow_path":
            ".github/workflows/changeset-validation.yml",
        "conclusion": "success",
    },
    "pr54_product_regression": {
        "run_id": CS019_PR_REGRESSION_RUN,
        "run_attempt": 1,
        "head_sha": RECOVERY,
        "workflow_path":
            ".github/workflows/current-product-regression.yml",
        "conclusion": "success",
    },
    "pr54_cs000j_stale_scope": {
        "run_id": CS000J_STALE_RUN,
        "run_attempt": 1,
        "head_sha": RECOVERY,
        "workflow_path":
            ".github/workflows/"
            "cs000j-ev01-ledger-closure-validation.yml",
        "conclusion": "failure",
        "classification": "NOT_APPLICABLE_STALE_SCOPE",
        "rerun": False,
    },
    "post_acceptance_net_zero_recovery": {
        "binding_sha": CS019_BINDING,
        "accidental_sha": ACCIDENTAL,
        "recovery_sha": RECOVERY,
        "tree_sha": EXPECTED_TREE,
        "transient_path": "NONEXISTENT",
        "net_tree_delta": "ZERO",
        "history_rewrite": "NONE",
        "qualifying_rerun": False,
    },
    "main_changeset_validation": {
        "run_id": MAIN_CHANGESET_RUN,
        "run_attempt": 1,
        "head_sha": BASE,
        "workflow_path":
            ".github/workflows/changeset-validation.yml",
        "conclusion": "success",
    },
    "post_merge_product_regression": {
        "run_id": POST_MERGE_REGRESSION_RUN,
        "run_attempt": 1,
        "head_sha": BASE,
        "workflow_path":
            ".github/workflows/current-product-regression.yml",
        "conclusion": "success",
    },
}


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


def load(path: Path) -> Any:
    return json.loads(
        (ROOT / path).read_text(encoding="utf-8")
    )


def git_show_bytes(ref: str, path: str) -> bytes | None:
    proc = subprocess.run(
        ["git", "show", f"{ref}:{path}"],
        cwd=ROOT,
        capture_output=True,
        check=False,
    )

    return proc.stdout if proc.returncode == 0 else None


def git_show_json(ref: str, path: str) -> Any:
    raw = git_show_bytes(ref, path)

    if raw is None:
        raise ValueError(f"cannot read {path} at {ref}")

    return json.loads(raw.decode("utf-8"))


def sha256_lf(path: Path) -> str:
    return hashlib.sha256(
        path.read_bytes()
        .replace(b"\r\n", b"\n")
        .replace(b"\r", b"\n")
    ).hexdigest()


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


def trigger_paths(text: str) -> set[str]:
    values: set[str] = set()
    active = False

    for line in text.splitlines():
        if line == "    paths:":
            active = True
            continue

        if active:
            if line.startswith("      - "):
                values.add(
                    line.split("- ", 1)[1]
                    .strip()
                    .strip("'\"")
                )
                continue

            if line.strip() and not line.startswith("      "):
                break

    return values


def changed_paths() -> set[str]:
    paths: set[str] = set()

    commands = [
        ("git", "diff", "--name-only", f"{BASE}...HEAD"),
        ("git", "diff", "--name-only", "HEAD"),
        ("git", "diff", "--cached", "--name-only"),
    ]

    for args in commands:
        proc = run(*args)

        if proc.returncode != 0:
            raise RuntimeError(
                proc.stderr.strip() or "git diff failed"
            )

        paths.update(
            line.strip()
            for line in proc.stdout.splitlines()
            if line.strip()
        )

    return paths


def parents_of(ref: str) -> list[str]:
    proc = run(
        "git",
        "rev-list",
        "--parents",
        "-n",
        "1",
        ref,
    )

    if proc.returncode != 0:
        raise RuntimeError(
            proc.stderr.strip() or f"cannot inspect {ref}"
        )

    return proc.stdout.split()


def tree_of(ref: str) -> str:
    proc = run(
        "git",
        "rev-parse",
        f"{ref}^{{tree}}",
    )

    if proc.returncode != 0:
        raise RuntimeError(
            proc.stderr.strip() or f"cannot resolve tree {ref}"
        )

    return proc.stdout.strip()


def commit_paths(ref: str) -> set[str]:
    proc = run(
        "git",
        "diff-tree",
        "--no-commit-id",
        "--name-only",
        "-r",
        ref,
    )

    if proc.returncode != 0:
        raise RuntimeError(
            proc.stderr.strip() or f"cannot inspect paths {ref}"
        )

    return {
        line.strip()
        for line in proc.stdout.splitlines()
        if line.strip()
    }


def github_json(path: str) -> Any:
    url = f"https://api.github.com/repos/{REPO}{path}"

    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "NeoEng-D-Core-CS000K-verifier",
    }

    token = os.environ.get("GITHUB_TOKEN")

    if token:
        headers["Authorization"] = f"Bearer {token}"

    request = urllib.request.Request(
        url,
        headers=headers,
        method="GET",
    )

    with urllib.request.urlopen(
        request,
        timeout=30,
    ) as response:
        return json.loads(
            response.read().decode("utf-8")
        )


def emit(label: str, errors: list[str]) -> int:
    if errors:
        print(
            f"CS000K EV-02 LEDGER CLOSURE: "
            f"REJECT — {label}"
        )

        for item in errors:
            print(f"- {item}")

        return 1

    print(
        f"CS000K EV-02 LEDGER CLOSURE: "
        f"PASS — {label}"
    )

    return 0


def self_test_errors() -> list[str]:
    errors: list[str] = []

    manual = (
        "name: x\n\n"
        "on:\n"
        "  workflow_dispatch:\n\n"
        "permissions:\n"
        "  contents: read\n"
    )

    if top_level_on_keys(manual) != {"workflow_dispatch"}:
        errors.append("manual-only parser fixture failed")

    scoped = (
        "name: x\n\n"
        "on:\n"
        "  pull_request:\n"
        "    branches: [main]\n"
        "    paths:\n"
        "      - 'a'\n"
        "      - 'b'\n\n"
        "permissions:\n"
        "  contents: read\n"
    )

    if trigger_paths(scoped) != {"a", "b"}:
        errors.append("path-scope parser fixture failed")

    bad = set(SOURCE_REQUIRED)
    bad.add("src/forbidden.cpp")

    if not (bad - CLOSURE_ALLOWED):
        errors.append("forbidden scope fixture was not rejected")

    for lifecycle_path in (
        "audit/CURRENT_CHANGESET_VALIDATION.json",
        "MANIFEST.sha256",
        RESULT.as_posix(),
    ):
        if lifecycle_path in EXPECTED_TRIGGER_PATHS:
            errors.append(
                "lifecycle path unexpectedly triggers: "
                + lifecycle_path
            )

    if len(EXPECTED_TRIGGER_PATHS) != 9:
        errors.append("trigger inventory cardinality changed")

    return errors


def authority_errors() -> list[str]:
    errors: list[str] = []

    transition = load(TRANSITION)

    expected_authority = {
        "regime_id": "CHANGESET_VALIDATION",
        "policy": "audit/CHANGESET_VALIDATION_POLICY.json",
        "policy_document":
            "docs/governance/CHANGESET_VALIDATION_POLICY.md",
        "workflow":
            ".github/workflows/changeset-validation.yml",
        "verifier":
            "scripts/verify_changeset_validation.py",
        "required_branch_check":
            "Trusted ChangeSet validation gate",
        "required_branch_check_app_id":
            TRUSTED_APP_ID,
    }

    if (
        transition.get("prospective_authority")
        != expected_authority
    ):
        errors.append(
            "prospective ChangeSet validation authority mismatch"
        )

    legacy = transition.get("legacy_cs016e", {})

    if not (
        legacy.get("status") == "SUPERSEDED_UNACCEPTED"
        and legacy.get("accepted") is False
        and legacy.get("accepted_source_commit") is None
        and legacy.get("evidence_manifest") is None
        and legacy.get("preserve_bytes") is True
        and legacy.get("may_be_reclassified_as_accepted") is False
    ):
        errors.append("legacy CS016E state mismatch")

    policy = load(POLICY)

    invariants = {
        "ci_green_is_acceptance": False,
        "required_test_nonpass_blocks_validation": True,
        "test_inventory_frozen_before_execution": True,
        "allow_test_removal_after_execution": False,
        "exact_run_binding_required": True,
        "failure_preservation_required": True,
        "accepted_requires_validated": True,
        "all_required_tests_must_pass": True,
        "candidate_verifier_is_authoritative": False,
        "trusted_base_verifier_required_for_pr_acceptance": True,
        "release_is_separate_from_changeset_acceptance": True,
    }

    for key, value in invariants.items():
        if policy.get(key) != value:
            errors.append(
                f"ChangeSet policy invariant mismatch: {key}"
            )

    return errors


def integration_errors() -> list[str]:
    errors: list[str] = []

    if parents_of(BASE) != [
        BASE,
        PROTECTED_BASE,
        RECOVERY,
    ]:
        errors.append("CS019 protected merge parents mismatch")

    if tree_of(BASE) != EXPECTED_TREE:
        errors.append("CS019 merge tree mismatch")

    if tree_of(RECOVERY) != EXPECTED_TREE:
        errors.append("CS019 recovery tree mismatch")

    proc = run(
        "git",
        "diff",
        "--quiet",
        RECOVERY,
        BASE,
        "--",
    )

    if proc.returncode != 0:
        errors.append(
            "protected merge differs from accepted recovery tree"
        )

    result = load(
        Path(
            "audit/validation/CS019/"
            "VALIDATION_RESULT_R2.json"
        )
    )

    expected_result = {
        "changeset": "CS019",
        "plan_commit": CS019_SOURCE,
        "source_sha": CS019_SOURCE,
        "run_id": CS019_QUALIFYING_RUN,
        "run_attempt": 1,
        "workflow_path":
            ".github/workflows/"
            "cs019-ev02-fundamental-contract-hardening-validation.yml",
        "validation_state": "VALIDATED",
        "acceptance_decision": "ACCEPTED",
    }

    for key, value in expected_result.items():
        if result.get(key) != value:
            errors.append(f"CS019 result mismatch: {key}")

    tests = result.get("tests")

    if not isinstance(tests, list) or len(tests) != 30:
        errors.append("CS019 result test inventory is not 30")
    elif any(
        not isinstance(row, dict)
        or row.get("status") != "PASS"
        for row in tests
    ):
        errors.append("CS019 result contains non-PASS test")

    plan_bytes = (
        ROOT /
        "audit/validation/CS019/VALIDATION_PLAN_R2.json"
    ).read_bytes()

    if (
        hashlib.sha256(plan_bytes).hexdigest()
        != result.get("plan_sha256")
    ):
        errors.append(
            "CS019 plan hash/result binding mismatch"
        )

    base_descriptor = git_show_json(
        BASE,
        "audit/CURRENT_CHANGESET_VALIDATION.json",
    )

    expected_descriptor = {
        "schema":
            "neoeng.dcore.current-changeset-validation.v1",
        "plan_path":
            "audit/validation/CS019/VALIDATION_PLAN_R2.json",
        "result_path":
            "audit/validation/CS019/VALIDATION_RESULT_R2.json",
    }

    if base_descriptor != expected_descriptor:
        errors.append(
            "integrated main did not contain accepted "
            "CS019 descriptor"
        )

    for rel in (
        "audit/validation/CS019/VALIDATION_PLAN_R2.json",
        "audit/validation/CS019/VALIDATION_RESULT_R2.json",
        "audit/validation/CS019/ATTEMPT_001_NONACCEPTANCE.json",
    ):
        prior = git_show_bytes(BASE, rel)

        if prior is None:
            errors.append(
                f"cannot read integrated evidence: {rel}"
            )
            continue

        if (ROOT / rel).read_bytes() != prior:
            errors.append(
                "integrated CS019 evidence mutated "
                f"during closure: {rel}"
            )

    return errors


def recovery_errors() -> list[str]:
    errors: list[str] = []

    if parents_of(ACCIDENTAL) != [
        ACCIDENTAL,
        CS019_BINDING,
    ]:
        errors.append("accidental commit ancestry mismatch")

    if parents_of(RECOVERY) != [
        RECOVERY,
        ACCIDENTAL,
    ]:
        errors.append("recovery commit ancestry mismatch")

    if commit_paths(ACCIDENTAL) != {"NONEXISTENT"}:
        errors.append(
            "accidental commit touched unexpected paths"
        )

    if commit_paths(RECOVERY) != {"NONEXISTENT"}:
        errors.append(
            "recovery commit touched unexpected paths"
        )

    if tree_of(CS019_BINDING) != EXPECTED_TREE:
        errors.append("accepted binding tree mismatch")

    if tree_of(RECOVERY) != EXPECTED_TREE:
        errors.append("recovery tree mismatch")

    proc = run(
        "git",
        "diff",
        "--quiet",
        CS019_BINDING,
        RECOVERY,
        "--",
    )

    if proc.returncode != 0:
        errors.append(
            "binding-to-recovery net tree delta is not zero"
        )

    proc = run(
        "git",
        "diff",
        "--quiet",
        RECOVERY,
        BASE,
        "--",
    )

    if proc.returncode != 0:
        errors.append(
            "recovery-to-merge tree delta is not zero"
        )

    return errors


def _run_errors(
    run_id: int,
    *,
    event: str,
    head_sha: str,
    workflow_path: str,
    conclusion: str,
    head_branch: str | None = None,
) -> list[str]:
    errors: list[str] = []

    data = github_json(
        f"/actions/runs/{run_id}"
    )

    if data.get("event") != event:
        errors.append(f"run {run_id} event mismatch")

    if data.get("head_sha") != head_sha:
        errors.append(f"run {run_id} head_sha mismatch")

    if data.get("path") != workflow_path:
        errors.append(
            f"run {run_id} workflow path mismatch"
        )

    if data.get("run_attempt") != 1:
        errors.append(f"run {run_id} attempt mismatch")

    if data.get("status") != "completed":
        errors.append(f"run {run_id} is not completed")

    if data.get("conclusion") != conclusion:
        errors.append(
            f"run {run_id} conclusion mismatch"
        )

    if (
        head_branch is not None
        and data.get("head_branch") != head_branch
    ):
        errors.append(
            f"run {run_id} head_branch mismatch"
        )

    return errors


def github_errors() -> list[str]:
    errors: list[str] = []

    errors.extend(
        _run_errors(
            CS019_QUALIFYING_RUN,
            event="push",
            head_sha=CS019_SOURCE,
            workflow_path=
                ".github/workflows/"
                "cs019-ev02-fundamental-contract-hardening-validation.yml",
            conclusion="success",
            head_branch=
                "agent/cs019-ev02-fundamental-contract-hardening",
        )
    )

    errors.extend(
        _run_errors(
            CS019_TRUSTED_RUN,
            event="pull_request_target",
            head_sha=RECOVERY,
            workflow_path=
                ".github/workflows/changeset-validation.yml",
            conclusion="success",
        )
    )

    errors.extend(
        _run_errors(
            CS019_DIAGNOSTIC_RUN,
            event="pull_request",
            head_sha=RECOVERY,
            workflow_path=
                ".github/workflows/changeset-validation.yml",
            conclusion="success",
        )
    )

    errors.extend(
        _run_errors(
            CS019_PR_REGRESSION_RUN,
            event="pull_request",
            head_sha=RECOVERY,
            workflow_path=
                ".github/workflows/current-product-regression.yml",
            conclusion="success",
        )
    )

    errors.extend(
        _run_errors(
            CS000J_STALE_RUN,
            event="pull_request",
            head_sha=RECOVERY,
            workflow_path=
                ".github/workflows/"
                "cs000j-ev01-ledger-closure-validation.yml",
            conclusion="failure",
        )
    )

    errors.extend(
        _run_errors(
            MAIN_CHANGESET_RUN,
            event="push",
            head_sha=BASE,
            workflow_path=
                ".github/workflows/changeset-validation.yml",
            conclusion="success",
            head_branch="main",
        )
    )

    errors.extend(
        _run_errors(
            POST_MERGE_REGRESSION_RUN,
            event="push",
            head_sha=BASE,
            workflow_path=
                ".github/workflows/current-product-regression.yml",
            conclusion="success",
            head_branch="main",
        )
    )

    stale_jobs = github_json(
        f"/actions/runs/{CS000J_STALE_RUN}/jobs?per_page=100"
    )

    stale = [
        job
        for job in stale_jobs.get("jobs", [])
        if job.get("name")
        == "CS000J EV-01 ledger closure"
    ]

    if len(stale) != 1:
        errors.append(
            "CS000J stale job cardinality mismatch"
        )
    else:
        failed = [
            step
            for step in stale[0].get("steps", [])
            if step.get("conclusion") == "failure"
        ]

        if not (
            len(failed) == 1
            and failed[0].get("name")
            == "Verify exact EV-01 ledger closure"
        ):
            errors.append(
                "CS000J stale failure step mismatch"
            )

    pr = github_json(
        f"/pulls/{PR_NUMBER}"
    )

    if pr.get("state") != "closed":
        errors.append("PR #54 is not closed")

    if pr.get("merged_at") is None:
        errors.append("PR #54 is not merged")

    if pr.get("merge_commit_sha") != BASE:
        errors.append("PR #54 merge SHA mismatch")

    if pr.get("head", {}).get("sha") != RECOVERY:
        errors.append("PR #54 head SHA mismatch")

    if (
        pr.get("base", {}).get("sha")
        != PROTECTED_BASE
    ):
        errors.append("PR #54 base SHA mismatch")

    main = github_json(
        "/branches/main"
    )

    if (
        main.get("commit", {}).get("sha")
        != BASE
    ):
        errors.append("protected main moved")

    if main.get("protected") is not True:
        errors.append("main is no longer protected")

    checks = (
        main.get("protection", {})
        .get("required_status_checks", {})
        .get("checks", [])
    )

    trusted = [
        row
        for row in checks
        if (
            row.get("context")
            == "Trusted ChangeSet validation gate"
            and row.get("app_id") == TRUSTED_APP_ID
        )
    ]

    if len(trusted) != 1:
        errors.append(
            "trusted main protection check mismatch"
        )

    runs = github_json(
        "/actions/workflows/"
        "cs019-ev02-fundamental-contract-hardening-validation.yml"
        "/runs?event=push&per_page=100"
    )

    original = [
        row
        for row in runs.get("workflow_runs", [])
        if (
            row.get("head_sha") == CS019_SOURCE
            and row.get("event") == "push"
        )
    ]

    if not (
        len(original) == 1
        and original[0].get("id")
        == CS019_QUALIFYING_RUN
        and original[0].get("run_attempt") == 1
        and original[0].get("conclusion") == "success"
    ):
        errors.append(
            "original CS019 qualifying run identity changed"
        )

    forbidden_heads = {
        CS019_BINDING,
        ACCIDENTAL,
        RECOVERY,
        BASE,
    }

    unexpected = [
        row
        for row in runs.get("workflow_runs", [])
        if row.get("head_sha") in forbidden_heads
    ]

    if unexpected:
        errors.append(
            "CS019 qualifying workflow reran "
            "after source qualification"
        )

    return errors


def workflow_retirement_errors() -> list[str]:
    errors: list[str] = []

    current_old = (
        ROOT / OLD_WORKFLOW
    ).read_text(encoding="utf-8")

    prior_raw = git_show_bytes(
        BASE,
        OLD_WORKFLOW.as_posix(),
    )

    if prior_raw is None:
        return [
            "cannot read historical CS000J workflow"
        ]

    prior_old = prior_raw.decode("utf-8")

    if top_level_on_keys(prior_old) != {"pull_request"}:
        errors.append(
            "historical CS000J workflow trigger "
            "history changed"
        )

    if (
        top_level_on_keys(current_old)
        != {"workflow_dispatch"}
    ):
        errors.append(
            "CS000J workflow is not manual-only"
        )

    if (
        suffix_from_permissions(current_old)
        != suffix_from_permissions(prior_old)
    ):
        errors.append(
            "CS000J historical workflow body "
            "changed during retirement"
        )

    self_text = (
        ROOT / SELF_WORKFLOW
    ).read_text(encoding="utf-8")

    if (
        top_level_on_keys(self_text)
        != {"pull_request"}
    ):
        errors.append(
            "CS000K workflow trigger mismatch"
        )

    if (
        trigger_paths(self_text)
        != EXPECTED_TRIGGER_PATHS
    ):
        errors.append(
            "CS000K trigger-path inventory mismatch"
        )

    for forbidden in (
        "audit/CURRENT_CHANGESET_VALIDATION.json",
        "MANIFEST.sha256",
        RESULT.as_posix(),
    ):
        if forbidden in trigger_paths(self_text):
            errors.append(
                "binding path unexpectedly retriggers "
                f"CS000K: {forbidden}"
            )

    required_tokens = (
        "name: CS000K EV-02 ledger closure",
        "name: Checkout exact candidate",
        "ref: ${{ github.event.pull_request.head.sha }}",
        "name: Verify preserved net-zero post-acceptance recovery",
        "name: Verify exact EV-02 ledger closure",
        "name: Verify EVREQ-009 through EVREQ-012",
        "name: Verify no product release or EV-03 effects",
    )

    for token in required_tokens:
        if token not in self_text:
            errors.append(
                "CS000K workflow token missing: "
                + token
            )

    return errors


def ledger_errors() -> list[str]:
    errors: list[str] = []

    prior = git_show_json(
        BASE,
        ROADMAP.as_posix(),
    )

    expected = copy.deepcopy(prior)

    stages = {
        row.get("stage_id"): row
        for row in expected.get("stages", [])
        if isinstance(row, dict)
    }

    ev02 = stages.get("EV-02")

    if not isinstance(ev02, dict):
        return [
            "EV-02 missing from baseline roadmap"
        ]

    ev02["status"] = "accepted"
    ev02["accepted_commit"] = BASE
    ev02["evidence_manifest"] = EVIDENCE.as_posix()
    ev02["decision_record"] = DECISION.as_posix()

    actual = load(ROADMAP)

    if actual != expected:
        errors.append(
            "roadmap differs from exact "
            "EV-02-only closure"
        )

    if actual.get("current_stage") != "EV-02":
        errors.append(
            "current_stage advanced during closure"
        )

    if actual.get("release_authorized") is not False:
        errors.append(
            "release became authorized"
        )

    actual_stages = {
        row.get("stage_id"): row
        for row in actual.get("stages", [])
        if isinstance(row, dict)
    }

    ev03 = actual_stages.get("EV-03")

    if not isinstance(ev03, dict):
        errors.append("EV-03 missing")
    else:
        if ev03.get("status") != "not_started":
            errors.append(
                "EV-03 started prematurely"
            )

        if ev03.get("planned_changeset") != "CS020":
            errors.append(
                "EV-03 planned ChangeSet changed"
            )

        if ev03.get("accepted_commit") is not None:
            errors.append(
                "EV-03 accepted prematurely"
            )

    return errors


def requirements_errors() -> list[str]:
    errors: list[str] = []

    prior = git_show_json(
        BASE,
        REQS.as_posix(),
    )

    expected = copy.deepcopy(prior)

    found: set[str] = set()

    for row in expected.get("requirements", []):
        if not isinstance(row, dict):
            continue

        rid = row.get("requirement_id")

        if rid not in REQ_EVIDENCE:
            continue

        found.add(rid)
        row["status"] = "verified"
        row["evidence"] = REQ_EVIDENCE[rid]

    if found != set(REQ_EVIDENCE):
        errors.append(
            "baseline EV-02 requirement identity mismatch"
        )
        return errors

    actual = load(REQS)

    if actual != expected:
        errors.append(
            "requirements ledger differs from exact "
            "EVREQ-009..012 closure"
        )

    for row in actual.get("requirements", []):
        if not isinstance(row, dict):
            continue

        if row.get("stage") == "EV-03":
            if row.get("status") != "planned":
                errors.append(
                    f"{row.get('requirement_id')} "
                    "started prematurely"
                )

            if row.get("evidence") != []:
                errors.append(
                    f"{row.get('requirement_id')} "
                    "has premature evidence"
                )

    return errors


def evidence_errors() -> list[str]:
    errors: list[str] = []

    manifest = load(EVIDENCE)

    expected_header = {
        "schema":
            "neoeng.dcore.evolution-evidence-manifest.v1",
        "stage": "EV-02",
        "changeset": "CS019",
        "closure_changeset": "CS000K",
        "source_commit": BASE,
        "hash_mode": "lf-normalized-text",
    }

    for key, value in expected_header.items():
        if manifest.get(key) != value:
            errors.append(
                f"EV-02 evidence manifest mismatch: {key}"
            )

    if (
        manifest.get("external_evidence")
        != EXTERNAL_EVIDENCE
    ):
        errors.append(
            "external EV-02 evidence binding mismatch"
        )

    rows = manifest.get("files")

    if not isinstance(rows, list):
        errors.append(
            "evidence file inventory missing"
        )
    else:
        actual_files = {
            row.get("path"): row.get("sha256")
            for row in rows
            if isinstance(row, dict)
        }

        expected_files = {
            rel: sha256_lf(ROOT / rel)
            for rel in EVIDENCE_FILES
        }

        if actual_files != expected_files:
            errors.append(
                "EV-02 evidence file hashes mismatch"
            )

        if len(rows) != len(EVIDENCE_FILES):
            errors.append(
                "EV-02 evidence file cardinality mismatch"
            )

    decision = (
        ROOT / DECISION
    ).read_text(encoding="utf-8")

    required_tokens = (
        "Decision: **ACCEPT EV-02 ledger closure**",
        "`9e35c25fd4c618d4707067f8760712c549f01a3e`",
        "`32644940394` attempt `1`",
        "`32647449825` attempt `1`",
        "`32650927099` attempt `1`",
        "`32650927094` attempt `1`",
        "`NOT_APPLICABLE_STALE_SCOPE`",
        "`EVREQ-009..012 = verified`",
        "`EV-03` remains `not_started`",
        "`release_authorized` remains `false`",
    )

    for token in required_tokens:
        if token not in decision:
            errors.append(
                "EV-02 decision token missing: "
                + token
            )

    return errors


def scope_errors() -> list[str]:
    errors: list[str] = []

    actual = changed_paths()

    expected = (
        CLOSURE_ALLOWED
        if (ROOT / RESULT).exists()
        else SOURCE_REQUIRED
    )

    extra = sorted(actual - expected)
    missing = sorted(expected - actual)

    if extra:
        errors.append(
            "paths outside administrative closure: "
            + ", ".join(extra)
        )

    if missing:
        errors.append(
            "required administrative paths missing: "
            + ", ".join(missing)
        )

    return errors


def non_effect_errors() -> list[str]:
    errors: list[str] = []

    actual = changed_paths()

    forbidden_prefixes = (
        "src/",
        "include/",
        "tests/",
        "modules/",
        "apps/",
        "tools/",
    )

    for rel in sorted(actual):
        if rel.startswith(forbidden_prefixes):
            errors.append(
                f"product path changed by CS000K: {rel}"
            )

        if rel == "CMakeLists.txt":
            errors.append(
                "root build definition changed"
            )

    for rel in EVIDENCE_FILES:
        prior = git_show_bytes(BASE, rel)

        if prior is None:
            errors.append(
                "cannot read immutable predecessor "
                f"evidence: {rel}"
            )
            continue

        current = ROOT / rel

        if not current.is_file():
            errors.append(
                "immutable predecessor evidence "
                f"missing: {rel}"
            )
            continue

        if current.read_bytes() != prior:
            errors.append(
                "predecessor evidence bytes "
                f"changed: {rel}"
            )

    roadmap = load(ROADMAP)

    if roadmap.get("current_stage") != "EV-02":
        errors.append("current_stage advanced")

    if roadmap.get("release_authorized") is not False:
        errors.append("release became authorized")

    stages = {
        row.get("stage_id"): row
        for row in roadmap.get("stages", [])
        if isinstance(row, dict)
    }

    ev03 = stages.get("EV-03")

    if (
        not isinstance(ev03, dict)
        or ev03.get("status") != "not_started"
    ):
        errors.append("EV-03 effect detected")

    return errors


def manifest_errors() -> list[str]:
    proc = run(
        "python",
        "-B",
        "scripts/generate_manifest.py",
        "--check",
    )

    if proc.returncode != 0:
        return [
            proc.stdout.strip()
            or proc.stderr.strip()
            or "manifest check failed"
        ]

    return []


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
        "--integration",
        action="store_true",
    )

    parser.add_argument(
        "--recovery",
        action="store_true",
    )

    parser.add_argument(
        "--github",
        action="store_true",
    )

    parser.add_argument(
        "--workflow-retirement",
        action="store_true",
    )

    parser.add_argument(
        "--ledger",
        action="store_true",
    )

    parser.add_argument(
        "--requirements",
        action="store_true",
    )

    parser.add_argument(
        "--evidence",
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

    parser.add_argument(
        "--manifest",
        action="store_true",
    )

    args = parser.parse_args()

    selected = (
        args.self_test,
        args.authority,
        args.integration,
        args.recovery,
        args.github,
        args.workflow_retirement,
        args.ledger,
        args.requirements,
        args.evidence,
        args.scope,
        args.non_effects,
        args.manifest,
    )

    if sum(selected) != 1:
        parser.error(
            "select exactly one verification mode"
        )

    try:
        if args.self_test:
            return emit(
                "self-test",
                self_test_errors(),
            )

        if args.authority:
            return emit(
                "prospective-authority",
                authority_errors(),
            )

        if args.integration:
            return emit(
                "cs019-integration",
                integration_errors(),
            )

        if args.recovery:
            return emit(
                "net-zero-recovery",
                recovery_errors(),
            )

        if args.github:
            return emit(
                "github-provenance",
                github_errors(),
            )

        if args.workflow_retirement:
            return emit(
                "cs000j-workflow-retirement",
                workflow_retirement_errors(),
            )

        if args.ledger:
            return emit(
                "ev02-ledger-closure",
                ledger_errors(),
            )

        if args.requirements:
            return emit(
                "evreq-009-012",
                requirements_errors(),
            )

        if args.evidence:
            return emit(
                "ev02-acceptance-evidence",
                evidence_errors(),
            )

        if args.scope:
            return emit(
                "administrative-only-scope",
                scope_errors(),
            )

        if args.non_effects:
            return emit(
                "non-effects",
                non_effect_errors(),
            )

        return emit(
            "manifest",
            manifest_errors(),
        )

    except (
        OSError,
        RuntimeError,
        ValueError,
        KeyError,
        json.JSONDecodeError,
    ) as exc:
        return emit(
            "exception",
            [str(exc)],
        )


if __name__ == "__main__":
    raise SystemExit(main())
