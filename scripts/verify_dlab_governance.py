#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import fnmatch
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
ACCEPTED_V13_COMMIT = "7393b32d2be3fd2e65eab6a738a0066c13848f6c"
BASELINE_COMMIT = "e3fff973554a2e56b8bd7afdc1132f75f3ec337c"
BASELINE_VERSION = "1.14.1"

ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
AMENDMENTS = Path("audit/EVOLUTION_AMENDMENTS.json")
POLICY = Path("audit/DLAB_EXECUTION_POLICY.json")
SCENARIOS = Path("audit/DLAB_SCENARIO_CATALOG.json")
INDEX = Path("audit/SOURCE_OF_TRUTH_INDEX.json")
AUTHOR = Path("scripts/authorize_evolution_action.py")
REQ_D = Path("audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016D.json")
INV_D = Path("audit/EVOLUTION_INVARIANTS_AMENDMENT_016D.json")
AMENDMENT_D = Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_4_AMENDMENT.md")
DEV_D = Path("docs/records/evolution/DEV-0004.md")
CHANGESET_D = Path("docs/changesets/016D/CHANGESET.md")
TEST_STATUS_D = Path("docs/changesets/016D/TEST_STATUS.md")
SCOPE_D = Path("docs/changesets/016D/ACTION_SCOPE.json")

IMMUTABLE_V13 = [
    Path("docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md"),
    Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md"),
    Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_1_AMENDMENT.md"),
    Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_2_AMENDMENT.md"),
    Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_3_AMENDMENT.md"),
    Path("docs/governance/DLAB_VALIDATION_STANDARD.md"),
    Path("docs/records/evolution/DEV-0001.md"),
    Path("docs/records/evolution/DEV-0002.md"),
    Path("docs/records/evolution/DEV-0003.md"),
    Path("docs/changesets/016A/ACTION_SCOPE.json"),
    Path("docs/changesets/016A/CHANGESET.md"),
    Path("docs/changesets/016A/TEST_STATUS.md"),
    Path("docs/changesets/016A/evidence/EVIDENCE_MANIFEST_ACCEPTED.json"),
    Path("docs/changesets/016B/ACTION_SCOPE.json"),
    Path("docs/changesets/016B/CHANGESET.md"),
    Path("docs/changesets/016B/TEST_STATUS.md"),
    Path("docs/changesets/016B/evidence/EVIDENCE_MANIFEST_ACCEPTED.json"),
    Path("docs/changesets/016C/ACTION_SCOPE.json"),
    Path("docs/changesets/016C/CHANGESET.md"),
    Path("docs/changesets/016C/TEST_STATUS.md"),
    Path("docs/changesets/016C/evidence/EVIDENCE_MANIFEST_ACCEPTED.json"),
]

REQUIRED_CLASSES = {
    "normal", "integration", "degraded", "adversarial",
    "recovery", "soak", "combinatorial", "regression",
}
REQUIRED_TYPES = {"real", "simulated", "hybrid", "physical"}
TERMINAL_STATES = {"PASSED", "FAILED", "BLOCKED", "ABORTED"}
AMENDMENT_STATUSES = {"in_progress", "blocked", "failed", "accepted", "superseded"}
REQ_STATUSES = {"planned", "in_progress", "verified", "blocked", "rejected", "superseded"}

KNOWN_ACCEPTED = {
    "CS016A": (
        "fd7c5d1645a044ff8db8ab60ea1290c1d20137d9",
        "docs/changesets/016A/evidence/EVIDENCE_MANIFEST_ACCEPTED.json",
    ),
    "CS016B": (
        "b11f91fc0db5610c7e195ff1a282e04aee80e987",
        "docs/changesets/016B/evidence/EVIDENCE_MANIFEST_ACCEPTED.json",
    ),
    "CS016C": (
        "2dfa5c0fae6a2639277bf2b6f2917428fbde4383",
        "docs/changesets/016C/evidence/EVIDENCE_MANIFEST_ACCEPTED.json",
    ),
}

EXPECTED_REGRESSIONS = {
    "SCN-REGRESSION-002": "passed",
    "SCN-REGRESSION-003": "passed",
}


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"missing required file: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"root JSON value must be object: {path}")
    return value


def is_sha(value: object) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{40}", value) is not None


def sha256_lf(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def run(cmd: list[str], *, cwd: Path = ROOT) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, check=False)


def git_bytes(commit: str, path: Path) -> bytes:
    proc = subprocess.run(
        ["git", "-C", str(ROOT), "show", f"{commit}:{path.as_posix()}"],
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        raise ValueError(f"cannot read snapshot {commit}:{path}")
    return proc.stdout


def git_file_exists(commit: str, path: Path) -> bool:
    proc = subprocess.run(
        ["git", "-C", str(ROOT), "cat-file", "-e", f"{commit}:{path.as_posix()}"],
        capture_output=True,
        check=False,
    )
    return proc.returncode == 0


def run_previous_gate(*, self_test: bool) -> list[str]:
    errors: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        worktree = Path(tmp) / "governance-v13"
        add = run([
            "git", "-C", str(ROOT), "worktree", "add", "--detach", "--force",
            str(worktree), ACCEPTED_V13_COMMIT,
        ])
        if add.returncode != 0:
            return ["unable to create accepted v1.3 governance worktree: " + add.stderr.strip()]
        try:
            cmd = [sys.executable, "scripts/verify_dlab_governance.py"]
            if self_test:
                cmd.append("--self-test")
            proc = run(cmd, cwd=worktree)
            if proc.returncode != 0:
                label = "self-test" if self_test else "verification"
                errors.append(
                    f"accepted v1.3 D-Lab {label} failed at {ACCEPTED_V13_COMMIT}: "
                    + proc.stdout.strip() + proc.stderr.strip()
                )
        finally:
            run(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(worktree)])
            run(["git", "-C", str(ROOT), "worktree", "prune"])
    return errors


def validate_immutable_v13(root: Path) -> list[str]:
    errors: list[str] = []
    for rel in IMMUTABLE_V13:
        path = root / rel
        if not path.is_file():
            errors.append(f"immutable accepted artifact missing: {rel}")
            continue
        try:
            expected = git_bytes(ACCEPTED_V13_COMMIT, rel)
        except ValueError as exc:
            errors.append(str(exc))
            continue
        if path.read_bytes() != expected:
            errors.append(f"immutable accepted artifact changed after v1.3 acceptance: {rel}")
    return errors


def validate_evidence_manifest(rel: str, source_commit: str) -> list[str]:
    errors: list[str] = []
    path = ROOT / rel
    try:
        doc = load_json(path)
    except ValueError as exc:
        return [str(exc)]
    if doc.get("source_commit") != source_commit:
        errors.append(f"evidence manifest source mismatch: {rel}")
    if doc.get("hash_mode") != "lf-normalized-text":
        errors.append(f"evidence manifest hash mode mismatch: {rel}")
    rows = doc.get("files")
    if not isinstance(rows, list) or not rows:
        return errors + [f"evidence manifest has no files: {rel}"]
    seen: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            errors.append(f"invalid evidence row in {rel}")
            continue
        evidence_rel = row.get("path")
        digest = row.get("sha256")
        if not isinstance(evidence_rel, str) or not evidence_rel.startswith("docs/changesets/"):
            errors.append(f"invalid evidence path in {rel}: {evidence_rel!r}")
            continue
        if evidence_rel == rel:
            errors.append(f"evidence manifest hashes itself: {rel}")
        if evidence_rel in seen:
            errors.append(f"duplicate evidence path in {rel}: {evidence_rel}")
        seen.add(evidence_rel)
        evidence_path = ROOT / evidence_rel
        if not evidence_path.is_file():
            errors.append(f"missing evidence file: {evidence_rel}")
        elif not isinstance(digest, str) or digest != sha256_lf(evidence_path):
            errors.append(f"evidence hash mismatch: {evidence_rel}")
    return errors


def amendment_rows(doc: dict[str, Any]) -> list[dict[str, Any]]:
    rows = doc.get("amendments")
    return [row for row in rows if isinstance(row, dict)] if isinstance(rows, list) else []


def find_amendment(doc: dict[str, Any], changeset: str) -> dict[str, Any] | None:
    return next((row for row in amendment_rows(doc) if row.get("changeset") == changeset), None)


def stage_rows(roadmap: dict[str, Any]) -> list[dict[str, Any]]:
    rows = roadmap.get("stages")
    return [row for row in rows if isinstance(row, dict)] if isinstance(rows, list) else []


def find_stage(roadmap: dict[str, Any], stage_id: str) -> dict[str, Any] | None:
    return next((row for row in stage_rows(roadmap) if row.get("stage_id") == stage_id), None)


def validate_operational_state(roadmap: dict[str, Any], amendments: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if roadmap.get("schema") != "neoeng.dcore.evolution-roadmap.v1":
        errors.append("roadmap schema mismatch")
    if roadmap.get("project_version") != BASELINE_VERSION:
        errors.append("roadmap project version mismatch")
    baseline = roadmap.get("baseline")
    if not isinstance(baseline, dict):
        errors.append("roadmap baseline missing")
    else:
        if baseline.get("release_tag") != "v1.14.1":
            errors.append("roadmap baseline tag mismatch")
        if baseline.get("release_commit") != BASELINE_COMMIT:
            errors.append("roadmap baseline commit mismatch")
        if baseline.get("historical_immutable") is not True:
            errors.append("roadmap baseline must remain immutable")
    current = roadmap.get("current_stage")
    if not isinstance(current, str):
        errors.append("roadmap current_stage missing")
        return errors
    stage = find_stage(roadmap, current)
    if stage is None:
        errors.append(f"current stage absent from roadmap: {current}")
        return errors
    if stage.get("status") not in {"not_started", "in_progress", "blocked", "failed", "accepted", "superseded"}:
        errors.append("current stage status invalid")
    if current == "EV-00" and stage.get("planned_changeset") != "CS017":
        errors.append("EV-00 planned changeset must remain CS017")
    if roadmap.get("release_authorized") is not False:
        errors.append("release_authorized must remain false before final program closure")
    rows = amendment_rows(amendments)
    if not rows:
        errors.append("amendment ledger empty")
        return errors
    seen: set[str] = set()
    for row in rows:
        cid = row.get("changeset")
        if not isinstance(cid, str) or cid in seen:
            errors.append(f"invalid/duplicate amendment id: {cid!r}")
            continue
        seen.add(cid)
        if row.get("status") not in AMENDMENT_STATUSES:
            errors.append(f"invalid amendment status: {cid}")
    return errors


def validate_amendments(doc: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if doc.get("schema") != "neoeng.dcore.evolution-amendments.v1":
        errors.append("amendments schema mismatch")
    rows = amendment_rows(doc)
    by_id = {str(row.get("changeset")): row for row in rows}
    for cid, (source, manifest) in KNOWN_ACCEPTED.items():
        row = by_id.get(cid)
        if row is None:
            errors.append(f"accepted amendment missing: {cid}")
            continue
        if row.get("status") != "accepted":
            errors.append(f"{cid} no longer accepted")
        if row.get("accepted_source_commit") != source:
            errors.append(f"{cid} accepted source changed")
        if row.get("evidence_manifest") != manifest:
            errors.append(f"{cid} evidence manifest binding changed")
        errors.extend(validate_evidence_manifest(manifest, source))
        amendment_doc = row.get("amendment_document")
        if isinstance(amendment_doc, str):
            rel = Path(amendment_doc)
            if not git_file_exists(source, rel):
                errors.append(f"{cid} amendment document absent from accepted source")
            elif (ROOT / rel).read_bytes() != git_bytes(source, rel):
                errors.append(f"{cid} normative amendment changed after accepted source")
        else:
            errors.append(f"{cid} amendment document missing")
    d = by_id.get("CS016D")
    if d is not None:
        if d.get("required_before_stage") != "EV-00":
            errors.append("CS016D must gate EV-00")
        if d.get("amendment_document") != str(AMENDMENT_D):
            errors.append("CS016D amendment document binding mismatch")
        if d.get("deviation_record") != str(DEV_D):
            errors.append("CS016D deviation binding mismatch")
        status = d.get("status")
        if status == "in_progress":
            if d.get("accepted_source_commit") is not None or d.get("evidence_manifest") is not None:
                errors.append("in-progress CS016D cannot carry acceptance evidence")
        elif status == "accepted":
            source = d.get("accepted_source_commit")
            manifest = d.get("evidence_manifest")
            if not is_sha(source) or not isinstance(manifest, str):
                errors.append("accepted CS016D lacks source/evidence binding")
            else:
                errors.extend(validate_evidence_manifest(manifest, source))
                if (ROOT / AMENDMENT_D).read_bytes() != git_bytes(source, AMENDMENT_D):
                    errors.append("CS016D normative amendment changed after accepted source")
        elif status not in {"blocked", "failed", "superseded"}:
            errors.append("CS016D status invalid")
    return errors


def validate_scenarios(doc: dict[str, Any], amendments: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if doc.get("schema") != "neoeng.dlab.scenario-catalog.v1":
        errors.append("scenario catalog schema mismatch")
    classes = doc.get("classes")
    types = doc.get("types")
    statuses = doc.get("statuses")
    if not isinstance(classes, list) or not REQUIRED_CLASSES.issubset(set(classes)):
        errors.append("scenario classes incomplete")
    if not isinstance(types, list) or not REQUIRED_TYPES.issubset(set(types)):
        errors.append("scenario types incomplete")
    if not isinstance(statuses, list) or "planned" not in statuses or "passed" not in statuses:
        errors.append("scenario statuses incomplete")
    rows = doc.get("scenarios")
    if not isinstance(rows, list):
        return errors + ["scenario list missing"]
    seen: set[str] = set()
    by_id: dict[str, dict[str, Any]] = {}
    for row in rows:
        if not isinstance(row, dict):
            errors.append("scenario row must be object")
            continue
        sid = row.get("scenario_id")
        if not isinstance(sid, str) or sid in seen:
            errors.append(f"invalid/duplicate scenario id: {sid!r}")
            continue
        seen.add(sid)
        by_id[sid] = row
        if row.get("class") not in REQUIRED_CLASSES:
            errors.append(f"invalid scenario class: {sid}")
        if row.get("type") not in REQUIRED_TYPES:
            errors.append(f"invalid scenario type: {sid}")
        if not isinstance(row.get("oracle"), str) or not row.get("oracle"):
            errors.append(f"scenario oracle missing: {sid}")
        if not isinstance(row.get("steps"), list) or not row.get("steps"):
            errors.append(f"scenario steps missing: {sid}")
        if row.get("randomized") is True:
            if row.get("seed") is None or not row.get("generator_version"):
                errors.append(f"randomized scenario lacks seed/generator: {sid}")
        if not isinstance(row.get("evidence"), list):
            errors.append(f"scenario evidence must be list: {sid}")
    for sid, expected_status in EXPECTED_REGRESSIONS.items():
        row = by_id.get(sid)
        if row is None:
            errors.append(f"required regression missing: {sid}")
        elif row.get("status") != expected_status or not row.get("evidence"):
            errors.append(f"required accepted regression not preserved: {sid}")
    d = find_amendment(amendments, "CS016D")
    reg4 = by_id.get("SCN-REGRESSION-004")
    if d is not None:
        if reg4 is None:
            errors.append("SCN-REGRESSION-004 missing while CS016D exists")
        elif d.get("status") == "accepted":
            if reg4.get("status") != "passed" or not reg4.get("evidence"):
                errors.append("accepted CS016D requires SCN-REGRESSION-004 passed with evidence")
        elif d.get("status") == "in_progress" and reg4.get("status") != "planned":
            errors.append("in-progress CS016D requires SCN-REGRESSION-004 planned")
    return errors


def validate_policy(doc: dict[str, Any], amendments: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if doc.get("schema") != "neoeng.dlab.execution-policy.v1":
        errors.append("D-Lab policy schema mismatch")
    if doc.get("fail_closed") is not True:
        errors.append("D-Lab policy must be fail_closed")
    if set(doc.get("terminal_run_states", [])) != TERMINAL_STATES:
        errors.append("terminal run states mismatch")
    actions = set(doc.get("action_types", []))
    required_actions = {
        "governance_amendment", "preflight", "prepare_stage_changeset",
        "start_stage", "stage_operation", "advance_stage", "release",
    }
    if not required_actions.issubset(actions):
        errors.append("D-Lab action types incomplete")
    for key, sid in (
        ("anti_skip_regression", "SCN-REGRESSION-001"),
        ("lifecycle_selftest_regression", "SCN-REGRESSION-002"),
        ("path_canonicalization_regression", "SCN-REGRESSION-003"),
    ):
        value = doc.get(key)
        if not isinstance(value, dict) or value.get("scenario_id") != sid:
            errors.append(f"policy regression binding invalid: {sid}")
    if find_amendment(amendments, "CS016D") is not None:
        value = doc.get("verifier_lifecycle_scope_regression")
        if not isinstance(value, dict) or value.get("scenario_id") != "SCN-REGRESSION-004":
            errors.append("policy regression binding invalid: SCN-REGRESSION-004")
        elif value.get("release_authorized_expected") is not False:
            errors.append("SCN-REGRESSION-004 must preserve release false")
    return errors


def validate_req_inv_d(amendments: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    d = find_amendment(amendments, "CS016D")
    if d is None:
        return errors
    req = load_json(ROOT / REQ_D)
    inv = load_json(ROOT / INV_D)
    req_rows = req.get("requirements")
    if req.get("schema") != "neoeng.dcore.evolution-requirements-amendment-016d.v1":
        errors.append("CS016D requirement schema mismatch")
    if not isinstance(req_rows, list) or len(req_rows) != 1 or req_rows[0].get("requirement_id") != "EVREQ-074":
        errors.append("CS016D must contain exactly EVREQ-074")
    else:
        row = req_rows[0]
        if row.get("status") not in REQ_STATUSES:
            errors.append("EVREQ-074 status invalid")
        if d.get("status") == "accepted":
            if row.get("status") != "verified" or not row.get("evidence"):
                errors.append("accepted CS016D requires EVREQ-074 verified with evidence")
        elif d.get("status") == "in_progress" and row.get("status") != "planned":
            errors.append("in-progress CS016D requires EVREQ-074 planned")
    inv_rows = inv.get("invariants")
    if inv.get("schema") != "neoeng.dcore.evolution-invariants-amendment-016d.v1":
        errors.append("CS016D invariant schema mismatch")
    if not isinstance(inv_rows, list) or len(inv_rows) != 1 or inv_rows[0].get("invariant_id") != "INV-EV-030":
        errors.append("CS016D must contain exactly INV-EV-030")
    elif inv_rows[0].get("status") != "active" or not inv_rows[0].get("enforcement"):
        errors.append("INV-EV-030 must be active and enforced")
    return errors


def validate_index(doc: dict[str, Any], amendments: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if doc.get("schema") != "neoeng.dcore.source-of-truth-index.v1":
        errors.append("source index schema mismatch")
    if doc.get("primary_document") != "docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md":
        errors.append("source index primary document mismatch")
    rows = amendment_rows(amendments)
    if not rows:
        return errors + ["amendment ledger empty for index validation"]
    latest = rows[-1]
    latest_doc = latest.get("amendment_document")
    active = doc.get("active_evolution_program")
    if not isinstance(active, dict):
        errors.append("active_evolution_program missing")
        return errors
    if active.get("effective_master_plan_amendment") != latest_doc:
        errors.append("effective amendment does not match latest amendment ledger row")
    precedence = doc.get("precedence")
    ledgers = doc.get("machine_ledgers")
    if not isinstance(precedence, list) or not isinstance(ledgers, list):
        errors.append("source index precedence/ledgers invalid")
        return errors
    for row in rows:
        amend_doc = row.get("amendment_document")
        if isinstance(amend_doc, str) and precedence.count(amend_doc) != 1:
            errors.append(f"amendment document not uniquely registered: {amend_doc}")
    for rel in (str(REQ_D), str(INV_D)):
        if find_amendment(amendments, "CS016D") is not None:
            if precedence.count(rel) != 1 or ledgers.count(rel) != 1:
                errors.append(f"CS016D ledger not uniquely registered: {rel}")
    verifiers = doc.get("verifiers")
    for rel in (
        "scripts/verify_dlab_governance.py",
        "scripts/authorize_evolution_action.py",
        "scripts/verify_evolution_plan.py",
    ):
        if not isinstance(verifiers, list) or rel not in verifiers:
            errors.append(f"required verifier not registered: {rel}")
    if active.get("baseline_commit") != BASELINE_COMMIT:
        errors.append("active program baseline changed")
    return errors


def normalize_repo_path(path: str) -> str | None:
    if not isinstance(path, str) or not path:
        return None
    value = path.replace("\\", "/")
    while value.startswith("./"):
        value = value[2:]
    if not value or value.startswith("/") or re.match(r"^[A-Za-z]:/", value):
        return None
    parts = value.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        return None
    return value


def path_allowed(path: str, allowed: list[str], forbidden: list[str]) -> bool:
    value = normalize_repo_path(path)
    if value is None:
        return False
    if any(fnmatch.fnmatch(value, pattern) for pattern in forbidden):
        return False
    return any(fnmatch.fnmatch(value, pattern) for pattern in allowed)


def validate_scope_for_current_work(roadmap: dict[str, Any], amendments: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    current = roadmap.get("current_stage")
    stage = find_stage(roadmap, str(current))
    changeset: str | None = None
    scope_rel: Path | None = None
    if isinstance(stage, dict) and stage.get("status") in {"in_progress", "accepted"}:
        planned = stage.get("planned_changeset")
        if isinstance(planned, str):
            changeset = planned
            scope_rel = Path(f"docs/changesets/{planned.removeprefix('CS')}/ACTION_SCOPE.json")
    else:
        rows = amendment_rows(amendments)
        if rows:
            latest = rows[-1]
            if latest.get("required_before_stage") == current and latest.get("status") in {"in_progress", "accepted"}:
                cid = latest.get("changeset")
                if isinstance(cid, str):
                    changeset = cid
                    scope_rel = Path(f"docs/changesets/{cid.removeprefix('CS')}/ACTION_SCOPE.json")
    if scope_rel is None or changeset is None or not (ROOT / scope_rel).is_file():
        return errors
    scope = load_json(ROOT / scope_rel)
    base = scope.get("control_base_commit")
    allowed = scope.get("allowed_paths")
    forbidden = scope.get("forbidden_paths")
    if not is_sha(base) or not isinstance(allowed, list) or not isinstance(forbidden, list):
        return [f"invalid ACTION_SCOPE for current work: {changeset}"]
    anc = run(["git", "-C", str(ROOT), "merge-base", "--is-ancestor", base, "HEAD"])
    if anc.returncode != 0:
        return [f"ACTION_SCOPE control base is not ancestor: {changeset}"]
    diff = run(["git", "-C", str(ROOT), "diff", "--name-only", f"{base}...HEAD"])
    if diff.returncode != 0:
        return [f"cannot enumerate current work scope: {changeset}"]
    changed = [line.strip() for line in diff.stdout.splitlines() if line.strip()]
    bad = [path for path in changed if not path_allowed(path, allowed, forbidden)]
    if bad:
        errors.append(f"current work contains paths outside ACTION_SCOPE {changeset}: " + ", ".join(bad))
    return errors


def load_authorizer_module():
    spec = importlib.util.spec_from_file_location("neoeng_action_authorizer", ROOT / AUTHOR)
    if spec is None or spec.loader is None:
        raise ValueError("unable to load action authorizer module")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_authorizer_and_lifecycle(roadmap: dict[str, Any], amendments: dict[str, Any], policy: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    proc = run([sys.executable, str(ROOT / AUTHOR), "--self-test"])
    if proc.returncode != 0:
        errors.append("Action Authorization self-test failed: " + proc.stdout.strip() + proc.stderr.strip())
        return errors
    try:
        auth = load_authorizer_module()
    except ValueError as exc:
        return errors + [str(exc)]
    ns = copy.deepcopy(roadmap)
    ip = copy.deepcopy(roadmap)
    ns_stage = find_stage(ns, "EV-00")
    ip_stage = find_stage(ip, "EV-00")
    if ns_stage is None or ip_stage is None:
        return errors + ["EV-00 missing from lifecycle fixture"]
    for stage, status in ((ns_stage, "not_started"), (ip_stage, "in_progress")):
        stage["status"] = status
        stage["accepted_commit"] = None
        stage["evidence_manifest"] = None
        stage["decision_record"] = None
    accepted = copy.deepcopy(amendments)
    for row in amendment_rows(accepted):
        if row.get("required_before_stage") == "EV-00":
            row["status"] = "accepted"
            if not is_sha(row.get("accepted_source_commit")):
                row["accepted_source_commit"] = "a" * 40
            if not row.get("evidence_manifest"):
                row["evidence_manifest"] = "fixture-evidence.json"
    ready = auth.authorize_state(root=ROOT, roadmap=ns, amendments=accepted, policy=policy, action="prepare_stage_changeset", changeset="CS017", stage="EV-00", paths=[])
    repeated = auth.authorize_state(root=ROOT, roadmap=ip, amendments=accepted, policy=policy, action="prepare_stage_changeset", changeset="CS017", stage="EV-00", paths=[])
    start = auth.authorize_state(root=ROOT, roadmap=ip, amendments=accepted, policy=policy, action="start_stage", changeset="CS017", stage="EV-00", paths=[])
    if not ready.get("authorized"):
        errors.append("lifecycle fixture: not_started preparation was not AUTHORIZED")
    if repeated.get("authorized"):
        errors.append("lifecycle fixture: in_progress re-preparation was not REJECT")
    if not start.get("authorized"):
        errors.append("lifecycle fixture: in_progress start_stage was not AUTHORIZED")
    errors.extend(validate_operational_state(ip, accepted))
    return errors


def validate_current() -> list[str]:
    errors: list[str] = []
    ancestry = run(["git", "-C", str(ROOT), "merge-base", "--is-ancestor", ACCEPTED_V13_COMMIT, "HEAD"])
    if ancestry.returncode != 0:
        errors.append("accepted v1.3 control commit is not ancestor of HEAD")
    errors.extend(validate_immutable_v13(ROOT))
    amendments = load_json(ROOT / AMENDMENTS)
    if find_amendment(amendments, "CS016D") is not None:
        for rel in (AMENDMENT_D, DEV_D, CHANGESET_D, TEST_STATUS_D, SCOPE_D, REQ_D, INV_D):
            if not (ROOT / rel).is_file():
                errors.append(f"missing CS016D governance file: {rel}")
    roadmap = load_json(ROOT / ROADMAP)
    policy = load_json(ROOT / POLICY)
    scenarios = load_json(ROOT / SCENARIOS)
    index = load_json(ROOT / INDEX)
    errors.extend(validate_operational_state(roadmap, amendments))
    errors.extend(validate_amendments(amendments))
    errors.extend(validate_policy(policy, amendments))
    errors.extend(validate_scenarios(scenarios, amendments))
    errors.extend(validate_req_inv_d(amendments))
    errors.extend(validate_index(index, amendments))
    errors.extend(validate_scope_for_current_work(roadmap, amendments))
    errors.extend(validate_authorizer_and_lifecycle(roadmap, amendments, policy))
    d = find_amendment(amendments, "CS016D")
    if d is not None:
        text = (ROOT / AMENDMENT_D).read_text(encoding="utf-8")
        for token in (
            "Versão normativa efetiva: **1.4**", "DEV-0004", "CS016D",
            "EVREQ-074", "INV-EV-030", "SCN-REGRESSION-004",
            "7393b32d2be3fd2e65eab6a738a0066c13848f6c",
        ):
            if token not in text:
                errors.append(f"Amendment 1.4 missing token: {token}")
        dev = (ROOT / DEV_D).read_text(encoding="utf-8")
        for token in ("31613924661", "94171862183", "5c5f328afc919527775742c702e3d1a47c1490c9", "audit/EVOLUTION_ROADMAP.json"):
            if token not in dev:
                errors.append(f"DEV-0004 missing preserved failure identity: {token}")
        if d.get("status") == "in_progress":
            if "State: in_progress" not in (ROOT / TEST_STATUS_D).read_text(encoding="utf-8"):
                errors.append("in-progress CS016D TEST_STATUS mismatch")
        if d.get("status") == "accepted":
            if "State: accepted" not in (ROOT / TEST_STATUS_D).read_text(encoding="utf-8"):
                errors.append("accepted CS016D TEST_STATUS mismatch")
    return errors


def self_test() -> list[str]:
    failures = run_previous_gate(self_test=True)
    failures.extend(validate_current())
    with tempfile.TemporaryDirectory() as tmp:
        fixture = Path(tmp)
        for rel in IMMUTABLE_V13:
            target = fixture / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((ROOT / rel).read_bytes())
        target = fixture / IMMUTABLE_V13[0]
        target.write_bytes(target.read_bytes() + b"\nTAMPER\n")
        tamper_errors = validate_immutable_v13(fixture)
        if not any("changed after v1.3 acceptance" in item for item in tamper_errors):
            failures.append("immutable-artifact tamper regression was not rejected")
    if ROADMAP in IMMUTABLE_V13:
        failures.append("operational roadmap is incorrectly classified as immutable")
    for rel in (
        Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json"),
        Path("audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016A.json"),
        Path("audit/DLAB_HISTORICAL_REVALIDATION_MATRIX.json"),
        Path("audit/DLAB_SCENARIO_CATALOG.json"),
    ):
        if rel in IMMUTABLE_V13:
            failures.append(f"operational ledger incorrectly classified immutable: {rel}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        failures = self_test()
        if failures:
            print("D-LAB GOVERNANCE SELF-TEST: REJECT")
            for item in failures:
                print(f"- {item}")
            return 1
        print("D-LAB GOVERNANCE SELF-TEST: ACCEPT")
        return 0
    errors = run_previous_gate(self_test=False)
    errors.extend(validate_current())
    if errors:
        print("D-LAB GOVERNANCE VERIFICATION: REJECT")
        for item in errors:
            print(f"- {item}")
        return 1
    print("D-LAB GOVERNANCE VERIFICATION: ACCEPT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
