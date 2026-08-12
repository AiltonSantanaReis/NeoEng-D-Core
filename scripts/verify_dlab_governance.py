#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
BASE_CONTROL_COMMIT = "855ff4563b96c4e5b2a7acac6e200fdf7f8d20d1"
BASELINE_COMMIT = "e3fff973554a2e56b8bd7afdc1132f75f3ec337c"
BASELINE_VERSION = "1.14.1"

AMENDMENT_V13 = Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_3_AMENDMENT.md")
REQ_C = Path("audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016C.json")
INV_C = Path("audit/EVOLUTION_INVARIANTS_AMENDMENT_016C.json")
AMENDMENTS = Path("audit/EVOLUTION_AMENDMENTS.json")
INDEX = Path("audit/SOURCE_OF_TRUTH_INDEX.json")
POLICY = Path("audit/DLAB_EXECUTION_POLICY.json")
SCENARIOS = Path("audit/DLAB_SCENARIO_CATALOG.json")
ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
AUTHOR = Path("scripts/authorize_evolution_action.py")
DEV_C = Path("docs/records/evolution/DEV-0003.md")
CHANGESET_C = Path("docs/changesets/016C/CHANGESET.md")
TEST_STATUS_C = Path("docs/changesets/016C/TEST_STATUS.md")
SCOPE_C = Path("docs/changesets/016C/ACTION_SCOPE.json")

PROTECTED_EXACT = [
    Path("docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md"),
    Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md"),
    Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_1_AMENDMENT.md"),
    Path("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_2_AMENDMENT.md"),
    Path("docs/governance/DLAB_VALIDATION_STANDARD.md"),
    Path("audit/EVOLUTION_ROADMAP.json"),
    Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json"),
    Path("audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016A.json"),
    Path("audit/EVOLUTION_REQUIREMENTS_AMENDMENT_016B.json"),
    Path("audit/EVOLUTION_INVARIANTS.json"),
    Path("audit/EVOLUTION_INVARIANTS_AMENDMENT_016A.json"),
    Path("audit/EVOLUTION_INVARIANTS_AMENDMENT_016B.json"),
    Path("audit/DLAB_HISTORICAL_REVALIDATION_MATRIX.json"),
    Path(".github/workflows/evolution-governance.yml"),
    Path("docs/records/evolution/DEV-0001.md"),
    Path("docs/records/evolution/DEV-0002.md"),
    Path("docs/changesets/016A/CHANGESET.md"),
    Path("docs/changesets/016A/TEST_STATUS.md"),
    Path("docs/changesets/016A/ACTION_SCOPE.json"),
    Path("docs/changesets/016B/CHANGESET.md"),
    Path("docs/changesets/016B/TEST_STATUS.md"),
    Path("docs/changesets/016B/ACTION_SCOPE.json"),
    Path("docs/changesets/016A/evidence/EVIDENCE_MANIFEST_ACCEPTED.json"),
    Path("docs/changesets/016B/evidence/EVIDENCE_MANIFEST_ACCEPTED.json"),
]

REQ_STATUSES = {"planned", "in_progress", "verified", "blocked", "rejected", "superseded"}
AMENDMENT_STATUSES = {"in_progress", "blocked", "failed", "accepted", "superseded"}

EXPECTED_PATH_POLICY = {
    "scenario_id": "SCN-REGRESSION-003",
    "allowlisted_dotpath_expected": "AUTHORIZED",
    "literal_dot_slash_prefix_equivalent": True,
    "dot_component_preserved": True,
    "forbidden_path_expected": "REJECT",
    "absolute_path_expected": "REJECT",
    "traversal_path_expected": "REJECT",
    "ambiguous_component_expected": "REJECT",
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


def sha256_text(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def git_bytes(commit: str, path: Path) -> bytes:
    proc = subprocess.run(
        ["git", "-C", str(ROOT), "show", f"{commit}:{path.as_posix()}"],
        capture_output=True,
        check=False,
    )
    if proc.returncode != 0:
        raise ValueError(f"cannot read protected snapshot {commit}:{path}")
    return proc.stdout


def git_json(commit: str, path: Path) -> dict[str, Any]:
    try:
        value = json.loads(git_bytes(commit, path).decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ValueError(f"invalid JSON in protected snapshot {commit}:{path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"snapshot JSON must be object: {path}")
    return value


def run(cmd: list[str], *, cwd: Path = ROOT) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, text=True, capture_output=True, check=False)


def run_base_gate(*, self_test: bool) -> list[str]:
    errors: list[str] = []
    with tempfile.TemporaryDirectory() as tmp:
        worktree = Path(tmp) / "governance-v12"
        add = run(
            [
                "git", "-C", str(ROOT), "worktree", "add", "--detach", "--force",
                str(worktree), BASE_CONTROL_COMMIT,
            ]
        )
        if add.returncode != 0:
            return ["unable to create accepted v1.2 governance worktree: " + add.stderr.strip()]
        try:
            cmd = [sys.executable, "scripts/verify_dlab_governance.py"]
            if self_test:
                cmd.append("--self-test")
            proc = run(cmd, cwd=worktree)
            if proc.returncode != 0:
                label = "self-test" if self_test else "verification"
                errors.append(
                    f"accepted v1.2 D-Lab {label} failed at {BASE_CONTROL_COMMIT}: "
                    + proc.stdout.strip()
                    + proc.stderr.strip()
                )
        finally:
            run(["git", "-C", str(ROOT), "worktree", "remove", "--force", str(worktree)])
            run(["git", "-C", str(ROOT), "worktree", "prune"])
    return errors


def validate_evidence_manifest(rel: str, source_commit: str) -> list[str]:
    errors: list[str] = []
    try:
        doc = load_json(ROOT / rel)
    except ValueError as exc:
        return [str(exc)]
    if doc.get("schema") != "neoeng.dcore.evolution-amendment-evidence-manifest.v1":
        errors.append("CS016C evidence manifest schema mismatch")
    if doc.get("source_commit") != source_commit:
        errors.append("CS016C evidence manifest source_commit mismatch")
    if doc.get("hash_mode") != "lf-normalized-text":
        errors.append("CS016C evidence manifest hash_mode mismatch")
    rows = doc.get("files")
    if not isinstance(rows, list) or not rows:
        return errors + ["CS016C evidence manifest must list evidence files"]
    prefix = "docs/changesets/016C/evidence/"
    seen: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            errors.append("CS016C evidence file row must be object")
            continue
        path_rel = row.get("path")
        digest = row.get("sha256")
        if not isinstance(path_rel, str) or not path_rel.startswith(prefix):
            errors.append(f"invalid CS016C evidence path: {path_rel!r}")
            continue
        if path_rel == rel:
            errors.append("CS016C evidence manifest cannot hash itself")
        if path_rel in seen:
            errors.append(f"duplicate CS016C evidence path: {path_rel}")
        seen.add(path_rel)
        path = ROOT / path_rel
        if not path.is_file():
            errors.append(f"missing CS016C evidence file: {path_rel}")
        elif digest != sha256_text(path):
            errors.append(f"CS016C evidence hash mismatch: {path_rel}")
    return errors


def validate_requirement(doc: dict[str, Any], *, accepted: bool) -> list[str]:
    errors: list[str] = []
    if doc.get("schema") != "neoeng.dcore.evolution-requirements-amendment-016c.v1":
        errors.append("CS016C requirement schema mismatch")
    if doc.get("project_version") != BASELINE_VERSION or doc.get("amendment") != "CS016C":
        errors.append("CS016C requirement document identity mismatch")
    rows = doc.get("requirements")
    if not isinstance(rows, list) or len(rows) != 1:
        return errors + ["CS016C requirements must contain exactly EVREQ-073"]
    row = rows[0]
    if not isinstance(row, dict) or row.get("requirement_id") != "EVREQ-073":
        return errors + ["CS016C requirements must contain exactly EVREQ-073"]
    if row.get("stage") != "EV-00":
        errors.append("EVREQ-073 stage mismatch")
    status = row.get("status")
    if status not in REQ_STATUSES:
        errors.append("EVREQ-073 status invalid")
    evidence = row.get("evidence")
    if not isinstance(row.get("evidence_required"), list) or not row.get("evidence_required"):
        errors.append("EVREQ-073 evidence_required missing")
    if not isinstance(evidence, list):
        errors.append("EVREQ-073 evidence must be list")
        evidence = []
    if accepted and status != "verified":
        errors.append("accepted CS016C requires EVREQ-073 verified")
    if status == "verified" and not evidence:
        errors.append("verified EVREQ-073 lacks evidence")
    return errors


def validate_invariant(doc: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if doc.get("schema") != "neoeng.dcore.evolution-invariants-amendment-016c.v1":
        errors.append("CS016C invariant schema mismatch")
    if doc.get("project_version") != BASELINE_VERSION or doc.get("amendment") != "CS016C":
        errors.append("CS016C invariant document identity mismatch")
    rows = doc.get("invariants")
    if not isinstance(rows, list) or len(rows) != 1:
        return errors + ["CS016C invariants must contain exactly INV-EV-029"]
    row = rows[0]
    if not isinstance(row, dict) or row.get("invariant_id") != "INV-EV-029":
        return errors + ["CS016C invariants must contain exactly INV-EV-029"]
    if row.get("status") != "active" or not row.get("statement") or not row.get("enforcement"):
        errors.append("INV-EV-029 must be active and fully enforced")
    return errors


def validate_current_delta(*, run_authorizer: bool) -> list[str]:
    errors: list[str] = []

    ancestry = run(["git", "-C", str(ROOT), "merge-base", "--is-ancestor", BASE_CONTROL_COMMIT, "HEAD"])
    if ancestry.returncode != 0:
        errors.append("accepted v1.2 control commit is not ancestor of current HEAD")

    for rel in PROTECTED_EXACT:
        path = ROOT / rel
        if not path.is_file():
            errors.append(f"protected file missing: {rel}")
            continue
        try:
            if path.read_bytes() != git_bytes(BASE_CONTROL_COMMIT, rel):
                errors.append(f"protected v1.2 file changed during CS016C: {rel}")
        except ValueError as exc:
            errors.append(str(exc))

    required_files = [AMENDMENT_V13, REQ_C, INV_C, DEV_C, CHANGESET_C, TEST_STATUS_C, SCOPE_C]
    for rel in required_files:
        if not (ROOT / rel).is_file():
            errors.append(f"missing CS016C file: {rel}")
    if errors:
        return errors

    amendment_text = (ROOT / AMENDMENT_V13).read_text(encoding="utf-8")
    for token in (
        "Versão normativa efetiva: **1.3**",
        "CS016C",
        "DEV-0003",
        "SCN-REGRESSION-003",
        "EVREQ-073",
        "INV-EV-029",
        "lstrip",
        "STOP -> deviation record -> impact analysis -> amendment -> verification -> resume",
    ):
        if token not in amendment_text:
            errors.append(f"Amendment 1.3 missing token: {token}")

    deviation = (ROOT / DEV_C).read_text(encoding="utf-8")
    if "DEV-0003" not in deviation or "871f4c571f776e599c136ccbd131123003a69a77" not in deviation:
        errors.append("DEV-0003 does not preserve triggering CS017 control identity")
    if "No product execution occurred after detection" not in deviation:
        errors.append("DEV-0003 must explicitly bound product execution claim")

    amendments = load_json(ROOT / AMENDMENTS)
    base_amendments = git_json(BASE_CONTROL_COMMIT, AMENDMENTS)
    rows = amendments.get("amendments")
    base_rows = base_amendments.get("amendments")
    if not isinstance(rows, list) or not isinstance(base_rows, list):
        errors.append("amendment ledger rows invalid")
        return errors
    if len(rows) != len(base_rows) + 1 or rows[: len(base_rows)] != base_rows:
        errors.append("CS016C amendment ledger is not append-only over accepted A/B rows")
        return errors
    c = rows[-1]
    if not isinstance(c, dict) or c.get("changeset") != "CS016C":
        errors.append("CS016C must be the single appended amendment row")
        return errors
    if c.get("required_before_stage") != "EV-00":
        errors.append("CS016C must block EV-00 until accepted")
    if c.get("status") not in AMENDMENT_STATUSES:
        errors.append("CS016C amendment status invalid")
    if c.get("amendment_document") != str(AMENDMENT_V13) or c.get("deviation_record") != str(DEV_C):
        errors.append("CS016C amendment document/deviation binding mismatch")
    accepted = c.get("status") == "accepted"
    test_status = (ROOT / TEST_STATUS_C).read_text(encoding="utf-8")
    changeset = (ROOT / CHANGESET_C).read_text(encoding="utf-8")
    if "ChangeSet 016C" not in changeset or BASELINE_COMMIT not in changeset:
        errors.append("CS016C ChangeSet identity/baseline missing")
    if accepted:
        source = c.get("accepted_source_commit")
        manifest = c.get("evidence_manifest")
        if not is_sha(source):
            errors.append("accepted CS016C lacks valid accepted_source_commit")
        if not isinstance(manifest, str) or not manifest:
            errors.append("accepted CS016C lacks evidence manifest")
        elif is_sha(source):
            errors.extend(validate_evidence_manifest(manifest, source))
            anc = run(["git", "-C", str(ROOT), "merge-base", "--is-ancestor", source, "HEAD"])
            if anc.returncode != 0:
                errors.append("accepted CS016C source commit is not ancestor of HEAD")
        if "State: accepted" not in test_status:
            errors.append("accepted CS016C not reflected in TEST_STATUS")
    elif c.get("status") == "in_progress":
        if c.get("accepted_source_commit") is not None or c.get("evidence_manifest") is not None:
            errors.append("in-progress CS016C cannot carry acceptance evidence")
        if "State: in_progress" not in test_status:
            errors.append("in-progress CS016C TEST_STATUS mismatch")

    errors.extend(validate_requirement(load_json(ROOT / REQ_C), accepted=accepted))
    errors.extend(validate_invariant(load_json(ROOT / INV_C)))

    scope = load_json(ROOT / SCOPE_C)
    if scope.get("changeset") != "CS016C" or scope.get("runtime_change_authorized") is not False:
        errors.append("CS016C ACTION_SCOPE identity/runtime authorization invalid")
    allowed = scope.get("allowed_paths")
    forbidden = scope.get("forbidden_paths")
    if not isinstance(allowed, list) or not isinstance(forbidden, list):
        errors.append("CS016C ACTION_SCOPE path lists invalid")
    else:
        for rel in (str(AMENDMENT_V13), str(DEV_C), str(REQ_C), str(INV_C), str(AUTHOR), "scripts/verify_dlab_governance.py"):
            if rel not in allowed:
                errors.append(f"CS016C required governance path not allowlisted: {rel}")
        for rel in (
            "src/**", "include/**", "CMakeLists.txt", "tests/**",
            "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md",
            "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_1_AMENDMENT.md",
            "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_2_AMENDMENT.md",
            "docs/governance/DLAB_VALIDATION_STANDARD.md",
            "audit/EVOLUTION_ROADMAP.json",
        ):
            if rel not in forbidden:
                errors.append(f"CS016C protected path not forbidden: {rel}")

    index = load_json(ROOT / INDEX)
    base_index = git_json(BASE_CONTROL_COMMIT, INDEX)
    precedence = index.get("precedence")
    base_precedence = base_index.get("precedence")
    if not isinstance(precedence, list) or not isinstance(base_precedence, list):
        errors.append("source index precedence invalid")
    else:
        new_entries = {str(AMENDMENT_V13), str(REQ_C), str(INV_C)}
        if [x for x in precedence if x not in new_entries] != base_precedence:
            errors.append("source index precedence changed beyond append-only CS016C registration")
        for rel in new_entries:
            if precedence.count(rel) != 1:
                errors.append(f"source index precedence missing/duplicate CS016C entry: {rel}")
        if precedence.index(str(AMENDMENT_V13)) > precedence.index("docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_2_AMENDMENT.md"):
            errors.append("Amendment 1.3 must precede Amendment 1.2")

    ledgers = index.get("machine_ledgers")
    base_ledgers = base_index.get("machine_ledgers")
    if not isinstance(ledgers, list) or not isinstance(base_ledgers, list):
        errors.append("source index machine_ledgers invalid")
    else:
        if [x for x in ledgers if x not in {str(REQ_C), str(INV_C)}] != base_ledgers:
            errors.append("machine_ledgers changed beyond CS016C additions")
        for rel in (str(REQ_C), str(INV_C)):
            if ledgers.count(rel) != 1:
                errors.append(f"CS016C ledger registration invalid: {rel}")

    if index.get("verifiers") != base_index.get("verifiers"):
        errors.append("registered verifier set changed during CS016C")
    active = index.get("active_evolution_program")
    base_active = base_index.get("active_evolution_program")
    if not isinstance(active, dict) or not isinstance(base_active, dict):
        errors.append("active_evolution_program invalid")
    else:
        for key, value in base_active.items():
            if key in {"effective_master_plan_amendment", "previous_master_plan_amendment"}:
                continue
            if active.get(key) != value:
                errors.append(f"active_evolution_program changed protected field: {key}")
        expected_new = {
            "effective_master_plan_amendment": str(AMENDMENT_V13),
            "previous_master_plan_amendment": "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_2_AMENDMENT.md",
            "previous_master_plan_amendment_v11": "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_1_AMENDMENT.md",
            "requirements_amendment_016c": str(REQ_C),
            "invariants_amendment_016c": str(INV_C),
        }
        for key, value in expected_new.items():
            if active.get(key) != value:
                errors.append(f"active_evolution_program CS016C field mismatch: {key}")

    policy = load_json(ROOT / POLICY)
    base_policy = git_json(BASE_CONTROL_COMMIT, POLICY)
    for key, value in base_policy.items():
        if policy.get(key) != value:
            errors.append(f"D-Lab execution policy prior field changed: {key}")
    if policy.get("path_canonicalization_regression") != EXPECTED_PATH_POLICY:
        errors.append("path canonicalization execution policy mismatch")
    if set(policy) != set(base_policy) | {"path_canonicalization_regression"}:
        errors.append("D-Lab execution policy contains unexpected CS016C changes")

    scenarios = load_json(ROOT / SCENARIOS)
    base_scenarios = git_json(BASE_CONTROL_COMMIT, SCENARIOS)
    rows_s = scenarios.get("scenarios")
    base_rows_s = base_scenarios.get("scenarios")
    for key in ("schema", "program_id", "stage", "classes", "types", "statuses"):
        if scenarios.get(key) != base_scenarios.get(key):
            errors.append(f"scenario catalog protected field changed: {key}")
    if not isinstance(rows_s, list) or not isinstance(base_rows_s, list):
        errors.append("scenario rows invalid")
    elif len(rows_s) != len(base_rows_s) + 1 or rows_s[: len(base_rows_s)] != base_rows_s:
        errors.append("SCN-REGRESSION-003 is not a pure append to the accepted scenario catalog")
    else:
        reg = rows_s[-1]
        if not isinstance(reg, dict) or reg.get("scenario_id") != "SCN-REGRESSION-003":
            errors.append("appended scenario must be SCN-REGRESSION-003")
        else:
            if reg.get("class") != "regression" or reg.get("type") != "simulated" or reg.get("risk") != "critical":
                errors.append("SCN-REGRESSION-003 classification mismatch")
            if not reg.get("oracle") or reg.get("randomized") is not False:
                errors.append("SCN-REGRESSION-003 oracle/randomization invalid")
            evidence = reg.get("evidence")
            if accepted:
                if reg.get("status") != "passed" or not isinstance(evidence, list) or not evidence:
                    errors.append("accepted CS016C requires SCN-REGRESSION-003 passed with evidence")
            elif reg.get("status") != "planned":
                errors.append("in-progress CS016C requires SCN-REGRESSION-003 planned")

    author_text = (ROOT / AUTHOR).read_text(encoding="utf-8")
    if 'lstrip("./")' in author_text:
        errors.append("unsafe character-set lstrip path normalization remains present")
    if "def normalize_repository_path" not in author_text:
        errors.append("canonical repository path function missing")

    roadmap = load_json(ROOT / ROADMAP)
    stages = roadmap.get("stages")
    stage0 = next((row for row in stages if isinstance(row, dict) and row.get("stage_id") == "EV-00"), None) if isinstance(stages, list) else None
    if roadmap.get("current_stage") != "EV-00" or not isinstance(stage0, dict):
        errors.append("EV-00 must remain current while CS016C is handled")
    elif stage0.get("status") != "not_started":
        errors.append("official EV-00 must remain not_started throughout CS016C")
    if roadmap.get("release_authorized") is not False:
        errors.append("CS016C cannot authorize release")

    if run_authorizer:
        self_proc = run([sys.executable, str(ROOT / AUTHOR), "--self-test"])
        if self_proc.returncode != 0:
            errors.append("current action authorizer self-test failed: " + self_proc.stdout.strip() + self_proc.stderr.strip())
        if c.get("status") == "in_progress":
            gov = run([
                sys.executable, str(ROOT / AUTHOR), "--action", "governance_amendment",
                "--changeset", "CS016C", "--path", "scripts/authorize_evolution_action.py",
                "--path", str(AMENDMENT_V13),
            ])
            if gov.returncode != 0:
                errors.append("authorizer rejected current CS016C governance work: " + gov.stdout.strip() + gov.stderr.strip())
            blocked = run([
                sys.executable, str(ROOT / AUTHOR), "--action", "prepare_stage_changeset",
                "--stage", "EV-00", "--changeset", "CS017",
            ])
            if blocked.returncode == 0 or "CS016C" not in (blocked.stdout + blocked.stderr):
                errors.append("CS017 was not fail-closed blocked by in-progress CS016C")
        elif c.get("status") == "accepted":
            closed = run([
                sys.executable, str(ROOT / AUTHOR), "--action", "governance_amendment",
                "--changeset", "CS016C",
            ])
            if closed.returncode == 0:
                errors.append("accepted CS016C still authorizes governance amendment work")
            ready = run([
                sys.executable, str(ROOT / AUTHOR), "--action", "prepare_stage_changeset",
                "--stage", "EV-00", "--changeset", "CS017",
            ])
            if ready.returncode != 0:
                errors.append("CS017 preparation rejected after all amendments accepted: " + ready.stdout.strip() + ready.stderr.strip())

    return errors


def self_test() -> list[str]:
    failures = run_base_gate(self_test=True)
    failures.extend(validate_current_delta(run_authorizer=True))
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

    errors = run_base_gate(self_test=False)
    errors.extend(validate_current_delta(run_authorizer=True))
    if errors:
        print("D-LAB GOVERNANCE VERIFICATION: REJECT")
        for item in errors:
            print(f"- {item}")
        return 1
    print("D-LAB GOVERNANCE VERIFICATION: ACCEPT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
