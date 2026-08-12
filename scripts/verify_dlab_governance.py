#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Callable

ROOT = Path(__file__).resolve().parents[1]

BASELINE_COMMIT = "e3fff973554a2e56b8bd7afdc1132f75f3ec337c"
BASELINE_VERSION = "1.14.1"

FILES = {
    "index": Path("audit/SOURCE_OF_TRUTH_INDEX.json"),
    "roadmap": Path("audit/EVOLUTION_ROADMAP.json"),
    "base_master": Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md"),
    "amendment_v11": Path(
        "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_1_AMENDMENT.md"
    ),
    "amendment_v12": Path(
        "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_2_AMENDMENT.md"
    ),
    "standard": Path("docs/governance/DLAB_VALIDATION_STANDARD.md"),
    "base_requirements": Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json"),
    "requirements_a": Path("audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016A.json"),
    "requirements_b": Path("audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016B.json"),
    "base_invariants": Path("audit/EVOLUTION_INVARIANTS.json"),
    "invariants_a": Path("audit/EVOLUTION_INVARIANTS_AMENDMENT_016A.json"),
    "invariants_b": Path("audit/EVOLUTION_INVARIANTS_AMENDMENT_016B.json"),
    "amendments": Path("audit/EVOLUTION_AMENDMENTS.json"),
    "execution_policy": Path("audit/DLAB_EXECUTION_POLICY.json"),
    "historical": Path("audit/DLAB_HISTORICAL_REVALIDATION_MATRIX.json"),
    "scenarios": Path("audit/DLAB_SCENARIO_CATALOG.json"),
    "deviation_a": Path("docs/records/evolution/DEV-0001.md"),
    "deviation_b": Path("docs/records/evolution/DEV-0002.md"),
    "changeset_a": Path("docs/changesets/016A/CHANGESET.md"),
    "test_status_a": Path("docs/changesets/016A/TEST_STATUS.md"),
    "action_scope_a": Path("docs/changesets/016A/ACTION_SCOPE.json"),
    "changeset_b": Path("docs/changesets/016B/CHANGESET.md"),
    "test_status_b": Path("docs/changesets/016B/TEST_STATUS.md"),
    "action_scope_b": Path("docs/changesets/016B/ACTION_SCOPE.json"),
    "workflow": Path(".github/workflows/evolution-governance.yml"),
    "authorizer": Path("scripts/authorize_evolution_action.py"),
    "verifier": Path("scripts/verify_dlab_governance.py"),
}

EXPECTED_SCHEMAS = {
    "amendments": "neoeng.dcore.evolution-amendments.v1",
    "requirements_a": "neoeng.dcore.evolution-requirements-amendment-016a.v1",
    "requirements_b": "neoeng.dcore.evolution-requirements-amendment-016b.v1",
    "invariants_a": "neoeng.dcore.evolution-invariants-amendment-016a.v1",
    "invariants_b": "neoeng.dcore.evolution-invariants-amendment-016b.v1",
    "execution_policy": "neoeng.dlab.execution-policy.v1",
    "historical": "neoeng.dlab.historical-revalidation-matrix.v1",
    "scenarios": "neoeng.dlab.scenario-catalog.v1",
    "action_scope_a": "neoeng.dcore.changeset-action-scope.v1",
    "action_scope_b": "neoeng.dcore.changeset-action-scope.v1",
}

REQUIRED_PRECEDENCE = [
    str(FILES["amendment_v12"]),
    str(FILES["amendment_v11"]),
    str(FILES["standard"]),
    str(FILES["base_master"]),
    str(FILES["roadmap"]),
    str(FILES["amendments"]),
    str(FILES["base_requirements"]),
    str(FILES["requirements_b"]),
    str(FILES["requirements_a"]),
    str(FILES["base_invariants"]),
    str(FILES["invariants_b"]),
    str(FILES["invariants_a"]),
    str(FILES["execution_policy"]),
    str(FILES["historical"]),
    str(FILES["scenarios"]),
]

REQUIRED_MACHINE_LEDGERS = [
    str(FILES["amendments"]),
    str(FILES["requirements_a"]),
    str(FILES["requirements_b"]),
    str(FILES["invariants_a"]),
    str(FILES["invariants_b"]),
    str(FILES["execution_policy"]),
    str(FILES["historical"]),
    str(FILES["scenarios"]),
]

REQUIRED_VERIFIERS = [
    str(FILES["verifier"]),
    str(FILES["authorizer"]),
]

EXPECTED_REQUIREMENTS_A = {f"EVREQ-{i:03d}" for i in range(55, 72)}
EXPECTED_REQUIREMENTS_B = {"EVREQ-072"}
EXPECTED_INVARIANTS_A = {f"INV-EV-{i:03d}" for i in range(21, 28)}
EXPECTED_INVARIANTS_B = {"INV-EV-028"}
EXPECTED_HISTORY = [f"CS{i:03d}" for i in range(1, 16)]
EXPECTED_CLASSES = [
    "normal",
    "integration",
    "degraded",
    "adversarial",
    "recovery",
    "soak",
    "combinatorial",
    "regression",
]
EXPECTED_TYPES = ["real", "simulated", "hybrid", "physical"]
EXPECTED_DLAB_RULES = [f"DLAB-R{i:03d}" for i in range(1, 31)]

AMENDMENT_STATUSES = {"in_progress", "blocked", "failed", "accepted", "superseded"}
REQ_STATUSES = {"planned", "in_progress", "verified", "blocked", "rejected", "superseded"}

V11_TOKENS = [
    "Versão normativa efetiva: **1.1**",
    "CS016A",
    "D-Lab v2",
    "Historical Assurance Revalidation",
    "Action Authorization Gate",
    "SCN-REGRESSION-001",
    "PRE-CS017",
    "EVREQ-071",
    "INV-EV-027",
]

V12_TOKENS = [
    "Versão normativa efetiva: **1.2**",
    "CS016B",
    "DEV-0002",
    "SCN-REGRESSION-002",
    "EVREQ-072",
    "INV-EV-028",
    "not_started",
    "in_progress",
    "sem relaxar",
]

STANDARD_TOKENS = [
    "NEOENG-DLAB-STANDARD-001",
    "Source-under-test",
    "PASSED",
    "FAILED",
    "BLOCKED",
    "ABORTED",
    "CS001-CS015",
    "git apply --check --whitespace=error-all",
    "PRE-CS017 antes de CS016A aceito",
]

WORKFLOW_COMMANDS = [
    "python3 scripts/authorize_evolution_action.py --self-test",
    "python3 scripts/verify_dlab_governance.py --self-test",
    "python3 scripts/verify_dlab_governance.py",
    "Required evolution amendments gate",
    "prepare_stage_changeset",
    "start_stage",
]


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


def sha256_text(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def is_sha(value: object) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9a-f]{40}", value) is not None


def git_head(root: Path) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), "rev-parse", "HEAD"],
        capture_output=True,
        text=True,
        check=False,
    )
    head = result.stdout.strip()
    if result.returncode != 0 or not is_sha(head):
        raise ValueError("git HEAD is unavailable; repository identity is mandatory")
    return head


def is_ancestor(root: Path, ancestor: str, descendant: str) -> bool:
    return subprocess.run(
        ["git", "-C", str(root), "merge-base", "--is-ancestor", ancestor, descendant],
        capture_output=True,
        check=False,
    ).returncode == 0


def amendment_map(doc: dict[str, Any]) -> dict[str, dict[str, Any]]:
    rows = doc.get("amendments")
    if not isinstance(rows, list):
        return {}
    return {
        str(row.get("changeset")): row
        for row in rows
        if isinstance(row, dict) and isinstance(row.get("changeset"), str)
    }


def validate_evidence_manifest(
    root: Path, rel: str, source_commit: str, changeset: str
) -> list[str]:
    errors: list[str] = []
    try:
        doc = load_json(root / rel)
    except ValueError as exc:
        return [str(exc)]
    if doc.get("schema") != "neoeng.dcore.evolution-amendment-evidence-manifest.v1":
        errors.append(f"{changeset} evidence manifest schema mismatch")
    if doc.get("source_commit") != source_commit:
        errors.append(f"{changeset} evidence manifest source_commit mismatch")
    if doc.get("hash_mode") != "lf-normalized-text":
        errors.append(f"{changeset} evidence manifest hash_mode mismatch")
    rows = doc.get("files")
    if not isinstance(rows, list) or not rows:
        return errors + [f"{changeset} evidence manifest must list evidence files"]
    prefix = f"docs/changesets/{changeset.removeprefix('CS')}/evidence/"
    seen: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            errors.append(f"{changeset} evidence file row must be object")
            continue
        path_rel = row.get("path")
        digest = row.get("sha256")
        if not isinstance(path_rel, str) or not path_rel.startswith(prefix):
            errors.append(f"invalid {changeset} evidence path: {path_rel!r}")
            continue
        if path_rel == rel:
            errors.append(f"{changeset} evidence manifest cannot hash itself")
        if path_rel in seen:
            errors.append(f"duplicate {changeset} evidence path: {path_rel}")
        seen.add(path_rel)
        path = root / path_rel
        if not path.is_file():
            errors.append(f"missing {changeset} evidence file: {path_rel}")
        elif digest != sha256_text(path):
            errors.append(f"{changeset} evidence hash mismatch: {path_rel}")
    return errors


def validate_requirement_doc(
    doc: dict[str, Any],
    *,
    expected_ids: set[str],
    label: str,
    require_verified: bool,
) -> list[str]:
    errors: list[str] = []
    rows = doc.get("requirements")
    ids: set[str] = set()
    if not isinstance(rows, list):
        return [f"{label} requirements must be a list"]
    for row in rows:
        if not isinstance(row, dict):
            errors.append(f"{label} requirement row must be object")
            continue
        rid = row.get("requirement_id")
        if not isinstance(rid, str):
            errors.append(f"{label} requirement id missing")
            continue
        if rid in ids:
            errors.append(f"duplicate {label} requirement: {rid}")
        ids.add(rid)
        if row.get("stage") != "EV-00":
            errors.append(f"{label} requirement not assigned EV-00: {rid}")
        status = row.get("status")
        if status not in REQ_STATUSES:
            errors.append(f"invalid {label} requirement status: {rid}")
        evidence_required = row.get("evidence_required")
        if not isinstance(evidence_required, list) or not evidence_required:
            errors.append(f"{label} requirement lacks evidence_required: {rid}")
        evidence = row.get("evidence")
        if not isinstance(evidence, list):
            errors.append(f"{label} requirement evidence must be list: {rid}")
            evidence = []
        if status == "verified" and not evidence:
            errors.append(f"verified {label} requirement lacks evidence: {rid}")
        if require_verified and status != "verified":
            errors.append(f"{label} requirement must be verified: {rid}")
    if ids != expected_ids:
        errors.append(
            f"{label} requirements must be exactly " + ", ".join(sorted(expected_ids))
        )
    return errors


def validate_invariant_doc(
    doc: dict[str, Any], *, expected_ids: set[str], label: str
) -> list[str]:
    errors: list[str] = []
    rows = doc.get("invariants")
    ids: set[str] = set()
    if not isinstance(rows, list):
        return [f"{label} invariants must be a list"]
    for row in rows:
        if not isinstance(row, dict):
            errors.append(f"{label} invariant row must be object")
            continue
        iid = row.get("invariant_id")
        ids.add(str(iid))
        if row.get("status") != "active":
            errors.append(f"{label} invariant not active: {iid}")
        if not row.get("statement") or not row.get("enforcement"):
            errors.append(f"{label} invariant incomplete: {iid}")
    if ids != expected_ids:
        errors.append(
            f"{label} invariants must be exactly " + ", ".join(sorted(expected_ids))
        )
    return errors


def validate_scope(scope: dict[str, Any], changeset: str) -> list[str]:
    errors: list[str] = []
    if scope.get("changeset") != changeset:
        errors.append(f"{changeset} ACTION_SCOPE identity mismatch")
    if scope.get("runtime_change_authorized") is not False:
        errors.append(f"{changeset} must not authorize runtime changes")
    allowed = scope.get("allowed_paths")
    forbidden = scope.get("forbidden_paths")
    if not isinstance(allowed, list) or not isinstance(forbidden, list):
        return errors + [f"{changeset} ACTION_SCOPE path lists invalid"]
    for protected in ("src/**", "include/**"):
        if protected not in forbidden:
            errors.append(f"{changeset} forbidden path missing: {protected}")
    for path in allowed:
        if path.startswith("src/") or path.startswith("include/"):
            errors.append(f"runtime path allowed by {changeset}: {path}")
    return errors


def validate_amendment(
    root: Path,
    *,
    row: dict[str, Any],
    changeset: str,
    amendment_document: Path,
    deviation_record: Path,
    test_status: str,
    check_git: bool,
) -> list[str]:
    errors: list[str] = []
    if row.get("changeset") != changeset:
        errors.append(f"{changeset} amendment identity mismatch")
    if row.get("required_before_stage") != "EV-00":
        errors.append(f"{changeset} must be required_before_stage EV-00")
    if row.get("status") not in AMENDMENT_STATUSES:
        errors.append(f"invalid {changeset} status")
    if row.get("amendment_document") != str(amendment_document):
        errors.append(f"{changeset} amendment_document mismatch")
    if row.get("deviation_record") != str(deviation_record):
        errors.append(f"{changeset} deviation_record mismatch")

    status = row.get("status")
    if status == "in_progress":
        if row.get("accepted_source_commit") is not None:
            errors.append(f"in-progress {changeset} cannot have accepted_source_commit")
        if row.get("evidence_manifest") is not None:
            errors.append(f"in-progress {changeset} cannot have evidence_manifest")
        if "State: in_progress" not in test_status:
            errors.append(f"{changeset} TEST_STATUS must be in_progress")
    elif status == "accepted":
        source = row.get("accepted_source_commit")
        manifest = row.get("evidence_manifest")
        if not is_sha(source):
            errors.append(f"accepted {changeset} lacks valid accepted_source_commit")
        if not isinstance(manifest, str) or not manifest:
            errors.append(f"accepted {changeset} lacks evidence_manifest")
        elif is_sha(source):
            errors.extend(validate_evidence_manifest(root, manifest, source, changeset))
        if "State: accepted" not in test_status:
            errors.append(f"accepted {changeset} not reflected in TEST_STATUS")
        if check_git and is_sha(source):
            head = git_head(root)
            if not is_ancestor(root, source, head):
                errors.append(f"accepted {changeset} source commit is not ancestor of HEAD")
    return errors


def validate_repository(
    root: Path, *, check_git: bool = True, check_authorizer: bool = True
) -> list[str]:
    errors: list[str] = []
    docs: dict[str, Any] = {}
    text_keys = {
        "base_master",
        "amendment_v11",
        "amendment_v12",
        "standard",
        "deviation_a",
        "deviation_b",
        "changeset_a",
        "test_status_a",
        "changeset_b",
        "test_status_b",
        "workflow",
        "authorizer",
        "verifier",
    }

    for key, rel in FILES.items():
        path = root / rel
        try:
            docs[key] = (
                path.read_text(encoding="utf-8")
                if key in text_keys
                else load_json(path)
            )
        except (FileNotFoundError, ValueError) as exc:
            errors.append(str(exc))
    if errors:
        return errors

    index = docs["index"]
    roadmap = docs["roadmap"]
    amendments = docs["amendments"]
    historical = docs["historical"]
    scenarios = docs["scenarios"]
    policy = docs["execution_policy"]
    workflow = docs["workflow"]

    for key, schema in EXPECTED_SCHEMAS.items():
        if docs[key].get("schema") != schema:
            errors.append(f"{key} schema mismatch")
    for key in (
        "amendments",
        "requirements_a",
        "requirements_b",
        "invariants_a",
        "invariants_b",
    ):
        if docs[key].get("project_version") != BASELINE_VERSION:
            errors.append(f"{key} project_version mismatch")

    if index.get("schema") != "neoeng.dcore.source-of-truth-index.v1":
        errors.append("source-of-truth index schema mismatch")

    precedence = index.get("precedence")
    if not isinstance(precedence, list):
        errors.append("precedence must be a list")
    else:
        if len(precedence) != len(set(precedence)):
            errors.append("precedence contains duplicates")
        positions: list[int] = []
        for rel in REQUIRED_PRECEDENCE:
            if rel not in precedence:
                errors.append(f"missing D-Lab precedence entry: {rel}")
            else:
                positions.append(precedence.index(rel))
            if not (root / rel).exists():
                errors.append(f"D-Lab precedence path missing: {rel}")
        if positions and positions != sorted(positions):
            errors.append("D-Lab/evolution precedence is out of order")

    ledgers = index.get("machine_ledgers")
    if not isinstance(ledgers, list):
        errors.append("machine_ledgers must be a list")
    else:
        for rel in REQUIRED_MACHINE_LEDGERS:
            if rel not in ledgers:
                errors.append(f"D-Lab machine ledger not registered: {rel}")

    verifiers = index.get("verifiers")
    if not isinstance(verifiers, list):
        errors.append("verifiers must be a list")
    else:
        for rel in REQUIRED_VERIFIERS:
            if rel not in verifiers:
                errors.append(f"D-Lab verifier not registered: {rel}")

    active = index.get("active_evolution_program")
    expected_active = {
        "program_id": "POST_1_14_1",
        "baseline_commit": BASELINE_COMMIT,
        "master_plan": str(FILES["base_master"]),
        "effective_master_plan_amendment": str(FILES["amendment_v12"]),
        "previous_master_plan_amendment": str(FILES["amendment_v11"]),
        "amendments": str(FILES["amendments"]),
        "requirements_amendment": str(FILES["requirements_a"]),
        "requirements_amendment_016b": str(FILES["requirements_b"]),
        "invariants_amendment": str(FILES["invariants_a"]),
        "invariants_amendment_016b": str(FILES["invariants_b"]),
        "dlab_standard": str(FILES["standard"]),
        "dlab_execution_policy": str(FILES["execution_policy"]),
        "dlab_historical_matrix": str(FILES["historical"]),
        "dlab_scenario_catalog": str(FILES["scenarios"]),
        "dlab_verifier": str(FILES["verifier"]),
        "action_authorizer": str(FILES["authorizer"]),
    }
    if not isinstance(active, dict):
        errors.append("active_evolution_program missing")
    else:
        for key, expected in expected_active.items():
            if active.get(key) != expected:
                errors.append(f"active_evolution_program {key} mismatch")

    for token in V11_TOKENS:
        if token not in docs["amendment_v11"]:
            errors.append(f"v1.1 amendment missing token: {token}")
    for token in V12_TOKENS:
        if token not in docs["amendment_v12"]:
            errors.append(f"v1.2 amendment missing token: {token}")

    rules = re.findall(
        r"^### (DLAB-R\d{3})\b", docs["standard"], flags=re.MULTILINE
    )
    if rules != EXPECTED_DLAB_RULES:
        errors.append("D-Lab Standard must contain DLAB-R001..DLAB-R030 exactly once")
    for token in STANDARD_TOKENS:
        if token not in docs["standard"]:
            errors.append(f"D-Lab Standard missing token: {token}")

    if "DEV-0001" not in docs["deviation_a"] or "Action Authorization Gate" not in docs["deviation_a"]:
        errors.append("DEV-0001 does not record the original process root cause")
    if (
        "DEV-0002" not in docs["deviation_b"]
        or "31594048822" not in docs["deviation_b"]
        or "bfafa432ad4dc7c402753293da080fc6d920c8ce" not in docs["deviation_b"]
    ):
        errors.append("DEV-0002 does not preserve the CS017 lifecycle self-test failure")

    if "ChangeSet 016A" not in docs["changeset_a"]:
        errors.append("CS016A ChangeSet identity missing")
    if BASELINE_COMMIT not in docs["changeset_a"]:
        errors.append("CS016A does not bind protected v1.14.1 baseline")
    if "ChangeSet 016B" not in docs["changeset_b"] or "State: in_progress" not in docs["changeset_b"]:
        errors.append("CS016B ChangeSet identity/state missing")
    if BASELINE_COMMIT not in docs["changeset_b"]:
        errors.append("CS016B does not bind protected v1.14.1 baseline")

    base_req_rows = docs["base_requirements"].get("requirements")
    base_req_ids = {
        row.get("requirement_id")
        for row in base_req_rows
        if isinstance(row, dict)
    } if isinstance(base_req_rows, list) else set()
    for rid in ("EVREQ-001", "EVREQ-002", "EVREQ-003", "EVREQ-004"):
        if rid not in base_req_ids:
            errors.append(f"base EV-00 requirement missing: {rid}")

    base_inv_rows = docs["base_invariants"].get("invariants")
    base_inv_ids = {
        row.get("invariant_id")
        for row in base_inv_rows
        if isinstance(row, dict)
    } if isinstance(base_inv_rows, list) else set()
    if base_inv_ids != {f"INV-EV-{i:03d}" for i in range(1, 21)}:
        errors.append("base invariant set changed unexpectedly")

    amend_rows = amendments.get("amendments")
    expected_amendment_order = ["CS016A", "CS016B"]
    ids = [
        row.get("changeset") for row in amend_rows if isinstance(row, dict)
    ] if isinstance(amend_rows, list) else []
    if ids != expected_amendment_order:
        errors.append("EVOLUTION_AMENDMENTS must contain CS016A then CS016B exactly once")
        amend_by_id: dict[str, dict[str, Any]] = {}
    else:
        amend_by_id = amendment_map(amendments)

    amend_a = amend_by_id.get("CS016A", {})
    amend_b = amend_by_id.get("CS016B", {})
    errors.extend(
        validate_amendment(
            root,
            row=amend_a,
            changeset="CS016A",
            amendment_document=FILES["amendment_v11"],
            deviation_record=FILES["deviation_a"],
            test_status=docs["test_status_a"],
            check_git=check_git,
        )
    )
    errors.extend(
        validate_amendment(
            root,
            row=amend_b,
            changeset="CS016B",
            amendment_document=FILES["amendment_v12"],
            deviation_record=FILES["deviation_b"],
            test_status=docs["test_status_b"],
            check_git=check_git,
        )
    )
    if amend_a.get("status") != "accepted":
        errors.append("CS016A must remain accepted while CS016B exists")

    stages = roadmap.get("stages")
    stage0 = (
        next(
            (
                row
                for row in stages
                if isinstance(row, dict) and row.get("stage_id") == "EV-00"
            ),
            None,
        )
        if isinstance(stages, list)
        else None
    )
    if roadmap.get("current_stage") != "EV-00" or not isinstance(stage0, dict):
        errors.append("EV-00 must remain current stage during CS016B")
    if amend_b.get("status") == "in_progress" and stage0 and stage0.get("status") != "not_started":
        errors.append("EV-00 must remain not_started while CS016B is in_progress")

    errors.extend(
        validate_requirement_doc(
            docs["requirements_a"],
            expected_ids=EXPECTED_REQUIREMENTS_A,
            label="CS016A supplemental",
            require_verified=bool(stage0 and stage0.get("status") == "accepted"),
        )
    )
    errors.extend(
        validate_requirement_doc(
            docs["requirements_b"],
            expected_ids=EXPECTED_REQUIREMENTS_B,
            label="CS016B supplemental",
            require_verified=amend_b.get("status") == "accepted",
        )
    )
    errors.extend(
        validate_invariant_doc(
            docs["invariants_a"],
            expected_ids=EXPECTED_INVARIANTS_A,
            label="CS016A supplemental",
        )
    )
    errors.extend(
        validate_invariant_doc(
            docs["invariants_b"],
            expected_ids=EXPECTED_INVARIANTS_B,
            label="CS016B supplemental",
        )
    )

    if historical.get("baseline_commit") != BASELINE_COMMIT:
        errors.append("historical matrix baseline commit mismatch")
    history_rows = historical.get("entries")
    history_ids = [
        row.get("changeset") for row in history_rows if isinstance(row, dict)
    ] if isinstance(history_rows, list) else []
    if history_ids != EXPECTED_HISTORY:
        errors.append("historical matrix must contain CS001..CS015 exactly once and in order")
    allowed_assessment = set(historical.get("assessment_statuses", []))
    allowed_risk = set(historical.get("risk_classes", []))
    allowed_rerun = set(historical.get("rerun_statuses", []))
    for row in history_rows if isinstance(history_rows, list) else []:
        if not isinstance(row, dict):
            continue
        if row.get("assessment_status") not in allowed_assessment:
            errors.append(f"invalid historical assessment: {row.get('changeset')}")
        if row.get("risk_class") not in allowed_risk:
            errors.append(f"invalid historical risk: {row.get('changeset')}")
        if row.get("rerun_status") not in allowed_rerun:
            errors.append(f"invalid historical rerun status: {row.get('changeset')}")
        if not isinstance(row.get("findings"), list):
            errors.append(f"historical findings must be list: {row.get('changeset')}")

    if scenarios.get("classes") != EXPECTED_CLASSES:
        errors.append("scenario classes mismatch")
    if scenarios.get("types") != EXPECTED_TYPES:
        errors.append("scenario types mismatch")
    scen_rows = scenarios.get("scenarios")
    seen_scenarios: set[str] = set()
    classes_seen: set[str] = set()
    scenario_by_id: dict[str, dict[str, Any]] = {}
    if not isinstance(scen_rows, list):
        errors.append("scenario catalog scenarios must be list")
        scen_rows = []
    for row in scen_rows:
        if not isinstance(row, dict):
            errors.append("scenario row must be object")
            continue
        sid = row.get("scenario_id")
        if not isinstance(sid, str) or not sid:
            errors.append("scenario_id missing")
            continue
        if sid in seen_scenarios:
            errors.append(f"duplicate scenario_id: {sid}")
        seen_scenarios.add(sid)
        scenario_by_id[sid] = row
        cls = row.get("class")
        typ = row.get("type")
        if cls not in EXPECTED_CLASSES:
            errors.append(f"invalid scenario class: {sid}")
        else:
            classes_seen.add(cls)
        if typ not in EXPECTED_TYPES:
            errors.append(f"invalid scenario type: {sid}")
        if not isinstance(row.get("oracle"), str) or not row.get("oracle").strip():
            errors.append(f"scenario lacks oracle: {sid}")
        if row.get("randomized") is True:
            if row.get("seed") is None or not row.get("generator_version"):
                errors.append(f"randomized scenario lacks seed/generator: {sid}")
        claim_scope = str(row.get("claim_scope", "")).lower()
        if typ != "physical" and "power-loss físico qualificado" in claim_scope:
            errors.append(f"non-physical scenario promotes physical claim: {sid}")
    if classes_seen != set(EXPECTED_CLASSES):
        errors.append("scenario catalog must include every mandatory class")
    for sid in ("SCN-REGRESSION-001", "SCN-REGRESSION-002"):
        if sid not in seen_scenarios:
            errors.append(f"required regression scenario missing: {sid}")
    regression2 = scenario_by_id.get("SCN-REGRESSION-002")
    if amend_b.get("status") == "accepted" and regression2 and regression2.get("status") != "passed":
        errors.append("CS016B accepted before SCN-REGRESSION-002 passed")

    if policy.get("fail_closed") is not True:
        errors.append("D-Lab execution policy must be fail_closed")
    anti = policy.get("anti_skip_regression")
    expected_anti = {
        "scenario_id": "SCN-REGRESSION-001",
        "requested_action": "prepare_stage_changeset",
        "stage": "EV-00",
        "changeset": "CS017",
        "expected_while_cs016a_not_accepted": "REJECT",
    }
    if anti != expected_anti:
        errors.append("anti-skip execution policy mismatch")
    lifecycle = policy.get("lifecycle_selftest_regression")
    expected_lifecycle = {
        "scenario_id": "SCN-REGRESSION-002",
        "not_started_prepare_expected": "AUTHORIZED",
        "in_progress_prepare_expected": "REJECT",
        "in_progress_start_expected": "AUTHORIZED",
        "fixture_state_source": "explicit",
    }
    if lifecycle != expected_lifecycle:
        errors.append("lifecycle self-test execution policy mismatch")
    actions = policy.get("action_types")
    if not isinstance(actions, list) or "preflight" not in actions:
        errors.append("execution policy must include preflight action")

    errors.extend(validate_scope(docs["action_scope_a"], "CS016A"))
    errors.extend(validate_scope(docs["action_scope_b"], "CS016B"))

    for command in WORKFLOW_COMMANDS:
        if command not in workflow:
            errors.append(f"workflow missing D-Lab command: {command}")

    if stage0 and stage0.get("status") == "accepted":
        if any(
            isinstance(row, dict) and row.get("assessment_status") == "not_assessed"
            for row in history_rows if isinstance(history_rows, list)
        ):
            errors.append("EV-00 accepted with historical entries not_assessed")
        for row in history_rows if isinstance(history_rows, list) else []:
            if not isinstance(row, dict):
                continue
            if (
                row.get("risk_class") in {"critical", "high"}
                and row.get("reproducibility") in {"reproducible", "reproduced"}
                and row.get("rerun_status") != "passed"
            ):
                errors.append(
                    f"EV-00 accepted without required historical rerun: {row.get('changeset')}"
                )

    if check_authorizer:
        auth = root / FILES["authorizer"]
        b_status = amend_b.get("status")
        if b_status == "in_progress":
            good = subprocess.run(
                [
                    sys.executable,
                    str(auth),
                    "--action",
                    "governance_amendment",
                    "--changeset",
                    "CS016B",
                    "--path",
                    "scripts/authorize_evolution_action.py",
                ],
                cwd=root,
                capture_output=True,
                text=True,
                check=False,
            )
            if good.returncode != 0:
                errors.append(
                    "action authorizer rejected current CS016B governance work: "
                    + good.stdout.strip()
                    + good.stderr.strip()
                )
            bad = subprocess.run(
                [
                    sys.executable,
                    str(auth),
                    "--action",
                    "prepare_stage_changeset",
                    "--stage",
                    "EV-00",
                    "--changeset",
                    "CS017",
                ],
                cwd=root,
                capture_output=True,
                text=True,
                check=False,
            )
            if bad.returncode == 0:
                errors.append("action authorizer accepted CS017 before CS016B acceptance")
            if "CS016B" not in (bad.stdout + bad.stderr):
                errors.append("CS017 rejection did not expose CS016B blocker")
        elif b_status == "accepted" and stage0:
            status = stage0.get("status")
            if status == "not_started":
                ready = subprocess.run(
                    [
                        sys.executable,
                        str(auth),
                        "--action",
                        "prepare_stage_changeset",
                        "--stage",
                        "EV-00",
                        "--changeset",
                        "CS017",
                    ],
                    cwd=root,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                if ready.returncode != 0:
                    errors.append(
                        "action authorizer rejected CS017 after all amendments accepted: "
                        + ready.stdout.strip()
                        + ready.stderr.strip()
                    )
            elif status == "in_progress":
                repeat = subprocess.run(
                    [
                        sys.executable,
                        str(auth),
                        "--action",
                        "prepare_stage_changeset",
                        "--stage",
                        "EV-00",
                        "--changeset",
                        "CS017",
                    ],
                    cwd=root,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                if repeat.returncode == 0:
                    errors.append("authorizer allowed CS017 re-preparation while EV-00 in_progress")
                start = subprocess.run(
                    [
                        sys.executable,
                        str(auth),
                        "--action",
                        "start_stage",
                        "--stage",
                        "EV-00",
                        "--changeset",
                        "CS017",
                    ],
                    cwd=root,
                    capture_output=True,
                    text=True,
                    check=False,
                )
                if start.returncode != 0:
                    errors.append(
                        "authorizer rejected start_stage while EV-00 in_progress: "
                        + start.stdout.strip()
                        + start.stderr.strip()
                    )

    if check_git:
        head = git_head(root)
        if not is_ancestor(root, BASELINE_COMMIT, head):
            errors.append("historical v1.14.1 baseline is not ancestor of HEAD")

    return errors


def copy_fixture(dst: Path) -> None:
    for rel in FILES.values():
        src = ROOT / rel
        target = dst / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)
    for changeset in ("016A", "016B"):
        evidence = ROOT / f"docs/changesets/{changeset}/evidence"
        if evidence.is_dir():
            shutil.copytree(
                evidence,
                dst / f"docs/changesets/{changeset}/evidence",
                dirs_exist_ok=True,
            )


def mutate_json(path: Path, mutator: Callable[[dict[str, Any]], None]) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    mutator(data)
    path.write_text(
        json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )


def self_test() -> list[str]:
    failures: list[str] = []

    with tempfile.TemporaryDirectory() as tmp:
        case = Path(tmp)
        copy_fixture(case)
        initial = validate_repository(
            case, check_git=False, check_authorizer=False
        )
        if initial:
            failures.append(
                "valid D-Lab governance candidate rejected: " + "; ".join(initial)
            )

    def expect_reject(name: str, mutator: Callable[[Path], None]) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            case = Path(tmp)
            copy_fixture(case)
            mutator(case)
            if not validate_repository(
                case, check_git=False, check_authorizer=False
            ):
                failures.append(f"negative case accepted: {name}")

    expect_reject(
        "missing historical ChangeSet",
        lambda case: mutate_json(
            case / FILES["historical"], lambda d: d["entries"].pop()
        ),
    )
    expect_reject(
        "scenario without oracle",
        lambda case: mutate_json(
            case / FILES["scenarios"],
            lambda d: d["scenarios"][0].update({"oracle": ""}),
        ),
    )
    expect_reject(
        "randomized scenario without seed",
        lambda case: mutate_json(
            case / FILES["scenarios"],
            lambda d: d["scenarios"][0].update(
                {"randomized": True, "seed": None, "generator_version": None}
            ),
        ),
    )
    expect_reject(
        "missing CS016A invariant",
        lambda case: mutate_json(
            case / FILES["invariants_a"], lambda d: d["invariants"].pop()
        ),
    )
    expect_reject(
        "missing CS016B invariant",
        lambda case: mutate_json(
            case / FILES["invariants_b"], lambda d: d["invariants"].pop()
        ),
    )
    expect_reject(
        "authorizer unregistered",
        lambda case: mutate_json(
            case / FILES["index"],
            lambda d: d.update(
                {
                    "verifiers": [
                        item
                        for item in d.get("verifiers", [])
                        if item != str(FILES["authorizer"])
                    ]
                }
            ),
        ),
    )
    expect_reject(
        "runtime path allowed by CS016B",
        lambda case: mutate_json(
            case / FILES["action_scope_b"],
            lambda d: d["allowed_paths"].append("src/**"),
        ),
    )
    expect_reject(
        "anti-skip policy weakened",
        lambda case: mutate_json(
            case / FILES["execution_policy"],
            lambda d: d["anti_skip_regression"].update(
                {"expected_while_cs016a_not_accepted": "AUTHORIZED"}
            ),
        ),
    )
    expect_reject(
        "lifecycle regression removed",
        lambda case: mutate_json(
            case / FILES["scenarios"],
            lambda d: d.update(
                {
                    "scenarios": [
                        row
                        for row in d["scenarios"]
                        if row.get("scenario_id") != "SCN-REGRESSION-002"
                    ]
                }
            ),
        ),
    )
    expect_reject(
        "CS016B missing from amendment ledger",
        lambda case: mutate_json(
            case / FILES["amendments"], lambda d: d["amendments"].pop()
        ),
    )
    expect_reject(
        "CS016B accepted without evidence",
        lambda case: (
            mutate_json(
                case / FILES["amendments"],
                lambda d: d["amendments"][1].update(
                    {
                        "status": "accepted",
                        "accepted_source_commit": "a" * 40,
                        "evidence_manifest": None,
                    }
                ),
            ),
            (case / FILES["test_status_b"]).write_text(
                "State: accepted\n", encoding="utf-8"
            ),
        ),
    )

    # A synthetic accepted CS016B fixture must verify, and tampering must fail.
    with tempfile.TemporaryDirectory() as tmp:
        case = Path(tmp)
        copy_fixture(case)
        ev_dir = case / "docs/changesets/016B/evidence"
        ev_dir.mkdir(parents=True, exist_ok=True)
        evidence_rel = "docs/changesets/016B/evidence/VALIDATION.json"
        evidence_path = case / evidence_rel
        evidence_path.write_text(
            json.dumps(
                {
                    "schema": "neoeng.dcore.cs016b-validation.v1",
                    "source_commit": "a" * 40,
                    "conclusion": "success",
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        manifest_rel = "docs/changesets/016B/evidence/EVIDENCE_MANIFEST.json"
        (case / manifest_rel).write_text(
            json.dumps(
                {
                    "schema": "neoeng.dcore.evolution-amendment-evidence-manifest.v1",
                    "source_commit": "a" * 40,
                    "hash_mode": "lf-normalized-text",
                    "files": [
                        {
                            "path": evidence_rel,
                            "sha256": sha256_text(evidence_path),
                        }
                    ],
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        mutate_json(
            case / FILES["amendments"],
            lambda d: d["amendments"][1].update(
                {
                    "status": "accepted",
                    "accepted_source_commit": "a" * 40,
                    "evidence_manifest": manifest_rel,
                }
            ),
        )
        mutate_json(
            case / FILES["requirements_b"],
            lambda d: d["requirements"][0].update(
                {
                    "status": "verified",
                    "evidence": ["fixture://SCN-REGRESSION-002"],
                }
            ),
        )
        mutate_json(
            case / FILES["scenarios"],
            lambda d: next(
                row
                for row in d["scenarios"]
                if row["scenario_id"] == "SCN-REGRESSION-002"
            ).update(
                {"status": "passed", "evidence": ["fixture://authorizer-self-test"]}
            ),
        )
        (case / FILES["test_status_b"]).write_text(
            "State: accepted\n", encoding="utf-8"
        )
        accepted_errors = validate_repository(
            case, check_git=False, check_authorizer=False
        )
        if accepted_errors:
            failures.append(
                "valid accepted CS016B fixture rejected: "
                + "; ".join(accepted_errors)
            )
        evidence_path.write_text('{"tampered":true}\n', encoding="utf-8")
        tamper_errors = validate_repository(
            case, check_git=False, check_authorizer=False
        )
        if not any("CS016B evidence hash mismatch" in e for e in tamper_errors):
            failures.append("tampered CS016B evidence was not rejected")

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

    errors = validate_repository(ROOT, check_git=True, check_authorizer=True)
    if errors:
        print("D-LAB GOVERNANCE VERIFICATION: REJECT")
        for item in errors:
            print(f"- {item}")
        return 1
    print("D-LAB GOVERNANCE VERIFICATION: ACCEPT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
