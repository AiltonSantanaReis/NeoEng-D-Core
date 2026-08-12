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

BASELINE_VERSION = "1.14.1"
BASELINE_TAG = "v1.14.1"
BASELINE_COMMIT = "e3fff973554a2e56b8bd7afdc1132f75f3ec337c"

FILES = {
    "index": Path("audit/SOURCE_OF_TRUTH_INDEX.json"),
    "roadmap": Path("audit/EVOLUTION_ROADMAP.json"),
    "requirements": Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json"),
    "invariants": Path("audit/EVOLUTION_INVARIANTS.json"),
    "master": Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md"),
    "changeset": Path("docs/changesets/016/CHANGESET.md"),
    "test_status": Path("docs/changesets/016/TEST_STATUS.md"),
}

EXPECTED_SCHEMAS = {
    "roadmap": "neoeng.dcore.evolution-roadmap.v1",
    "requirements": "neoeng.dcore.evolution-requirements-traceability.v1",
    "invariants": "neoeng.dcore.evolution-invariants.v1",
}
REQUIRED_PRECEDENCE = [
    "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_INVARIANTS.json",
]
REQUIRED_MACHINE_LEDGERS = REQUIRED_PRECEDENCE[1:]
REQUIRED_VERIFIER = "scripts/verify_evolution_plan.py"

STAGE_STATUS_ORDER = [
    "not_started", "in_progress", "blocked", "failed", "accepted", "superseded"
]
REQ_STATUS_ORDER = [
    "planned", "in_progress", "verified", "blocked", "rejected", "superseded"
]
STAGE_STATUSES = set(STAGE_STATUS_ORDER)
REQ_STATUSES = set(REQ_STATUS_ORDER)
EXPECTED_STAGE_IDS = [f"EV-{i:02d}" for i in range(21)]
EXPECTED_CHANGESETS = [f"CS{i:03d}" for i in range(17, 38)]
REQUIRED_INVARIANTS = {f"INV-EV-{i:03d}" for i in range(1, 21)}
MASTER_TOKENS = [
    "NEOENG-DCORE-EVOLUTION-001",
    BASELINE_COMMIT,
    "R-001",
    "R-015",
    "EV-00",
    "EV-20",
    "CS016",
    "sem arquitetura paralela",
    "scripts/verify_evolution_plan.py",
    "STOP -> deviation record -> impact analysis -> amendment -> verification -> resume",
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
        capture_output=True, text=True, check=False,
    )
    head = result.stdout.strip()
    if result.returncode != 0 or not is_sha(head):
        raise ValueError("git HEAD is unavailable; repository identity is mandatory")
    return head


def is_ancestor(root: Path, ancestor: str, descendant: str) -> bool:
    return subprocess.run(
        ["git", "-C", str(root), "merge-base", "--is-ancestor", ancestor, descendant],
        capture_output=True, check=False,
    ).returncode == 0


def validate_evidence_manifest(root: Path, rel: str, source_commit: str) -> list[str]:
    errors: list[str] = []
    try:
        doc = load_json(root / rel)
    except ValueError as exc:
        return [str(exc)]
    if doc.get("schema") != "neoeng.dcore.evolution-evidence-manifest.v1":
        errors.append("evidence manifest schema mismatch")
    if doc.get("source_commit") != source_commit:
        errors.append("evidence manifest source_commit mismatch")
    if doc.get("hash_mode") != "lf-normalized-text":
        errors.append("evidence manifest hash_mode mismatch")
    rows = doc.get("files")
    if not isinstance(rows, list) or not rows:
        return errors + ["evidence manifest must list evidence files"]
    seen: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            errors.append("evidence file row must be object")
            continue
        path_rel = row.get("path")
        digest = row.get("sha256")
        if not isinstance(path_rel, str) or not path_rel.startswith(
            "docs/changesets/016/evidence/"
        ):
            errors.append(f"invalid evidence path: {path_rel!r}")
            continue
        if path_rel == rel:
            errors.append("evidence manifest cannot hash itself")
        if path_rel in seen:
            errors.append(f"duplicate evidence path: {path_rel}")
        seen.add(path_rel)
        path = root / path_rel
        if not path.is_file():
            errors.append(f"missing evidence file: {path_rel}")
        elif digest != sha256_text(path):
            errors.append(f"evidence hash mismatch: {path_rel}")
    return errors


def validate_repository(root: Path, *, check_git: bool = True) -> list[str]:
    errors: list[str] = []
    docs: dict[str, Any] = {}
    for key, rel in FILES.items():
        path = root / rel
        try:
            docs[key] = (
                path.read_text(encoding="utf-8")
                if key in {"master", "changeset", "test_status"}
                else load_json(path)
            )
        except (FileNotFoundError, ValueError) as exc:
            errors.append(str(exc))
    if errors:
        return errors

    index = docs["index"]
    roadmap = docs["roadmap"]
    req_doc = docs["requirements"]
    inv_doc = docs["invariants"]
    master = docs["master"]
    changeset = docs["changeset"]
    test_status = docs["test_status"]

    for key, schema in EXPECTED_SCHEMAS.items():
        if docs[key].get("schema") != schema:
            errors.append(f"{key} schema mismatch")
        if docs[key].get("project_version") != BASELINE_VERSION:
            errors.append(f"{key} project_version mismatch")

    if index.get("schema") != "neoeng.dcore.source-of-truth-index.v1":
        errors.append("source-of-truth index schema mismatch")
    if index.get("project_version") != BASELINE_VERSION:
        errors.append("source-of-truth index project_version mismatch")
    if index.get("required_for_every_changeset") is not True:
        errors.append("source of truth must be required for every ChangeSet")

    precedence = index.get("precedence")
    if not isinstance(precedence, list):
        errors.append("precedence must be a list")
    else:
        if len(precedence) != len(set(precedence)):
            errors.append("precedence contains duplicates")
        positions: list[int] = []
        for rel in REQUIRED_PRECEDENCE:
            if rel not in precedence:
                errors.append(f"missing precedence entry: {rel}")
            else:
                positions.append(precedence.index(rel))
            if not (root / rel).exists():
                errors.append(f"precedence path missing: {rel}")
        if positions and positions != sorted(positions):
            errors.append("evolution precedence is out of order")

    ledgers = index.get("machine_ledgers")
    if not isinstance(ledgers, list):
        errors.append("machine_ledgers must be a list")
    else:
        for rel in REQUIRED_MACHINE_LEDGERS:
            if rel not in ledgers:
                errors.append(f"machine ledger not registered: {rel}")
    verifiers = index.get("verifiers")
    if not isinstance(verifiers, list) or REQUIRED_VERIFIER not in verifiers:
        errors.append("evolution verifier not registered")

    active = index.get("active_evolution_program")
    expected_active = {
        "program_id": "POST_1_14_1",
        "baseline_commit": BASELINE_COMMIT,
        "master_plan": str(FILES["master"]),
        "roadmap": str(FILES["roadmap"]),
        "bootstrap_changeset": "CS016",
    }
    if not isinstance(active, dict):
        errors.append("active_evolution_program missing")
    else:
        for key, expected in expected_active.items():
            if active.get(key) != expected:
                errors.append(f"active_evolution_program {key} mismatch")

    for token in MASTER_TOKENS:
        if token not in master:
            errors.append(f"master plan missing token: {token}")
    headings = re.findall(r"^### (EV-\d{2})\b", master, flags=re.MULTILINE)
    if headings != EXPECTED_STAGE_IDS:
        errors.append("master plan must contain EV-00..EV-20 exactly once and in order")

    baseline = roadmap.get("baseline")
    if not isinstance(baseline, dict):
        errors.append("roadmap baseline missing")
    else:
        if baseline.get("release_tag") != BASELINE_TAG:
            errors.append("baseline tag mismatch")
        if baseline.get("release_commit") != BASELINE_COMMIT:
            errors.append("baseline commit mismatch")
        if baseline.get("historical_immutable") is not True:
            errors.append("baseline must be historical_immutable")
    if roadmap.get("normative_document") != str(FILES["master"]):
        errors.append("roadmap normative_document mismatch")
    if roadmap.get("stage_statuses") != STAGE_STATUS_ORDER:
        errors.append("stage status set/order mismatch")
    if roadmap.get("requirement_statuses") != REQ_STATUS_ORDER:
        errors.append("requirement status set/order mismatch")

    bootstrap = roadmap.get("governance_bootstrap")
    if not isinstance(bootstrap, dict):
        errors.append("governance_bootstrap missing")
        bootstrap = {}
    if bootstrap.get("changeset") != "CS016":
        errors.append("bootstrap changeset must be CS016")
    bootstrap_status = bootstrap.get("status")
    if bootstrap_status not in {"in_progress", "accepted"}:
        errors.append("bootstrap status must be in_progress or accepted")

    rows = roadmap.get("stages")
    if not isinstance(rows, list):
        rows = []
        errors.append("stages must be a list")
    ids = [row.get("stage_id") for row in rows if isinstance(row, dict)]
    if ids != EXPECTED_STAGE_IDS:
        errors.append("roadmap must contain EV-00..EV-20 exactly once and in order")

    status_by_id: dict[str, Any] = {}
    in_progress: list[str] = []
    for idx, row in enumerate(rows):
        if not isinstance(row, dict):
            errors.append("stage row must be object")
            continue
        sid = row.get("stage_id")
        status = row.get("status")
        status_by_id[str(sid)] = status
        if status not in STAGE_STATUSES:
            errors.append(f"invalid stage status: {sid}: {status!r}")
        if status == "in_progress":
            in_progress.append(str(sid))
        expected_dep = [] if idx == 0 else [EXPECTED_STAGE_IDS[idx - 1]]
        if row.get("depends_on") != expected_dep:
            errors.append(f"stage dependency mismatch: {sid}")
        if row.get("planned_changeset") != EXPECTED_CHANGESETS[idx]:
            errors.append(f"planned changeset mismatch: {sid}")
        if status == "accepted":
            if not is_sha(row.get("accepted_commit")):
                errors.append(f"accepted stage lacks accepted_commit: {sid}")
            if not row.get("evidence_manifest"):
                errors.append(f"accepted stage lacks evidence_manifest: {sid}")
        elif row.get("accepted_commit") is not None:
            errors.append(f"non-accepted stage has accepted_commit: {sid}")
    if len(in_progress) > 1:
        errors.append("more than one stage is in_progress")
    for row in rows:
        if not isinstance(row, dict) or row.get("status") not in {"in_progress", "accepted"}:
            continue
        for dep in row.get("depends_on", []):
            if status_by_id.get(dep) != "accepted":
                errors.append(f"{row.get('stage_id')} advanced before {dep} was accepted")

    program_state = roadmap.get("program_state")
    current_stage = roadmap.get("current_stage")
    release_authorized = roadmap.get("release_authorized")
    if bootstrap_status == "in_progress":
        if program_state != "locked_pending_bootstrap_acceptance":
            errors.append("program must remain locked during CS016")
        if current_stage is not None:
            errors.append("current_stage must be null during CS016")
        if any(value != "not_started" for value in status_by_id.values()):
            errors.append("all EV stages must remain not_started during CS016")
        if release_authorized is not False:
            errors.append("release_authorized must be false during CS016")
        if bootstrap.get("accepted_source_commit") is not None:
            errors.append("in-progress bootstrap cannot have accepted_source_commit")
        if bootstrap.get("evidence_manifest") is not None:
            errors.append("in-progress bootstrap cannot have evidence_manifest")
        if "State: in_progress" not in test_status:
            errors.append("CS016 TEST_STATUS must declare State: in_progress")
    elif bootstrap_status == "accepted":
        source = bootstrap.get("accepted_source_commit")
        manifest = bootstrap.get("evidence_manifest")
        if not is_sha(source):
            errors.append("accepted bootstrap lacks valid accepted_source_commit")
        if not isinstance(manifest, str) or not manifest:
            errors.append("accepted bootstrap lacks evidence_manifest")
        elif is_sha(source):
            errors.extend(validate_evidence_manifest(root, manifest, source))
        if program_state not in {"active", "completed"}:
            errors.append("accepted bootstrap requires active/completed program_state")
        if program_state == "active" and current_stage not in EXPECTED_STAGE_IDS:
            errors.append("active program current_stage must be EV-00..EV-20")
        if "State: accepted" not in test_status:
            errors.append("accepted CS016 must be reflected in TEST_STATUS")
        if check_git and is_sha(source):
            head = git_head(root)
            if not is_ancestor(root, source, head):
                errors.append("accepted bootstrap source commit is not ancestor of HEAD")

    if "ChangeSet 016" not in changeset or "CS016" not in changeset:
        errors.append("CS016 identity missing")
    if BASELINE_COMMIT not in changeset:
        errors.append("CS016 does not bind the historical baseline")
    if "nenhum código do núcleo" not in changeset.lower():
        errors.append("CS016 must explicitly exclude core code changes")

    req_rows = req_doc.get("requirements")
    if not isinstance(req_rows, list) or not req_rows:
        req_rows = []
        errors.append("requirements ledger must be non-empty")
    req_ids: set[str] = set()
    stages_with_req: set[str] = set()
    for row in req_rows:
        if not isinstance(row, dict):
            errors.append("requirement row must be object")
            continue
        rid = row.get("requirement_id")
        if not isinstance(rid, str) or re.fullmatch(r"EVREQ-\d{3}", rid) is None:
            errors.append(f"invalid requirement id: {rid!r}")
            continue
        if rid in req_ids:
            errors.append(f"duplicate requirement id: {rid}")
        req_ids.add(rid)
        stage = row.get("stage")
        if stage not in EXPECTED_STAGE_IDS:
            errors.append(f"requirement references unknown stage: {rid}")
        else:
            stages_with_req.add(stage)
        status = row.get("status")
        if status not in REQ_STATUSES:
            errors.append(f"invalid requirement status: {rid}: {status!r}")
        if not isinstance(row.get("evidence_required"), list) or not row.get("evidence_required"):
            errors.append(f"requirement lacks evidence_required: {rid}")
        evidence = row.get("evidence")
        if not isinstance(evidence, list):
            errors.append(f"requirement evidence must be list: {rid}")
            evidence = []
        if status == "verified" and not evidence:
            errors.append(f"verified requirement lacks evidence: {rid}")
    missing = sorted(set(EXPECTED_STAGE_IDS) - stages_with_req)
    if missing:
        errors.append("stages without requirements: " + ", ".join(missing))

    inv_rows = inv_doc.get("invariants")
    if not isinstance(inv_rows, list):
        inv_rows = []
        errors.append("invariants must be a list")
    inv_ids: set[str] = set()
    for row in inv_rows:
        if not isinstance(row, dict):
            errors.append("invariant row must be object")
            continue
        iid = row.get("invariant_id")
        if not isinstance(iid, str) or re.fullmatch(r"INV-EV-\d{3}", iid) is None:
            errors.append(f"invalid invariant id: {iid!r}")
            continue
        if iid in inv_ids:
            errors.append(f"duplicate invariant id: {iid}")
        inv_ids.add(iid)
        if row.get("status") != "active":
            errors.append(f"invariant must remain active: {iid}")
        if not row.get("statement") or not row.get("enforcement"):
            errors.append(f"invariant incomplete: {iid}")
    if inv_ids != REQUIRED_INVARIANTS:
        errors.append("invariant set must be exactly INV-EV-001..INV-EV-020")

    all_accepted = len(rows) == 21 and all(
        isinstance(row, dict) and row.get("status") == "accepted" for row in rows
    )
    all_verified = bool(req_rows) and all(
        isinstance(row, dict) and row.get("status") == "verified" for row in req_rows
    )
    if release_authorized is True and not (all_accepted and all_verified):
        errors.append("release_authorized before full accepted/verified closure")
    if release_authorized not in {True, False}:
        errors.append("release_authorized must be boolean")
    if program_state == "completed" and not (all_accepted and all_verified):
        errors.append("program completed without full closure")

    if check_git:
        head = git_head(root)
        if not is_ancestor(root, BASELINE_COMMIT, head):
            errors.append("historical v1.14.1 baseline is not ancestor of HEAD")
    return errors


def copy_fixture(dst: Path) -> None:
    for rel in FILES.values():
        target = dst / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / rel, target)
    evidence = ROOT / "docs/changesets/016/evidence"
    if evidence.is_dir():
        shutil.copytree(
            evidence,
            dst / "docs/changesets/016/evidence",
            dirs_exist_ok=True,
        )
    (dst / "scripts").mkdir(parents=True, exist_ok=True)


def mutate_json(path: Path, mutator: Callable[[dict[str, Any]], None]) -> None:
    data = json.loads(path.read_text(encoding="utf-8"))
    mutator(data)
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def self_test() -> list[str]:
    failures: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        base = Path(tmp)
        copy_fixture(base)
        initial = validate_repository(base, check_git=False)
        if initial:
            failures.append("valid candidate rejected: " + "; ".join(initial))

    def expect_reject(name: str, mutator: Callable[[Path], None]) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            case = Path(tmp)
            copy_fixture(case)
            mutator(case)
            if not validate_repository(case, check_git=False):
                failures.append(f"negative case accepted: {name}")

    expect_reject(
        "stage before dependency",
        lambda case: mutate_json(
            case / FILES["roadmap"],
            lambda d: d["stages"][1].update({"status": "in_progress"}),
        ),
    )
    expect_reject(
        "release early",
        lambda case: mutate_json(
            case / FILES["roadmap"], lambda d: d.update({"release_authorized": True})
        ),
    )
    expect_reject(
        "accepted stage without evidence",
        lambda case: mutate_json(
            case / FILES["roadmap"],
            lambda d: d["stages"][0].update(
                {"status": "accepted", "accepted_commit": "a" * 40}
            ),
        ),
    )
    expect_reject(
        "unknown requirement stage",
        lambda case: mutate_json(
            case / FILES["requirements"],
            lambda d: d["requirements"][0].update({"stage": "EV-99"}),
        ),
    )
    expect_reject(
        "missing invariant",
        lambda case: mutate_json(
            case / FILES["invariants"], lambda d: d["invariants"].pop()
        ),
    )
    expect_reject(
        "verifier unregistered",
        lambda case: mutate_json(
            case / FILES["index"], lambda d: d.update({"verifiers": []})
        ),
    )
    expect_reject(
        "bootstrap accepted without evidence",
        lambda case: (
            mutate_json(
                case / FILES["roadmap"],
                lambda d: d.update(
                    {
                        "governance_bootstrap": {
                            "changeset": "CS016",
                            "status": "accepted",
                            "accepted_source_commit": "a" * 40,
                            "evidence_manifest": None,
                        },
                        "program_state": "active",
                        "current_stage": "EV-00",
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
        evidence_rel = "docs/changesets/016/evidence/VALIDATION.json"
        evidence_path = case / evidence_rel
        evidence_path.parent.mkdir(parents=True, exist_ok=True)
        evidence_path.write_text(
            json.dumps(
                {
                    "schema": "neoeng.dcore.evolution-governance-validation.v1",
                    "source_commit": "a" * 40,
                    "conclusion": "success",
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        manifest_rel = "docs/changesets/016/evidence/EVIDENCE_MANIFEST.json"
        manifest_path = case / manifest_rel
        manifest_path.write_text(
            json.dumps(
                {
                    "schema": "neoeng.dcore.evolution-evidence-manifest.v1",
                    "source_commit": "a" * 40,
                    "hash_mode": "lf-normalized-text",
                    "files": [{"path": evidence_rel, "sha256": sha256_text(evidence_path)}],
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        mutate_json(
            case / FILES["roadmap"],
            lambda d: d.update(
                {
                    "governance_bootstrap": {
                        "changeset": "CS016",
                        "status": "accepted",
                        "accepted_source_commit": "a" * 40,
                        "evidence_manifest": manifest_rel,
                    },
                    "program_state": "active",
                    "current_stage": "EV-00",
                }
            ),
        )
        (case / FILES["test_status"]).write_text("State: accepted\n", encoding="utf-8")
        accepted_errors = validate_repository(case, check_git=False)
        if accepted_errors:
            failures.append("valid accepted fixture rejected: " + "; ".join(accepted_errors))
        evidence_path.write_text('{"tampered":true}\n', encoding="utf-8")
        tamper = validate_repository(case, check_git=False)
        if not any("evidence hash mismatch" in error for error in tamper):
            failures.append("tampered evidence was not rejected")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        failures = self_test()
        if failures:
            print("EVOLUTION GOVERNANCE SELF-TEST: REJECT")
            for item in failures:
                print(f"- {item}")
            return 1
        print("EVOLUTION GOVERNANCE SELF-TEST: ACCEPT")
        return 0

    errors = validate_repository(ROOT, check_git=True)
    if errors:
        print("EVOLUTION PLAN VERIFICATION: REJECT")
        for item in errors:
            print(f"- {item}")
        return 1
    print("EVOLUTION PLAN VERIFICATION: ACCEPT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
