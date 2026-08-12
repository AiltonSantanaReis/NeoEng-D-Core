#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path
from typing import Any

AMENDMENTS = Path("audit/EVOLUTION_AMENDMENTS.json")
POLICY = Path("audit/DLAB_EXECUTION_POLICY.json")
SCENARIOS = Path("audit/DLAB_SCENARIO_CATALOG.json")
INDEX = Path("audit/SOURCE_OF_TRUTH_INDEX.json")

REQUIRED_CLASSES = {"normal", "integration", "degraded", "adversarial", "recovery", "soak", "combinatorial", "regression"}
REQUIRED_TYPES = {"real", "simulated", "hybrid", "physical"}
REQUIRED_REGRESSIONS = {"SCN-REGRESSION-001", "SCN-REGRESSION-002", "SCN-REGRESSION-003", "SCN-REGRESSION-004", "SCN-REGRESSION-005"}
ROOT_PRECEDENCE = {
    "audit/GOVERNANCE_ROOT_OF_TRUST.json",
    "audit/REPOSITORY_PROTECTION_POLICY.json",
    "audit/STAGE_SCOPE_MAXIMA.json",
    "audit/GOVERNANCE_ACCEPTANCE_CHAIN.json",
    "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_5_AMENDMENT.md",
}
ROOT_LEDGERS = {
    "audit/GOVERNANCE_ROOT_OF_TRUST.json",
    "audit/REPOSITORY_PROTECTION_POLICY.json",
    "audit/STAGE_SCOPE_MAXIMA.json",
    "audit/GOVERNANCE_ACCEPTANCE_CHAIN.json",
}
ROOT_VERIFIERS = {
    "scripts/authorize_evolution_action.py",
    "scripts/verify_dlab_governance.py",
    "scripts/verify_dlab_governance_v15.py",
    "scripts/verify_governance_root.py",
    "scripts/verify_governance_history.py",
    "scripts/verify_github_evidence.py",
    "scripts/verify_repository_protection.py",
    "scripts/verify_release_attestation.py",
}


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


def amendment_rows(doc: dict[str, Any]) -> list[dict[str, Any]]:
    rows = doc.get("amendments")
    return [r for r in rows if isinstance(r, dict)] if isinstance(rows, list) else []


def validate_accepted_rows(candidate: Path, trusted: Path) -> list[str]:
    errors: list[str] = []
    if candidate.resolve() == trusted.resolve() or not (trusted / AMENDMENTS).is_file():
        return errors
    trusted_rows = amendment_rows(load_json(trusted / AMENDMENTS))
    candidate_rows = amendment_rows(load_json(candidate / AMENDMENTS))
    by_id = {r.get("changeset"): r for r in candidate_rows if isinstance(r.get("changeset"), str)}
    for old in trusted_rows:
        cid = old.get("changeset")
        if old.get("status") != "accepted" or not isinstance(cid, str):
            continue
        current = by_id.get(cid)
        if current is None:
            errors.append(f"accepted amendment row removed: {cid}")
        elif current != old:
            errors.append(f"accepted amendment row rewritten: {cid}")
    return errors


def validate_policy(root: Path) -> list[str]:
    errors: list[str] = []
    doc = load_json(root / POLICY)
    if doc.get("schema") != "neoeng.dlab.execution-policy.v1":
        errors.append("DLAB execution policy schema mismatch")
    if doc.get("fail_closed") is not True:
        errors.append("DLAB execution policy is not fail_closed")
    actions = set(doc.get("action_types", [])) if isinstance(doc.get("action_types"), list) else set()
    expected_actions = {"governance_amendment", "preflight", "prepare_stage_changeset", "start_stage", "stage_operation", "advance_stage", "release"}
    if not expected_actions.issubset(actions):
        errors.append("DLAB action types incomplete")
    bindings = {
        "anti_skip_regression": "SCN-REGRESSION-001",
        "lifecycle_selftest_regression": "SCN-REGRESSION-002",
        "path_canonicalization_regression": "SCN-REGRESSION-003",
        "verifier_lifecycle_scope_regression": "SCN-REGRESSION-004",
        "root_of_trust_regression": "SCN-REGRESSION-005",
    }
    for key, sid in bindings.items():
        value = doc.get(key)
        if not isinstance(value, dict) or value.get("scenario_id") != sid:
            errors.append(f"DLAB policy regression binding invalid: {sid}")
    return errors


def validate_scenarios(root: Path) -> list[str]:
    errors: list[str] = []
    doc = load_json(root / SCENARIOS)
    if doc.get("schema") != "neoeng.dlab.scenario-catalog.v1":
        errors.append("scenario catalog schema mismatch")
    classes = set(doc.get("classes", [])) if isinstance(doc.get("classes"), list) else set()
    types = set(doc.get("types", [])) if isinstance(doc.get("types"), list) else set()
    if not REQUIRED_CLASSES.issubset(classes):
        errors.append("scenario catalog classes incomplete")
    if not REQUIRED_TYPES.issubset(types):
        errors.append("scenario catalog types incomplete")
    rows = doc.get("scenarios")
    if not isinstance(rows, list):
        return errors + ["scenario catalog rows missing"]
    by_id: dict[str, dict[str, Any]] = {}
    for row in rows:
        if not isinstance(row, dict):
            continue
        sid = row.get("scenario_id")
        if isinstance(sid, str):
            if sid in by_id:
                errors.append(f"duplicate scenario id: {sid}")
            by_id[sid] = row
    missing = sorted(REQUIRED_REGRESSIONS - set(by_id))
    if missing:
        errors.append("required regression scenarios missing: " + ", ".join(missing))
    for sid in ("SCN-REGRESSION-002", "SCN-REGRESSION-003", "SCN-REGRESSION-004"):
        row = by_id.get(sid)
        if isinstance(row, dict) and (row.get("status") != "passed" or not row.get("evidence")):
            errors.append(f"accepted historical regression not preserved: {sid}")
    amendments = load_json(root / AMENDMENTS)
    e = next((r for r in amendment_rows(amendments) if r.get("changeset") == "CS016E"), None)
    reg5 = by_id.get("SCN-REGRESSION-005")
    if isinstance(e, dict) and isinstance(reg5, dict):
        if e.get("status") == "in_progress" and (reg5.get("status") != "planned" or reg5.get("evidence") != []):
            errors.append("in-progress CS016E requires SCN-REGRESSION-005 planned with no acceptance evidence")
        if e.get("status") == "accepted" and (reg5.get("status") != "passed" or not reg5.get("evidence")):
            errors.append("accepted CS016E requires SCN-REGRESSION-005 passed with evidence")
    return errors


def validate_index(root: Path) -> list[str]:
    errors: list[str] = []
    doc = load_json(root / INDEX)
    if doc.get("schema") != "neoeng.dcore.source-of-truth-index.v1":
        errors.append("Source of Truth Index schema mismatch")
    if doc.get("primary_document") != "docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md":
        errors.append("Source of Truth primary document changed")
    precedence = doc.get("precedence")
    ledgers = doc.get("machine_ledgers")
    verifiers = doc.get("verifiers")
    if not isinstance(precedence, list) or not ROOT_PRECEDENCE.issubset(set(precedence)):
        errors.append("Source of Truth precedence omits root-of-trust authorities")
    if not isinstance(ledgers, list) or not ROOT_LEDGERS.issubset(set(ledgers)):
        errors.append("Source of Truth machine ledgers omit root-of-trust authorities")
    if not isinstance(verifiers, list) or not ROOT_VERIFIERS.issubset(set(verifiers)):
        errors.append("Source of Truth verifier registry omits root-of-trust verifiers")
    active = doc.get("active_evolution_program")
    if not isinstance(active, dict):
        return errors + ["active_evolution_program missing"]
    expected = {
        "effective_master_plan_amendment": "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_5_AMENDMENT.md",
        "governance_root": "audit/GOVERNANCE_ROOT_OF_TRUST.json",
        "stage_scope_maxima": "audit/STAGE_SCOPE_MAXIMA.json",
        "acceptance_chain": "audit/GOVERNANCE_ACCEPTANCE_CHAIN.json",
        "repository_protection_policy": "audit/REPOSITORY_PROTECTION_POLICY.json",
        "dlab_verifier": "scripts/verify_dlab_governance_v15.py",
        "historical_dlab_verifier": "scripts/verify_dlab_governance.py",
        "action_authorizer": "scripts/authorize_evolution_action.py",
        "governance_root_verifier": "scripts/verify_governance_root.py",
        "governance_history_verifier": "scripts/verify_governance_history.py",
        "github_evidence_verifier": "scripts/verify_github_evidence.py",
        "repository_protection_verifier": "scripts/verify_repository_protection.py",
        "release_attestation_verifier": "scripts/verify_release_attestation.py",
    }
    for key, value in expected.items():
        if active.get(key) != value:
            errors.append(f"active evolution program binding mismatch: {key}")
    return errors


def validate(candidate: Path, trusted: Path) -> list[str]:
    errors: list[str] = []
    errors.extend(validate_accepted_rows(candidate, trusted))
    errors.extend(validate_policy(candidate))
    errors.extend(validate_scenarios(candidate))
    errors.extend(validate_index(candidate))
    return errors


def self_test() -> list[str]:
    failures: list[str] = []
    old = {"amendments": [{"changeset": "A", "status": "accepted", "value": 1}]}
    candidate = copy.deepcopy(old)
    candidate["amendments"][0]["value"] = 2
    old_row = old["amendments"][0]
    if candidate["amendments"][0] == old_row:
        failures.append("accepted amendment mutation fixture invalid")
    if not REQUIRED_REGRESSIONS.issuperset({"SCN-REGRESSION-002", "SCN-REGRESSION-005"}):
        failures.append("regression invariant fixture invalid")
    if "scripts/verify_governance_history.py" not in ROOT_VERIFIERS:
        failures.append("history verifier is not self-registered")
    return failures


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--candidate-root", default=".")
    ap.add_argument("--trusted-root")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        errors = self_test()
    else:
        candidate = Path(args.candidate_root).resolve()
        trusted = Path(args.trusted_root).resolve() if args.trusted_root else candidate
        try:
            errors = validate(candidate, trusted)
        except (ValueError, json.JSONDecodeError) as exc:
            errors = [str(exc)]
    if errors:
        print("GOVERNANCE HISTORY VERIFICATION: REJECT")
        for item in errors:
            print(f"- {item}")
        return 1
    print("GOVERNANCE HISTORY VERIFICATION: ACCEPT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
