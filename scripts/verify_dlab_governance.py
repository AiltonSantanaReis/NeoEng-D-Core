#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
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
    "base_requirements": Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json"),
    "base_invariants": Path("audit/EVOLUTION_INVARIANTS.json"),
    "amendments": Path("audit/EVOLUTION_AMENDMENTS.json"),
    "requirements": Path("audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016A.json"),
    "invariants": Path("audit/EVOLUTION_INVARIANTS_AMENDMENT_016A.json"),
    "execution_policy": Path("audit/DLAB_EXECUTION_POLICY.json"),
    "historical": Path("audit/DLAB_HISTORICAL_REVALIDATION_MATRIX.json"),
    "scenarios": Path("audit/DLAB_SCENARIO_CATALOG.json"),
    "amendment_doc": Path(
        "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_1_AMENDMENT.md"
    ),
    "standard": Path("docs/governance/DLAB_VALIDATION_STANDARD.md"),
    "deviation": Path("docs/records/evolution/DEV-0001.md"),
    "changeset": Path("docs/changesets/016A/CHANGESET.md"),
    "test_status": Path("docs/changesets/016A/TEST_STATUS.md"),
    "action_scope": Path("docs/changesets/016A/ACTION_SCOPE.json"),
    "workflow": Path(".github/workflows/evolution-governance.yml"),
    "authorizer": Path("scripts/authorize_evolution_action.py"),
    "verifier": Path("scripts/verify_dlab_governance.py"),
}

EXPECTED_SCHEMAS = {
    "amendments": "neoeng.dcore.evolution-amendments.v1",
    "requirements": "neoeng.dcore.evolution-requirements-amendment-016a.v1",
    "invariants": "neoeng.dcore.evolution-invariants-amendment-016a.v1",
    "execution_policy": "neoeng.dlab.execution-policy.v1",
    "historical": "neoeng.dlab.historical-revalidation-matrix.v1",
    "scenarios": "neoeng.dlab.scenario-catalog.v1",
    "action_scope": "neoeng.dcore.changeset-action-scope.v1",
}

REQUIRED_PRECEDENCE = [
    str(FILES["amendment_doc"]),
    str(FILES["standard"]),
    "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md",
    "audit/EVOLUTION_ROADMAP.json",
    str(FILES["amendments"]),
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    str(FILES["requirements"]),
    "audit/EVOLUTION_INVARIANTS.json",
    str(FILES["invariants"]),
    str(FILES["execution_policy"]),
    str(FILES["historical"]),
    str(FILES["scenarios"]),
]

REQUIRED_MACHINE_LEDGERS = [
    str(FILES["amendments"]),
    str(FILES["requirements"]),
    str(FILES["invariants"]),
    str(FILES["execution_policy"]),
    str(FILES["historical"]),
    str(FILES["scenarios"]),
]

REQUIRED_VERIFIERS = [
    str(FILES["verifier"]),
    str(FILES["authorizer"]),
]

EXPECTED_REQUIREMENTS = {f"EVREQ-{i:03d}" for i in range(55, 72)}
EXPECTED_INVARIANTS = {f"INV-EV-{i:03d}" for i in range(21, 28)}
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

AMENDMENT_TOKENS = [
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
    "CS016A authorization-state gate",
    "PRE-CS017 correctly rejected",
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


def validate_evidence_manifest(root: Path, rel: str, source_commit: str) -> list[str]:
    errors: list[str] = []
    try:
        doc = load_json(root / rel)
    except ValueError as exc:
        return [str(exc)]
    if doc.get("schema") != "neoeng.dcore.evolution-amendment-evidence-manifest.v1":
        errors.append("CS016A evidence manifest schema mismatch")
    if doc.get("source_commit") != source_commit:
        errors.append("CS016A evidence manifest source_commit mismatch")
    if doc.get("hash_mode") != "lf-normalized-text":
        errors.append("CS016A evidence manifest hash_mode mismatch")
    rows = doc.get("files")
    if not isinstance(rows, list) or not rows:
        return errors + ["CS016A evidence manifest must list evidence files"]
    seen: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            errors.append("CS016A evidence file row must be object")
            continue
        path_rel = row.get("path")
        digest = row.get("sha256")
        if not isinstance(path_rel, str) or not path_rel.startswith(
            "docs/changesets/016A/evidence/"
        ):
            errors.append(f"invalid CS016A evidence path: {path_rel!r}")
            continue
        if path_rel == rel:
            errors.append("CS016A evidence manifest cannot hash itself")
        if path_rel in seen:
            errors.append(f"duplicate CS016A evidence path: {path_rel}")
        seen.add(path_rel)
        path = root / path_rel
        if not path.is_file():
            errors.append(f"missing CS016A evidence file: {path_rel}")
        elif digest != sha256_text(path):
            errors.append(f"CS016A evidence hash mismatch: {path_rel}")
    return errors


def validate_repository(
    root: Path, *, check_git: bool = True, check_authorizer: bool = True
) -> list[str]:
    errors: list[str] = []
    docs: dict[str, Any] = {}

    text_keys = {
        "amendment_doc",
        "standard",
        "base_master",
        "deviation",
        "changeset",
        "test_status",
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
    req_doc = docs["requirements"]
    inv_doc = docs["invariants"]
    historical = docs["historical"]
    scenarios = docs["scenarios"]
    policy = docs["execution_policy"]
    scope = docs["action_scope"]
    amendment_doc = docs["amendment_doc"]
    standard = docs["standard"]
    test_status = docs["test_status"]
    workflow = docs["workflow"]

    for key, schema in EXPECTED_SCHEMAS.items():
        if docs[key].get("schema") != schema:
            errors.append(f"{key} schema mismatch")

    for key in ("amendments", "requirements", "invariants"):
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
        "effective_master_plan_amendment": str(FILES["amendment_doc"]),
        "amendments": str(FILES["amendments"]),
        "requirements_amendment": str(FILES["requirements"]),
        "invariants_amendment": str(FILES["invariants"]),
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

    for token in AMENDMENT_TOKENS:
        if token not in amendment_doc:
            errors.append(f"amendment document missing token: {token}")

    rules = re.findall(r"^### (DLAB-R\d{3})\b", standard, flags=re.MULTILINE)
    if rules != EXPECTED_DLAB_RULES:
        errors.append("D-Lab Standard must contain DLAB-R001..DLAB-R030 exactly once")
    for token in STANDARD_TOKENS:
        if token not in standard:
            errors.append(f"D-Lab Standard missing token: {token}")

    if "DEV-0001" not in docs["deviation"] or "Action Authorization Gate" not in docs["deviation"]:
        errors.append("DEV-0001 does not record the process root cause")

    if "ChangeSet 016A" not in docs["changeset"] or "State: in_progress" not in docs["changeset"]:
        errors.append("CS016A ChangeSet identity/state missing")
    if BASELINE_COMMIT not in docs["changeset"]:
        errors.append("CS016A does not bind protected v1.14.1 baseline")

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

    rows = amendments.get("amendments")
    if not isinstance(rows, list) or len(rows) != 1 or not isinstance(rows[0], dict):
        errors.append("EVOLUTION_AMENDMENTS must contain exactly CS016A")
        amend = {}
    else:
        amend = rows[0]
        if amend.get("changeset") != "CS016A":
            errors.append("amendment ledger changeset must be CS016A")
        if amend.get("required_before_stage") != "EV-00":
            errors.append("CS016A must be required_before_stage EV-00")
        if amend.get("status") not in AMENDMENT_STATUSES:
            errors.append("invalid CS016A status")
        if amend.get("amendment_document") != str(FILES["amendment_doc"]):
            errors.append("CS016A amendment_document mismatch")
        if amend.get("deviation_record") != str(FILES["deviation"]):
            errors.append("CS016A deviation_record mismatch")

    stages = roadmap.get("stages")
    stage0 = (
        next(
            (row for row in stages if isinstance(row, dict) and row.get("stage_id") == "EV-00"),
            None,
        )
        if isinstance(stages, list)
        else None
    )
    if roadmap.get("current_stage") != "EV-00" or not isinstance(stage0, dict):
        errors.append("EV-00 must remain current stage during CS016A")
    amendment_status = amend.get("status") if isinstance(amend, dict) else None
    if amendment_status == "in_progress":
        if stage0 and stage0.get("status") != "not_started":
            errors.append("EV-00 must remain not_started while CS016A is in_progress")
        if amend.get("accepted_source_commit") is not None:
            errors.append("in-progress CS016A cannot have accepted_source_commit")
        if amend.get("evidence_manifest") is not None:
            errors.append("in-progress CS016A cannot have evidence_manifest")
        if "State: in_progress" not in test_status:
            errors.append("CS016A TEST_STATUS must be in_progress")
    elif amendment_status == "accepted":
        source = amend.get("accepted_source_commit")
        manifest = amend.get("evidence_manifest")
        if not is_sha(source):
            errors.append("accepted CS016A lacks valid accepted_source_commit")
        if not isinstance(manifest, str) or not manifest:
            errors.append("accepted CS016A lacks evidence_manifest")
        elif is_sha(source):
            errors.extend(validate_evidence_manifest(root, manifest, source))
        if "State: accepted" not in test_status:
            errors.append("accepted CS016A not reflected in TEST_STATUS")
        if check_git and is_sha(source):
            head = git_head(root)
            if not is_ancestor(root, source, head):
                errors.append("accepted CS016A source commit is not ancestor of HEAD")

    req_rows = req_doc.get("requirements")
    req_ids: set[str] = set()
    if not isinstance(req_rows, list):
        errors.append("supplemental requirements must be a list")
        req_rows = []
    for row in req_rows:
        if not isinstance(row, dict):
            errors.append("supplemental requirement row must be object")
            continue
        rid = row.get("requirement_id")
        if rid in req_ids:
            errors.append(f"duplicate supplemental requirement: {rid}")
        req_ids.add(str(rid))
        if row.get("stage") != "EV-00":
            errors.append(f"supplemental requirement not assigned EV-00: {rid}")
        if row.get("status") not in REQ_STATUSES:
            errors.append(f"invalid supplemental requirement status: {rid}")
        if not isinstance(row.get("evidence_required"), list) or not row.get("evidence_required"):
            errors.append(f"supplemental requirement lacks evidence_required: {rid}")
        evidence = row.get("evidence")
        if not isinstance(evidence, list):
            errors.append(f"supplemental requirement evidence must be list: {rid}")
        if row.get("status") == "verified" and not evidence:
            errors.append(f"verified supplemental requirement lacks evidence: {rid}")
    if req_ids != EXPECTED_REQUIREMENTS:
        errors.append("supplemental requirements must be exactly EVREQ-055..EVREQ-071")

    inv_rows = inv_doc.get("invariants")
    inv_ids: set[str] = set()
    if not isinstance(inv_rows, list):
        errors.append("supplemental invariants must be a list")
        inv_rows = []
    for row in inv_rows:
        if not isinstance(row, dict):
            errors.append("supplemental invariant row must be object")
            continue
        iid = row.get("invariant_id")
        inv_ids.add(str(iid))
        if row.get("status") != "active":
            errors.append(f"supplemental invariant not active: {iid}")
        if not row.get("statement") or not row.get("enforcement"):
            errors.append(f"supplemental invariant incomplete: {iid}")
    if inv_ids != EXPECTED_INVARIANTS:
        errors.append("supplemental invariants must be exactly INV-EV-021..INV-EV-027")

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
    if "SCN-REGRESSION-001" not in seen_scenarios:
        errors.append("anti-skip regression scenario missing")

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
    actions = policy.get("action_types")
    if not isinstance(actions, list) or "preflight" not in actions:
        errors.append("execution policy must include preflight action")

    if scope.get("changeset") != "CS016A":
        errors.append("CS016A ACTION_SCOPE identity mismatch")
    if scope.get("runtime_change_authorized") is not False:
        errors.append("CS016A must not authorize runtime changes")
    allowed_paths = scope.get("allowed_paths")
    forbidden_paths = scope.get("forbidden_paths")
    if not isinstance(allowed_paths, list) or not isinstance(forbidden_paths, list):
        errors.append("CS016A ACTION_SCOPE path lists invalid")
    else:
        for protected in ("src/**", "include/**"):
            if protected not in forbidden_paths:
                errors.append(f"CS016A forbidden path missing: {protected}")
        for path in allowed_paths:
            if path.startswith("src/") or path.startswith("include/"):
                errors.append(f"runtime path allowed by CS016A: {path}")

    for command in WORKFLOW_COMMANDS:
        if command not in workflow:
            errors.append(f"workflow missing D-Lab command: {command}")

    if stage0 and stage0.get("status") == "accepted":
        if any(row.get("assessment_status") == "not_assessed" for row in history_rows):
            errors.append("EV-00 accepted with historical entries not_assessed")
        for row in history_rows:
            if (
                row.get("risk_class") in {"critical", "high"}
                and row.get("reproducibility") in {"reproducible", "reproduced"}
                and row.get("rerun_status") != "passed"
            ):
                errors.append(
                    f"EV-00 accepted without required historical rerun: {row.get('changeset')}"
                )
        for row in req_rows:
            if row.get("status") != "verified":
                errors.append(
                    f"EV-00 accepted with supplemental requirement not verified: "
                    f"{row.get('requirement_id')}"
                )

    if check_authorizer and amendment_status == "in_progress":
        auth = root / FILES["authorizer"]
        good = subprocess.run(
            [
                sys.executable,
                str(auth),
                "--action",
                "governance_amendment",
                "--changeset",
                "CS016A",
            ],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
        if good.returncode != 0:
            errors.append(
                "action authorizer rejected current CS016A governance work: "
                + good.stdout.strip()
                + good.stderr.strip()
            )
        bad = subprocess.run(
            [
                sys.executable,
                str(auth),
                "--action",
                "preflight",
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
            errors.append("action authorizer accepted PRE-CS017 before CS016A acceptance")
        if "required amendments not accepted" not in (bad.stdout + bad.stderr):
            errors.append("PRE-CS017 rejection did not expose CS016A blocker")

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
    evidence = ROOT / "docs/changesets/016A/evidence"
    if evidence.is_dir():
        shutil.copytree(
            evidence,
            dst / "docs/changesets/016A/evidence",
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
            failures.append("valid D-Lab governance candidate rejected: " + "; ".join(initial))

    def expect_reject(name: str, mutator: Callable[[Path], None]) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            case = Path(tmp)
            copy_fixture(case)
            mutator(case)
            if not validate_repository(case, check_git=False, check_authorizer=False):
                failures.append(f"negative case accepted: {name}")

    expect_reject(
        "missing historical ChangeSet",
        lambda case: mutate_json(
            case / FILES["historical"],
            lambda d: d["entries"].pop(),
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
        "missing supplemental invariant",
        lambda case: mutate_json(
            case / FILES["invariants"],
            lambda d: d["invariants"].pop(),
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
        "runtime path allowed",
        lambda case: mutate_json(
            case / FILES["action_scope"],
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
        "CS016A accepted without evidence",
        lambda case: (
            mutate_json(
                case / FILES["amendments"],
                lambda d: d["amendments"][0].update(
                    {
                        "status": "accepted",
                        "accepted_source_commit": "a" * 40,
                        "evidence_manifest": None,
                    }
                ),
            ),
            (case / FILES["test_status"]).write_text(
                "State: accepted\n", encoding="utf-8"
            ),
        ),
    )

    with tempfile.TemporaryDirectory() as tmp:
        case = Path(tmp)
        copy_fixture(case)
        ev_dir = case / "docs/changesets/016A/evidence"
        ev_dir.mkdir(parents=True, exist_ok=True)
        evidence_rel = "docs/changesets/016A/evidence/VALIDATION.json"
        evidence_path = case / evidence_rel
        evidence_path.write_text(
            json.dumps(
                {
                    "schema": "neoeng.dcore.cs016a-validation.v1",
                    "source_commit": "a" * 40,
                    "conclusion": "success",
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        manifest_rel = "docs/changesets/016A/evidence/EVIDENCE_MANIFEST.json"
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
            lambda d: d["amendments"][0].update(
                {
                    "status": "accepted",
                    "accepted_source_commit": "a" * 40,
                    "evidence_manifest": manifest_rel,
                }
            ),
        )
        (case / FILES["test_status"]).write_text(
            "State: accepted\n", encoding="utf-8"
        )
        accepted_errors = validate_repository(
            case, check_git=False, check_authorizer=False
        )
        if accepted_errors:
            failures.append(
                "valid accepted CS016A fixture rejected: "
                + "; ".join(accepted_errors)
            )
        evidence_path.write_text('{"tampered":true}\n', encoding="utf-8")
        tamper_errors = validate_repository(
            case, check_git=False, check_authorizer=False
        )
        if not any("evidence hash mismatch" in e for e in tamper_errors):
            failures.append("tampered CS016A evidence was not rejected")

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
