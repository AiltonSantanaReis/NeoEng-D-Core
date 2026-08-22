#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import fnmatch
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
AMENDMENTS = Path("audit/EVOLUTION_AMENDMENTS.json")
POLICY = Path("audit/DLAB_EXECUTION_POLICY.json")
ROOT_TRUST = Path("audit/GOVERNANCE_ROOT_OF_TRUST.json")
SCOPE_MAXIMA = Path("audit/STAGE_SCOPE_MAXIMA.json")

WRITE_ACTIONS = {"governance_amendment", "stage_operation"}
DECISION_ONLY_ACTIONS = {"preflight", "start_stage", "advance_stage", "release"}
SHA_RE = re.compile(r"^[0-9a-f]{40}$")


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


def stage_map(roadmap: dict[str, Any]) -> dict[str, dict[str, Any]]:
    rows = roadmap.get("stages")
    if not isinstance(rows, list):
        return {}
    return {str(r.get("stage_id")): r for r in rows if isinstance(r, dict) and isinstance(r.get("stage_id"), str)}


def amendment_map(doc: dict[str, Any]) -> dict[str, dict[str, Any]]:
    rows = doc.get("amendments")
    if not isinstance(rows, list):
        return {}
    return {str(r.get("changeset")): r for r in rows if isinstance(r, dict) and isinstance(r.get("changeset"), str)}


def required_amendments_for_stage(amendments: dict[str, Any], stage: str) -> list[dict[str, Any]]:
    rows = amendments.get("amendments")
    if not isinstance(rows, list):
        return []
    return [r for r in rows if isinstance(r, dict) and r.get("required_before_stage") == stage]


def normalize_repository_path(path: str) -> str | None:
    if not isinstance(path, str) or not path:
        return None
    value = path.replace("\\", "/")
    while value.startswith("./"):
        value = value[2:]
    if not value or value.startswith("/") or value.startswith("//"):
        return None
    if re.match(r"^[A-Za-z]:/", value):
        return None
    parts = value.split("/")
    if any(part in {"", ".", ".."} for part in parts):
        return None
    return value


def path_allowed(path: str, allowed: list[str], forbidden: list[str]) -> bool:
    value = normalize_repository_path(path)
    if value is None:
        return False
    if any(fnmatch.fnmatch(value, pat) for pat in forbidden):
        return False
    return any(fnmatch.fnmatch(value, pat) for pat in allowed)


def load_scope(root: Path, changeset: str) -> dict[str, Any] | None:
    p = root / f"docs/changesets/{changeset.removeprefix('CS')}/ACTION_SCOPE.json"
    return load_json(p) if p.is_file() else None


def _pattern_contract(scope: dict[str, Any], maximum: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    allowed = scope.get("allowed_paths")
    forbidden = scope.get("forbidden_paths")
    max_allowed = maximum.get("allowed_patterns")
    mandatory_forbidden = maximum.get("mandatory_forbidden_patterns")
    if not isinstance(allowed, list) or not all(isinstance(x, str) for x in allowed):
        return ["invalid ACTION_SCOPE allowed_paths"]
    if not isinstance(forbidden, list) or not all(isinstance(x, str) for x in forbidden):
        return ["invalid ACTION_SCOPE forbidden_paths"]
    if not isinstance(max_allowed, list) or not all(isinstance(x, str) for x in max_allowed):
        return ["invalid root maximum allowed_patterns"]
    if not isinstance(mandatory_forbidden, list) or not all(isinstance(x, str) for x in mandatory_forbidden):
        return ["invalid root maximum mandatory_forbidden_patterns"]
    extra = sorted(set(allowed) - set(max_allowed))
    if extra:
        errors.append("ACTION_SCOPE attempts to expand root maximum: " + ", ".join(extra))
    missing_forbidden = sorted(set(mandatory_forbidden) - set(forbidden))
    if missing_forbidden:
        errors.append("ACTION_SCOPE omits mandatory forbidden patterns: " + ", ".join(missing_forbidden))
    if "**" in allowed or "*" in allowed:
        errors.append("unbounded wildcard is forbidden in ACTION_SCOPE")
    return errors


def _stage_maximum(maxima: dict[str, Any], stage: str) -> dict[str, Any] | None:
    rows = maxima.get("stages")
    if not isinstance(rows, list):
        return None
    return next((r for r in rows if isinstance(r, dict) and r.get("stage_id") == stage), None)


def _scope_contract_errors(
    *, scope: dict[str, Any], root_trust: dict[str, Any], maxima: dict[str, Any],
    action: str, stage: str | None, changeset: str,
) -> list[str]:
    errors: list[str] = []
    if scope.get("changeset") != changeset:
        errors.append("ACTION_SCOPE changeset mismatch")
    if scope.get("runtime_change_authorized") is not False and action == "governance_amendment":
        errors.append("governance amendment cannot authorize runtime change")
    if action == "governance_amendment":
        key = "bootstrap_governance_amendment_maximum" if changeset == root_trust.get("bootstrap_changeset") else "governance_amendment_maximum"
        maximum = root_trust.get(key)
        if not isinstance(maximum, dict):
            return errors + [f"root trust governance amendment maximum missing: {key}"]
        errors.extend(_pattern_contract(scope, maximum))
    else:
        if not isinstance(stage, str):
            return errors + ["stage missing for stage scope validation"]
        maximum = _stage_maximum(maxima, stage)
        if maximum is None:
            return errors + [f"root stage maximum missing: {stage}"]
        errors.extend(_pattern_contract(scope, maximum))
    return errors


def _base_result(action: str, changeset: str | None, stage: str | None, paths: list[str]) -> dict[str, Any]:
    return {
        "schema": "neoeng.dcore.evolution-action-authorization.v2",
        "action": action,
        "changeset": changeset,
        "stage": stage,
        "paths": paths,
        "authorized": False,
        "decision": "REJECT",
        "reasons": [],
    }


def authorize_state(
    *, root: Path, roadmap: dict[str, Any], amendments: dict[str, Any], policy: dict[str, Any],
    action: str, changeset: str | None, stage: str | None, paths: list[str],
    scope_override: dict[str, Any] | None = None,
    root_trust_override: dict[str, Any] | None = None,
    maxima_override: dict[str, Any] | None = None,
) -> dict[str, Any]:
    result = _base_result(action, changeset, stage, paths)
    reasons: list[str] = result["reasons"]

    if policy.get("fail_closed") is not True:
        reasons.append("execution policy is not fail_closed")
        return result
    action_types = policy.get("action_types")
    if not isinstance(action_types, list) or action not in action_types:
        reasons.append(f"unknown or unauthorized action type: {action}")
        return result

    if action in WRITE_ACTIONS and not paths:
        reasons.append(f"{action} requires at least one explicit --path")
        return result
    if action in DECISION_ONLY_ACTIONS and paths:
        reasons.append(f"{action} is decision-only and must not receive write paths")
        return result

    root_trust = root_trust_override if root_trust_override is not None else load_json(root / ROOT_TRUST)
    maxima = maxima_override if maxima_override is not None else load_json(root / SCOPE_MAXIMA)
    if root_trust.get("fail_closed") is not True or root_trust.get("self_authorizing_scope_forbidden") is not True:
        reasons.append("governance root of trust is not fail-closed")
        return result

    stages = stage_map(roadmap)
    amend_by_id = amendment_map(amendments)

    if action == "governance_amendment":
        if not changeset:
            reasons.append("governance_amendment requires --changeset")
            return result
        amendment = amend_by_id.get(changeset)
        if amendment is None:
            reasons.append(f"unknown amendment: {changeset}")
            return result
        if amendment.get("status") != "in_progress":
            reasons.append(f"amendment {changeset} must be in_progress for governance work")
            return result
        scope = scope_override if scope_override is not None else load_scope(root, changeset)
        if scope is None:
            reasons.append(f"missing ACTION_SCOPE for {changeset}")
            return result
        contract = _scope_contract_errors(scope=scope, root_trust=root_trust, maxima=maxima, action=action, stage=None, changeset=changeset)
        if contract:
            reasons.extend(contract)
            return result
        allowed = scope["allowed_paths"]
        forbidden = scope["forbidden_paths"]
        bad = [p for p in paths if not path_allowed(p, allowed, forbidden)]
        if bad:
            reasons.append("paths outside ACTION_SCOPE: " + ", ".join(bad))
            return result
        result["authorized"] = True
        result["decision"] = "AUTHORIZED"
        return result

    effective_stage = stage or roadmap.get("current_stage")
    if not isinstance(effective_stage, str) or effective_stage not in stages:
        reasons.append("stage is missing or not present in roadmap")
        return result
    result["stage"] = effective_stage
    if roadmap.get("current_stage") != effective_stage:
        reasons.append(f"requested stage {effective_stage} differs from current_stage {roadmap.get('current_stage')}")
        return result

    stage_row = stages[effective_stage]
    planned_changeset = stage_row.get("planned_changeset")
    if not changeset:
        reasons.append("stage action requires --changeset")
        return result
    if changeset != planned_changeset:
        reasons.append(f"changeset {changeset} does not match planned {planned_changeset}")
        return result

    blockers = [str(r.get("changeset")) for r in required_amendments_for_stage(amendments, effective_stage) if r.get("status") != "accepted"]
    if blockers:
        reasons.append("required amendments not accepted: " + ", ".join(sorted(blockers)))
        return result

    status = stage_row.get("status")
    maximum = _stage_maximum(maxima, effective_stage)
    if maximum is None:
        reasons.append(f"root stage maximum missing: {effective_stage}")
        return result

    if action == "preflight":
        if status != "not_started":
            reasons.append(f"preflight requires not_started; actual={status!r}")
            return result
        result["authorized"] = True; result["decision"] = "AUTHORIZED"; return result

    if action == "prepare_stage_changeset":
        if status != "not_started":
            reasons.append(f"prepare_stage_changeset requires stage status not_started; actual={status!r}")
            return result
        prep_allowed = maximum.get("preparation_allowed_patterns")
        mandatory_forbidden = maximum.get("mandatory_forbidden_patterns")
        if not isinstance(prep_allowed, list) or not isinstance(mandatory_forbidden, list):
            reasons.append("invalid stage preparation maximum")
            return result
        if paths:
            bad = [p for p in paths if not path_allowed(p, prep_allowed, mandatory_forbidden)]
            if bad:
                reasons.append("preparation paths outside root maximum: " + ", ".join(bad))
                return result
        result["authorized"] = True; result["decision"] = "AUTHORIZED"; return result

    if action == "start_stage":
        if status != "in_progress":
            reasons.append(f"start_stage requires in_progress; actual={status!r}")
            return result
        scope = scope_override if scope_override is not None else load_scope(root, changeset)
        if scope is None:
            reasons.append(f"missing ACTION_SCOPE for {changeset}")
            return result
        contract = _scope_contract_errors(scope=scope, root_trust=root_trust, maxima=maxima, action=action, stage=effective_stage, changeset=changeset)
        if contract:
            reasons.extend(contract); return result
        result["authorized"] = True; result["decision"] = "AUTHORIZED"; return result

    if action == "stage_operation":
        if status != "in_progress":
            reasons.append(f"stage_operation requires in_progress; actual={status!r}")
            return result
        scope = scope_override if scope_override is not None else load_scope(root, changeset)
        if scope is None:
            reasons.append(f"missing ACTION_SCOPE for {changeset}")
            return result
        contract = _scope_contract_errors(scope=scope, root_trust=root_trust, maxima=maxima, action=action, stage=effective_stage, changeset=changeset)
        if contract:
            reasons.extend(contract); return result
        bad = [p for p in paths if not path_allowed(p, scope["allowed_paths"], scope["forbidden_paths"])]
        if bad:
            reasons.append("paths outside ACTION_SCOPE: " + ", ".join(bad))
            return result
        result["authorized"] = True; result["decision"] = "AUTHORIZED"; return result

    if action == "advance_stage":
        if status != "accepted":
            reasons.append(f"advance_stage requires accepted current stage; actual={status!r}")
            return result
        for dep in stage_row.get("depends_on", []):
            dep_row = stages.get(str(dep))
            if dep_row is None or dep_row.get("status") != "accepted":
                reasons.append(f"dependency not accepted: {dep}")
        if reasons:
            return result
        if not isinstance(stage_row.get("accepted_commit"), str) or not SHA_RE.fullmatch(stage_row["accepted_commit"]):
            reasons.append("accepted stage lacks exact accepted_commit")
        if not stage_row.get("evidence_manifest") or not stage_row.get("decision_record"):
            reasons.append("accepted stage lacks evidence_manifest/decision_record")
        if reasons:
            return result
        result["authorized"] = True; result["decision"] = "AUTHORIZED"; return result

    if action == "release":
        if roadmap.get("release_authorized") is not True:
            reasons.append("release_authorized is not true")
            return result
        incomplete = [r.get("stage_id") for r in stages.values() if r.get("status") != "accepted"]
        if incomplete:
            reasons.append("release closure incomplete: " + ", ".join(map(str, incomplete)))
            return result
        weak = [r.get("stage_id") for r in stages.values() if not (isinstance(r.get("accepted_commit"), str) and SHA_RE.fullmatch(r["accepted_commit"]) and r.get("evidence_manifest") and r.get("decision_record"))]
        if weak:
            reasons.append("release stages lack exact acceptance bindings: " + ", ".join(map(str, weak)))
            return result
        result["authorized"] = True; result["decision"] = "AUTHORIZED"; return result

    reasons.append(f"no authorization rule for action: {action}")
    return result


def set_stage_status(roadmap: dict[str, Any], stage: str, status: str) -> None:
    row = stage_map(roadmap).get(stage)
    if row is None:
        raise ValueError(f"stage fixture missing: {stage}")
    row["status"] = status
    row["accepted_commit"] = None
    row["evidence_manifest"] = None
    row["decision_record"] = None


def self_test(root: Path) -> list[str]:
    failures: list[str] = []
    roadmap = load_json(root / ROADMAP)
    amendments = load_json(root / AMENDMENTS)
    policy = load_json(root / POLICY)
    trust = load_json(root / ROOT_TRUST)
    maxima = load_json(root / SCOPE_MAXIMA)

    accepted = copy.deepcopy(amendments)
    for row in accepted.get("amendments", []):
        if isinstance(row, dict) and row.get("required_before_stage") == "EV-00":
            row["status"] = "accepted"
            row["accepted_source_commit"] = row.get("accepted_source_commit") or ("a" * 40)
            row["evidence_manifest"] = row.get("evidence_manifest") or "fixture.json"

    ns = copy.deepcopy(roadmap); set_stage_status(ns, "EV-00", "not_started")
    ip = copy.deepcopy(roadmap); set_stage_status(ip, "EV-00", "in_progress")
    scope = {
        "schema": "neoeng.dcore.changeset-action-scope.v2",
        "changeset": "CS017", "stage": "EV-00", "runtime_change_authorized": False,
        "allowed_paths": ["docs/changesets/017/**", "scripts/dlab/**", ".github/workflows/ev00-dlab.yml", "audit/EVOLUTION_ROADMAP.json", "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json", "audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016A.json", "audit/DLAB_HISTORICAL_REVALIDATION_MATRIX.json", "audit/DLAB_SCENARIO_CATALOG.json", "docs/records/evolution/**", "MANIFEST.sha256"],
        "forbidden_paths": _stage_maximum(maxima, "EV-00")["mandatory_forbidden_patterns"],
    }

    empty = authorize_state(root=root, roadmap=ip, amendments=accepted, policy=policy, action="stage_operation", changeset="CS017", stage="EV-00", paths=[], scope_override=scope, root_trust_override=trust, maxima_override=maxima)
    if empty["authorized"]: failures.append("stage_operation accepted empty path set")

    empty_gov = authorize_state(root=root, roadmap=ns, amendments=amendments, policy=policy, action="governance_amendment", changeset="CS016E", stage=None, paths=[], root_trust_override=trust, maxima_override=maxima)
    if empty_gov["authorized"]: failures.append("governance_amendment accepted empty path set")

    broad = copy.deepcopy(scope); broad["allowed_paths"] = ["**"]
    broad_decision = authorize_state(root=root, roadmap=ip, amendments=accepted, policy=policy, action="stage_operation", changeset="CS017", stage="EV-00", paths=["scripts/dlab/run.py"], scope_override=broad, root_trust_override=trust, maxima_override=maxima)
    if broad_decision["authorized"]: failures.append("self-authorizing broad ACTION_SCOPE was accepted")

    weak_forbidden = copy.deepcopy(scope); weak_forbidden["forbidden_paths"] = ["src/**"]
    weak_decision = authorize_state(root=root, roadmap=ip, amendments=accepted, policy=policy, action="stage_operation", changeset="CS017", stage="EV-00", paths=["scripts/dlab/run.py"], scope_override=weak_forbidden, root_trust_override=trust, maxima_override=maxima)
    if weak_decision["authorized"]: failures.append("ACTION_SCOPE missing mandatory forbidden paths was accepted")

    for path in (".github/workflows/ev00-dlab.yml", "./.github/workflows/ev00-dlab.yml", ".github\\workflows\\ev00-dlab.yml"):
        d = authorize_state(root=root, roadmap=ip, amendments=accepted, policy=policy, action="stage_operation", changeset="CS017", stage="EV-00", paths=[path], scope_override=scope, root_trust_override=trust, maxima_override=maxima)
        if not d["authorized"]: failures.append(f"allowlisted canonical path rejected: {path}")

    for path in ("src/core.cpp", "../scripts/dlab/run.py", "/tmp/x", "C:/tmp/x", "scripts//dlab/run.py", "scripts/./dlab/run.py"):
        d = authorize_state(root=root, roadmap=ip, amendments=accepted, policy=policy, action="stage_operation", changeset="CS017", stage="EV-00", paths=[path], scope_override=scope, root_trust_override=trust, maxima_override=maxima)
        if d["authorized"]: failures.append(f"unsafe path authorized: {path}")

    prep = authorize_state(root=root, roadmap=ns, amendments=accepted, policy=policy, action="prepare_stage_changeset", changeset="CS017", stage="EV-00", paths=["docs/changesets/017/ACTION_SCOPE.json", "audit/EVOLUTION_ROADMAP.json"], root_trust_override=trust, maxima_override=maxima)
    if not prep["authorized"]: failures.append("valid preparation was rejected")
    prep_empty = authorize_state(root=root, roadmap=ns, amendments=accepted, policy=policy, action="prepare_stage_changeset", changeset="CS017", stage="EV-00", paths=[], root_trust_override=trust, maxima_override=maxima)
    if not prep_empty["authorized"]: failures.append("authorization-only preparation decision was rejected")

    future = copy.deepcopy(ns)
    future["current_stage"] = "EV-01"
    future_stage = stage_map(future).get("EV-01")
    if future_stage is not None:
        future_stage["status"] = "not_started"
        undefined = authorize_state(root=root, roadmap=future, amendments=accepted, policy=policy, action="prepare_stage_changeset", changeset="CS018", stage="EV-01", paths=[], root_trust_override=trust, maxima_override=maxima)
        if undefined["authorized"]: failures.append("undefined future stage maximum was authorized")

    rel = copy.deepcopy(roadmap); rel["release_authorized"] = True
    release = authorize_state(root=root, roadmap=rel, amendments=accepted, policy=policy, action="release", changeset="CS017", stage="EV-00", paths=[], root_trust_override=trust, maxima_override=maxima)
    if release["authorized"]: failures.append("release authorized with incomplete stages")
    return failures


def _run_release_verifiers(root: Path) -> list[str]:
    errors: list[str] = []
    for rel in ("scripts/verify_evolution_plan.py", "scripts/verify_release_assurance.py", "scripts/verify_final_acceptance.py", "scripts/verify_release_attestation.py"):
        proc = subprocess.run([sys.executable, rel], cwd=root, text=True, capture_output=True, check=False)
        if proc.returncode != 0:
            errors.append(f"release verifier failed: {rel}: {proc.stdout.strip()}{proc.stderr.strip()}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--action")
    parser.add_argument("--changeset")
    parser.add_argument("--stage")
    parser.add_argument("--path", action="append", default=[])
    args = parser.parse_args()
    if args.self_test:
        try: failures = self_test(ROOT)
        except ValueError as exc: failures = [str(exc)]
        if failures:
            print("EVOLUTION ACTION AUTHORIZATION SELF-TEST: REJECT")
            for item in failures: print(f"- {item}")
            return 1
        print("EVOLUTION ACTION AUTHORIZATION SELF-TEST: ACCEPT")
        return 0
    if not args.action:
        parser.error("--action is required unless --self-test is used")
    try:
        roadmap = load_json(ROOT / ROADMAP); amendments = load_json(ROOT / AMENDMENTS); policy = load_json(ROOT / POLICY)
        result = authorize_state(root=ROOT, roadmap=roadmap, amendments=amendments, policy=policy, action=args.action, changeset=args.changeset, stage=args.stage, paths=args.path)
        if result["authorized"] and args.action == "release":
            extra = _run_release_verifiers(ROOT)
            if extra:
                result["authorized"] = False; result["decision"] = "REJECT"; result["reasons"].extend(extra)
    except ValueError as exc:
        result = _base_result(args.action, args.changeset, args.stage, args.path); result["reasons"].append(str(exc))
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0 if result["authorized"] else 2


if __name__ == "__main__":
    sys.exit(main())
