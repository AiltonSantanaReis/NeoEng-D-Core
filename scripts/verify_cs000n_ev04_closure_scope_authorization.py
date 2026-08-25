#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import hashlib
import io
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]

BASE = "d75fb80e7aa304576060339c31ff87fdb9dae206"
BASE_TREE = "52c8b90c69806008322a4fc5043aff60c7125040"
BASE_PARENT_1 = "9e5c00faa4db0868da48913b8ffa24e0f64972e2"
BASE_PARENT_2 = "8a44bc57bea3f5053746a3648f7e0b26bf8f4bb3"

BRANCH = "agent/cs000n-ev04-closure-scope-authorization"

WORKFLOW = Path(
    ".github/workflows/cs000n-ev04-closure-scope-authorization-validation.yml"
)
PLAN = Path("audit/validation/CS000N/VALIDATION_PLAN.json")
RESULT = Path("audit/validation/CS000N/VALIDATION_RESULT.json")
DESCRIPTOR = Path("audit/CURRENT_CHANGESET_VALIDATION.json")
SCOPE_LEDGER = Path("audit/STAGE_SCOPE_MAXIMA.json")
ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
REQS = Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json")
CHANGESET = Path("docs/changesets/000N/CHANGESET.md")
DECISION = Path("docs/records/evolution/DEV-0014.md")
SELF = Path("scripts/verify_cs000n_ev04_closure_scope_authorization.py")

POLICY = Path("audit/CHANGESET_VALIDATION_POLICY.json")
TRANSITION = Path("audit/GOVERNANCE_TRANSITION_STATE.json")
SOURCE_INDEX = Path("audit/SOURCE_OF_TRUTH_INDEX.json")

CS000M_PLAN = Path("audit/validation/CS000M/VALIDATION_PLAN.json")
CS000M_RESULT = Path("audit/validation/CS000M/VALIDATION_RESULT.json")
CS000M_CHANGESET = Path("docs/changesets/000M/CHANGESET.md")
CS000M_DECISION = Path("docs/records/evolution/DEV-0013.md")
CS000M_WORKFLOW = Path(
    ".github/workflows/cs000m-ev04-scope-authorization-validation.yml"
)
CS000M_VERIFIER = Path(
    "scripts/verify_cs000m_ev04_scope_authorization.py"
)

CS021_PLAN = Path("audit/validation/CS021/VALIDATION_PLAN.json")
CS021_RESULT = Path("audit/validation/CS021/VALIDATION_RESULT.json")
CS021_CHANGESET = Path("docs/changesets/021/CHANGESET.md")
CS021_WORKFLOW = Path(
    ".github/workflows/cs021-ev04-property-model-testing-validation.yml"
)
CS021_VERIFIER = Path(
    "scripts/verify_cs021_ev04_property_model_testing.py"
)
PROPERTY_TEST = Path("tests/property_model_tests.cpp")

CS021_SOURCE = "0c4b66b4474c128583f2ce1920aef848bebc3db8"
CS021_SOURCE_TREE = "c8c6cb2c38c5b09480629992da24a85528877e4c"
CS021_BINDING = "8a44bc57bea3f5053746a3648f7e0b26bf8f4bb3"
CS021_BINDING_TREE = BASE_TREE
CS021_QUALIFYING_RUN = 32794089676

BASE_MANIFEST = Path("MANIFEST.sha256")
BASE_MANIFEST_EXPECTED_ROWS = 1103
BASE_MANIFEST_ANOMALY = {
    "audit/CURRENT_CHANGESET_VALIDATION.json": {
        "manifest_sha256":
            "b1098370c6f36a6b8d9a3ede6c9e0426a616bdf4734dfb81d9c8f34dd48e157b",
        "blob_sha256":
            "9d25d9170e1e3251a66525ea1c0141847318c52c3d9c51a0c124311c373f4214",
    },
    "audit/validation/CS021/VALIDATION_RESULT.json": {
        "manifest_sha256":
            "ebfdf94cf26fe71e32d44388dfbc3bc93aafcb51c98b69096558107e51baa24e",
        "blob_sha256":
            "28a96b7dd7a871508764a2db41591596443d0c856d506442df64a5ac74188921",
    },
}

BASE_PREPARATION = [
    ".github/workflows/cs021-ev04-property-model-testing-validation.yml",
    "CMakeLists.txt",
    "MANIFEST.sha256",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/validation/CS021/**",
    "docs/changesets/021/**",
    "scripts/verify_cs021_ev04_property_model_testing.py",
    "tests/property_model_tests.cpp",
]

CLOSURE_ADDITIONS = [
    ".github/workflows/cs000o-ev04-ledger-closure-validation.yml",
    "audit/validation/CS000O/**",
    "docs/changesets/000O/**",
    "docs/records/evolution/DEV-0015.md",
    "scripts/verify_cs000o_ev04_ledger_closure.py",
]

BASE_FORBIDDEN = [
    "src/**",
    "include/**",
    "modules/**",
    "apps/**",
    "cmake/**",
    "docs/governance/**",
    "audit/SOURCE_OF_TRUTH_INDEX.json",
    "audit/STAGE_SCOPE_MAXIMA.json",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/EVOLUTION_INVARIANTS.json",
    ".github/workflows/changeset-validation.yml",
    ".github/workflows/current-product-regression.yml",
    "scripts/generate_manifest.py",
    "scripts/verify_changeset_validation.py",
    "scripts/verify_evolution_plan.py",
    "tests/golden/ev03/v1/**",
    "docs/contracts/GOLDEN_CORPUS_V1.md",
    "audit/validation/CS000L/**",
    "docs/changesets/000L/**",
]

EXPECTED_BASE_EV04 = {
    "stage_id": "EV-04",
    "planned_changeset": "CS021",
    "status": "defined",
    "preparation_allowed_patterns": BASE_PREPARATION,
    "allowed_patterns": list(BASE_PREPARATION),
    "mandatory_forbidden_patterns": BASE_FORBIDDEN,
}

EXPECTED_EXTENDED_EV04 = copy.deepcopy(EXPECTED_BASE_EV04)
EXPECTED_EXTENDED_EV04["preparation_allowed_patterns"] = (
    BASE_PREPARATION + CLOSURE_ADDITIONS
)
EXPECTED_EXTENDED_EV04["allowed_patterns"] = (
    BASE_PREPARATION + CLOSURE_ADDITIONS
)

TRIGGER_SCOPE = {
    ".github/workflows/cs000n-ev04-closure-scope-authorization-validation.yml",
    "audit/STAGE_SCOPE_MAXIMA.json",
    "audit/validation/CS000N/VALIDATION_PLAN.json",
    "docs/changesets/000N/CHANGESET.md",
    "docs/records/evolution/DEV-0014.md",
    "scripts/verify_cs000n_ev04_closure_scope_authorization.py",
}

SOURCE_SCOPE = TRIGGER_SCOPE | {
    "MANIFEST.sha256",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
}

PROTECTED_BASE_FILES = {
    ".gitattributes",
    ".github/workflows/changeset-validation.yml",
    ".github/workflows/cs000m-ev04-scope-authorization-validation.yml",
    ".github/workflows/cs021-ev04-property-model-testing-validation.yml",
    ".github/workflows/current-product-regression.yml",
    "CMakeLists.txt",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    "audit/EVOLUTION_INVARIANTS.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/SOURCE_OF_TRUTH_INDEX.json",
    "audit/validation/CS000M/VALIDATION_PLAN.json",
    "audit/validation/CS000M/VALIDATION_RESULT.json",
    "audit/validation/CS021/VALIDATION_PLAN.json",
    "audit/validation/CS021/VALIDATION_RESULT.json",
    "docs/changesets/000M/CHANGESET.md",
    "docs/changesets/021/CHANGESET.md",
    "docs/contracts/GOLDEN_CORPUS_V1.md",
    "docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md",
    "docs/records/evolution/DEV-0013.md",
    "scripts/generate_manifest.py",
    "scripts/verify_changeset_validation.py",
    "scripts/verify_cs000m_ev04_scope_authorization.py",
    "scripts/verify_cs021_ev04_property_model_testing.py",
    "scripts/verify_evolution_plan.py",
    "tests/golden_corpus_tests.cpp",
    "tests/property_model_tests.cpp",
    "tests/golden/ev03/v1/corpus.json",
    "tests/golden/ev03/v1/evidence_envelope.bin",
    "tests/golden/ev03/v1/manifest.json",
    "tests/golden/ev03/v1/world_after_rollback_replay.bin",
    "tests/golden/ev03/v1/world_after_transition.bin",
    "tests/golden/ev03/v1/world_initial.bin",
}

EXPECTED_TESTS = [
    "cs000n.verifier-self-test",
    "cs000n.authority",
    "cs000n.reconciliation",
    "cs000n.scope-ledger",
    "cs000n.predecessor",
    "cs000n.base-manifest-anomaly",
    "cs000n.nonclosure",
    "cs000n.descriptor",
    "cs000n.scope",
    "cs000n.non-effects",
    "evolution.plan",
    "repository.manifest",
    "changeset.plan-structure",
]


def run(*args: str, binary: bool = False) -> subprocess.CompletedProcess:
    return subprocess.run(
        args,
        cwd=ROOT,
        text=not binary,
        capture_output=True,
        check=False,
    )


def load(path: Path) -> dict[str, Any]:
    value = json.loads((ROOT / path).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {path}")
    return value


def git_show_bytes(ref: str, path: str) -> bytes | None:
    proc = run("git", "show", f"{ref}:{path}", binary=True)
    return proc.stdout if proc.returncode == 0 else None


def base_json(path: Path) -> dict[str, Any]:
    raw = git_show_bytes(BASE, path.as_posix())
    if raw is None:
        raise ValueError(f"cannot read base path: {path}")
    value = json.loads(raw.decode("utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"base JSON root must be object: {path}")
    return value


def tree_of(ref: str) -> str:
    proc = run("git", "rev-parse", f"{ref}^{{tree}}")
    if proc.returncode != 0:
        raise ValueError(proc.stderr.strip() or f"cannot resolve tree: {ref}")
    return proc.stdout.strip()


def parent_line(ref: str) -> list[str]:
    proc = run("git", "rev-list", "--parents", "-n", "1", ref)
    if proc.returncode != 0:
        raise ValueError(proc.stderr.strip() or f"cannot inspect parents: {ref}")
    return proc.stdout.strip().split()


def changed_paths() -> set[str]:
    result: set[str] = set()

    commands = [
        ("git", "diff", "--name-only", f"{BASE}...HEAD"),
        ("git", "diff", "--name-only", "HEAD"),
        ("git", "diff", "--cached", "--name-only"),
        ("git", "ls-files", "--others", "--exclude-standard"),
        ("git", "ls-files", "--others", "--ignored", "--exclude-standard"),
    ]

    for command in commands:
        proc = run(*command)
        if proc.returncode != 0:
            raise ValueError(
                proc.stderr.strip()
                or f"failed command: {' '.join(command)}"
            )
        result.update(
            line.strip()
            for line in proc.stdout.splitlines()
            if line.strip()
        )

    return result


def top_level_on_block(text: str) -> list[str]:
    lines = text.splitlines()
    try:
        start = lines.index("on:") + 1
    except ValueError as exc:
        raise ValueError("missing top-level on block") from exc

    block: list[str] = []
    for line in lines[start:]:
        if line and not line.startswith(" "):
            break
        block.append(line)
    return block


def workflow_branch_and_paths(text: str) -> tuple[list[str], list[str]]:
    block = top_level_on_block(text)
    branches: list[str] = []
    paths: list[str] = []
    mode: str | None = None

    for line in block:
        stripped = line.strip()
        if stripped == "branches:":
            mode = "branches"
            continue
        if stripped == "paths:":
            mode = "paths"
            continue
        if stripped.endswith(":") and not stripped.startswith("-"):
            if stripped not in {"push:", "branches:", "paths:"}:
                mode = None
            continue
        if stripped.startswith("- "):
            value = stripped[2:].strip().strip("'").strip('"')
            if mode == "branches":
                branches.append(value)
            elif mode == "paths":
                paths.append(value)

    return branches, paths


def expected_scope_document() -> dict[str, Any]:
    expected = copy.deepcopy(base_json(SCOPE_LEDGER))
    stages = expected.get("stages")

    if not isinstance(stages, list):
        raise ValueError("base stage-scope stages is not list")

    matches = [
        index
        for index, row in enumerate(stages)
        if isinstance(row, dict) and row.get("stage_id") == "EV-04"
    ]

    if len(matches) != 1:
        raise ValueError(
            f"base EV-04 scope row count is {len(matches)}, expected 1"
        )

    current = stages[matches[0]]
    if current != EXPECTED_BASE_EV04:
        raise ValueError("protected base EV-04 maximum drifted")

    stages[matches[0]] = copy.deepcopy(EXPECTED_EXTENDED_EV04)
    return expected


def parse_manifest_bytes(raw: bytes) -> dict[str, str]:
    rows: dict[str, str] = {}
    for line in raw.decode("utf-8").splitlines():
        parts = line.split("  ", 1)
        if len(parts) != 2:
            raise ValueError("invalid manifest row")
        digest, path = parts
        if len(digest) != 64 or any(ch not in "0123456789abcdef" for ch in digest):
            raise ValueError(f"invalid manifest digest: {path}")
        if path in rows:
            raise ValueError(f"duplicate manifest path: {path}")
        rows[path] = digest
    return rows


def base_blob_inventory() -> list[tuple[str, str]]:
    proc = run("git", "ls-tree", "-r", "-z", BASE, binary=True)
    if proc.returncode != 0:
        raise ValueError("cannot enumerate protected base tree")

    result: list[tuple[str, str]] = []
    for row in proc.stdout.split(b"\0"):
        if not row:
            continue
        try:
            meta, path_raw = row.split(b"\t", 1)
            mode, obj_type, oid = meta.decode("ascii").split()
            path = path_raw.decode("utf-8")
        except Exception as exc:
            raise ValueError("cannot parse protected base tree row") from exc

        if path == BASE_MANIFEST.as_posix():
            continue
        if obj_type != "blob":
            # generate_manifest.py only includes paths materialized as files.
            continue
        if mode not in {"100644", "100755", "120000"}:
            raise ValueError(f"unexpected protected base file mode: {mode} {path}")
        result.append((path, oid))

    return result


def batch_blob_bytes(entries: list[tuple[str, str]]) -> dict[str, bytes]:
    proc = subprocess.Popen(
        ["git", "cat-file", "--batch"],
        cwd=ROOT,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert proc.stdin is not None
    assert proc.stdout is not None

    payload = b"".join(oid.encode("ascii") + b"\n" for _, oid in entries)
    stdout, stderr = proc.communicate(payload)
    if proc.returncode != 0:
        raise ValueError(
            "git cat-file --batch failed: "
            + stderr.decode("utf-8", errors="replace").strip()
        )

    stream = io.BytesIO(stdout)
    result: dict[str, bytes] = {}
    for path, expected_oid in entries:
        header = stream.readline().rstrip(b"\n")
        parts = header.split()
        if len(parts) != 3:
            raise ValueError(f"invalid cat-file header for {path}")
        oid, obj_type, size_raw = parts
        if oid.decode("ascii") != expected_oid or obj_type != b"blob":
            raise ValueError(f"cat-file identity mismatch for {path}")
        size = int(size_raw)
        data = stream.read(size)
        if len(data) != size:
            raise ValueError(f"truncated cat-file data for {path}")
        if stream.read(1) != b"\n":
            raise ValueError(f"missing cat-file separator for {path}")
        result[path] = data

    if stream.read(1) != b"":
        raise ValueError("unexpected trailing cat-file output")
    return result


def protected_base_manifest_anomaly_errors() -> list[str]:
    errors: list[str] = []

    raw_manifest = git_show_bytes(BASE, BASE_MANIFEST.as_posix())
    if raw_manifest is None:
        return ["cannot read protected base MANIFEST.sha256"]

    try:
        manifest = parse_manifest_bytes(raw_manifest)
        entries = base_blob_inventory()
        blobs = batch_blob_bytes(entries)
    except ValueError as exc:
        return [str(exc)]

    expected_paths = {path for path, _ in entries}
    if len(expected_paths) != BASE_MANIFEST_EXPECTED_ROWS:
        errors.append(
            "protected base tracked-file count mismatch: "
            f"{len(expected_paths)}"
        )

    if len(manifest) != BASE_MANIFEST_EXPECTED_ROWS:
        errors.append(
            "protected base manifest row count mismatch: "
            f"{len(manifest)}"
        )

    missing = sorted(expected_paths - set(manifest))
    extra = sorted(set(manifest) - expected_paths)
    if missing:
        errors.append("protected base manifest missing rows: " + ", ".join(missing))
    if extra:
        errors.append("protected base manifest extra rows: " + ", ".join(extra))

    mismatches: dict[str, tuple[str, str]] = {}
    for path in sorted(expected_paths & set(manifest)):
        actual = hashlib.sha256(blobs[path]).hexdigest()
        recorded = manifest[path]
        if actual != recorded:
            mismatches[path] = (recorded, actual)

    if set(mismatches) != set(BASE_MANIFEST_ANOMALY):
        errors.append(
            "protected base manifest mismatch paths differ from exact preserved anomaly: "
            + ", ".join(sorted(mismatches))
        )
        return errors

    for path, expected in BASE_MANIFEST_ANOMALY.items():
        recorded, actual = mismatches[path]
        if recorded != expected["manifest_sha256"]:
            errors.append(f"protected base stale manifest hash changed: {path}")
        if actual != expected["blob_sha256"]:
            errors.append(f"protected base blob hash changed: {path}")

        data = blobs[path]
        if b"\r" in data:
            errors.append(f"protected base blob unexpectedly contains CR bytes: {path}")
            continue
        crlf = data.replace(b"\n", b"\r\n")
        crlf_sha = hashlib.sha256(crlf).hexdigest()
        if crlf_sha != recorded:
            errors.append(
                f"stale manifest row is not exact CRLF reconstruction: {path}"
            )

    return errors


def base_manifest_anomaly_errors() -> list[str]:
    errors = protected_base_manifest_anomaly_errors()
    if errors:
        return errors

    # Historical CS021 bytes remain immutable. Only the current manifest is
    # prospectively repaired as part of CS000N source preparation.
    result_bytes = git_show_bytes(BASE, CS021_RESULT.as_posix())
    current_result = ROOT / CS021_RESULT
    if result_bytes is None or not current_result.is_file():
        errors.append("CS021 result missing while checking manifest repair")
    elif current_result.read_bytes() != result_bytes:
        errors.append("CS021 result bytes changed while repairing manifest")

    current_manifest = ROOT / BASE_MANIFEST
    if not current_manifest.is_file():
        errors.append("current MANIFEST.sha256 missing")
        return errors

    try:
        rows = parse_manifest_bytes(current_manifest.read_bytes())
    except ValueError as exc:
        errors.append(str(exc))
        return errors

    expected_result_sha = BASE_MANIFEST_ANOMALY[
        CS021_RESULT.as_posix()
    ]["blob_sha256"]
    if rows.get(CS021_RESULT.as_posix()) != expected_result_sha:
        errors.append("current manifest did not repair CS021 result row")

    descriptor = ROOT / DESCRIPTOR
    if not descriptor.is_file():
        errors.append("current descriptor missing")
    else:
        descriptor_sha = hashlib.sha256(descriptor.read_bytes()).hexdigest()
        if rows.get(DESCRIPTOR.as_posix()) != descriptor_sha:
            errors.append("current manifest does not match CS000N descriptor bytes")

    return errors


def authority_errors() -> list[str]:
    errors: list[str] = []

    policy = load(POLICY)
    transition = load(TRANSITION)
    index = load(SOURCE_INDEX)

    if policy.get("schema") != (
        "neoeng.dcore.changeset-validation-policy.v1"
    ):
        errors.append("ChangeSet policy schema changed")

    for key in [
        "test_inventory_frozen_before_execution",
        "exact_run_binding_required",
        "failure_preservation_required",
        "accepted_requires_validated",
        "all_required_tests_must_pass",
        "trusted_base_verifier_required_for_pr_acceptance",
        "historical_records_are_immutable",
        "release_is_separate_from_changeset_acceptance",
    ]:
        if policy.get(key) is not True:
            errors.append(f"ChangeSet policy invariant changed: {key}")

    if policy.get("candidate_verifier_is_authoritative") is not False:
        errors.append("candidate verifier became authoritative")

    authority = transition.get("prospective_authority", {})
    if authority.get("regime_id") != "CHANGESET_VALIDATION":
        errors.append("prospective ChangeSet regime mismatch")

    precedence = index.get("precedence")
    if not isinstance(precedence, list):
        errors.append("source-of-truth precedence missing")
    else:
        required = [
            "audit/STAGE_SCOPE_MAXIMA.json",
            "audit/EVOLUTION_ROADMAP.json",
        ]
        for path in required:
            if path not in precedence:
                errors.append(f"precedence path missing: {path}")

        if all(path in precedence for path in required):
            if precedence.index(required[0]) > precedence.index(required[1]):
                errors.append(
                    "stage-scope ledger no longer precedes evolution roadmap"
                )

    conflict = index.get("conflict_policy")
    if (
        not isinstance(conflict, str)
        or "conflict" not in conflict.lower()
        or "stop" not in conflict.lower()
    ):
        errors.append("source-of-truth conflict STOP policy changed")

    return errors


def reconciliation_errors() -> list[str]:
    current = load(SCOPE_LEDGER)
    try:
        expected = expected_scope_document()
    except ValueError as exc:
        return [str(exc)]

    if current != expected:
        return [
            "STAGE_SCOPE_MAXIMA differs from exact five-pattern "
            "EV-04 closure-scope extension"
        ]
    return []


def scope_ledger_errors() -> list[str]:
    errors: list[str] = []
    doc = load(SCOPE_LEDGER)

    rows = [
        row
        for row in doc.get("stages", [])
        if isinstance(row, dict) and row.get("stage_id") == "EV-04"
    ]

    if len(rows) != 1:
        return [f"EV-04 scope row count is {len(rows)}, expected 1"]

    if rows[0] != EXPECTED_EXTENDED_EV04:
        errors.append("EV-04 scope row does not match frozen extension")

    base = base_json(SCOPE_LEDGER)
    if doc.get("undefined_stages") != base.get("undefined_stages"):
        errors.append("undefined_stages changed in CS000N")

    if doc.get("default_policy") != base.get("default_policy"):
        errors.append("stage-scope default policy changed")

    return errors


def predecessor_errors() -> list[str]:
    errors: list[str] = []

    cs000m = load(CS000M_RESULT)
    if cs000m.get("changeset") != "CS000M":
        errors.append("scope predecessor is not CS000M")
    if cs000m.get("validation_state") != "VALIDATED":
        errors.append("CS000M validation state changed")
    if cs000m.get("acceptance_decision") != "ACCEPTED":
        errors.append("CS000M acceptance changed")

    cs021 = load(CS021_RESULT)
    expected_pairs = {
        "changeset": "CS021",
        "source_sha": CS021_SOURCE,
        "run_id": CS021_QUALIFYING_RUN,
        "run_attempt": 1,
        "workflow_path": CS021_WORKFLOW.as_posix(),
        "validation_state": "VALIDATED",
        "acceptance_decision": "ACCEPTED",
    }
    for key, expected in expected_pairs.items():
        if cs021.get(key) != expected:
            errors.append(
                f"CS021 accepted result mismatch: {key}={cs021.get(key)!r}"
            )

    qualification = cs021.get("qualification")
    if not isinstance(qualification, dict):
        errors.append("CS021 qualification block missing")
    else:
        q_expected = {
            "run_number": 1,
            "event": "push",
            "head_branch": "agent/cs021-ev04-property-model-testing",
            "head_sha": CS021_SOURCE,
            "head_tree": CS021_SOURCE_TREE,
            "status": "completed",
            "conclusion": "success",
            "dedicated_qualification_rerun": False,
        }
        for key, expected in q_expected.items():
            if qualification.get(key) != expected:
                errors.append(
                    f"CS021 qualification mismatch: {key}"
                )

    effects = cs021.get("effects")
    if not isinstance(effects, dict):
        errors.append("CS021 effects block missing")
    else:
        if effects.get("ev04_stage_acceptance") != (
            "NOT_SET_BY_THIS_RESULT"
        ):
            errors.append("CS021 result unexpectedly closes EV-04")
        if effects.get("evreq_016_018_ledger_verification") != (
            "NOT_SET_BY_THIS_RESULT"
        ):
            errors.append("CS021 result unexpectedly closes EVREQ-016..018")
        if effects.get("ev05") != "NOT_STARTED":
            errors.append("CS021 result unexpectedly starts EV-05")
        if effects.get("release") != "NOT_AUTHORIZED":
            errors.append("CS021 result unexpectedly authorizes release")

    try:
        if tree_of(BASE) != BASE_TREE:
            errors.append("protected CS021 merge tree mismatch")
        if tree_of(CS021_SOURCE) != CS021_SOURCE_TREE:
            errors.append("CS021 source tree mismatch")
        if tree_of(CS021_BINDING) != CS021_BINDING_TREE:
            errors.append("CS021 binding tree mismatch")

        base_parents = parent_line(BASE)
        if base_parents != [BASE, BASE_PARENT_1, BASE_PARENT_2]:
            errors.append("protected CS021 merge parent geometry mismatch")

        binding_parents = parent_line(CS021_BINDING)
        if binding_parents != [CS021_BINDING, CS021_SOURCE]:
            errors.append("CS021 binding parent mismatch")
    except ValueError as exc:
        errors.append(str(exc))

    for path in [
        CS000M_PLAN,
        CS000M_RESULT,
        CS000M_CHANGESET,
        CS000M_DECISION,
        CS000M_WORKFLOW,
        CS000M_VERIFIER,
        CS021_PLAN,
        CS021_RESULT,
        CS021_CHANGESET,
        CS021_WORKFLOW,
        CS021_VERIFIER,
        PROPERTY_TEST,
    ]:
        prior = git_show_bytes(BASE, path.as_posix())
        current = ROOT / path
        if prior is None:
            errors.append(f"cannot read predecessor at base: {path}")
        elif not current.is_file():
            errors.append(f"predecessor path missing: {path}")
        elif current.read_bytes() != prior:
            errors.append(f"predecessor bytes changed: {path}")

    ancestry = run("git", "merge-base", "--is-ancestor", BASE, "HEAD")
    if ancestry.returncode != 0:
        errors.append("protected CS021 integration is not ancestor")

    return errors


def nonclosure_errors() -> list[str]:
    errors: list[str] = []

    for path in [ROADMAP, REQS]:
        prior = git_show_bytes(BASE, path.as_posix())
        current = ROOT / path
        if prior is None:
            errors.append(f"cannot read base lifecycle ledger: {path}")
        elif current.read_bytes() != prior:
            errors.append(f"lifecycle ledger changed in CS000N: {path}")

    roadmap = load(ROADMAP)
    if roadmap.get("current_stage") != "EV-04":
        errors.append("current_stage must remain EV-04")
    if roadmap.get("release_authorized") is not False:
        errors.append("release authorization changed")

    stages = {
        row.get("stage_id"): row
        for row in roadmap.get("stages", [])
        if isinstance(row, dict)
    }

    ev03 = stages.get("EV-03")
    ev04 = stages.get("EV-04")
    ev05 = stages.get("EV-05")

    if not isinstance(ev03, dict) or ev03.get("status") != "accepted":
        errors.append("EV-03 is not accepted")

    if not isinstance(ev04, dict):
        errors.append("EV-04 row missing")
    else:
        if ev04.get("status") != "in_progress":
            errors.append("EV-04 must remain in_progress")
        if ev04.get("planned_changeset") != "CS021":
            errors.append("EV-04 planned ChangeSet changed")
        for key in [
            "accepted_commit",
            "evidence_manifest",
            "decision_record",
        ]:
            if ev04.get(key) is not None:
                errors.append(f"EV-04 {key} closed prematurely")

    if not isinstance(ev05, dict) or ev05.get("status") != "not_started":
        errors.append("EV-05 lifecycle changed")

    req_doc = load(REQS)
    rows = {
        row.get("requirement_id"): row
        for row in req_doc.get("requirements", [])
        if isinstance(row, dict)
    }

    for req_id in ["EVREQ-016", "EVREQ-017", "EVREQ-018"]:
        row = rows.get(req_id)
        if not isinstance(row, dict):
            errors.append(f"{req_id} missing")
            continue
        if row.get("status") != "in_progress":
            errors.append(f"{req_id} must remain in_progress")
        if row.get("evidence") != []:
            errors.append(f"{req_id} evidence bound prematurely")

    for req_id in ["EVREQ-019", "EVREQ-020", "EVREQ-021"]:
        row = rows.get(req_id)
        if not isinstance(row, dict):
            errors.append(f"{req_id} missing")
            continue
        if row.get("status") != "planned":
            errors.append(f"{req_id} lifecycle changed")
        if row.get("evidence") != []:
            errors.append(f"{req_id} evidence changed")

    return errors


def descriptor_errors() -> list[str]:
    errors: list[str] = []

    descriptor = load(DESCRIPTOR)
    expected_descriptor = {
        "schema": "neoeng.dcore.current-changeset-validation.v1",
        "plan_path": PLAN.as_posix(),
    }
    if descriptor != expected_descriptor:
        errors.append("CS000N descriptor is not exact plan-only form")

    if (ROOT / RESULT).exists():
        errors.append("CS000N result exists in source candidate")

    plan = load(PLAN)
    if plan.get("schema") != "neoeng.dcore.changeset-validation-plan.v1":
        errors.append("CS000N plan schema mismatch")
    if plan.get("changeset") != "CS000N":
        errors.append("CS000N plan changeset mismatch")
    if plan.get("base_sha") != BASE:
        errors.append("CS000N plan base mismatch")
    if plan.get("execution_workflow") != WORKFLOW.as_posix():
        errors.append("CS000N plan workflow mismatch")
    if plan.get("acceptance_requires_all_required_pass") is not True:
        errors.append("CS000N plan all-pass requirement changed")
    if plan.get("allow_test_removal_after_execution") is not False:
        errors.append("CS000N plan allows test removal")

    tests = plan.get("required_tests")
    if not isinstance(tests, list):
        errors.append("CS000N required_tests missing")
    else:
        ids = [
            item.get("test_id")
            for item in tests
            if isinstance(item, dict)
        ]
        if ids != EXPECTED_TESTS:
            errors.append("CS000N required test inventory mismatch")
        for item in tests:
            if not isinstance(item, dict) or item.get("required") is not True:
                errors.append("CS000N contains non-required test")
                break

    frozen = plan.get("frozen_files")
    if not isinstance(frozen, list):
        errors.append("CS000N frozen_files missing")
    else:
        required_frozen = (
            PROTECTED_BASE_FILES
            | TRIGGER_SCOPE
            | {PLAN.as_posix()}
        )
        if not required_frozen.issubset(set(frozen)):
            missing = sorted(required_frozen - set(frozen))
            errors.append(
                "CS000N frozen_files missing: " + ", ".join(missing)
            )

    text = (ROOT / WORKFLOW).read_text(encoding="utf-8")
    try:
        branches, paths = workflow_branch_and_paths(text)
    except ValueError as exc:
        errors.append(str(exc))
    else:
        if branches != [BRANCH]:
            errors.append("CS000N workflow branch trigger mismatch")
        if set(paths) != TRIGGER_SCOPE or len(paths) != len(TRIGGER_SCOPE):
            errors.append("CS000N workflow path trigger mismatch")

    for lifecycle in [
        "MANIFEST.sha256",
        DESCRIPTOR.as_posix(),
        RESULT.as_posix(),
    ]:
        if lifecycle in TRIGGER_SCOPE:
            errors.append(f"lifecycle path leaked into trigger: {lifecycle}")

    return errors


def scope_errors() -> list[str]:
    try:
        actual = changed_paths()
    except ValueError as exc:
        return [str(exc)]

    if actual != SOURCE_SCOPE:
        missing = sorted(SOURCE_SCOPE - actual)
        extra = sorted(actual - SOURCE_SCOPE)
        errors = []
        if missing:
            errors.append("missing source paths: " + ", ".join(missing))
        if extra:
            errors.append("extra source paths: " + ", ".join(extra))
        return errors

    return []


def non_effects_errors() -> list[str]:
    errors: list[str] = []

    for path_text in sorted(PROTECTED_BASE_FILES):
        path = Path(path_text)
        prior = git_show_bytes(BASE, path_text)
        current = ROOT / path

        if prior is None:
            errors.append(f"cannot read protected base path: {path_text}")
        elif not current.is_file():
            errors.append(f"protected path missing: {path_text}")
        elif current.read_bytes() != prior:
            errors.append(f"protected path changed: {path_text}")

    return errors


def self_test_errors() -> list[str]:
    errors: list[str] = []

    if len(CLOSURE_ADDITIONS) != 5:
        errors.append("closure-addition cardinality fixture failed")
    if len(TRIGGER_SCOPE) != 6:
        errors.append("trigger scope cardinality fixture failed")
    if len(SOURCE_SCOPE) != 8:
        errors.append("source scope cardinality fixture failed")
    if len(EXPECTED_TESTS) != 13:
        errors.append("required test cardinality fixture failed")
    if len(BASE_MANIFEST_ANOMALY) != 2:
        errors.append("base manifest anomaly cardinality fixture failed")

    if EXPECTED_EXTENDED_EV04["preparation_allowed_patterns"] != (
        BASE_PREPARATION + CLOSURE_ADDITIONS
    ):
        errors.append("preparation extension fixture failed")

    if EXPECTED_EXTENDED_EV04["mandatory_forbidden_patterns"] != BASE_FORBIDDEN:
        errors.append("forbidden preservation fixture failed")

    if EXPECTED_EXTENDED_EV04["planned_changeset"] != "CS021":
        errors.append("planned ChangeSet preservation fixture failed")

    bad = set(SOURCE_SCOPE)
    bad.add("src/forbidden.cpp")
    if not (bad - SOURCE_SCOPE):
        errors.append("forbidden source-path fixture failed")

    for lifecycle in [
        "MANIFEST.sha256",
        DESCRIPTOR.as_posix(),
        RESULT.as_posix(),
    ]:
        if lifecycle in TRIGGER_SCOPE:
            errors.append(f"lifecycle trigger fixture failed: {lifecycle}")

    return errors


def emit(label: str, errors: list[str]) -> int:
    if errors:
        print(f"CS000N EV-04 CLOSURE SCOPE AUTHORIZATION: REJECT — {label}")
        for error in errors:
            print(f"- {error}")
        return 1

    print(f"CS000N EV-04 CLOSURE SCOPE AUTHORIZATION: PASS — {label}")
    return 0


CHECKS = {
    "self-test": self_test_errors,
    "authority": authority_errors,
    "reconciliation": reconciliation_errors,
    "scope-ledger": scope_ledger_errors,
    "predecessor": predecessor_errors,
    "base-manifest-anomaly": base_manifest_anomaly_errors,
    "nonclosure": nonclosure_errors,
    "descriptor": descriptor_errors,
    "scope": scope_errors,
    "non-effects": non_effects_errors,
}


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)

    for name in CHECKS:
        group.add_argument(f"--{name}", action="store_true")

    group.add_argument("--all", action="store_true")
    args = parser.parse_args()

    if args.all:
        failed = 0
        for name, check in CHECKS.items():
            failed += emit(name, check())
        if failed:
            return 1
        print("CS000N EV-04 CLOSURE SCOPE AUTHORIZATION: PASS — all")
        return 0

    for name, check in CHECKS.items():
        attr = name.replace("-", "_")
        if getattr(args, attr):
            return emit(name, check())

    return 2


if __name__ == "__main__":
    sys.exit(main())
