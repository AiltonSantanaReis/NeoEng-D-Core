#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import fnmatch
import json
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]

ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
AMENDMENTS = Path("audit/EVOLUTION_AMENDMENTS.json")
POLICY = Path("audit/DLAB_EXECUTION_POLICY.json")


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
    return {
        str(row.get("stage_id")): row
        for row in rows
        if isinstance(row, dict) and isinstance(row.get("stage_id"), str)
    }


def amendment_map(doc: dict[str, Any]) -> dict[str, dict[str, Any]]:
    rows = doc.get("amendments")
    if not isinstance(rows, list):
        return {}
    return {
        str(row.get("changeset")): row
        for row in rows
        if isinstance(row, dict) and isinstance(row.get("changeset"), str)
    }


def required_amendments_for_stage(
    amendments: dict[str, Any], stage: str
) -> list[dict[str, Any]]:
    rows = amendments.get("amendments")
    if not isinstance(rows, list):
        return []
    return [
        row
        for row in rows
        if isinstance(row, dict) and row.get("required_before_stage") == stage
    ]


def path_allowed(path: str, allowed: list[str], forbidden: list[str]) -> bool:
    normalized = path.replace("\\", "/").lstrip("./")
    if any(fnmatch.fnmatch(normalized, pat) for pat in forbidden):
        return False
    return any(fnmatch.fnmatch(normalized, pat) for pat in allowed)


def load_scope(root: Path, changeset: str) -> dict[str, Any] | None:
    path = root / f"docs/changesets/{changeset.removeprefix('CS')}/ACTION_SCOPE.json"
    if not path.is_file():
        return None
    return load_json(path)


def authorize_state(
    *,
    root: Path,
    roadmap: dict[str, Any],
    amendments: dict[str, Any],
    policy: dict[str, Any],
    action: str,
    changeset: str | None,
    stage: str | None,
    paths: list[str],
    scope_override: dict[str, Any] | None = None,
) -> dict[str, Any]:
    result: dict[str, Any] = {
        "schema": "neoeng.dcore.evolution-action-authorization.v1",
        "action": action,
        "changeset": changeset,
        "stage": stage,
        "paths": paths,
        "authorized": False,
        "decision": "REJECT",
        "reasons": [],
    }
    reasons: list[str] = result["reasons"]

    if policy.get("fail_closed") is not True:
        reasons.append("execution policy is not fail_closed")
        return result

    action_types = policy.get("action_types")
    if not isinstance(action_types, list) or action not in action_types:
        reasons.append(f"unknown or unauthorized action type: {action}")
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
            reasons.append(
                f"amendment {changeset} must be in_progress for governance work"
            )
            return result
        scope = scope_override if scope_override is not None else load_scope(root, changeset)
        if scope is None:
            reasons.append(f"missing ACTION_SCOPE for {changeset}")
            return result
        if scope.get("runtime_change_authorized") is not False:
            reasons.append("governance amendment cannot authorize runtime change")
            return result
        if paths:
            allowed = scope.get("allowed_paths")
            forbidden = scope.get("forbidden_paths")
            if not isinstance(allowed, list) or not isinstance(forbidden, list):
                reasons.append("invalid ACTION_SCOPE path lists")
                return result
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

    current_stage = roadmap.get("current_stage")
    if current_stage != effective_stage:
        reasons.append(
            f"requested stage {effective_stage} differs from current_stage {current_stage}"
        )
        return result

    stage_row = stages[effective_stage]
    planned_changeset = stage_row.get("planned_changeset")
    if not changeset:
        reasons.append("stage action requires --changeset")
        return result
    if changeset != planned_changeset:
        reasons.append(
            f"changeset {changeset} does not match planned {planned_changeset}"
        )
        return result

    blockers = [
        str(row.get("changeset"))
        for row in required_amendments_for_stage(amendments, effective_stage)
        if row.get("status") != "accepted"
    ]
    if blockers:
        reasons.append(
            "required amendments not accepted: " + ", ".join(sorted(blockers))
        )
        return result

    status = stage_row.get("status")

    if action in {"preflight", "prepare_stage_changeset"}:
        if status != "not_started":
            reasons.append(
                f"{action} requires stage status not_started; actual={status!r}"
            )
            return result
        preparation_allowed = [
            f"docs/changesets/{changeset.removeprefix('CS')}/**",
            "audit/EVOLUTION_ROADMAP.json",
            "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
            "audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016A.json",
            "audit/EVOLUTION_INVARIANTS.json",
            "audit/EVOLUTION_INVARIANTS_AMENDMENT_016A.json",
            "MANIFEST.sha256",
        ]
        preparation_forbidden = ["src/**", "include/**"]
        bad = [
            p
            for p in paths
            if not path_allowed(p, preparation_allowed, preparation_forbidden)
        ]
        if bad:
            reasons.append(
                "preparation paths outside fail-closed allowlist: " + ", ".join(bad)
            )
            return result
        result["authorized"] = True
        result["decision"] = "AUTHORIZED"
        return result

    if action == "start_stage":
        if status != "in_progress":
            reasons.append(
                "start_stage is a post-change verification action and requires "
                f"roadmap status in_progress; actual={status!r}"
            )
            return result
        result["authorized"] = True
        result["decision"] = "AUTHORIZED"
        return result

    if action == "stage_operation":
        if status != "in_progress":
            reasons.append(
                f"stage_operation requires in_progress; actual={status!r}"
            )
            return result
        scope = scope_override if scope_override is not None else load_scope(root, changeset)
        if scope is None:
            reasons.append(f"missing ACTION_SCOPE for {changeset}")
            return result
        allowed = scope.get("allowed_paths")
        forbidden = scope.get("forbidden_paths")
        if not isinstance(allowed, list) or not isinstance(forbidden, list):
            reasons.append("invalid ACTION_SCOPE path lists")
            return result
        bad = [p for p in paths if not path_allowed(p, allowed, forbidden)]
        if bad:
            reasons.append("paths outside ACTION_SCOPE: " + ", ".join(bad))
            return result
        result["authorized"] = True
        result["decision"] = "AUTHORIZED"
        return result

    if action == "advance_stage":
        if status != "accepted":
            reasons.append(
                f"advance_stage requires accepted current stage; actual={status!r}"
            )
            return result
        result["authorized"] = True
        result["decision"] = "AUTHORIZED"
        return result

    if action == "release":
        if roadmap.get("release_authorized") is not True:
            reasons.append("release_authorized is not true")
            return result
        result["authorized"] = True
        result["decision"] = "AUTHORIZED"
        return result

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


def set_amendment_status(
    amendments: dict[str, Any], changeset: str, status: str
) -> None:
    row = amendment_map(amendments).get(changeset)
    if row is None:
        raise ValueError(f"amendment fixture missing: {changeset}")
    row["status"] = status
    if status == "accepted":
        row["accepted_source_commit"] = "a" * 40
        row["evidence_manifest"] = "fixture-evidence.json"
    else:
        row["accepted_source_commit"] = None
        row["evidence_manifest"] = None


def self_test(root: Path) -> list[str]:
    failures: list[str] = []
    actual_roadmap = load_json(root / ROADMAP)
    actual_amendments = load_json(root / AMENDMENTS)
    policy = load_json(root / POLICY)

    scope_a = load_scope(root, "CS016A")
    if scope_a is None:
        return ["CS016A ACTION_SCOPE missing"]
    scope_b = load_scope(root, "CS016B")
    if scope_b is None:
        return ["CS016B ACTION_SCOPE missing"]

    # Every lifecycle state used by a regression is explicit. The live repository
    # state must never become an implicit precondition of a synthetic self-test.
    roadmap_not_started = copy.deepcopy(actual_roadmap)
    set_stage_status(roadmap_not_started, "EV-00", "not_started")
    roadmap_in_progress = copy.deepcopy(actual_roadmap)
    set_stage_status(roadmap_in_progress, "EV-00", "in_progress")

    all_accepted = copy.deepcopy(actual_amendments)
    set_amendment_status(all_accepted, "CS016A", "accepted")
    set_amendment_status(all_accepted, "CS016B", "accepted")

    a_in_progress = copy.deepcopy(all_accepted)
    set_amendment_status(a_in_progress, "CS016A", "in_progress")
    b_in_progress = copy.deepcopy(all_accepted)
    set_amendment_status(b_in_progress, "CS016B", "in_progress")

    # Preserve SCN-REGRESSION-001: CS016A not accepted blocks PRE-CS017.
    premature_a = authorize_state(
        root=root,
        roadmap=roadmap_not_started,
        amendments=a_in_progress,
        policy=policy,
        action="preflight",
        changeset="CS017",
        stage="EV-00",
        paths=[],
    )
    if premature_a["authorized"]:
        failures.append("PRE-CS017 was authorized before CS016A acceptance")
    if not any("CS016A" in r for r in premature_a["reasons"]):
        failures.append("PRE-CS017 rejection did not identify CS016A blocker")

    # New CS016B governance work is authorized only while CS016B is in_progress.
    gov_b = authorize_state(
        root=root,
        roadmap=roadmap_not_started,
        amendments=b_in_progress,
        policy=policy,
        action="governance_amendment",
        changeset="CS016B",
        stage=None,
        paths=["scripts/authorize_evolution_action.py"],
        scope_override=scope_b,
    )
    if not gov_b["authorized"]:
        failures.append(
            "valid in-progress CS016B governance amendment was rejected: "
            + "; ".join(gov_b["reasons"])
        )

    premature_b = authorize_state(
        root=root,
        roadmap=roadmap_not_started,
        amendments=b_in_progress,
        policy=policy,
        action="prepare_stage_changeset",
        changeset="CS017",
        stage="EV-00",
        paths=[],
    )
    if premature_b["authorized"]:
        failures.append("CS017 preparation was authorized before CS016B acceptance")
    if not any("CS016B" in r for r in premature_b["reasons"]):
        failures.append("CS017 preparation rejection did not identify CS016B blocker")

    # Ready-to-prepare fixture: amendments accepted + stage not_started.
    ready = authorize_state(
        root=root,
        roadmap=roadmap_not_started,
        amendments=all_accepted,
        policy=policy,
        action="prepare_stage_changeset",
        changeset="CS017",
        stage="EV-00",
        paths=[],
    )
    if not ready["authorized"]:
        failures.append(
            "EV-00 preparation rejected in explicit not_started fixture: "
            + "; ".join(ready["reasons"])
        )

    # SCN-REGRESSION-002: once stage is in_progress, preparation must be rejected
    # while start_stage remains the valid lifecycle action.
    repeated_prepare = authorize_state(
        root=root,
        roadmap=roadmap_in_progress,
        amendments=all_accepted,
        policy=policy,
        action="prepare_stage_changeset",
        changeset="CS017",
        stage="EV-00",
        paths=[],
    )
    if repeated_prepare["authorized"]:
        failures.append("CS017 re-preparation was authorized while EV-00 is in_progress")
    if not any("status not_started" in r for r in repeated_prepare["reasons"]):
        failures.append("in_progress preparation rejection did not expose lifecycle reason")

    start = authorize_state(
        root=root,
        roadmap=roadmap_in_progress,
        amendments=all_accepted,
        policy=policy,
        action="start_stage",
        changeset="CS017",
        stage="EV-00",
        paths=[],
    )
    if not start["authorized"]:
        failures.append(
            "start_stage rejected in explicit in_progress fixture: "
            + "; ".join(start["reasons"])
        )

    closed_b = authorize_state(
        root=root,
        roadmap=roadmap_not_started,
        amendments=all_accepted,
        policy=policy,
        action="governance_amendment",
        changeset="CS016B",
        stage=None,
        paths=[],
        scope_override=scope_b,
    )
    if closed_b["authorized"]:
        failures.append("accepted CS016B still authorized governance_amendment work")

    unknown = authorize_state(
        root=root,
        roadmap=roadmap_not_started,
        amendments=b_in_progress,
        policy=policy,
        action="invented_action",
        changeset="CS016B",
        stage=None,
        paths=[],
    )
    if unknown["authorized"]:
        failures.append("unknown action type was authorized")

    outside = authorize_state(
        root=root,
        roadmap=roadmap_not_started,
        amendments=b_in_progress,
        policy=policy,
        action="governance_amendment",
        changeset="CS016B",
        stage=None,
        paths=["src/core.cpp"],
        scope_override=scope_b,
    )
    if outside["authorized"]:
        failures.append("runtime path was authorized by CS016B")

    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--action")
    parser.add_argument("--changeset")
    parser.add_argument("--stage")
    parser.add_argument("--path", action="append", default=[])
    args = parser.parse_args()

    if args.self_test:
        try:
            failures = self_test(ROOT)
        except ValueError as exc:
            failures = [str(exc)]
        if failures:
            print("EVOLUTION ACTION AUTHORIZATION SELF-TEST: REJECT")
            for item in failures:
                print(f"- {item}")
            return 1
        print("EVOLUTION ACTION AUTHORIZATION SELF-TEST: ACCEPT")
        return 0

    if not args.action:
        parser.error("--action is required unless --self-test is used")

    try:
        roadmap = load_json(ROOT / ROADMAP)
        amendments = load_json(ROOT / AMENDMENTS)
        policy = load_json(ROOT / POLICY)
        result = authorize_state(
            root=ROOT,
            roadmap=roadmap,
            amendments=amendments,
            policy=policy,
            action=args.action,
            changeset=args.changeset,
            stage=args.stage,
            paths=args.path,
        )
    except ValueError as exc:
        result = {
            "schema": "neoeng.dcore.evolution-action-authorization.v1",
            "action": args.action,
            "changeset": args.changeset,
            "stage": args.stage,
            "paths": args.path,
            "authorized": False,
            "decision": "REJECT",
            "reasons": [str(exc)],
        }

    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0 if result["authorized"] else 2


if __name__ == "__main__":
    sys.exit(main())
