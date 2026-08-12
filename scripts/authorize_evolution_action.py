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
    rel = Path(f"docs/changesets/{changeset.removeprefix('CS')}/ACTION_SCOPE.json")
    path = root / rel
    if not path.is_file():
        if changeset == "CS016A":
            path = root / "docs/changesets/016A/ACTION_SCOPE.json"
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
            reasons.append("CS016A governance amendment cannot authorize runtime change")
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


def self_test(root: Path) -> list[str]:
    failures: list[str] = []
    roadmap = load_json(root / ROADMAP)
    actual_amendments = load_json(root / AMENDMENTS)
    policy = load_json(root / POLICY)

    scope = load_scope(root, "CS016A")
    if scope is None:
        return ["CS016A ACTION_SCOPE missing"]

    # Self-tests are state-independent: explicit fixtures prevent the test from
    # becoming invalid merely because the repository advanced from in_progress
    # to accepted.
    in_progress = copy.deepcopy(actual_amendments)
    row = amendment_map(in_progress).get("CS016A")
    if row is None:
        return ["CS016A amendment missing"]
    row["status"] = "in_progress"
    row["accepted_source_commit"] = None
    row["evidence_manifest"] = None

    accepted = copy.deepcopy(in_progress)
    accepted_row = amendment_map(accepted)["CS016A"]
    accepted_row["status"] = "accepted"
    accepted_row["accepted_source_commit"] = "a" * 40
    accepted_row["evidence_manifest"] = "evidence.json"

    gov = authorize_state(
        root=root,
        roadmap=roadmap,
        amendments=in_progress,
        policy=policy,
        action="governance_amendment",
        changeset="CS016A",
        stage=None,
        paths=["docs/governance/DLAB_VALIDATION_STANDARD.md"],
        scope_override=scope,
    )
    if not gov["authorized"]:
        failures.append("valid in-progress CS016A governance amendment was rejected")

    premature = authorize_state(
        root=root,
        roadmap=roadmap,
        amendments=in_progress,
        policy=policy,
        action="preflight",
        changeset="CS017",
        stage="EV-00",
        paths=[],
    )
    if premature["authorized"]:
        failures.append("PRE-CS017 was authorized before CS016A acceptance")
    if not any(
        "required amendments not accepted" in r for r in premature["reasons"]
    ):
        failures.append("PRE-CS017 rejection did not identify amendment blocker")

    ready = authorize_state(
        root=root,
        roadmap=roadmap,
        amendments=accepted,
        policy=policy,
        action="preflight",
        changeset="CS017",
        stage="EV-00",
        paths=[],
    )
    if not ready["authorized"]:
        failures.append(
            "EV-00 preflight remained rejected after simulated amendment acceptance: "
            + "; ".join(ready["reasons"])
        )

    closed_governance = authorize_state(
        root=root,
        roadmap=roadmap,
        amendments=accepted,
        policy=policy,
        action="governance_amendment",
        changeset="CS016A",
        stage=None,
        paths=[],
        scope_override=scope,
    )
    if closed_governance["authorized"]:
        failures.append("accepted CS016A still authorized governance_amendment work")

    unknown = authorize_state(
        root=root,
        roadmap=roadmap,
        amendments=in_progress,
        policy=policy,
        action="invented_action",
        changeset="CS016A",
        stage=None,
        paths=[],
    )
    if unknown["authorized"]:
        failures.append("unknown action type was authorized")

    outside = authorize_state(
        root=root,
        roadmap=roadmap,
        amendments=in_progress,
        policy=policy,
        action="governance_amendment",
        changeset="CS016A",
        stage=None,
        paths=["src/core.cpp"],
        scope_override=scope,
    )
    if outside["authorized"]:
        failures.append("runtime path was authorized by CS016A")

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
