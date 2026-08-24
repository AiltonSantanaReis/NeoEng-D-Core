#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]

REPO = "AiltonSantanaReis/NeoEng-D-Core"

BASE = "0adf4721ebefb77723e2c59ee042f35fb291854a"
BASE_TREE = "2a3171424c518a0c890f9483d5e181c88e5ae52b"
TECHNICAL_BASE = "98c1042249ced4c1775dddf9a871e29dc6070828"

CS020_SOURCE = "cc5dd6239cc7ca30e75cc0952247a74d33012b04"
CS020_SOURCE_TREE = "9343c8f899cdcb55f19a8a58f52db5c8e127a125"
CS020_BINDING = "60ce67e3baf45722d240eff19f2a845348ee44d8"
CS020_BINDING_TREE = "2a3171424c518a0c890f9483d5e181c88e5ae52b"

PR_NUMBER = 56

CS020_QUALIFYING_RUN = 32711035281
CS020_CANDIDATE_RUN = 32714464146
CS020_TRUSTED_RUN = 32714464156
CS020_PRODUCT_PR_RUN = 32714464182
POSTMERGE_CHANGESET_RUN = 32716710211
POSTMERGE_PRODUCT_RUN = 32716710243

TRUSTED_APP_ID = 15368

BRANCH = "agent/cs000l-ev03-ledger-closure"

WORKFLOW = Path(
    ".github/workflows/cs000l-ev03-ledger-closure-validation.yml"
)
SELF = Path("scripts/verify_cs000l_ev03_ledger_closure.py")
PLAN = Path("audit/validation/CS000L/VALIDATION_PLAN.json")
RESULT = Path("audit/validation/CS000L/VALIDATION_RESULT.json")
DESCRIPTOR = Path("audit/CURRENT_CHANGESET_VALIDATION.json")
ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
REQS = Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json")
CHANGESET = Path("docs/changesets/000L/CHANGESET.md")
EVIDENCE = Path(
    "docs/changesets/000L/evidence/EV03_ACCEPTANCE_MANIFEST.json"
)
DECISION = Path("docs/records/evolution/DEV-0012.md")
MANIFEST = Path("MANIFEST.sha256")

TRANSITION = Path("audit/GOVERNANCE_TRANSITION_STATE.json")
POLICY = Path("audit/CHANGESET_VALIDATION_POLICY.json")

CS000K_WORKFLOW = Path(
    ".github/workflows/cs000k-ev02-ledger-closure-validation.yml"
)
CS000K_RESULT = Path("audit/validation/CS000K/VALIDATION_RESULT.json")

CS020_WORKFLOW = Path(
    ".github/workflows/cs020-ev03-deterministic-golden-corpus-validation.yml"
)
CS020_PLAN = Path("audit/validation/CS020/VALIDATION_PLAN.json")
CS020_RESULT = Path("audit/validation/CS020/VALIDATION_RESULT.json")

EXPECTED_CS020_PLAN_SHA = (
    "f1942553800cbe11fa634fc2d4d287be7c92cc423808950cfafaca45c44c12ef"
)

ACCEPTED_EVIDENCE_FINGERPRINT = (
    "42af126570f379866d486f3f16ccdaf016be86bd74c4c7e3a94c409005fa8daa"
)

TRIGGER_SCOPE = {
    ".github/workflows/cs000l-ev03-ledger-closure-validation.yml",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/validation/CS000L/VALIDATION_PLAN.json",
    "docs/changesets/000L/CHANGESET.md",
    "docs/changesets/000L/evidence/EV03_ACCEPTANCE_MANIFEST.json",
    "docs/records/evolution/DEV-0012.md",
    "scripts/verify_cs000l_ev03_ledger_closure.py",
}

SOURCE_REQUIRED = TRIGGER_SCOPE | {
    "MANIFEST.sha256",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
}

CLOSURE_ALLOWED = SOURCE_REQUIRED | {
    "audit/validation/CS000L/VALIDATION_RESULT.json",
}

ACCEPTED_EVIDENCE_FILES = {
    ".gitattributes",
    ".github/workflows/changeset-validation.yml",
    ".github/workflows/cs000k-ev02-ledger-closure-validation.yml",
    ".github/workflows/cs020-ev03-deterministic-golden-corpus-validation.yml",
    ".github/workflows/current-product-regression.yml",
    "CMakeLists.txt",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    "audit/EVOLUTION_INVARIANTS.json",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/validation/CS000K/VALIDATION_RESULT.json",
    "audit/validation/CS020/VALIDATION_PLAN.json",
    "audit/validation/CS020/VALIDATION_RESULT.json",
    "docs/changesets/000K/evidence/EV02_ACCEPTANCE_MANIFEST.json",
    "docs/changesets/020/CHANGESET.md",
    "docs/contracts/GOLDEN_CORPUS_V1.md",
    "docs/records/evolution/DEV-0010.md",
    "docs/records/evolution/DEV-0011.md",
    "scripts/generate_manifest.py",
    "scripts/verify_changeset_validation.py",
    "scripts/verify_cs020_ev03_golden_corpus.py",
    "scripts/verify_evolution_plan.py",
    "tests/golden_corpus_tests.cpp",
    "tests/golden/ev03/v1/corpus.json",
    "tests/golden/ev03/v1/manifest.json",
    "tests/golden/ev03/v1/world_initial.bin",
    "tests/golden/ev03/v1/world_after_transition.bin",
    "tests/golden/ev03/v1/world_after_rollback_replay.bin",
    "tests/golden/ev03/v1/evidence_envelope.bin",
}

FROZEN_FILES = TRIGGER_SCOPE | ACCEPTED_EVIDENCE_FILES

GOLDEN_HASHES = {
    "tests/golden/ev03/v1/corpus.json":
        "42978121523d1b17cf9df6100e82a23d8bed46a5d5e53e474f3649ca69c08f32",
    "tests/golden/ev03/v1/manifest.json":
        "8c05f319ce98c76113916668d69e17d0cde3c3d447ec9cf4996cb8e65266b35f",
    "tests/golden/ev03/v1/world_initial.bin":
        "8a0bd49d7bcfe755d29962e26e781ff0ba041375c595770ef6705254e3efaeee",
    "tests/golden/ev03/v1/world_after_transition.bin":
        "9475640a4aa7842cfdc548eb14f97e1ed492b01c764708f833948493ebbbc793",
    "tests/golden/ev03/v1/world_after_rollback_replay.bin":
        "4059584698bad4d2e864bab6d9f17c6710a7e4e5a93bbe3ddb53f52cb64d6cf7",
    "tests/golden/ev03/v1/evidence_envelope.bin":
        "3e84dbcaea8e769516a94730847119e71e2a5d37a29b01d22b41d3bba54f4f85",
}

REQ_EVIDENCE = {
    "EVREQ-013": [
        "docs/contracts/GOLDEN_CORPUS_V1.md",
        "tests/golden/ev03/v1/corpus.json",
        "tests/golden/ev03/v1/world_initial.bin",
        "tests/golden/ev03/v1/world_after_transition.bin",
        "audit/validation/CS020/VALIDATION_RESULT.json",
        EVIDENCE.as_posix(),
    ],
    "EVREQ-014": [
        "tests/golden/ev03/v1/corpus.json",
        "tests/golden/ev03/v1/manifest.json",
        "tests/golden/ev03/v1/world_after_rollback_replay.bin",
        "tests/golden/ev03/v1/evidence_envelope.bin",
        "audit/validation/CS020/VALIDATION_RESULT.json",
        EVIDENCE.as_posix(),
    ],
    "EVREQ-015": [
        "docs/contracts/GOLDEN_CORPUS_V1.md",
        "docs/records/evolution/DEV-0011.md",
        "tests/golden/ev03/v1/manifest.json",
        "audit/validation/CS020/VALIDATION_RESULT.json",
        EVIDENCE.as_posix(),
    ],
}


def load(path: Path) -> dict[str, Any]:
    value = json.loads((ROOT / path).read_text(encoding="utf-8"))

    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {path}")

    return value


def run(*args: str, binary: bool = False) -> subprocess.CompletedProcess:
    return subprocess.run(
        args,
        cwd=ROOT,
        text=not binary,
        capture_output=True,
        check=False,
    )


def sha256(path: Path) -> str:
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()


def git_show_bytes(ref: str, path: str) -> bytes | None:
    proc = run(
        "git",
        "show",
        f"{ref}:{path}",
        binary=True,
    )

    return proc.stdout if proc.returncode == 0 else None


def tree_of(ref: str) -> str:
    proc = run(
        "git",
        "rev-parse",
        f"{ref}^{{tree}}",
    )

    if proc.returncode != 0:
        raise ValueError(proc.stderr.strip() or f"cannot resolve tree {ref}")

    return proc.stdout.strip()


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
        raise ValueError(proc.stderr.strip() or f"cannot inspect {ref}")

    return proc.stdout.strip().split()


def changed_paths() -> set[str]:
    paths: set[str] = set()

    commands = [
        ("git", "diff", "--name-only", f"{BASE}...HEAD"),
        ("git", "diff", "--name-only", "HEAD"),
        ("git", "diff", "--cached", "--name-only"),
    ]

    for command in commands:
        proc = run(*command)

        if proc.returncode != 0:
            raise ValueError(proc.stderr.strip() or "git diff failed")

        paths.update(
            line.strip()
            for line in proc.stdout.splitlines()
            if line.strip()
        )

    return paths


def manifest_map() -> dict[str, str]:
    rows: dict[str, str] = {}

    for line in (ROOT / MANIFEST).read_text(
        encoding="utf-8"
    ).splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)

        if match is None:
            continue

        digest, path = match.groups()

        if path in rows:
            raise ValueError(f"duplicate manifest path: {path}")

        rows[path] = digest

    return rows


def manifest_map_at(ref: str) -> dict[str, str]:
    raw = git_show_bytes(
        ref,
        MANIFEST.as_posix(),
    )

    if raw is None:
        raise ValueError(
            f"cannot read MANIFEST.sha256 at {ref}"
        )

    rows: dict[str, str] = {}

    for line in raw.decode("utf-8").splitlines():
        match = re.fullmatch(
            r"([0-9a-f]{64})  (.+)",
            line,
        )

        if match is None:
            continue

        digest, path = match.groups()

        if path in rows:
            raise ValueError(
                f"duplicate protected manifest path: {path}"
            )

        rows[path] = digest

    return rows


def evidence_fingerprint(rows: dict[str, str]) -> str:
    text = "".join(
        f"{path}\t{rows[path]}\n"
        for path in sorted(rows)
    )

    return hashlib.sha256(text.encode("utf-8")).hexdigest()


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


def github_json(path: str) -> Any:
    url = f"https://api.github.com/repos/{REPO}{path}"

    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "NeoEng-D-Core-CS000L-verifier",
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
        print(f"CS000L EV-03 LEDGER CLOSURE: REJECT — {label}")

        for error in errors:
            print(f"- {error}")

        return 1

    print(f"CS000L EV-03 LEDGER CLOSURE: PASS — {label}")
    return 0


def self_test_errors() -> list[str]:
    errors: list[str] = []

    if len(TRIGGER_SCOPE) != 8:
        errors.append("trigger scope cardinality fixture failed")

    if len(SOURCE_REQUIRED) != 10:
        errors.append("source scope cardinality fixture failed")

    if len(CLOSURE_ALLOWED) != 11:
        errors.append("closure scope cardinality fixture failed")

    if len(ACCEPTED_EVIDENCE_FILES) != 28:
        errors.append("accepted evidence cardinality fixture failed")

    if len(FROZEN_FILES) != 36:
        errors.append("frozen file cardinality fixture failed")

    bad = set(SOURCE_REQUIRED)
    bad.add("src/forbidden.cpp")

    if not (bad - CLOSURE_ALLOWED):
        errors.append("forbidden source path fixture was not rejected")

    for lifecycle in (
        "MANIFEST.sha256",
        "audit/CURRENT_CHANGESET_VALIDATION.json",
        RESULT.as_posix(),
    ):
        if lifecycle in TRIGGER_SCOPE:
            errors.append(
                f"lifecycle path leaked into trigger fixture: {lifecycle}"
            )

    fixture = {
        "b": "2" * 64,
        "a": "1" * 64,
    }

    expected = hashlib.sha256(
        (
            f"a\t{'1' * 64}\n"
            f"b\t{'2' * 64}\n"
        ).encode("utf-8")
    ).hexdigest()

    if evidence_fingerprint(fixture) != expected:
        errors.append("evidence fingerprint fixture failed")

    canonical_fixture = {
        ".gitattributes": "3" * 64,
        "CMakeLists.txt": "2" * 64,
        "audit/example.json": "1" * 64,
    }

    canonical_expected = hashlib.sha256(
        (
            f".gitattributes\t{'3' * 64}\n"
            f"CMakeLists.txt\t{'2' * 64}\n"
            f"audit/example.json\t{'1' * 64}\n"
        ).encode("utf-8")
    ).hexdigest()

    if (
        evidence_fingerprint(canonical_fixture)
        != canonical_expected
    ):
        errors.append(
            "ordinal evidence fingerprint canonicalization fixture failed"
        )

    return errors


def authority_errors() -> list[str]:
    errors: list[str] = []

    transition = load(TRANSITION)
    authority = transition.get("prospective_authority")

    if not isinstance(authority, dict):
        return ["prospective_authority missing"]

    expected = {
        "regime_id": "CHANGESET_VALIDATION",
        "policy": "audit/CHANGESET_VALIDATION_POLICY.json",
        "workflow": ".github/workflows/changeset-validation.yml",
        "verifier": "scripts/verify_changeset_validation.py",
        "required_branch_check": "Trusted ChangeSet validation gate",
        "required_branch_check_app_id": TRUSTED_APP_ID,
    }

    for key, value in expected.items():
        if authority.get(key) != value:
            errors.append(f"authority mismatch: {key}")

    policy = load(POLICY)

    required_true = (
        "required_test_nonpass_blocks_validation",
        "test_inventory_frozen_before_execution",
        "exact_run_binding_required",
        "failure_preservation_required",
        "accepted_requires_validated",
        "all_required_tests_must_pass",
        "trusted_base_verifier_required_for_pr_acceptance",
        "historical_records_are_immutable",
        "release_is_separate_from_changeset_acceptance",
    )

    for key in required_true:
        if policy.get(key) is not True:
            errors.append(f"policy requirement not active: {key}")

    if policy.get("ci_green_is_acceptance") is not False:
        errors.append("CI green incorrectly treated as acceptance")

    if policy.get("allow_test_removal_after_execution") is not False:
        errors.append("test removal is not forbidden")

    return errors


def integration_errors() -> list[str]:
    errors: list[str] = []

    try:
        parents = parents_of(BASE)
    except ValueError as exc:
        return [str(exc)]

    if parents != [BASE, TECHNICAL_BASE, CS020_BINDING]:
        errors.append("protected merge parents mismatch")

    try:
        if tree_of(BASE) != BASE_TREE:
            errors.append("protected merge tree mismatch")
    except ValueError as exc:
        errors.append(str(exc))

    if CS020_BINDING_TREE != BASE_TREE:
        errors.append("binding tree / protected merge tree mismatch")

    if sha256(CS020_PLAN) != EXPECTED_CS020_PLAN_SHA:
        errors.append("CS020 validation plan hash mismatch")

    plan = load(CS020_PLAN)
    result = load(CS020_RESULT)

    if plan.get("changeset") != "CS020":
        errors.append("CS020 plan identity mismatch")

    if plan.get("base_sha") != TECHNICAL_BASE:
        errors.append("CS020 plan base mismatch")

    required = plan.get("required_tests")

    if not isinstance(required, list) or len(required) != 28:
        errors.append("CS020 required test inventory is not 28")

    if result.get("changeset") != "CS020":
        errors.append("CS020 result identity mismatch")

    if result.get("source_sha") != CS020_SOURCE:
        errors.append("CS020 result source mismatch")

    if result.get("plan_commit") != CS020_SOURCE:
        errors.append("CS020 result plan commit mismatch")

    if result.get("run_id") != CS020_QUALIFYING_RUN:
        errors.append("CS020 qualifying run binding mismatch")

    if result.get("run_attempt") != 1:
        errors.append("CS020 qualifying attempt mismatch")

    if result.get("validation_state") != "VALIDATED":
        errors.append("CS020 is not VALIDATED")

    if result.get("acceptance_decision") != "ACCEPTED":
        errors.append("CS020 is not ACCEPTED")

    tests = result.get("tests")

    if not isinstance(tests, list) or len(tests) != 28:
        errors.append("CS020 result test inventory is not 28")
    elif any(
        not isinstance(item, dict)
        or item.get("status") != "PASS"
        for item in tests
    ):
        errors.append("CS020 result contains non-PASS required test")

    effects = result.get("effects")

    if not isinstance(effects, dict):
        errors.append("CS020 effects missing")
    else:
        if effects.get("ev03_stage_acceptance") != "NOT_SET_BY_THIS_RESULT":
            errors.append("CS020 result improperly closes EV-03")

        if effects.get("ev04") != "NOT_STARTED":
            errors.append("CS020 result improperly advances EV-04")

        if effects.get("release") != "NOT_AUTHORIZED":
            errors.append("CS020 result improperly authorizes release")

        if effects.get("golden_reemission") != "NONE":
            errors.append("CS020 reports golden re-emission")

    prior_result = git_show_bytes(
        BASE,
        CS020_RESULT.as_posix(),
    )

    if prior_result is None:
        errors.append("CS020 result unavailable at protected merge")
    elif prior_result != (ROOT / CS020_RESULT).read_bytes():
        errors.append("CS020 result changed after protected merge")

    return errors


def github_errors() -> list[str]:
    errors: list[str] = []

    try:
        pr = github_json(f"/pulls/{PR_NUMBER}")
    except Exception as exc:
        return [f"cannot read PR #{PR_NUMBER}: {exc}"]

    if pr.get("state") != "closed" or pr.get("merged") is not True:
        errors.append("PR #56 is not merged")

    if pr.get("base", {}).get("sha") != TECHNICAL_BASE:
        errors.append("PR #56 base SHA mismatch")

    if pr.get("head", {}).get("sha") != CS020_BINDING:
        errors.append("PR #56 head SHA mismatch")

    if pr.get("merge_commit_sha") != BASE:
        errors.append("PR #56 merge SHA mismatch")

    specs = [
        (
            CS020_QUALIFYING_RUN,
            "push",
            CS020_SOURCE,
            CS020_WORKFLOW.as_posix(),
        ),
        (
            CS020_CANDIDATE_RUN,
            "pull_request",
            CS020_BINDING,
            ".github/workflows/changeset-validation.yml",
        ),
        (
            CS020_TRUSTED_RUN,
            "pull_request_target",
            None,
            ".github/workflows/changeset-validation.yml",
        ),
        (
            CS020_PRODUCT_PR_RUN,
            "pull_request",
            CS020_BINDING,
            ".github/workflows/current-product-regression.yml",
        ),
        (
            POSTMERGE_CHANGESET_RUN,
            "push",
            BASE,
            ".github/workflows/changeset-validation.yml",
        ),
        (
            POSTMERGE_PRODUCT_RUN,
            "push",
            BASE,
            ".github/workflows/current-product-regression.yml",
        ),
    ]

    for run_id, event, head_sha, path in specs:
        try:
            run_doc = github_json(f"/actions/runs/{run_id}")
        except Exception as exc:
            errors.append(f"cannot read run {run_id}: {exc}")
            continue

        if run_doc.get("id") != run_id:
            errors.append(f"run identity mismatch: {run_id}")

        if run_doc.get("run_attempt") != 1:
            errors.append(f"run attempt mismatch: {run_id}")

        if run_doc.get("event") != event:
            errors.append(f"run event mismatch: {run_id}")

        if run_doc.get("path") != path:
            errors.append(f"run workflow mismatch: {run_id}")

        if run_doc.get("status") != "completed":
            errors.append(f"run not completed: {run_id}")

        if run_doc.get("conclusion") != "success":
            errors.append(f"run not success: {run_id}")

        if head_sha is not None and run_doc.get("head_sha") != head_sha:
            errors.append(f"run head SHA mismatch: {run_id}")

        if run_id == CS020_TRUSTED_RUN:
            if (
                run_doc.get("head_branch")
                != "agent/cs020-ev03-deterministic-golden-corpus"
            ):
                errors.append("trusted run head branch mismatch")

            if run_doc.get("head_sha") != CS020_BINDING:
                errors.append("trusted run head SHA mismatch")

            try:
                jobs_doc = github_json(
                    f"/actions/runs/{CS020_TRUSTED_RUN}"
                    "/attempts/1/jobs?per_page=100"
                )
            except Exception as exc:
                errors.append(f"cannot read trusted run jobs: {exc}")
            else:
                trusted_jobs = [
                    row
                    for row in jobs_doc.get("jobs", [])
                    if isinstance(row, dict)
                    and row.get("name")
                    == "Trusted ChangeSet validation gate"
                ]

                if len(trusted_jobs) != 1:
                    errors.append(
                        "trusted validation job cardinality mismatch"
                    )
                else:
                    trusted_job = trusted_jobs[0]

                    if trusted_job.get("status") != "completed":
                        errors.append(
                            "trusted validation job not completed"
                        )

                    if trusted_job.get("conclusion") != "success":
                        errors.append(
                            "trusted validation job not success"
                        )

                    trusted_steps = [
                        row
                        for row in trusted_job.get("steps", [])
                        if isinstance(row, dict)
                        and row.get("name")
                        == "Verify accepted candidate from trusted base"
                    ]

                    if len(trusted_steps) != 1:
                        errors.append(
                            "trusted authoritative step cardinality mismatch"
                        )
                    else:
                        trusted_step = trusted_steps[0]

                        if trusted_step.get("status") != "completed":
                            errors.append(
                                "trusted authoritative step not completed"
                            )

                        if trusted_step.get("conclusion") != "success":
                            errors.append(
                                "trusted authoritative step not success"
                            )

                    check_run_url = trusted_job.get("check_run_url")

                    prefix = (
                        f"https://api.github.com/repos/{REPO}"
                    )

                    if (
                        not isinstance(check_run_url, str)
                        or not check_run_url.startswith(prefix + "/")
                    ):
                        errors.append(
                            "trusted check_run_url missing or invalid"
                        )
                    else:
                        check_path = check_run_url[len(prefix):]

                        try:
                            check_doc = github_json(check_path)
                        except Exception as exc:
                            errors.append(
                                f"cannot read trusted check run: {exc}"
                            )
                        else:
                            if (
                                check_doc.get("name")
                                != "Trusted ChangeSet validation gate"
                            ):
                                errors.append(
                                    "trusted check name mismatch"
                                )

                            if check_doc.get("status") != "completed":
                                errors.append(
                                    "trusted check not completed"
                                )

                            if check_doc.get("conclusion") != "success":
                                errors.append(
                                    "trusted check not success"
                                )

                            if (
                                check_doc.get("app", {}).get("id")
                                != TRUSTED_APP_ID
                            ):
                                errors.append(
                                    "trusted check app id mismatch"
                                )

    encoded_branch = urllib.parse.quote(
        "agent/cs020-ev03-deterministic-golden-corpus",
        safe="",
    )

    try:
        runs_doc = github_json(
            f"/actions/runs?branch={encoded_branch}&event=push&per_page=100"
        )
    except Exception as exc:
        errors.append(f"cannot inspect CS020 qualification multiplicity: {exc}")
        return errors

    runs = [
        row
        for row in runs_doc.get("workflow_runs", [])
        if isinstance(row, dict)
        and row.get("path") == CS020_WORKFLOW.as_posix()
    ]

    source_runs = [
        row
        for row in runs
        if row.get("head_sha") == CS020_SOURCE
    ]

    binding_runs = [
        row
        for row in runs
        if row.get("head_sha") == CS020_BINDING
    ]

    if (
        len(source_runs) != 1
        or source_runs[0].get("id") != CS020_QUALIFYING_RUN
    ):
        errors.append("CS020 qualifying run multiplicity mismatch")

    if binding_runs:
        errors.append("CS020 binding requalification exists")

    return errors


def predecessor_errors() -> list[str]:
    errors: list[str] = []

    result = load(CS000K_RESULT)

    if result.get("changeset") != "CS000K":
        errors.append("CS000K result identity mismatch")

    if result.get("validation_state") != "VALIDATED":
        errors.append("CS000K no longer VALIDATED")

    if result.get("acceptance_decision") != "ACCEPTED":
        errors.append("CS000K no longer ACCEPTED")

    tests = result.get("tests")

    if not isinstance(tests, list) or len(tests) != 14:
        errors.append("CS000K test inventory mismatch")
    elif any(
        not isinstance(item, dict)
        or item.get("status") != "PASS"
        for item in tests
    ):
        errors.append("CS000K contains non-PASS required test")

    workflow_text = (ROOT / CS000K_WORKFLOW).read_text(
        encoding="utf-8"
    )

    try:
        keys = top_level_on_keys(workflow_text)
    except ValueError as exc:
        errors.append(str(exc))
    else:
        if keys != {"workflow_dispatch"}:
            errors.append("CS000K workflow is not manual-only")

    roadmap = load(ROADMAP)
    ev02 = [
        row
        for row in roadmap.get("stages", [])
        if isinstance(row, dict)
        and row.get("stage_id") == "EV-02"
    ]

    if len(ev02) != 1 or ev02[0].get("status") != "accepted":
        errors.append("accepted EV-02 predecessor changed")

    return errors


def golden_errors() -> list[str]:
    errors: list[str] = []

    for rel, expected in GOLDEN_HASHES.items():
        path = ROOT / rel

        if not path.is_file():
            errors.append(f"golden file missing: {rel}")
            continue

        actual = hashlib.sha256(path.read_bytes()).hexdigest()

        if actual != expected:
            errors.append(f"golden hash mismatch: {rel}")

    workflow_text = (ROOT / CS020_WORKFLOW).read_text(
        encoding="utf-8"
    )

    if "--emit" in workflow_text:
        errors.append("CS020 qualification workflow contains emission mode")

    if "agent/cs020-ev03-deterministic-golden-corpus" not in workflow_text:
        errors.append("CS020 branch-specific trigger missing")

    if not re.search(r"(?m)^  push:\s*$", workflow_text):
        errors.append("CS020 workflow is not push-triggered")

    if re.search(
        r"(?m)^  pull_request(?:_target)?:\s*$",
        workflow_text,
    ):
        errors.append("CS020 workflow gained PR applicability")

    return errors


def ledger_errors() -> list[str]:
    errors: list[str] = []

    roadmap = load(ROADMAP)

    if roadmap.get("program_state") != "active":
        errors.append("program_state is not active")

    if roadmap.get("current_stage") != "EV-03":
        errors.append("current_stage must remain EV-03")

    if roadmap.get("release_authorized") is not False:
        errors.append("release became authorized")

    by_id = {
        row.get("stage_id"): row
        for row in roadmap.get("stages", [])
        if isinstance(row, dict)
    }

    ev02 = by_id.get("EV-02")
    ev03 = by_id.get("EV-03")
    ev04 = by_id.get("EV-04")

    if not isinstance(ev02, dict) or ev02.get("status") != "accepted":
        errors.append("EV-02 predecessor is not accepted")

    if not isinstance(ev03, dict):
        errors.append("EV-03 row missing")
    else:
        if ev03.get("status") != "accepted":
            errors.append("EV-03 is not accepted")

        if ev03.get("accepted_commit") != BASE:
            errors.append("EV-03 accepted_commit mismatch")

        if ev03.get("evidence_manifest") != EVIDENCE.as_posix():
            errors.append("EV-03 evidence_manifest mismatch")

        if ev03.get("decision_record") != DECISION.as_posix():
            errors.append("EV-03 decision_record mismatch")

    if not isinstance(ev04, dict):
        errors.append("EV-04 row missing")
    else:
        if ev04.get("status") != "not_started":
            errors.append("EV-04 advanced during CS000L")

        if ev04.get("accepted_commit") is not None:
            errors.append("EV-04 accepted_commit unexpectedly set")

        if ev04.get("evidence_manifest") is not None:
            errors.append("EV-04 evidence unexpectedly set")

        if ev04.get("decision_record") is not None:
            errors.append("EV-04 decision unexpectedly set")

    return errors


def requirements_errors() -> list[str]:
    errors: list[str] = []

    doc = load(REQS)

    by_id = {
        row.get("requirement_id"): row
        for row in doc.get("requirements", [])
        if isinstance(row, dict)
    }

    for rid, expected in REQ_EVIDENCE.items():
        row = by_id.get(rid)

        if not isinstance(row, dict):
            errors.append(f"missing requirement: {rid}")
            continue

        if row.get("stage") != "EV-03":
            errors.append(f"requirement stage mismatch: {rid}")

        if row.get("status") != "verified":
            errors.append(f"requirement is not verified: {rid}")

        if row.get("evidence") != expected:
            errors.append(f"requirement evidence mismatch: {rid}")

    for rid in ("EVREQ-016", "EVREQ-017", "EVREQ-018"):
        row = by_id.get(rid)

        if not isinstance(row, dict):
            errors.append(f"missing EV-04 requirement: {rid}")
            continue

        if row.get("status") != "planned":
            errors.append(f"EV-04 requirement advanced: {rid}")

        if row.get("evidence") != []:
            errors.append(f"EV-04 requirement has evidence: {rid}")

    return errors


def evidence_errors() -> list[str]:
    errors: list[str] = []

    doc = load(EVIDENCE)

    if doc.get("schema") != "neoeng.dcore.evolution-evidence-manifest.v1":
        errors.append("evidence manifest schema mismatch")

    if doc.get("stage") != "EV-03":
        errors.append("evidence manifest stage mismatch")

    if doc.get("changeset") != "CS020":
        errors.append("evidence manifest technical changeset mismatch")

    if doc.get("closure_changeset") != "CS000L":
        errors.append("evidence manifest closure changeset mismatch")

    if doc.get("source_commit") != BASE:
        errors.append("evidence manifest source_commit mismatch")

    if doc.get("hash_mode") != "repository-manifest-sha256":
        errors.append("evidence manifest hash mode mismatch")

    if (
        doc.get("accepted_evidence_fingerprint_sha256")
        != ACCEPTED_EVIDENCE_FINGERPRINT
    ):
        errors.append("accepted evidence fingerprint declaration mismatch")

    files = doc.get("files")

    if not isinstance(files, list):
        return errors + ["evidence manifest files is not a list"]

    rows: dict[str, str] = {}

    for row in files:
        if not isinstance(row, dict):
            errors.append("invalid evidence file row")
            continue

        path = row.get("path")
        digest = row.get("sha256")

        if not isinstance(path, str) or not isinstance(digest, str):
            errors.append("invalid evidence path/hash row")
            continue

        if path in rows:
            errors.append(f"duplicate evidence path: {path}")
            continue

        rows[path] = digest

    if set(rows) != ACCEPTED_EVIDENCE_FILES:
        errors.append("accepted evidence path set mismatch")

    current_manifest = manifest_map()

    try:
        protected_manifest = manifest_map_at(BASE)
    except ValueError as exc:
        errors.append(
            f"cannot read protected-base evidence manifest: {exc}"
        )
        protected_manifest = {}

    protected_rows: dict[str, str] = {}

    for path in sorted(ACCEPTED_EVIDENCE_FILES):
        digest = protected_manifest.get(path)

        if digest is None:
            errors.append(
                f"accepted evidence absent from protected base: {path}"
            )
            continue

        protected_rows[path] = digest

    if protected_rows:
        protected_fingerprint = evidence_fingerprint(
            protected_rows
        )

        if (
            protected_fingerprint
            != ACCEPTED_EVIDENCE_FINGERPRINT
        ):
            errors.append(
                "protected-base accepted evidence fingerprint mismatch"
            )

    if (
        rows
        and protected_rows
        and rows != protected_rows
    ):
        errors.append(
            "accepted evidence rows differ from protected-base MANIFEST"
        )

    for path in sorted(ACCEPTED_EVIDENCE_FILES):
        expected = rows.get(path)

        if expected is None:
            continue

        current = ROOT / path

        if not current.is_file():
            errors.append(f"accepted evidence file missing: {path}")
            continue

        actual = hashlib.sha256(current.read_bytes()).hexdigest()

        if actual != expected:
            errors.append(f"accepted evidence hash mismatch: {path}")

        if current_manifest.get(path) != expected:
            errors.append(f"repository manifest evidence mismatch: {path}")

    if rows and evidence_fingerprint(rows) != ACCEPTED_EVIDENCE_FINGERPRINT:
        errors.append("accepted evidence aggregate fingerprint mismatch")

    if not (ROOT / DECISION).is_file():
        errors.append("DEV-0012 decision record missing")

    return errors


def scope_errors() -> list[str]:
    errors: list[str] = []

    actual = changed_paths()

    expected = (
        CLOSURE_ALLOWED
        if (ROOT / RESULT).is_file()
        else SOURCE_REQUIRED
    )

    if actual != expected:
        errors.append(
            "scope mismatch: actual="
            + ",".join(sorted(actual))
            + " expected="
            + ",".join(sorted(expected))
        )

    plan = load(PLAN)

    frozen = plan.get("frozen_files")

    if not isinstance(frozen, list):
        errors.append("plan frozen_files missing")
    elif set(frozen) != FROZEN_FILES:
        errors.append("plan frozen_files set mismatch")

    tests = plan.get("required_tests")

    if not isinstance(tests, list) or len(tests) != 14:
        errors.append("plan required test inventory is not exactly 14")

    if plan.get("base_sha") != BASE:
        errors.append("plan base mismatch")

    if plan.get("execution_workflow") != WORKFLOW.as_posix():
        errors.append("plan workflow mismatch")

    return errors


def non_effect_errors() -> list[str]:
    errors: list[str] = []

    actual = changed_paths()

    forbidden_prefixes = (
        "include/",
        "src/",
        "modules/",
        "tests/",
        "apps/",
    )

    forbidden_exact = {
        "CMakeLists.txt",
        CS020_PLAN.as_posix(),
        CS020_RESULT.as_posix(),
        CS020_WORKFLOW.as_posix(),
        CS000K_WORKFLOW.as_posix(),
        "audit/EVOLUTION_INVARIANTS.json",
        "audit/GOVERNANCE_TRANSITION_STATE.json",
        "audit/CHANGESET_VALIDATION_POLICY.json",
    }

    for path in sorted(actual):
        if path in forbidden_exact:
            errors.append(f"forbidden CS000L delta: {path}")

        if any(path.startswith(prefix) for prefix in forbidden_prefixes):
            errors.append(f"forbidden product/test delta: {path}")

    for rel in forbidden_exact:
        current = ROOT / rel

        if not current.is_file():
            errors.append(f"preserved file missing: {rel}")
            continue

        prior = git_show_bytes(BASE, rel)

        if prior is None:
            errors.append(f"cannot read preserved base file: {rel}")
        elif prior != current.read_bytes():
            errors.append(f"preserved file changed: {rel}")

    roadmap = load(ROADMAP)

    if roadmap.get("current_stage") != "EV-03":
        errors.append("current_stage advanced")

    if roadmap.get("release_authorized") is not False:
        errors.append("release authorized")

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--authority", action="store_true")
    parser.add_argument("--integration", action="store_true")
    parser.add_argument("--github", action="store_true")
    parser.add_argument("--predecessor", action="store_true")
    parser.add_argument("--golden", action="store_true")
    parser.add_argument("--ledger", action="store_true")
    parser.add_argument("--requirements", action="store_true")
    parser.add_argument("--evidence", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--non-effects", action="store_true")

    args = parser.parse_args()

    selected = [
        ("self-test", args.self_test, self_test_errors),
        ("authority", args.authority, authority_errors),
        ("integration", args.integration, integration_errors),
        ("github-provenance", args.github, github_errors),
        ("predecessor-preservation", args.predecessor, predecessor_errors),
        ("golden-preservation", args.golden, golden_errors),
        ("ledger-closure", args.ledger, ledger_errors),
        ("requirements-closure", args.requirements, requirements_errors),
        ("acceptance-evidence", args.evidence, evidence_errors),
        ("scope", args.scope, scope_errors),
        ("non-effects", args.non_effects, non_effect_errors),
    ]

    active = [
        (label, fn)
        for label, enabled, fn in selected
        if enabled
    ]

    if len(active) != 1:
        print("exactly one verification mode is required")
        return 2

    label, fn = active[0]

    try:
        errors = fn()
    except Exception as exc:
        errors = [f"verifier exception: {exc}"]

    return emit(label, errors)


if __name__ == "__main__":
    sys.exit(main())
