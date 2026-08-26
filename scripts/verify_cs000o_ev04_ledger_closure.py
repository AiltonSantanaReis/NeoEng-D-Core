#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import fnmatch
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

BASE = "08ae1545b68d99575e0ddadf76398055c84bb84e"
BASE_TREE = "6f59ad02302dd3c9de9902bb6c7f993e5d4837c2"
BASE_PARENT_1 = "d75fb80e7aa304576060339c31ff87fdb9dae206"
BASE_PARENT_2 = "47e1282a14e5329eaeb6d06b72bcf784250ac17b"
TECHNICAL_BASE = "9e5c00faa4db0868da48913b8ffa24e0f64972e2"

CS021_SOURCE = "0c4b66b4474c128583f2ce1920aef848bebc3db8"
CS021_SOURCE_TREE = "c8c6cb2c38c5b09480629992da24a85528877e4c"
CS021_BINDING = "8a44bc57bea3f5053746a3648f7e0b26bf8f4bb3"
CS021_BINDING_TREE = "52c8b90c69806008322a4fc5043aff60c7125040"
CS021_MERGE = "d75fb80e7aa304576060339c31ff87fdb9dae206"
PR_NUMBER = 59

CS021_QUALIFYING_RUN = 32794089676
CS021_CANDIDATE_RUN = 32795606536
CS021_TRUSTED_RUN = 32795606519
CS021_PRODUCT_PR_RUN = 32795606513
CS021_POSTMERGE_CHANGESET_RUN = 32796180635
CS021_POSTMERGE_PRODUCT_RUN = 32796180646

CS000N_SOURCE = "17241ced2a480c166481467d6aa72aa743e8e04a"
CS000N_BINDING = "47e1282a14e5329eaeb6d06b72bcf784250ac17b"
CS000N_POSTMERGE_CHANGESET_RUN = 32869265955
CS000N_POSTMERGE_PRODUCT_RUN = 32869265936
TRUSTED_APP_ID = 15368

BRANCH = "agent/cs000o-ev04-ledger-closure"
WORKFLOW = Path(".github/workflows/cs000o-ev04-ledger-closure-validation.yml")
PLAN = Path("audit/validation/CS000O/VALIDATION_PLAN.json")
RESULT = Path("audit/validation/CS000O/VALIDATION_RESULT.json")
DESCRIPTOR = Path("audit/CURRENT_CHANGESET_VALIDATION.json")
ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
REQS = Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json")
CHANGESET = Path("docs/changesets/000O/CHANGESET.md")
EVIDENCE = Path("docs/changesets/000O/evidence/EV04_ACCEPTANCE_MANIFEST.json")
DECISION = Path("docs/records/evolution/DEV-0015.md")
SELF = Path("scripts/verify_cs000o_ev04_ledger_closure.py")
MANIFEST = Path("MANIFEST.sha256")
SCOPE_LEDGER = Path("audit/STAGE_SCOPE_MAXIMA.json")
TRANSITION = Path("audit/GOVERNANCE_TRANSITION_STATE.json")
CS000N_RESULT = Path("audit/validation/CS000N/VALIDATION_RESULT.json")
CS021_PLAN = Path("audit/validation/CS021/VALIDATION_PLAN.json")
CS021_RESULT = Path("audit/validation/CS021/VALIDATION_RESULT.json")
PROPERTY_TEST = Path("tests/property_model_tests.cpp")

TRIGGER_SCOPE = {
    ".github/workflows/cs000o-ev04-ledger-closure-validation.yml",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/validation/CS000O/VALIDATION_PLAN.json",
    "docs/changesets/000O/CHANGESET.md",
    "docs/changesets/000O/evidence/EV04_ACCEPTANCE_MANIFEST.json",
    "docs/records/evolution/DEV-0015.md",
    "scripts/verify_cs000o_ev04_ledger_closure.py",
}
SOURCE_REQUIRED = TRIGGER_SCOPE | {
    "MANIFEST.sha256",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
}
CLOSURE_ALLOWED = SOURCE_REQUIRED | {
    "audit/validation/CS000O/VALIDATION_RESULT.json",
}

ACCEPTED_EVIDENCE_FILES = {
    ".gitattributes",
    ".github/workflows/changeset-validation.yml",
    ".github/workflows/cs000m-ev04-scope-authorization-validation.yml",
    ".github/workflows/cs000n-ev04-closure-scope-authorization-validation.yml",
    ".github/workflows/cs021-ev04-property-model-testing-validation.yml",
    ".github/workflows/current-product-regression.yml",
    "CMakeLists.txt",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    "audit/EVOLUTION_INVARIANTS.json",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/STAGE_SCOPE_MAXIMA.json",
    "audit/validation/CS000M/VALIDATION_PLAN.json",
    "audit/validation/CS000M/VALIDATION_RESULT.json",
    "audit/validation/CS000N/VALIDATION_PLAN.json",
    "audit/validation/CS000N/VALIDATION_RESULT.json",
    "audit/validation/CS021/VALIDATION_PLAN.json",
    "audit/validation/CS021/VALIDATION_RESULT.json",
    "docs/changesets/000L/evidence/EV03_ACCEPTANCE_MANIFEST.json",
    "docs/changesets/000M/CHANGESET.md",
    "docs/changesets/000N/CHANGESET.md",
    "docs/changesets/021/CHANGESET.md",
    "docs/contracts/GOLDEN_CORPUS_V1.md",
    "docs/records/evolution/DEV-0012.md",
    "docs/records/evolution/DEV-0013.md",
    "docs/records/evolution/DEV-0014.md",
    "scripts/generate_manifest.py",
    "scripts/verify_changeset_validation.py",
    "scripts/verify_cs000m_ev04_scope_authorization.py",
    "scripts/verify_cs000n_ev04_closure_scope_authorization.py",
    "scripts/verify_cs021_ev04_property_model_testing.py",
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
FROZEN_FILES = TRIGGER_SCOPE | ACCEPTED_EVIDENCE_FILES

REQ_EVIDENCE = {
    "EVREQ-016": [
        "tests/property_model_tests.cpp",
        ".github/workflows/cs021-ev04-property-model-testing-validation.yml",
        "audit/validation/CS021/VALIDATION_RESULT.json",
        EVIDENCE.as_posix(),
    ],
    "EVREQ-017": [
        "tests/property_model_tests.cpp",
        "audit/validation/CS021/VALIDATION_RESULT.json",
        EVIDENCE.as_posix(),
    ],
    "EVREQ-018": [
        "tests/property_model_tests.cpp",
        "docs/changesets/021/CHANGESET.md",
        "audit/validation/CS021/VALIDATION_RESULT.json",
        EVIDENCE.as_posix(),
    ],
}
EXPECTED_TESTS = [
    "cs000o.verifier-self-test",
    "cs000o.authority",
    "cs000o.cs021-integration",
    "cs000o.github-provenance",
    "cs000o.cs000n-preservation",
    "cs000o.technical-evidence",
    "cs000o.ledger-closure",
    "cs000o.requirements-closure",
    "cs000o.acceptance-evidence",
    "cs000o.descriptor",
    "cs000o.scope",
    "cs000o.non-effects",
    "evolution.plan",
    "repository.manifest",
    "changeset.plan-structure",
]

BASE_PREPARATION = [
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
CLOSURE_ADDITIONS = [
    ".github/workflows/cs000o-ev04-ledger-closure-validation.yml",
    "audit/validation/CS000O/**",
    "docs/changesets/000O/**",
    "docs/records/evolution/DEV-0015.md",
    "scripts/verify_cs000o_ev04_ledger_closure.py",
]
BASE_FORBIDDEN = [
    "src/**", "include/**", "modules/**", "apps/**", "cmake/**",
    "docs/governance/**", "audit/SOURCE_OF_TRUTH_INDEX.json",
    "audit/STAGE_SCOPE_MAXIMA.json", "audit/CHANGESET_VALIDATION_POLICY.json",
    "audit/GOVERNANCE_TRANSITION_STATE.json", "audit/EVOLUTION_INVARIANTS.json",
    ".github/workflows/changeset-validation.yml",
    ".github/workflows/current-product-regression.yml",
    "scripts/generate_manifest.py", "scripts/verify_changeset_validation.py",
    "scripts/verify_evolution_plan.py", "tests/golden/ev03/v1/**",
    "docs/contracts/GOLDEN_CORPUS_V1.md", "audit/validation/CS000L/**",
    "docs/changesets/000L/**",
]
EXPECTED_EV04_SCOPE = {
    "stage_id": "EV-04",
    "planned_changeset": "CS021",
    "status": "defined",
    "preparation_allowed_patterns": BASE_PREPARATION + CLOSURE_ADDITIONS,
    "allowed_patterns": BASE_PREPARATION + CLOSURE_ADDITIONS,
    "mandatory_forbidden_patterns": BASE_FORBIDDEN,
}


def run(*args: str, binary: bool = False) -> subprocess.CompletedProcess:
    return subprocess.run(args, cwd=ROOT, text=not binary, capture_output=True, check=False)


def load(path: Path) -> dict[str, Any]:
    value = json.loads((ROOT / path).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {path}")
    return value


def git_show_bytes(ref: str, path: str) -> bytes | None:
    proc = run("git", "show", f"{ref}:{path}", binary=True)
    return proc.stdout if proc.returncode == 0 else None


def base_json(path: Path) -> dict[str, Any]:
    raw = git_show_bytes(BASE, path.as_posix())
    if raw is None:
        raise ValueError(f"cannot read base path: {path}")
    value = json.loads(raw.decode("utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"base JSON root must be object: {path}")
    return value


def sha256(path: Path) -> str:
    return hashlib.sha256((ROOT / path).read_bytes()).hexdigest()


def tree_of(ref: str) -> str:
    proc = run("git", "rev-parse", f"{ref}^{{tree}}")
    if proc.returncode != 0:
        raise ValueError(proc.stderr.strip() or f"cannot resolve tree: {ref}")
    return proc.stdout.strip()


def parent_line(ref: str) -> list[str]:
    proc = run("git", "rev-list", "--parents", "-n", "1", ref)
    if proc.returncode != 0:
        raise ValueError(proc.stderr.strip() or f"cannot inspect parents: {ref}")
    return proc.stdout.strip().split()


def changed_paths() -> set[str]:
    paths: set[str] = set()
    for command in (
        ("git", "diff", "--name-only", f"{BASE}...HEAD"),
        ("git", "diff", "--name-only", "HEAD"),
        ("git", "diff", "--cached", "--name-only"),
    ):
        proc = run(*command)
        if proc.returncode != 0:
            raise ValueError(proc.stderr.strip() or "git diff failed")
        paths.update(line.strip() for line in proc.stdout.splitlines() if line.strip())
    return paths


def evidence_fingerprint(rows: dict[str, str]) -> str:
    text = "".join(f"{path}\t{rows[path]}\n" for path in sorted(rows))
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def github_json(path: str) -> Any:
    url = f"https://api.github.com/repos/{REPO}{path}"
    headers = {
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "NeoEng-D-Core-CS000O-verifier",
    }
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    request = urllib.request.Request(url, headers=headers, method="GET")
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read().decode("utf-8"))


def emit(label: str, errors: list[str]) -> int:
    if errors:
        print(f"CS000O EV-04 LEDGER CLOSURE: REJECT — {label}")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"CS000O EV-04 LEDGER CLOSURE: PASS — {label}")
    return 0


def workflow_trigger_paths() -> set[str]:
    text = (ROOT / WORKFLOW).read_text(encoding="utf-8")
    lines = text.splitlines()
    paths: set[str] = set()
    in_paths = False
    for line in lines:
        if line == "    paths:":
            in_paths = True
            continue
        if in_paths:
            if line.startswith("      - "):
                token = line[len("      - "):].strip().strip("'\"")
                paths.add(token)
                continue
            if line and not line.startswith("      "):
                break
    return paths


def self_test_errors() -> list[str]:
    errors: list[str] = []
    if len(TRIGGER_SCOPE) != 8:
        errors.append("trigger scope cardinality fixture failed")
    if len(SOURCE_REQUIRED) != 10:
        errors.append("source scope cardinality fixture failed")
    if len(CLOSURE_ALLOWED) != 11:
        errors.append("closure scope cardinality fixture failed")
    if len(ACCEPTED_EVIDENCE_FILES) != 39:
        errors.append("accepted evidence cardinality fixture failed")
    if len(FROZEN_FILES) != 47:
        errors.append("frozen file cardinality fixture failed")
    if len(EXPECTED_TESTS) != 15:
        errors.append("required-test cardinality fixture failed")
    bad = set(SOURCE_REQUIRED)
    bad.add("src/forbidden.cpp")
    if not (bad - CLOSURE_ALLOWED):
        errors.append("forbidden source path fixture was not rejected")
    for lifecycle in ("MANIFEST.sha256", DESCRIPTOR.as_posix(), RESULT.as_posix()):
        if lifecycle in TRIGGER_SCOPE:
            errors.append(f"lifecycle path leaked into trigger fixture: {lifecycle}")
    fixture = {"b": "2" * 64, "a": "1" * 64}
    expected = hashlib.sha256((f"a\t{'1'*64}\n" + f"b\t{'2'*64}\n").encode()).hexdigest()
    if evidence_fingerprint(fixture) != expected:
        errors.append("evidence fingerprint fixture failed")
    return errors


def authority_errors() -> list[str]:
    errors: list[str] = []
    try:
        if tree_of(BASE) != BASE_TREE:
            errors.append("CS000N protected base tree mismatch")
        if parent_line(BASE) != [BASE, BASE_PARENT_1, BASE_PARENT_2]:
            errors.append("CS000N protected base parent geometry mismatch")
        transition = load(TRANSITION)
        authority = transition.get("prospective_authority")
        expected_authority = {
            "regime_id": "CHANGESET_VALIDATION",
            "policy": "audit/CHANGESET_VALIDATION_POLICY.json",
            "policy_document": "docs/governance/CHANGESET_VALIDATION_POLICY.md",
            "workflow": ".github/workflows/changeset-validation.yml",
            "verifier": "scripts/verify_changeset_validation.py",
            "required_branch_check": "Trusted ChangeSet validation gate",
            "required_branch_check_app_id": TRUSTED_APP_ID,
        }
        if authority != expected_authority:
            errors.append("prospective ChangeSet authority mismatch")
        scope = load(SCOPE_LEDGER)
        stages = {item.get("stage_id"): item for item in scope.get("stages", [])}
        if stages.get("EV-04") != EXPECTED_EV04_SCOPE:
            errors.append("active EV-04 maximum does not equal CS000N-authorized closure scope")
        if "EV-05" not in scope.get("undefined_stages", []):
            errors.append("EV-05 unexpectedly has defined scope")
        for path in CLOSURE_ALLOWED:
            allowed = any(fnmatch.fnmatchcase(path, p) for p in EXPECTED_EV04_SCOPE["allowed_patterns"])
            if not allowed:
                errors.append(f"CS000O path is outside EV-04 maximum: {path}")
    except Exception as exc:
        errors.append(str(exc))
    return errors


def integration_errors() -> list[str]:
    errors: list[str] = []
    try:
        if tree_of(CS021_SOURCE) != CS021_SOURCE_TREE:
            errors.append("CS021 source tree mismatch")
        if tree_of(CS021_BINDING) != CS021_BINDING_TREE:
            errors.append("CS021 binding tree mismatch")
        if parent_line(CS021_BINDING) != [CS021_BINDING, CS021_SOURCE]:
            errors.append("CS021 binding parent mismatch")
        if tree_of(CS021_MERGE) != CS021_BINDING_TREE:
            errors.append("CS021 protected merge tree differs from accepted binding tree")
        if parent_line(CS021_MERGE) != [CS021_MERGE, TECHNICAL_BASE, CS021_BINDING]:
            errors.append("CS021 protected merge parent geometry mismatch")
        result = load(CS021_RESULT)
        if result.get("changeset") != "CS021":
            errors.append("CS021 result changeset mismatch")
        if result.get("source_sha") != CS021_SOURCE or result.get("plan_commit") != CS021_SOURCE:
            errors.append("CS021 result source binding mismatch")
        if result.get("run_id") != CS021_QUALIFYING_RUN or result.get("run_attempt") != 1:
            errors.append("CS021 qualifying run binding mismatch")
        if result.get("validation_state") != "VALIDATED" or result.get("acceptance_decision") != "ACCEPTED":
            errors.append("CS021 result is not VALIDATED/ACCEPTED")
        tests = result.get("tests", [])
        if len(tests) != 26 or any(t.get("status") != "PASS" for t in tests):
            errors.append("CS021 result does not contain 26/26 PASS")
        qualification = result.get("qualification", {})
        if qualification.get("head_tree") != CS021_SOURCE_TREE or qualification.get("dedicated_qualification_rerun") is not False:
            errors.append("CS021 qualification identity/rerun mismatch")
        plan = load(CS021_PLAN)
        if len(plan.get("required_tests", [])) != 26:
            errors.append("CS021 validation plan no longer has 26 required tests")
        expected_plan_sha = result.get("plan_sha256")
        if not isinstance(expected_plan_sha, str) or sha256(CS021_PLAN) != expected_plan_sha:
            errors.append("CS021 plan hash no longer matches accepted result")
    except Exception as exc:
        errors.append(str(exc))
    return errors


def check_run(run_id: int, *, name: str, path: str, event: str, head_sha: str, branch: str) -> list[str]:
    errors: list[str] = []
    run_data = github_json(f"/actions/runs/{run_id}")
    checks = {
        "id": run_id,
        "name": name,
        "path": path,
        "event": event,
        "head_sha": head_sha,
        "head_branch": branch,
        "status": "completed",
        "conclusion": "success",
        "run_attempt": 1,
    }
    for key, expected in checks.items():
        if run_data.get(key) != expected:
            errors.append(f"run {run_id} field {key} mismatch")
    if run_data.get("previous_attempt_url") is not None:
        errors.append(f"run {run_id} unexpectedly has previous_attempt_url")
    return errors


def github_errors() -> list[str]:
    errors: list[str] = []
    try:
        errors += check_run(
            CS021_QUALIFYING_RUN,
            name="CS021 EV-04 property and model testing",
            path=".github/workflows/cs021-ev04-property-model-testing-validation.yml",
            event="push", head_sha=CS021_SOURCE, branch=BRANCH.replace("cs000o-ev04-ledger-closure", "cs021-ev04-property-model-testing"),
        )
        source_runs = github_json("/actions/runs?" + urllib.parse.urlencode({"head_sha": CS021_SOURCE, "event": "push", "per_page": 100}))
        if source_runs.get("total_count") != 1:
            errors.append("CS021 source push-run multiplicity is not exactly one")
        binding_runs = github_json("/actions/runs?" + urllib.parse.urlencode({"head_sha": CS021_BINDING, "event": "push", "per_page": 100}))
        if binding_runs.get("total_count") != 0:
            errors.append("CS021 binding SHA unexpectedly has push qualification run")
        pr = github_json(f"/pulls/{PR_NUMBER}")
        if pr.get("state") != "closed" or pr.get("merged_at") is None:
            errors.append("PR #59 is not merged")
        if pr.get("merge_commit_sha") != CS021_MERGE:
            errors.append("PR #59 merge SHA mismatch")
        if pr.get("head", {}).get("sha") != CS021_BINDING or pr.get("base", {}).get("sha") != TECHNICAL_BASE:
            errors.append("PR #59 head/base identity mismatch")
        if pr.get("commits") != 2 or pr.get("changed_files") != 11:
            errors.append("PR #59 geometry mismatch")
        errors += check_run(CS021_CANDIDATE_RUN, name="ChangeSet validation", path=".github/workflows/changeset-validation.yml", event="pull_request", head_sha=CS021_BINDING, branch="agent/cs021-ev04-property-model-testing")
        errors += check_run(CS021_TRUSTED_RUN, name="ChangeSet validation", path=".github/workflows/changeset-validation.yml", event="pull_request_target", head_sha=CS021_BINDING, branch="agent/cs021-ev04-property-model-testing")
        trusted_jobs = github_json(f"/actions/runs/{CS021_TRUSTED_RUN}/jobs?per_page=100")
        trusted = [j for j in trusted_jobs.get("jobs", []) if j.get("name") == "Trusted ChangeSet validation gate"]
        if len(trusted) != 1 or trusted[0].get("conclusion") != "success":
            errors.append("PR #59 trusted ChangeSet gate job is not exactly one successful job")
        errors += check_run(CS021_PRODUCT_PR_RUN, name="Current product regression", path=".github/workflows/current-product-regression.yml", event="pull_request", head_sha=CS021_BINDING, branch="agent/cs021-ev04-property-model-testing")
        errors += check_run(CS021_POSTMERGE_CHANGESET_RUN, name="ChangeSet validation", path=".github/workflows/changeset-validation.yml", event="push", head_sha=CS021_MERGE, branch="main")
        errors += check_run(CS021_POSTMERGE_PRODUCT_RUN, name="Current product regression", path=".github/workflows/current-product-regression.yml", event="push", head_sha=CS021_MERGE, branch="main")
        errors += check_run(CS000N_POSTMERGE_CHANGESET_RUN, name="ChangeSet validation", path=".github/workflows/changeset-validation.yml", event="push", head_sha=BASE, branch="main")
        errors += check_run(CS000N_POSTMERGE_PRODUCT_RUN, name="Current product regression", path=".github/workflows/current-product-regression.yml", event="push", head_sha=BASE, branch="main")
    except Exception as exc:
        errors.append(f"GitHub provenance query failed: {exc}")
    return errors


def cs000n_errors() -> list[str]:
    errors: list[str] = []
    try:
        result = load(CS000N_RESULT)
        if result.get("changeset") != "CS000N" or result.get("source_sha") != CS000N_SOURCE:
            errors.append("CS000N accepted result identity mismatch")
        if result.get("validation_state") != "VALIDATED" or result.get("acceptance_decision") != "ACCEPTED":
            errors.append("CS000N is not VALIDATED/ACCEPTED")
        effects = result.get("effects", {})
        expected = {
            "stage_scope": "EV04_CLOSURE_SCOPE_AUTHORIZED",
            "current_stage": "EV-04", "ev04": "IN_PROGRESS", "cs000o": "DORMANT",
            "evreq_016_018": "IN_PROGRESS", "ev05": "NOT_STARTED", "release": "NOT_AUTHORIZED",
            "runtime": "NONE", "abi": "NONE", "product_source": "NONE", "product_tests": "NONE",
            "golden_corpus_bytes": "UNCHANGED", "historical_evidence_rewrite": "NONE",
            "base_manifest_anomaly": "PRESERVED", "current_manifest": "REPAIRED",
        }
        if effects != expected:
            errors.append("CS000N accepted effects mismatch")
        if parent_line(BASE) != [BASE, CS021_MERGE, CS000N_BINDING]:
            errors.append("CS000N protected integration parent mismatch")
        if tree_of(BASE) != BASE_TREE:
            errors.append("CS000N protected integration tree mismatch")
        base_scope_raw = git_show_bytes(BASE, SCOPE_LEDGER.as_posix())
        if base_scope_raw is None or (ROOT / SCOPE_LEDGER).read_bytes() != base_scope_raw:
            errors.append("STAGE_SCOPE_MAXIMA changed from accepted CS000N protected base")
    except Exception as exc:
        errors.append(str(exc))
    return errors


def technical_evidence_errors() -> list[str]:
    errors: list[str] = []
    try:
        evidence = load(EVIDENCE)
        rows = evidence.get("files")
        if not isinstance(rows, list):
            return ["acceptance evidence files is not a list"]
        by_path = {row.get("path"): row.get("sha256") for row in rows if isinstance(row, dict)}
        if set(by_path) != ACCEPTED_EVIDENCE_FILES or len(by_path) != len(rows):
            errors.append("accepted evidence file inventory mismatch")
        expected_rows: dict[str, str] = {}
        for path in sorted(ACCEPTED_EVIDENCE_FILES):
            raw = git_show_bytes(BASE, path)
            if raw is None:
                errors.append(f"accepted evidence path missing at protected base: {path}")
                continue
            digest = hashlib.sha256(raw).hexdigest()
            expected_rows[path] = digest
            if by_path.get(path) != digest:
                errors.append(f"acceptance manifest hash mismatch: {path}")
            current = ROOT / path
            if not current.is_file() or hashlib.sha256(current.read_bytes()).hexdigest() != digest:
                errors.append(f"accepted evidence bytes changed in candidate: {path}")
        fingerprint = evidence_fingerprint(expected_rows)
        if evidence.get("accepted_evidence_fingerprint_sha256") != fingerprint:
            errors.append("accepted evidence fingerprint mismatch")
        test_text = (ROOT / PROPERTY_TEST).read_text(encoding="utf-8")
        for token in (
            "0xC5021E0400000001", "reference_step", "canonical_serialize",
            "SnapshotStrategy", "rollback", "resimulate",
        ):
            if token not in test_text:
                errors.append(f"property/model technical evidence token missing: {token}")
    except Exception as exc:
        errors.append(str(exc))
    return errors


def ledger_errors() -> list[str]:
    errors: list[str] = []
    try:
        before = base_json(ROADMAP)
        expected = copy.deepcopy(before)
        stages = {item.get("stage_id"): item for item in expected.get("stages", [])}
        ev04 = stages.get("EV-04")
        if ev04 is None:
            return ["base roadmap lacks EV-04"]
        if ev04.get("status") != "in_progress" or any(ev04.get(k) is not None for k in ("accepted_commit", "evidence_manifest", "decision_record")):
            errors.append("protected base EV-04 is not exact open-stage state")
        ev04["status"] = "accepted"
        ev04["accepted_commit"] = CS021_MERGE
        ev04["evidence_manifest"] = EVIDENCE.as_posix()
        ev04["decision_record"] = DECISION.as_posix()
        current = load(ROADMAP)
        if current != expected:
            errors.append("roadmap differs from exact EV-04-only closure transform")
        if current.get("current_stage") != "EV-04" or current.get("release_authorized") is not False:
            errors.append("current_stage/release boundary changed")
        current_stages = {item.get("stage_id"): item for item in current.get("stages", [])}
        if current_stages.get("EV-05", {}).get("status") != "not_started":
            errors.append("EV-05 was advanced by CS000O")
    except Exception as exc:
        errors.append(str(exc))
    return errors


def requirements_errors() -> list[str]:
    errors: list[str] = []
    try:
        before = base_json(REQS)
        expected = copy.deepcopy(before)
        reqs = {item.get("requirement_id"): item for item in expected.get("requirements", [])}
        for rid, evidence_paths in REQ_EVIDENCE.items():
            item = reqs.get(rid)
            if item is None:
                errors.append(f"base requirements lacks {rid}")
                continue
            if item.get("status") != "in_progress" or item.get("evidence") != []:
                errors.append(f"protected base {rid} is not exact in_progress/empty-evidence state")
            item["status"] = "verified"
            item["evidence"] = evidence_paths
        current = load(REQS)
        if current != expected:
            errors.append("requirements ledger differs from exact EVREQ-016..018 closure transform")
        current_reqs = {item.get("requirement_id"): item for item in current.get("requirements", [])}
        for rid in ("EVREQ-019", "EVREQ-020", "EVREQ-021"):
            item = current_reqs.get(rid, {})
            if item.get("status") != "planned" or item.get("evidence") != []:
                errors.append(f"{rid} was advanced or received evidence")
    except Exception as exc:
        errors.append(str(exc))
    return errors


def acceptance_errors() -> list[str]:
    errors: list[str] = []
    try:
        evidence = load(EVIDENCE)
        expected_identity = {
            "source_sha": CS021_SOURCE,
            "source_tree": CS021_SOURCE_TREE,
            "binding_sha": CS021_BINDING,
            "binding_tree": CS021_BINDING_TREE,
            "protected_merge_sha": CS021_MERGE,
            "protected_merge_tree": CS021_BINDING_TREE,
            "protected_merge_parent_1": TECHNICAL_BASE,
            "protected_merge_parent_2": CS021_BINDING,
            "pull_request": PR_NUMBER,
        }
        if evidence.get("schema") != "neoeng.dcore.evolution-evidence-manifest.v1" or evidence.get("stage") != "EV-04" or evidence.get("changeset") != "CS021" or evidence.get("closure_changeset") != "CS000O" or evidence.get("source_commit") != CS021_MERGE or evidence.get("hash_mode") != "repository-manifest-sha256":
            errors.append("EV-04 acceptance manifest header mismatch")
        if evidence.get("technical_identity") != expected_identity:
            errors.append("EV-04 acceptance technical identity mismatch")
        external = evidence.get("external_evidence", {})
        required_ids = {
            "cs021_qualifying_run": CS021_QUALIFYING_RUN,
            "pr59_candidate_diagnostic": CS021_CANDIDATE_RUN,
            "pr59_trusted_gate": CS021_TRUSTED_RUN,
            "pr59_product_regression": CS021_PRODUCT_PR_RUN,
            "main_changeset_validation": CS021_POSTMERGE_CHANGESET_RUN,
            "post_merge_product_regression": CS021_POSTMERGE_PRODUCT_RUN,
        }
        for key, run_id in required_ids.items():
            if external.get(key, {}).get("run_id") != run_id or external.get(key, {}).get("run_attempt") != 1 or external.get(key, {}).get("conclusion") != "success":
                errors.append(f"acceptance external evidence mismatch: {key}")
        multiplicity = external.get("qualification_multiplicity", {})
        if multiplicity != {"source_sha_runs": 1, "binding_sha_runs": 0, "qualifying_run_id": CS021_QUALIFYING_RUN, "qualifying_rerun": False}:
            errors.append("qualification multiplicity evidence mismatch")
        authority = evidence.get("closure_authority", {})
        if authority.get("cs000n_protected_merge") != BASE or authority.get("post_merge_changeset_run") != CS000N_POSTMERGE_CHANGESET_RUN or authority.get("post_merge_product_run") != CS000N_POSTMERGE_PRODUCT_RUN:
            errors.append("CS000N closure authority evidence mismatch")
        text = (ROOT / DECISION).read_text(encoding="utf-8")
        for token in ("# DEV-0015", "ACCEPT EV-04 SUBJECT TO CS000O QUALIFICATION", CS021_MERGE, BASE, "EVREQ-016..018", "EV-05 remains `not_started`", "release remains unauthorized"):
            if token not in text:
                errors.append(f"DEV-0015 required token missing: {token}")
    except Exception as exc:
        errors.append(str(exc))
    return errors


def descriptor_errors() -> list[str]:
    errors: list[str] = []
    try:
        descriptor = load(DESCRIPTOR)
        if descriptor != {"schema": "neoeng.dcore.current-changeset-validation.v1", "plan_path": PLAN.as_posix()}:
            errors.append("source descriptor is not exact plan-only CS000O geometry")
        if (ROOT / RESULT).exists():
            errors.append("VALIDATION_RESULT.json must be absent from source candidate")
    except Exception as exc:
        errors.append(str(exc))
    return errors


def scope_errors() -> list[str]:
    errors: list[str] = []
    try:
        actual = changed_paths()
        if actual != SOURCE_REQUIRED:
            errors.append(f"source scope mismatch: expected={sorted(SOURCE_REQUIRED)} actual={sorted(actual)}")
        if workflow_trigger_paths() != TRIGGER_SCOPE:
            errors.append("workflow trigger paths are not exact 8-path trigger scope")
        text = (ROOT / WORKFLOW).read_text(encoding="utf-8")
        if f"      - {BRANCH}" not in text:
            errors.append("workflow branch trigger mismatch")
        if RESULT.as_posix() in text or "MANIFEST.sha256" in workflow_trigger_paths() or DESCRIPTOR.as_posix() in workflow_trigger_paths():
            errors.append("lifecycle path leaked into qualification trigger")
        plan = load(PLAN)
        tests = plan.get("required_tests", [])
        ids = [item.get("test_id") for item in tests]
        if ids != EXPECTED_TESTS or not all(item.get("required") is True for item in tests):
            errors.append("validation plan required-test inventory mismatch")
        if set(plan.get("frozen_files", [])) != FROZEN_FILES:
            errors.append("validation plan frozen-file inventory mismatch")
    except Exception as exc:
        errors.append(str(exc))
    return errors


def non_effects_errors() -> list[str]:
    errors: list[str] = []
    try:
        for path in (
            "CMakeLists.txt", "tests/property_model_tests.cpp", SCOPE_LEDGER.as_posix(),
            "audit/validation/CS021/VALIDATION_RESULT.json",
            ".github/workflows/changeset-validation.yml", ".github/workflows/current-product-regression.yml",
        ):
            raw = git_show_bytes(BASE, path)
            current = ROOT / path
            if raw is None or not current.is_file() or current.read_bytes() != raw:
                errors.append(f"forbidden/non-effect path changed: {path}")
        for path in ACCEPTED_EVIDENCE_FILES:
            raw = git_show_bytes(BASE, path)
            current = ROOT / path
            if raw is None or not current.is_file() or current.read_bytes() != raw:
                errors.append(f"accepted evidence path changed: {path}")
        roadmap = load(ROADMAP)
        if roadmap.get("release_authorized") is not False or roadmap.get("current_stage") != "EV-04":
            errors.append("release/current_stage non-effect failed")
    except Exception as exc:
        errors.append(str(exc))
    return errors


def all_errors() -> list[str]:
    errors: list[str] = []
    for label, fn in (
        ("self-test", self_test_errors), ("authority", authority_errors),
        ("integration", integration_errors), ("github", github_errors),
        ("cs000n", cs000n_errors), ("technical-evidence", technical_evidence_errors),
        ("ledger", ledger_errors), ("requirements", requirements_errors),
        ("acceptance", acceptance_errors), ("descriptor", descriptor_errors),
        ("scope", scope_errors), ("non-effects", non_effects_errors),
    ):
        for error in fn():
            errors.append(f"{label}: {error}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--self-test", action="store_true")
    group.add_argument("--authority", action="store_true")
    group.add_argument("--integration", action="store_true")
    group.add_argument("--github", action="store_true")
    group.add_argument("--cs000n", action="store_true")
    group.add_argument("--technical-evidence", action="store_true")
    group.add_argument("--ledger", action="store_true")
    group.add_argument("--requirements", action="store_true")
    group.add_argument("--acceptance", action="store_true")
    group.add_argument("--descriptor", action="store_true")
    group.add_argument("--scope", action="store_true")
    group.add_argument("--non-effects", action="store_true")
    group.add_argument("--all", action="store_true")
    args = parser.parse_args()
    checks = [
        (args.self_test, "self-test", self_test_errors),
        (args.authority, "authority", authority_errors),
        (args.integration, "CS021 integration", integration_errors),
        (args.github, "GitHub provenance", github_errors),
        (args.cs000n, "CS000N authorization", cs000n_errors),
        (args.technical_evidence, "technical evidence", technical_evidence_errors),
        (args.ledger, "ledger closure", ledger_errors),
        (args.requirements, "requirements closure", requirements_errors),
        (args.acceptance, "acceptance evidence", acceptance_errors),
        (args.descriptor, "descriptor", descriptor_errors),
        (args.scope, "scope", scope_errors),
        (args.non_effects, "non-effects", non_effects_errors),
        (args.all, "all", all_errors),
    ]
    for selected, label, fn in checks:
        if selected:
            return emit(label, fn())
    return 2


if __name__ == "__main__":
    sys.exit(main())
