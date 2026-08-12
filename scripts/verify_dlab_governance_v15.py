#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
ACCEPTED_V14_COMMIT = "de55e0882c6400a0409b5cf881c6ee796a975cdf"
ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
AMENDMENTS = Path("audit/EVOLUTION_AMENDMENTS.json")
POLICY = Path("audit/DLAB_EXECUTION_POLICY.json")
SCENARIOS = Path("audit/DLAB_SCENARIO_CATALOG.json")
INDEX = Path("audit/SOURCE_OF_TRUTH_INDEX.json")
ROOT_TRUST = Path("audit/GOVERNANCE_ROOT_OF_TRUST.json")
MAXIMA = Path("audit/STAGE_SCOPE_MAXIMA.json")
REQ_E = Path("audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016E.json")
INV_E = Path("audit/EVOLUTION_INVARIANTS_AMENDMENT_016E.json")
AMENDMENT_E = Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_5_AMENDMENT.md")
DEV_E = Path("docs/records/evolution/DEV-0005.md")
CHANGESET_E = Path("docs/changesets/016E/CHANGESET.md")
TEST_STATUS_E = Path("docs/changesets/016E/TEST_STATUS.md")
SCOPE_E = Path("docs/changesets/016E/ACTION_SCOPE.json")
AUTHOR = Path("scripts/authorize_evolution_action.py")
ROOT_VERIFIER = Path("scripts/verify_governance_root.py")
EXPECTED_REQS = {f"EVREQ-{n:03d}" for n in range(75, 87)}
EXPECTED_INVS = {f"INV-EV-{n:03d}" for n in range(31, 42)}


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"missing required file: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {path}")
    return value


def run(cmd: list[str], *, cwd: Path = ROOT) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, check=False)


def amendment_rows(doc: dict[str, Any]) -> list[dict[str, Any]]:
    rows = doc.get("amendments")
    return [r for r in rows if isinstance(r, dict)] if isinstance(rows, list) else []


def find_amendment(doc: dict[str, Any], changeset: str) -> dict[str, Any] | None:
    return next((r for r in amendment_rows(doc) if r.get("changeset") == changeset), None)


def stage_rows(doc: dict[str, Any]) -> list[dict[str, Any]]:
    rows = doc.get("stages")
    return [r for r in rows if isinstance(r, dict)] if isinstance(rows, list) else []


def find_stage(doc: dict[str, Any], stage_id: str) -> dict[str, Any] | None:
    return next((r for r in stage_rows(doc) if r.get("stage_id") == stage_id), None)


def run_accepted_v14_gate(*, self_test: bool) -> list[str]:
    errors: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        worktree = Path(tmp) / "accepted-v14"
        add = run(["git", "-C", str(ROOT), "worktree", "add", "--detach", "--force", str(worktree), ACCEPTED_V14_COMMIT])
        if add.returncode != 0:
            return ["unable to create accepted v1.4 worktree: " + add.stderr.strip()]
        try:
            cmd = [sys.executable, "scripts/verify_dlab_governance.py"]
            if self_test:
                cmd.append("--self-test")
            proc = run(cmd, cwd=worktree)
            if proc.returncode != 0:
                label = "self-test" if self_test else "verification"
                errors.append(
                    f"accepted v1.4 D-Lab {label} failed at {ACCEPTED_V14_COMMIT}: "
                    + proc.stdout.strip() + proc.stderr.strip()
                )
        finally:
            run(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(worktree)])
            run(["git", "-C", str(ROOT), "worktree", "prune"])
    return errors


def load_authorizer():
    spec = importlib.util.spec_from_file_location("neoeng_action_authorizer_v15", ROOT / AUTHOR)
    if spec is None or spec.loader is None:
        raise ValueError("unable to load hardened action authorizer")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def stage_maximum(maxima: dict[str, Any], stage_id: str) -> dict[str, Any] | None:
    rows = maxima.get("stages")
    if not isinstance(rows, list):
        return None
    return next((r for r in rows if isinstance(r, dict) and r.get("stage_id") == stage_id), None)


def set_stage(doc: dict[str, Any], stage_id: str, status: str) -> None:
    row = find_stage(doc, stage_id)
    if row is None:
        raise ValueError(f"fixture stage missing: {stage_id}")
    row["status"] = status
    row["accepted_commit"] = None
    row["evidence_manifest"] = None
    row["decision_record"] = None


def accepted_amendment_fixture(doc: dict[str, Any], stage_id: str) -> dict[str, Any]:
    fixture = copy.deepcopy(doc)
    for row in amendment_rows(fixture):
        if row.get("required_before_stage") == stage_id:
            row["status"] = "accepted"
            if not isinstance(row.get("accepted_source_commit"), str):
                row["accepted_source_commit"] = "a" * 40
            if not row.get("evidence_manifest"):
                row["evidence_manifest"] = "fixture-evidence.json"
    return fixture


def synthetic_ev00_scope(maxima: dict[str, Any]) -> dict[str, Any]:
    maximum = stage_maximum(maxima, "EV-00")
    if not isinstance(maximum, dict):
        raise ValueError("EV-00 root maximum missing")
    allowed = maximum.get("allowed_patterns")
    forbidden = maximum.get("mandatory_forbidden_patterns")
    if not isinstance(allowed, list) or not isinstance(forbidden, list):
        raise ValueError("EV-00 root maximum path lists invalid")
    return {
        "schema": "neoeng.dcore.changeset-action-scope.v2",
        "changeset": "CS017",
        "stage": "EV-00",
        "state": "in_progress",
        "control_base_commit": ACCEPTED_V14_COMMIT,
        "runtime_change_authorized": False,
        "claim_change_authorized": False,
        "release_authorized": False,
        "destructive_operations_authorized": False,
        "allowed_paths": list(allowed),
        "forbidden_paths": list(forbidden),
    }


def validate_hardened_lifecycle() -> list[str]:
    errors: list[str] = []
    auth_proc = run([sys.executable, str(ROOT / AUTHOR), "--self-test"])
    if auth_proc.returncode != 0:
        return ["hardened authorizer self-test failed: " + auth_proc.stdout.strip() + auth_proc.stderr.strip()]
    root_proc = run([sys.executable, str(ROOT / ROOT_VERIFIER), "--self-test"])
    if root_proc.returncode != 0:
        return ["governance root self-test failed: " + root_proc.stdout.strip() + root_proc.stderr.strip()]
    auth = load_authorizer()
    roadmap = load_json(ROOT / ROADMAP)
    amendments = load_json(ROOT / AMENDMENTS)
    policy = load_json(ROOT / POLICY)
    trust = load_json(ROOT / ROOT_TRUST)
    maxima = load_json(ROOT / MAXIMA)
    ns = copy.deepcopy(roadmap)
    ip = copy.deepcopy(roadmap)
    set_stage(ns, "EV-00", "not_started")
    set_stage(ip, "EV-00", "in_progress")
    accepted = accepted_amendment_fixture(amendments, "EV-00")
    scope = synthetic_ev00_scope(maxima)

    ready = auth.authorize_state(
        root=ROOT, roadmap=ns, amendments=accepted, policy=policy,
        action="prepare_stage_changeset", changeset="CS017", stage="EV-00", paths=[],
        root_trust_override=trust, maxima_override=maxima,
    )
    repeated = auth.authorize_state(
        root=ROOT, roadmap=ip, amendments=accepted, policy=policy,
        action="prepare_stage_changeset", changeset="CS017", stage="EV-00", paths=[],
        root_trust_override=trust, maxima_override=maxima,
    )
    start = auth.authorize_state(
        root=ROOT, roadmap=ip, amendments=accepted, policy=policy,
        action="start_stage", changeset="CS017", stage="EV-00", paths=[],
        scope_override=scope, root_trust_override=trust, maxima_override=maxima,
    )
    empty_operation = auth.authorize_state(
        root=ROOT, roadmap=ip, amendments=accepted, policy=policy,
        action="stage_operation", changeset="CS017", stage="EV-00", paths=[],
        scope_override=scope, root_trust_override=trust, maxima_override=maxima,
    )
    valid_operation = auth.authorize_state(
        root=ROOT, roadmap=ip, amendments=accepted, policy=policy,
        action="stage_operation", changeset="CS017", stage="EV-00",
        paths=["docs/changesets/017/probe.json"], scope_override=scope,
        root_trust_override=trust, maxima_override=maxima,
    )
    broad = copy.deepcopy(scope)
    broad["allowed_paths"] = ["**"]
    broad_operation = auth.authorize_state(
        root=ROOT, roadmap=ip, amendments=accepted, policy=policy,
        action="stage_operation", changeset="CS017", stage="EV-00",
        paths=["docs/changesets/017/probe.json"], scope_override=broad,
        root_trust_override=trust, maxima_override=maxima,
    )
    if not ready.get("authorized"):
        errors.append("v1.5 lifecycle fixture: not_started prepare was not AUTHORIZED")
    if repeated.get("authorized"):
        errors.append("v1.5 lifecycle fixture: in_progress re-prepare was not REJECT")
    if not start.get("authorized"):
        errors.append("v1.5 lifecycle fixture: in_progress start_stage with explicit valid scope was not AUTHORIZED")
    if empty_operation.get("authorized"):
        errors.append("v1.5 lifecycle fixture: empty stage_operation was not REJECT")
    if not valid_operation.get("authorized"):
        errors.append("v1.5 lifecycle fixture: valid scoped stage_operation was not AUTHORIZED")
    if broad_operation.get("authorized"):
        errors.append("v1.5 lifecycle fixture: self-expanding scope was not REJECT")
    return errors


def validate_cs016e() -> list[str]:
    errors: list[str] = []
    for rel in (ROOT_TRUST, MAXIMA, REQ_E, INV_E, AMENDMENT_E, DEV_E, CHANGESET_E, TEST_STATUS_E, SCOPE_E):
        if not (ROOT / rel).is_file():
            errors.append(f"CS016E required file missing: {rel}")
    if errors:
        return errors

    amendments = load_json(ROOT / AMENDMENTS)
    e = find_amendment(amendments, "CS016E")
    if e is None:
        return ["CS016E amendment ledger row missing"]
    if e.get("required_before_stage") != "EV-00":
        errors.append("CS016E must gate EV-00")
    if e.get("amendment_document") != str(AMENDMENT_E):
        errors.append("CS016E amendment document binding mismatch")
    if e.get("deviation_record") != str(DEV_E):
        errors.append("CS016E deviation record binding mismatch")
    status = e.get("status")
    if status == "in_progress":
        if e.get("accepted_source_commit") is not None or e.get("evidence_manifest") is not None:
            errors.append("in-progress CS016E cannot carry acceptance evidence")
    elif status == "accepted":
        source = e.get("accepted_source_commit")
        manifest = e.get("evidence_manifest")
        if not isinstance(source, str) or len(source) != 40 or not isinstance(manifest, str) or not manifest:
            errors.append("accepted CS016E lacks exact source/evidence binding")
    else:
        errors.append(f"CS016E status not executable for v1.5 validation: {status!r}")

    policy = load_json(ROOT / POLICY)
    reg_binding = policy.get("root_of_trust_regression")
    if not isinstance(reg_binding, dict) or reg_binding.get("scenario_id") != "SCN-REGRESSION-005":
        errors.append("DLAB policy missing SCN-REGRESSION-005 binding")

    scenarios = load_json(ROOT / SCENARIOS)
    rows = scenarios.get("scenarios")
    reg5 = next((r for r in rows if isinstance(r, dict) and r.get("scenario_id") == "SCN-REGRESSION-005"), None) if isinstance(rows, list) else None
    if reg5 is None:
        errors.append("SCN-REGRESSION-005 missing")
    elif status == "in_progress":
        if reg5.get("status") != "planned" or reg5.get("evidence") != []:
            errors.append("in-progress CS016E requires SCN-REGRESSION-005 planned without acceptance evidence")
    elif status == "accepted":
        if reg5.get("status") != "passed" or not reg5.get("evidence"):
            errors.append("accepted CS016E requires SCN-REGRESSION-005 passed with evidence")

    req = load_json(ROOT / REQ_E)
    req_rows = req.get("requirements")
    req_ids = {r.get("requirement_id") for r in req_rows if isinstance(r, dict)} if isinstance(req_rows, list) else set()
    if req.get("schema") != "neoeng.dcore.evolution-requirements-amendment-016e.v1" or req_ids != EXPECTED_REQS:
        errors.append("CS016E requirements must be exactly EVREQ-075..EVREQ-086")
    elif isinstance(req_rows, list):
        for row in req_rows:
            if not isinstance(row, dict):
                continue
            if status == "in_progress" and (row.get("status") != "planned" or row.get("evidence") != []):
                errors.append(f"in-progress requirement not cleanly planned: {row.get('requirement_id')}")
            if status == "accepted" and (row.get("status") != "verified" or not row.get("evidence")):
                errors.append(f"accepted requirement lacks verified evidence: {row.get('requirement_id')}")

    inv = load_json(ROOT / INV_E)
    inv_rows = inv.get("invariants")
    inv_ids = {r.get("invariant_id") for r in inv_rows if isinstance(r, dict)} if isinstance(inv_rows, list) else set()
    if inv.get("schema") != "neoeng.dcore.evolution-invariants-amendment-016e.v1" or inv_ids != EXPECTED_INVS:
        errors.append("CS016E invariants must be exactly INV-EV-031..INV-EV-041")
    elif isinstance(inv_rows, list):
        for row in inv_rows:
            if isinstance(row, dict) and (row.get("status") != "active" or not row.get("enforcement")):
                errors.append(f"CS016E invariant inactive/unenforced: {row.get('invariant_id')}")

    index = load_json(ROOT / INDEX)
    active = index.get("active_evolution_program")
    if not isinstance(active, dict) or active.get("effective_master_plan_amendment") != str(AMENDMENT_E):
        errors.append("Source of Truth Index does not activate Amendment 1.5")
    precedence = index.get("precedence")
    ledgers = index.get("machine_ledgers")
    for rel in (str(REQ_E), str(INV_E), str(ROOT_TRUST), str(MAXIMA)):
        if not isinstance(precedence, list) or precedence.count(rel) != 1:
            errors.append(f"CS016E authority not uniquely registered in precedence: {rel}")
        if rel.endswith(".json") and not isinstance(ledgers, list):
            errors.append("Source of Truth machine_ledgers invalid")

    roadmap = load_json(ROOT / ROADMAP)
    if roadmap.get("release_authorized") is not False:
        errors.append("CS016E cannot authorize release")
    ev00 = find_stage(roadmap, "EV-00")
    if status == "in_progress" and isinstance(ev00, dict) and ev00.get("status") != "not_started":
        errors.append("official EV-00 must remain not_started while CS016E is in_progress")

    amendment_text = (ROOT / AMENDMENT_E).read_text(encoding="utf-8")
    for token in ("Versão normativa efetiva: **1.5**", "DEV-0005", "CS016E", "EVREQ-075", "EVREQ-086", "INV-EV-031", "INV-EV-041", "SCN-REGRESSION-005", ACCEPTED_V14_COMMIT):
        if token not in amendment_text:
            errors.append(f"Amendment 1.5 missing token: {token}")
    return errors


def validate_current() -> list[str]:
    errors = validate_cs016e()
    errors.extend(validate_hardened_lifecycle())
    return errors


def self_test() -> list[str]:
    failures = run_accepted_v14_gate(self_test=True)
    failures.extend(validate_current())
    return failures


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        errors = self_test()
        label = "D-LAB GOVERNANCE V1.5 SELF-TEST"
    else:
        errors = run_accepted_v14_gate(self_test=False)
        errors.extend(validate_current())
        label = "D-LAB GOVERNANCE V1.5 VERIFICATION"
    if errors:
        print(label + ": REJECT")
        for item in errors:
            print(f"- {item}")
        return 1
    print(label + ": ACCEPT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
