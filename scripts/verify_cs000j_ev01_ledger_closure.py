#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

BASE = "342d19079f395c02c95655c630370077a089738c"
PROTECTED_BASE = "e9e095e61d4de2995db704c51e9308850e1c929d"
CS018_HEAD = "b77cc8495cbda8a9e73d117338bf0d94d75672bf"
CS018_SOURCE = "127f2afaf056ef396752f491d30091014e4ba7e4"

CS018_QUALIFYING_RUN = 32612620385
CS018_TRUSTED_RUN = 32613076285
CS018_DIAGNOSTIC_RUN = 32613076284
CS018_PR_REGRESSION_RUN = 32613076295
CS000I_STALE_RUN = 32613076327
MAIN_CHANGESET_RUN = 32613410810
POST_MERGE_REGRESSION_RUN = 32613410836

TRUSTED_APP_ID = 15368
PR_NUMBER = 52

ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
REQS = Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json")
TRANSITION = Path("audit/GOVERNANCE_TRANSITION_STATE.json")
POLICY = Path("audit/CHANGESET_VALIDATION_POLICY.json")

OLD_WORKFLOW = Path(
    ".github/workflows/cs000i-ev00-ledger-closure-validation.yml"
)
SELF_WORKFLOW = Path(
    ".github/workflows/cs000j-ev01-ledger-closure-validation.yml"
)

PLAN = Path("audit/validation/CS000J/VALIDATION_PLAN.json")
RESULT = Path("audit/validation/CS000J/VALIDATION_RESULT.json")
DESCRIPTOR = Path("audit/CURRENT_CHANGESET_VALIDATION.json")

CHANGESET = Path("docs/changesets/000J/CHANGESET.md")
EVIDENCE = Path(
    "docs/changesets/000J/evidence/EV01_ACCEPTANCE_MANIFEST.json"
)
DECISION = Path("docs/records/evolution/DEV-0008.md")
GLOBAL_MANIFEST = Path("MANIFEST.sha256")

EXPECTED_TRIGGER_PATHS = {
    ".github/workflows/cs000i-ev00-ledger-closure-validation.yml",
    ".github/workflows/cs000j-ev01-ledger-closure-validation.yml",
    "scripts/verify_cs000j_ev01_ledger_closure.py",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/validation/CS000J/VALIDATION_PLAN.json",
    "docs/changesets/000J/CHANGESET.md",
    "docs/changesets/000J/evidence/EV01_ACCEPTANCE_MANIFEST.json",
    "docs/records/evolution/DEV-0008.md",
}

SOURCE_REQUIRED = EXPECTED_TRIGGER_PATHS | {
    "audit/CURRENT_CHANGESET_VALIDATION.json",
    "MANIFEST.sha256",
}

CLOSURE_ALLOWED = SOURCE_REQUIRED | {
    "audit/validation/CS000J/VALIDATION_RESULT.json",
}

EVIDENCE_FILES = [
    "audit/validation/CS018/VALIDATION_PLAN_R2.json",
    "audit/validation/CS018/VALIDATION_RESULT_R2.json",
    "audit/validation/CS018/ATTEMPT_001_NONACCEPTANCE.json",
    "docs/changesets/018/BUILD_CI_DISCOVERY.json",
    "docs/changesets/018/CHANGESET.md",
    "docs/changesets/018/CHANGESET_R2.md",
    ".github/workflows/cs018-validation.yml",
    ".github/workflows/current-product-regression.yml",
    "scripts/verify_build_ci_governance.py",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    ".github/workflows/changeset-validation.yml",
]

REQ_EVIDENCE = {
    "EVREQ-005": [
        "docs/changesets/018/BUILD_CI_DISCOVERY.json",
        "audit/validation/CS018/VALIDATION_RESULT_R2.json",
        ".github/workflows/current-product-regression.yml",
        EVIDENCE.as_posix(),
    ],
    "EVREQ-006": [
        "audit/validation/CS018/VALIDATION_RESULT_R2.json",
        ".github/workflows/changeset-validation.yml",
        ".github/workflows/current-product-regression.yml",
        EVIDENCE.as_posix(),
    ],
    "EVREQ-007": [
        "docs/changesets/018/BUILD_CI_DISCOVERY.json",
        ".github/workflows/cs018-validation.yml",
        ".github/workflows/current-product-regression.yml",
        EVIDENCE.as_posix(),
    ],
    "EVREQ-008": [
        "audit/validation/CS018/ATTEMPT_001_NONACCEPTANCE.json",
        "audit/validation/CS018/VALIDATION_RESULT_R2.json",
        "docs/changesets/018/CHANGESET_R2.md",
        EVIDENCE.as_posix(),
    ],
}

EXTERNAL_EVIDENCE = {
    "cs018_qualifying_run": {
        "run_id": CS018_QUALIFYING_RUN,
        "run_attempt": 1,
        "source_sha": CS018_SOURCE,
        "workflow_path": ".github/workflows/cs018-validation.yml",
        "conclusion": "success",
    },
    "pr52_trusted_gate": {
        "run_id": CS018_TRUSTED_RUN,
        "run_attempt": 1,
        "head_sha": CS018_HEAD,
        "workflow_path": ".github/workflows/changeset-validation.yml",
        "conclusion": "success",
    },
    "pr52_candidate_diagnostic": {
        "run_id": CS018_DIAGNOSTIC_RUN,
        "run_attempt": 1,
        "head_sha": CS018_HEAD,
        "workflow_path": ".github/workflows/changeset-validation.yml",
        "conclusion": "success",
    },
    "pr52_product_regression": {
        "run_id": CS018_PR_REGRESSION_RUN,
        "run_attempt": 1,
        "head_sha": CS018_HEAD,
        "workflow_path": ".github/workflows/current-product-regression.yml",
        "conclusion": "success",
    },
    "pr52_cs000i_stale_scope": {
        "run_id": CS000I_STALE_RUN,
        "run_attempt": 1,
        "head_sha": CS018_HEAD,
        "workflow_path": OLD_WORKFLOW.as_posix(),
        "conclusion": "failure",
        "classification": "NOT_APPLICABLE_STALE_SCOPE",
        "rerun": False,
    },
    "main_changeset_validation": {
        "run_id": MAIN_CHANGESET_RUN,
        "run_attempt": 1,
        "head_sha": BASE,
        "workflow_path": ".github/workflows/changeset-validation.yml",
        "conclusion": "success",
    },
    "post_merge_product_regression": {
        "run_id": POST_MERGE_REGRESSION_RUN,
        "run_attempt": 1,
        "head_sha": BASE,
        "workflow_path": ".github/workflows/current-product-regression.yml",
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


def load(path: Path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


def git_show(ref: str, path: str) -> bytes | None:
    proc = subprocess.run(
        ["git", "show", f"{ref}:{path}"],
        cwd=ROOT,
        capture_output=True,
        check=False,
    )
    return proc.stdout if proc.returncode == 0 else None


def sha256_lf(path: Path) -> str:
    return hashlib.sha256(
        path.read_bytes().replace(b"\r\n", b"\n")
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


def emit(label: str, errors: list[str]) -> int:
    if errors:
        print(f"CS000J EV-01 LEDGER CLOSURE: REJECT — {label}")
        for item in errors:
            print(f"- {item}")
        return 1

    print(f"CS000J EV-01 LEDGER CLOSURE: PASS — {label}")
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

    if (
        "audit/CURRENT_CHANGESET_VALIDATION.json"
        in EXPECTED_TRIGGER_PATHS
    ):
        errors.append("descriptor unexpectedly triggers qualification")

    if "MANIFEST.sha256" in EXPECTED_TRIGGER_PATHS:
        errors.append("manifest unexpectedly triggers qualification")

    if RESULT.as_posix() in EXPECTED_TRIGGER_PATHS:
        errors.append("result unexpectedly triggers qualification")

    return errors


def authority_errors() -> list[str]:
    errors: list[str] = []

    transition = load(TRANSITION)

    if (
        transition.get("schema")
        != "neoeng.dcore.governance-transition-state.v1"
    ):
        errors.append("governance transition schema mismatch")
        return errors

    authority = transition.get("prospective_authority")

    expected_authority = {
        "regime_id": "CHANGESET_VALIDATION",
        "policy": "audit/CHANGESET_VALIDATION_POLICY.json",
        "policy_document":
            "docs/governance/CHANGESET_VALIDATION_POLICY.md",
        "workflow": ".github/workflows/changeset-validation.yml",
        "verifier": "scripts/verify_changeset_validation.py",
        "required_branch_check":
            "Trusted ChangeSet validation gate",
        "required_branch_check_app_id": TRUSTED_APP_ID,
    }

    if authority != expected_authority:
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

    if (
        policy.get("schema")
        != "neoeng.dcore.changeset-validation-policy.v1"
    ):
        errors.append("ChangeSet validation policy schema mismatch")

    if policy.get("lifecycle_states") != [
        "PLANNED",
        "IMPLEMENTED",
        "VALIDATED",
        "ACCEPTED",
    ]:
        errors.append("ChangeSet lifecycle mismatch")

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
            errors.append(f"ChangeSet policy invariant mismatch: {key}")

    if policy.get("required_run_fields") != [
        "source_sha",
        "run_id",
        "run_attempt",
        "workflow_path",
    ]:
        errors.append("required exact-run fields mismatch")

    return errors


def integration_errors() -> list[str]:
    errors: list[str] = []

    parents = run(
        "git",
        "rev-list",
        "--parents",
        "-n",
        "1",
        BASE,
    )

    expected_parents = [
        BASE,
        PROTECTED_BASE,
        CS018_HEAD,
    ]

    if (
        parents.returncode != 0
        or parents.stdout.split() != expected_parents
    ):
        errors.append("accepted CS018 merge parents mismatch")

    tree = run(
        "git",
        "diff",
        "--quiet",
        CS018_HEAD,
        BASE,
        "--",
    )

    if tree.returncode != 0:
        errors.append(
            "accepted integration tree differs from CS018 head"
        )

    result = load(
        Path("audit/validation/CS018/VALIDATION_RESULT_R2.json")
    )

    expected = {
        "changeset": "CS018",
        "plan_commit": CS018_SOURCE,
        "source_sha": CS018_SOURCE,
        "run_id": CS018_QUALIFYING_RUN,
        "run_attempt": 1,
        "workflow_path": ".github/workflows/cs018-validation.yml",
        "validation_state": "VALIDATED",
        "acceptance_decision": "ACCEPTED",
    }

    for key, value in expected.items():
        if result.get(key) != value:
            errors.append(
                f"CS018 validation result mismatch: {key}"
            )

    tests = result.get("tests")

    if not isinstance(tests, list) or len(tests) != 18:
        errors.append(
            "CS018 R2 result must contain exactly 18 tests"
        )
    elif any(
        not isinstance(item, dict)
        or item.get("status") != "PASS"
        for item in tests
    ):
        errors.append(
            "CS018 R2 result contains non-PASS test"
        )

    r1 = result.get("preserved_history", {}).get("r1", {})

    if not (
        r1.get("source_sha")
        == "4adccdb77607fc6d6f886369a842d2efa48a7028"
        and r1.get("validation_state") == "BLOCKED"
        and r1.get("acceptance_decision") == "NOT_ACCEPTED"
        and r1.get("rerun") is False
    ):
        errors.append("CS018 R1 preservation mismatch")

    effects = result.get("effects", {})

    if not (
        effects.get("runtime") == "NONE"
        and effects.get("abi") == "NONE"
        and effects.get("product_source") == "NONE"
        and effects.get("ev01_stage_acceptance") == "NOT_SET"
        and effects.get("release") == "NOT_AUTHORIZED"
        and effects.get("historical_evidence_rewrite") == "NONE"
    ):
        errors.append("CS018 result effects mismatch")

    return errors


def github_get(path: str):
    token = os.environ.get("GITHUB_TOKEN", "")
    repo = os.environ.get("GITHUB_REPOSITORY", "")

    if not token or not repo:
        raise ValueError(
            "GITHUB_TOKEN/GITHUB_REPOSITORY missing"
        )

    req = urllib.request.Request(
        f"https://api.github.com/repos/{repo}{path}",
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "neoeng-cs000j-verifier",
        },
    )

    with urllib.request.urlopen(
        req,
        timeout=20,
    ) as response:
        return json.loads(
            response.read().decode("utf-8")
        )


def expect_run(
    errors: list[str],
    *,
    run_id: int,
    sha: str,
    workflow: str,
    event: str,
    conclusion: str,
) -> dict:
    doc = github_get(f"/actions/runs/{run_id}")

    checks = {
        "id": run_id,
        "run_attempt": 1,
        "head_sha": sha,
        "path": workflow,
        "event": event,
        "status": "completed",
        "conclusion": conclusion,
    }

    for key, value in checks.items():
        if doc.get(key) != value:
            errors.append(
                f"run {run_id} mismatch: {key}"
            )

    return doc


def github_errors() -> list[str]:
    errors: list[str] = []

    try:
        pr = github_get(f"/pulls/{PR_NUMBER}")

        if pr.get("merged") is not True:
            errors.append("PR #52 is not merged")

        if pr.get("merge_commit_sha") != BASE:
            errors.append("PR #52 merge commit mismatch")

        if pr.get("head", {}).get("sha") != CS018_HEAD:
            errors.append("PR #52 head mismatch")

        if (
            pr.get("base", {}).get("sha")
            != PROTECTED_BASE
        ):
            errors.append("PR #52 base mismatch")

        expect_run(
            errors,
            run_id=CS018_QUALIFYING_RUN,
            sha=CS018_SOURCE,
            workflow=".github/workflows/cs018-validation.yml",
            event="push",
            conclusion="success",
        )

        expect_run(
            errors,
            run_id=CS018_TRUSTED_RUN,
            sha=CS018_HEAD,
            workflow=".github/workflows/changeset-validation.yml",
            event="pull_request_target",
            conclusion="success",
        )

        expect_run(
            errors,
            run_id=CS018_DIAGNOSTIC_RUN,
            sha=CS018_HEAD,
            workflow=".github/workflows/changeset-validation.yml",
            event="pull_request",
            conclusion="success",
        )

        expect_run(
            errors,
            run_id=CS018_PR_REGRESSION_RUN,
            sha=CS018_HEAD,
            workflow=".github/workflows/current-product-regression.yml",
            event="pull_request",
            conclusion="success",
        )

        expect_run(
            errors,
            run_id=CS000I_STALE_RUN,
            sha=CS018_HEAD,
            workflow=OLD_WORKFLOW.as_posix(),
            event="pull_request",
            conclusion="failure",
        )

        expect_run(
            errors,
            run_id=MAIN_CHANGESET_RUN,
            sha=BASE,
            workflow=".github/workflows/changeset-validation.yml",
            event="push",
            conclusion="success",
        )

        expect_run(
            errors,
            run_id=POST_MERGE_REGRESSION_RUN,
            sha=BASE,
            workflow=".github/workflows/current-product-regression.yml",
            event="push",
            conclusion="success",
        )

        stale_jobs = github_get(
            f"/actions/runs/{CS000I_STALE_RUN}/jobs?per_page=100"
        ).get("jobs", [])

        stale = [
            job
            for job in stale_jobs
            if job.get("name")
            == "CS000I EV-00 ledger closure"
        ]

        if len(stale) != 1:
            errors.append(
                "CS000I stale-scope job missing/ambiguous"
            )
        else:
            failed_steps = [
                step.get("name")
                for step in stale[0].get("steps", [])
                if step.get("conclusion") == "failure"
            ]

            if failed_steps != [
                "Verify exact EV-00 ledger closure"
            ]:
                errors.append(
                    "CS000I stale-scope failure shape changed"
                )

        regression_jobs = github_get(
            f"/actions/runs/{POST_MERGE_REGRESSION_RUN}"
            "/jobs?per_page=100"
        ).get("jobs", [])

        regression = [
            job
            for job in regression_jobs
            if job.get("name") == "Current product regression"
        ]

        if (
            len(regression) != 1
            or regression[0].get("conclusion") != "success"
        ):
            errors.append(
                "post-merge regression job is not successful"
            )
        else:
            required_steps = {
                "Checkout exact source",
                "Install build dependencies",
                "Build CI governance self-test",
                "Verify workflow classification",
                "Verify critical action pinning",
                "Verify current CMake options",
                "Verify permanent regression contract",
                "Verify historical workflow boundary",
                "Configure product regression",
                "Build product regression",
                "Run product smoke regression",
            }

            successful = {
                step.get("name")
                for step in regression[0].get("steps", [])
                if step.get("conclusion") == "success"
            }

            missing = sorted(required_steps - successful)

            if missing:
                errors.append(
                    "post-merge regression missing successful steps: "
                    + ", ".join(missing)
                )

        checks = github_get(
            f"/commits/{CS018_HEAD}/check-runs?per_page=100"
        ).get("check_runs", [])

        trusted_ok = any(
            item.get("name")
            == "Trusted ChangeSet validation gate"
            and item.get("conclusion") == "success"
            and item.get("app", {}).get("id")
            == TRUSTED_APP_ID
            for item in checks
            if isinstance(item, dict)
        )

        if not trusted_ok:
            errors.append(
                "trusted gate/app binding missing on CS018 head"
            )

        main_checks = github_get(
            f"/commits/{BASE}/check-runs?per_page=100"
        ).get("check_runs", [])

        main_ok = any(
            item.get("name")
            == "Main ChangeSet validation"
            and item.get("conclusion") == "success"
            and item.get("app", {}).get("id")
            == TRUSTED_APP_ID
            for item in main_checks
            if isinstance(item, dict)
        )

        if not main_ok:
            errors.append(
                "Main ChangeSet validation/app binding missing"
            )

    except Exception as exc:
        errors.append(f"GitHub provenance read failed: {exc}")

    return errors


def workflow_retirement_errors() -> list[str]:
    errors: list[str] = []

    prior_bytes = git_show(
        BASE,
        OLD_WORKFLOW.as_posix(),
    )

    if prior_bytes is None:
        return [
            "cannot read pre-closure CS000I workflow from base"
        ]

    prior = prior_bytes.decode("utf-8")
    current = (
        ROOT / OLD_WORKFLOW
    ).read_text(encoding="utf-8")

    if top_level_on_keys(current) != {"workflow_dispatch"}:
        errors.append(
            "CS000I workflow is not manual-only"
        )

    if (
        suffix_from_permissions(prior)
        != suffix_from_permissions(current)
    ):
        errors.append(
            "CS000I workflow body changed from permissions onward"
        )

    self_text = (
        ROOT / SELF_WORKFLOW
    ).read_text(encoding="utf-8")

    if top_level_on_keys(self_text) != {"pull_request"}:
        errors.append(
            "CS000J workflow must be pull_request-only"
        )

    paths = trigger_paths(self_text)

    if paths != EXPECTED_TRIGGER_PATHS:
        errors.append(
            "CS000J workflow trigger paths differ from exact bounded set"
        )

    prohibited_triggers = {
        DESCRIPTOR.as_posix(),
        RESULT.as_posix(),
        GLOBAL_MANIFEST.as_posix(),
    }

    if paths & prohibited_triggers:
        errors.append(
            "CS000J result/descriptor/manifest must not retrigger qualification"
        )

    return errors


def ledger_errors() -> list[str]:
    errors: list[str] = []

    prior_raw = git_show(BASE, ROADMAP.as_posix())

    if prior_raw is None:
        return ["cannot read base roadmap"]

    prior = json.loads(
        prior_raw.decode("utf-8")
    )

    current = load(ROADMAP)

    expected = copy.deepcopy(prior)

    ev01 = next(
        row
        for row in expected["stages"]
        if row["stage_id"] == "EV-01"
    )

    ev01.update({
        "status": "accepted",
        "accepted_commit": BASE,
        "evidence_manifest": EVIDENCE.as_posix(),
        "decision_record": DECISION.as_posix(),
    })

    if current != expected:
        errors.append(
            "roadmap differs from exact EV-01 acceptance transition"
        )

    live_ev02 = next(
        row
        for row in current["stages"]
        if row["stage_id"] == "EV-02"
    )

    if current.get("current_stage") != "EV-01":
        errors.append(
            "current_stage advanced during EV-01 closure"
        )

    if current.get("release_authorized") is not False:
        errors.append("release_authorized changed")

    if live_ev02.get("status") != "not_started":
        errors.append(
            "EV-02 started during EV-01 closure"
        )

    return errors


def requirements_errors() -> list[str]:
    errors: list[str] = []

    prior_raw = git_show(BASE, REQS.as_posix())

    if prior_raw is None:
        return ["cannot read base requirements ledger"]

    prior = json.loads(
        prior_raw.decode("utf-8")
    )

    expected = copy.deepcopy(prior)

    for row in expected["requirements"]:
        rid = row.get("requirement_id")

        if rid in REQ_EVIDENCE:
            row["status"] = "verified"
            row["evidence"] = REQ_EVIDENCE[rid]

    current = load(REQS)

    if current != expected:
        errors.append(
            "requirements ledger differs from exact EVREQ-005..008 closure"
        )

    by_id = {
        row.get("requirement_id"): row
        for row in current.get("requirements", [])
        if isinstance(row, dict)
    }

    for rid, evidence in REQ_EVIDENCE.items():
        row = by_id.get(rid)

        if not isinstance(row, dict):
            errors.append(f"missing requirement {rid}")
            continue

        if row.get("status") != "verified":
            errors.append(
                f"{rid} is not verified"
            )

        if row.get("evidence") != evidence:
            errors.append(
                f"{rid} evidence binding mismatch"
            )

    return errors


def evidence_errors() -> list[str]:
    errors: list[str] = []

    manifest = load(EVIDENCE)

    expected_header = {
        "schema": "neoeng.dcore.evolution-evidence-manifest.v1",
        "stage": "EV-01",
        "changeset": "CS018",
        "closure_changeset": "CS000J",
        "source_commit": BASE,
        "hash_mode": "lf-normalized-text",
    }

    for key, value in expected_header.items():
        if manifest.get(key) != value:
            errors.append(
                f"EV-01 evidence manifest mismatch: {key}"
            )

    if (
        manifest.get("external_evidence")
        != EXTERNAL_EVIDENCE
    ):
        errors.append(
            "EV-01 external evidence bindings mismatch"
        )

    rows = manifest.get("files")

    if not isinstance(rows, list):
        return errors + [
            "EV-01 evidence manifest files missing"
        ]

    by_path = {
        row.get("path"): row
        for row in rows
        if isinstance(row, dict)
    }

    if set(by_path) != set(EVIDENCE_FILES):
        errors.append(
            "EV-01 evidence file inventory mismatch"
        )

    for path in EVIDENCE_FILES:
        row = by_path.get(path)

        if not isinstance(row, dict):
            continue

        local = ROOT / path

        if not local.is_file():
            errors.append(
                f"evidence file missing: {path}"
            )
            continue

        actual = sha256_lf(local)

        if row.get("sha256") != actual:
            errors.append(
                f"evidence hash mismatch: {path}"
            )

    decision = (
        ROOT / DECISION
    ).read_text(encoding="utf-8")

    required_text = [
        "Decision: **ACCEPT EV-01 ledger closure**",
        BASE,
        CS018_HEAD,
        CS018_SOURCE,
        str(CS018_QUALIFYING_RUN),
        str(CS018_TRUSTED_RUN),
        str(POST_MERGE_REGRESSION_RUN),
        "NOT_APPLICABLE_STALE_SCOPE",
        "release_authorized` remains `false",
    ]

    for token in required_text:
        if token not in decision:
            errors.append(
                f"decision record missing binding: {token}"
            )

    return errors


def scope_errors() -> list[str]:
    try:
        paths = changed_paths()
    except RuntimeError as exc:
        return [str(exc)]

    errors: list[str] = []

    missing = sorted(
        SOURCE_REQUIRED - paths
    )

    extra = sorted(
        paths - CLOSURE_ALLOWED
    )

    if missing:
        errors.append(
            "required CS000J source paths missing: "
            + ", ".join(missing)
        )

    if extra:
        errors.append(
            "paths outside CS000J administrative closure: "
            + ", ".join(extra)
        )

    return errors


def non_effects_errors() -> list[str]:
    errors: list[str] = []

    roadmap = load(ROADMAP)

    if roadmap.get("release_authorized") is not False:
        errors.append(
            "release authorization changed"
        )

    ev02 = next(
        (
            row
            for row in roadmap.get("stages", [])
            if row.get("stage_id") == "EV-02"
        ),
        None,
    )

    if not isinstance(ev02, dict):
        errors.append("EV-02 row missing")
    elif ev02.get("status") != "not_started":
        errors.append(
            "EV-02 was started by administrative closure"
        )

    protected = run(
        "git",
        "diff",
        "--name-only",
        BASE,
        "--",
        "src",
        "include",
        "CMakeLists.txt",
        "cmake",
        "tests",
        "tools",
        "examples",
        "modules",
        "apps",
    )

    if protected.returncode != 0:
        errors.append(
            "cannot inspect product non-effects"
        )
    elif protected.stdout.strip():
        errors.append(
            "product/runtime/build/test surface changed"
        )

    return errors


def manifest_errors() -> list[str]:
    proc = subprocess.run(
        [
            sys.executable,
            "-B",
            str(ROOT / "scripts/generate_manifest.py"),
            "--check",
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )

    if proc.returncode != 0:
        return [
            proc.stdout.strip()
            or proc.stderr.strip()
            or "MANIFEST.sha256 check failed"
        ]

    return []


def main() -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--authority", action="store_true")
    parser.add_argument("--integration", action="store_true")
    parser.add_argument("--github", action="store_true")
    parser.add_argument(
        "--workflow-retirement",
        action="store_true",
    )
    parser.add_argument("--ledger", action="store_true")
    parser.add_argument("--requirements", action="store_true")
    parser.add_argument("--evidence", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--non-effects", action="store_true")
    parser.add_argument("--manifest", action="store_true")

    args = parser.parse_args()

    selected = [
        args.self_test,
        args.authority,
        args.integration,
        args.github,
        args.workflow_retirement,
        args.ledger,
        args.requirements,
        args.evidence,
        args.scope,
        args.non_effects,
        args.manifest,
    ]

    if sum(bool(item) for item in selected) != 1:
        parser.error("exactly one verification mode is required")

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

    if args.integration:
        return emit(
            "integration",
            integration_errors(),
        )

    if args.github:
        return emit(
            "github",
            github_errors(),
        )

    if args.workflow_retirement:
        return emit(
            "workflow-retirement",
            workflow_retirement_errors(),
        )

    if args.ledger:
        return emit(
            "ledger",
            ledger_errors(),
        )

    if args.requirements:
        return emit(
            "requirements",
            requirements_errors(),
        )

    if args.evidence:
        return emit(
            "evidence",
            evidence_errors(),
        )

    if args.scope:
        return emit(
            "scope",
            scope_errors(),
        )

    if args.non_effects:
        return emit(
            "non-effects",
            non_effects_errors(),
        )

    return emit(
        "manifest",
        manifest_errors(),
    )


if __name__ == "__main__":
    sys.exit(main())