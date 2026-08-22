#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path

import authorize_evolution_action_legacy as legacy

ROOT = Path(__file__).resolve().parents[1]
TRANSITION_STATE = Path("audit/GOVERNANCE_TRANSITION_STATE.json")


def _load_transition(root: Path) -> dict:
    return legacy.load_json(root / TRANSITION_STATE)


def _cs016e_supersession_is_authoritative(amendments: dict, transition: dict) -> bool:
    rows = legacy.required_amendments_for_stage(amendments, "EV-00")
    cs016e = next((row for row in rows if row.get("changeset") == "CS016E"), None)
    if not isinstance(cs016e, dict):
        return False
    legacy_state = transition.get("legacy_cs016e")
    prospective = transition.get("prospective_authority")
    return (
        transition.get("schema") == "neoeng.dcore.governance-transition-state.v1"
        and transition.get("changeset") == "CS000E"
        and isinstance(prospective, dict)
        and prospective.get("regime_id") == "CHANGESET_VALIDATION"
        and isinstance(legacy_state, dict)
        and legacy_state.get("status") == "SUPERSEDED_UNACCEPTED"
        and legacy_state.get("accepted") is False
        and legacy_state.get("accepted_source_commit") is None
        and legacy_state.get("evidence_manifest") is None
        and legacy_state.get("may_be_reclassified_as_accepted") is False
        and cs016e.get("status") == "superseded"
        and cs016e.get("superseded_by") == "CS000E"
        and cs016e.get("supersession_record") == str(TRANSITION_STATE)
        and cs016e.get("accepted_source_commit") is None
        and cs016e.get("evidence_manifest") is None
    )


def effective_amendments(amendments: dict, transition: dict) -> dict:
    effective = copy.deepcopy(amendments)
    rows = legacy.required_amendments_for_stage(effective, "EV-00")
    cs016e = next((row for row in rows if row.get("changeset") == "CS016E"), None)
    if isinstance(cs016e, dict) and cs016e.get("status") == "superseded":
        if not _cs016e_supersession_is_authoritative(amendments, transition):
            return effective
        # Operational resolution only. Persistent history remains superseded/unaccepted.
        cs016e["status"] = "accepted"
    return effective


def authorize_state(*, root: Path, roadmap: dict, amendments: dict, policy: dict,
                    action: str, changeset: str | None, stage: str | None, paths: list[str],
                    transition_override: dict | None = None, **kwargs) -> dict:
    transition = transition_override if transition_override is not None else _load_transition(root)
    return legacy.authorize_state(
        root=root,
        roadmap=roadmap,
        amendments=effective_amendments(amendments, transition),
        policy=policy,
        action=action,
        changeset=changeset,
        stage=stage,
        paths=paths,
        **kwargs,
    )


def self_test(root: Path) -> list[str]:
    failures = list(legacy.self_test(root))
    roadmap = legacy.load_json(root / legacy.ROADMAP)
    amendments = legacy.load_json(root / legacy.AMENDMENTS)
    policy = legacy.load_json(root / legacy.POLICY)
    transition = _load_transition(root)

    legacy_preflight = legacy.authorize_state(
        root=root, roadmap=roadmap, amendments=amendments, policy=policy,
        action="preflight", changeset="CS017", stage="EV-00", paths=[])
    if legacy_preflight.get("authorized"):
        failures.append("legacy authorizer unexpectedly resolved superseded CS016E")

    current = authorize_state(
        root=root, roadmap=roadmap, amendments=amendments, policy=policy,
        action="preflight", changeset="CS017", stage="EV-00", paths=[],
        transition_override=transition)
    if not current.get("authorized"):
        failures.append("authoritative CS016E supersession did not resolve EV-00 preflight")

    bad_transition = copy.deepcopy(transition)
    bad_transition["prospective_authority"]["regime_id"] = "TAMPERED"
    bad = authorize_state(
        root=root, roadmap=roadmap, amendments=amendments, policy=policy,
        action="preflight", changeset="CS017", stage="EV-00", paths=[],
        transition_override=bad_transition)
    if bad.get("authorized"):
        failures.append("tampered transition authority resolved CS016E")

    bad_amendments = copy.deepcopy(amendments)
    row = next(r for r in bad_amendments["amendments"] if r.get("changeset") == "CS016E")
    row["superseded_by"] = "CS999"
    bad = authorize_state(
        root=root, roadmap=roadmap, amendments=bad_amendments, policy=policy,
        action="preflight", changeset="CS017", stage="EV-00", paths=[],
        transition_override=transition)
    if bad.get("authorized"):
        failures.append("unbound supersession resolved CS016E")

    unrelated = copy.deepcopy(amendments)
    row = next(r for r in unrelated["amendments"] if r.get("changeset") == "CS016D")
    row["status"] = "superseded"
    bad = authorize_state(
        root=root, roadmap=roadmap, amendments=unrelated, policy=policy,
        action="preflight", changeset="CS017", stage="EV-00", paths=[],
        transition_override=transition)
    if bad.get("authorized"):
        failures.append("generic superseded amendment was treated as resolved")

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
        roadmap = legacy.load_json(ROOT / legacy.ROADMAP)
        amendments = legacy.load_json(ROOT / legacy.AMENDMENTS)
        policy = legacy.load_json(ROOT / legacy.POLICY)
        result = authorize_state(
            root=ROOT, roadmap=roadmap, amendments=amendments, policy=policy,
            action=args.action, changeset=args.changeset, stage=args.stage, paths=args.path)
        if result["authorized"] and args.action == "release":
            extra = legacy._run_release_verifiers(ROOT)
            if extra:
                result["authorized"] = False
                result["decision"] = "REJECT"
                result["reasons"].extend(extra)
    except ValueError as exc:
        result = legacy._base_result(args.action, args.changeset, args.stage, args.path)
        result["reasons"].append(str(exc))
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0 if result["authorized"] else 2


if __name__ == "__main__":
    sys.exit(main())
