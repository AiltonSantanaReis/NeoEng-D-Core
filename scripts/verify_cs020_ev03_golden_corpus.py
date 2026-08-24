#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]

BASE = "98c1042249ced4c1775dddf9a871e29dc6070828"
BRANCH = "agent/cs020-ev03-deterministic-golden-corpus"

PLAN = Path("audit/validation/CS020/VALIDATION_PLAN.json")
RESULT = Path("audit/validation/CS020/VALIDATION_RESULT.json")
ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
REQUIREMENTS = Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json")
OLD_WORKFLOW = Path(
    ".github/workflows/cs000k-ev02-ledger-closure-validation.yml"
)
WORKFLOW = Path(
    ".github/workflows/cs020-ev03-deterministic-golden-corpus-validation.yml"
)
CORPUS = Path("tests/golden/ev03/v1/corpus.json")
GOLDEN_MANIFEST = Path("tests/golden/ev03/v1/manifest.json")
CONTRACT = Path("docs/contracts/GOLDEN_CORPUS_V1.md")

SOURCE_SCOPE = {
    ".github/workflows/cs000k-ev02-ledger-closure-validation.yml",
    ".github/workflows/cs020-ev03-deterministic-golden-corpus-validation.yml",
    "CMakeLists.txt",
    "MANIFEST.sha256",
    "audit/CURRENT_CHANGESET_VALIDATION.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/validation/CS020/VALIDATION_PLAN.json",
    "docs/changesets/020/CHANGESET.md",
    "docs/contracts/GOLDEN_CORPUS_V1.md",
    "docs/records/evolution/DEV-0011.md",
    "scripts/verify_cs020_ev03_golden_corpus.py",
    "tests/golden_corpus_tests.cpp",
    "tests/golden/ev03/v1/corpus.json",
    "tests/golden/ev03/v1/manifest.json",
    "tests/golden/ev03/v1/world_initial.bin",
    "tests/golden/ev03/v1/world_after_transition.bin",
    "tests/golden/ev03/v1/world_after_rollback_replay.bin",
    "tests/golden/ev03/v1/evidence_envelope.bin",
}

RUNTIME_REFERENCES = {
    "docs/contracts/FUNDAMENTAL_TRANSITION_V1.md",
    "docs/contracts/STATE_EVIDENCE_V1.md",
    "docs/contracts/TEMPORAL_CLOSURE_V1.md",
    "include/neoeng/core/crypto_hash.hpp",
    "include/neoeng/core/fixed.hpp",
    "include/neoeng/core/hash.hpp",
    "include/neoeng/core/rollback.hpp",
    "include/neoeng/core/simulation.hpp",
    "include/neoeng/core/snapshot_store.hpp",
    "include/neoeng/core/state_evidence.hpp",
    "include/neoeng/core/types.hpp",
    "src/crypto_hash.cpp",
    "src/hash.cpp",
    "src/rollback.cpp",
    "src/simulation.cpp",
    "src/snapshot_store.cpp",
    "src/state_evidence.cpp",
    "tests/session_recovery_contract_tests.cpp",
    "tests/state_evidence_tests.cpp",
    "tests/temporal_closure_tests.cpp",
    "tests/test_main.cpp",
}

GOVERNANCE_REFERENCES = {
    ".github/workflows/changeset-validation.yml",
    ".github/workflows/current-product-regression.yml",
    "audit/CHANGESET_VALIDATION_POLICY.json",
    "audit/EVOLUTION_INVARIANTS.json",
    "audit/GOVERNANCE_TRANSITION_STATE.json",
    "audit/SOURCE_OF_TRUTH_INDEX.json",
    "audit/STAGE_SCOPE_MAXIMA.json",
    "audit/validation/CS000K/VALIDATION_PLAN.json",
    "audit/validation/CS000K/VALIDATION_RESULT.json",
    "docs/changesets/000K/CHANGESET.md",
    "docs/changesets/000K/evidence/EV02_ACCEPTANCE_MANIFEST.json",
    "docs/records/evolution/DEV-0010.md",
    "scripts/generate_manifest.py",
    "scripts/verify_changeset_validation.py",
    "scripts/verify_cs000k_ev02_ledger_closure.py",
    "scripts/verify_evolution_plan.py",
}

LEGACY_BLOB_BINDINGS = {
    "include/neoeng/core/fixed.hpp",
    "include/neoeng/core/hash.hpp",
    "include/neoeng/core/rollback.hpp",
    "include/neoeng/core/simulation.hpp",
    "include/neoeng/core/snapshot_store.hpp",
    "include/neoeng/core/types.hpp",
    "src/hash.cpp",
    "src/rollback.cpp",
    "src/simulation.cpp",
    "src/snapshot_store.cpp",
    "tests/test_main.cpp",
}

CS000K_PRESERVE = {
    "audit/validation/CS000K/VALIDATION_PLAN.json",
    "audit/validation/CS000K/VALIDATION_RESULT.json",
    "docs/changesets/000K/CHANGESET.md",
    "docs/changesets/000K/evidence/EV02_ACCEPTANCE_MANIFEST.json",
    "docs/records/evolution/DEV-0010.md",
    "scripts/verify_cs000k_ev02_ledger_closure.py",
}

ARTIFACTS = {
    "tests/golden/ev03/v1/world_initial.bin",
    "tests/golden/ev03/v1/world_after_transition.bin",
    "tests/golden/ev03/v1/world_after_rollback_replay.bin",
    "tests/golden/ev03/v1/evidence_envelope.bin",
    "tests/golden/ev03/v1/corpus.json",
}

CONTRACTS = {
    "docs/contracts/GOLDEN_CORPUS_V1.md",
    "docs/contracts/FUNDAMENTAL_TRANSITION_V1.md",
    "docs/contracts/STATE_EVIDENCE_V1.md",
    "docs/contracts/TEMPORAL_CLOSURE_V1.md",
}

PRODUCERS = {
    "include/neoeng/core/hash.hpp",
    "src/hash.cpp",
    "include/neoeng/core/simulation.hpp",
    "src/simulation.cpp",
    "include/neoeng/core/snapshot_store.hpp",
    "src/snapshot_store.cpp",
    "include/neoeng/core/rollback.hpp",
    "src/rollback.cpp",
    "include/neoeng/core/state_evidence.hpp",
    "src/state_evidence.cpp",
}

SNAPSHOT_STRATEGIES = [
    "full_copy",
    "delta_checkpoint",
    "paged_cow",
    "persistent_chunk_tree",
    "component_soa",
    "hybrid_adaptive",
]

CMAKE_INSERT = """
  add_executable(neoeng_golden_corpus_tests tests/golden_corpus_tests.cpp)
  target_link_libraries(neoeng_golden_corpus_tests PRIVATE neoeng_dcore)
  neoeng_set_warnings(neoeng_golden_corpus_tests)
  add_test(NAME neoeng_golden_corpus_tests
    COMMAND neoeng_golden_corpus_tests
      ${CMAKE_SOURCE_DIR}/tests/golden/ev03/v1)
  set_tests_properties(neoeng_golden_corpus_tests
    PROPERTIES LABELS "smoke;dcore;ev03;golden-corpus")"""


def git(*args: str, binary: bool = False):
    return subprocess.run(
        ["git", "-C", str(ROOT), *args],
        capture_output=True,
        text=not binary,
        check=False,
    )


def git_show(commit: str, rel: str) -> bytes | None:
    result = git("show", f"{commit}:{rel}", binary=True)
    return result.stdout if result.returncode == 0 else None


def git_blob(commit: str, rel: str) -> str | None:
    result = git("rev-parse", f"{commit}:{rel}")
    if result.returncode != 0:
        return None
    value = result.stdout.strip()
    return value or None


def read_json(rel: Path) -> dict[str, Any]:
    value = json.loads((ROOT / rel).read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {rel}")
    return value


def sha256_file(rel: str) -> str:
    return hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()


def changed_paths() -> set[str]:
    head_proc = git("rev-parse", "HEAD")
    if head_proc.returncode != 0:
        raise ValueError("cannot resolve HEAD")

    head = head_proc.stdout.strip()
    paths: set[str] = set()

    if head == BASE:
        proc = git(
            "diff",
            "--cached",
            "--name-only",
            BASE,
            "--",
        )
    else:
        proc = git(
            "diff",
            "--name-only",
            BASE,
            head,
            "--",
        )

    if proc.returncode != 0:
        raise ValueError("cannot inspect candidate delta")

    paths.update(
        line.strip()
        for line in proc.stdout.splitlines()
        if line.strip()
    )

    unstaged = git("diff", "--name-only", "HEAD", "--")
    if unstaged.returncode != 0:
        raise ValueError("cannot inspect unstaged delta")

    paths.update(
        line.strip()
        for line in unstaged.stdout.splitlines()
        if line.strip()
    )

    untracked = git(
        "ls-files",
        "--others",
        "--exclude-standard",
    )

    if untracked.returncode != 0:
        raise ValueError("cannot inspect untracked paths")

    paths.update(
        line.strip()
        for line in untracked.stdout.splitlines()
        if line.strip()
    )

    return paths


def authority_errors() -> list[str]:
    errors: list[str] = []

    policy = read_json(
        Path("audit/CHANGESET_VALIDATION_POLICY.json")
    )
    transition = read_json(
        Path("audit/GOVERNANCE_TRANSITION_STATE.json")
    )
    index = read_json(
        Path("audit/SOURCE_OF_TRUTH_INDEX.json")
    )

    if policy.get("schema") != (
        "neoeng.dcore.changeset-validation-policy.v1"
    ):
        errors.append("ChangeSet policy schema mismatch")

    for key in (
        "test_inventory_frozen_before_execution",
        "exact_run_binding_required",
        "failure_preservation_required",
        "accepted_requires_validated",
        "all_required_tests_must_pass",
        "trusted_base_verifier_required_for_pr_acceptance",
        "historical_records_are_immutable",
        "release_is_separate_from_changeset_acceptance",
    ):
        if policy.get(key) is not True:
            errors.append(f"policy invariant changed: {key}")

    if (
        transition.get("prospective_authority", {}).get("regime_id")
        != "CHANGESET_VALIDATION"
    ):
        errors.append("prospective regime mismatch")

    if (
        transition.get("legacy_cs016e", {}).get("status")
        != "SUPERSEDED_UNACCEPTED"
    ):
        errors.append("legacy CS016E classification mismatch")

    precedence = index.get("precedence")
    if not isinstance(precedence, list):
        errors.append("source-of-truth precedence missing")

    return errors


def validate_planning_docs(
    roadmap: dict[str, Any],
    requirements: dict[str, Any],
) -> list[str]:
    errors: list[str] = []

    if roadmap.get("current_stage") != "EV-03":
        errors.append("current_stage must be EV-03")

    if roadmap.get("release_authorized") is not False:
        errors.append("release_authorized changed")

    stages = {
        row.get("stage_id"): row
        for row in roadmap.get("stages", [])
        if isinstance(row, dict)
    }

    ev02 = stages.get("EV-02")
    ev03 = stages.get("EV-03")
    ev04 = stages.get("EV-04")
    ev05 = stages.get("EV-05")

    if not isinstance(ev02, dict) or ev02.get("status") != "accepted":
        errors.append("EV-02 dependency is not accepted")

    if not isinstance(ev03, dict):
        errors.append("EV-03 missing")
    else:
        if ev03.get("status") != "in_progress":
            errors.append("EV-03 must be in_progress")
        if ev03.get("planned_changeset") != "CS020":
            errors.append("EV-03 planned ChangeSet changed")
        if ev03.get("accepted_commit") is not None:
            errors.append("EV-03 has premature accepted_commit")
        if ev03.get("evidence_manifest") is not None:
            errors.append("EV-03 has premature evidence_manifest")
        if ev03.get("decision_record") is not None:
            errors.append("EV-03 has premature decision_record")

    for stage, label in (
        (ev04, "EV-04"),
        (ev05, "EV-05"),
    ):
        if not isinstance(stage, dict) or stage.get("status") != "not_started":
            errors.append(f"{label} must remain not_started")

    rows = {
        row.get("requirement_id"): row
        for row in requirements.get("requirements", [])
        if isinstance(row, dict)
    }

    for rid in ("EVREQ-013", "EVREQ-014", "EVREQ-015"):
        row = rows.get(rid)
        if not isinstance(row, dict):
            errors.append(f"{rid} missing")
            continue
        if row.get("stage") != "EV-03":
            errors.append(f"{rid} stage changed")
        if row.get("status") != "in_progress":
            errors.append(f"{rid} must be in_progress")
        if row.get("evidence") != []:
            errors.append(f"{rid} has premature evidence")

    for rid in ("EVREQ-016", "EVREQ-017", "EVREQ-018"):
        row = rows.get(rid)
        if not isinstance(row, dict):
            errors.append(f"{rid} missing")
            continue
        if row.get("status") != "planned":
            errors.append(f"{rid} must remain planned")
        if row.get("evidence") != []:
            errors.append(f"{rid} has premature evidence")

    return errors


def planning_errors() -> list[str]:
    return validate_planning_docs(
        read_json(ROADMAP),
        read_json(REQUIREMENTS),
    )


def cs000k_preservation_errors() -> list[str]:
    errors: list[str] = []

    for rel in sorted(CS000K_PRESERVE):
        prior = git_show(BASE, rel)
        current = ROOT / rel
        if prior is None:
            errors.append(f"cannot read CS000K base path: {rel}")
        elif not current.is_file():
            errors.append(f"CS000K path missing: {rel}")
        elif prior != current.read_bytes():
            errors.append(f"CS000K historical bytes changed: {rel}")

    result = read_json(
        Path("audit/validation/CS000K/VALIDATION_RESULT.json")
    )

    if result.get("validation_state") != "VALIDATED":
        errors.append("CS000K validation state changed")

    if result.get("acceptance_decision") != "ACCEPTED":
        errors.append("CS000K acceptance changed")

    ancestry = git(
        "merge-base",
        "--is-ancestor",
        BASE,
        "HEAD",
    )

    if ancestry.returncode != 0:
        errors.append("integrated CS000K base is not ancestor of HEAD")

    return errors


def workflow_retirement_errors() -> list[str]:
    errors: list[str] = []

    prior = git_show(BASE, OLD_WORKFLOW.as_posix())
    current = ROOT / OLD_WORKFLOW

    if prior is None:
        return ["cannot read CS000K workflow at base"]

    if not current.is_file():
        return ["retired CS000K workflow missing"]

    base_text = prior.decode("utf-8")
    current_text = current.read_text(encoding="utf-8")

    token = "permissions:\n"
    position = base_text.find(token)

    if position < 0:
        return ["cannot locate CS000K workflow body boundary"]

    expected = (
        "name: CS000K EV-02 ledger closure\n\n"
        "on:\n"
        "  workflow_dispatch:\n\n"
        + base_text[position:]
    )

    if current_text != expected:
        errors.append(
            "CS000K workflow retirement changed more than trigger block"
        )

    if "  pull_request:" in current_text:
        errors.append("CS000K still has pull_request trigger")

    return errors


def scope_errors() -> list[str]:
    errors: list[str] = []

    actual = changed_paths()

    if actual != SOURCE_SCOPE:
        missing = sorted(SOURCE_SCOPE - actual)
        extra = sorted(actual - SOURCE_SCOPE)

        if missing:
            errors.append(
                "source scope missing: " + ", ".join(missing)
            )
        if extra:
            errors.append(
                "source scope extra: " + ", ".join(extra)
            )

    if (ROOT / RESULT).exists():
        errors.append("VALIDATION_RESULT exists before qualification")

    return errors


def reference_errors() -> list[str]:
    errors: list[str] = []

    byte_frozen = (
        RUNTIME_REFERENCES | GOVERNANCE_REFERENCES
    ) - LEGACY_BLOB_BINDINGS

    for rel in sorted(byte_frozen):
        prior = git_show(BASE, rel)
        current = ROOT / rel

        if prior is None:
            errors.append(f"cannot read frozen base reference: {rel}")
        elif not current.is_file():
            errors.append(f"frozen reference missing: {rel}")
        elif prior != current.read_bytes():
            errors.append(f"frozen reference changed: {rel}")

    for rel in sorted(LEGACY_BLOB_BINDINGS):
        base_blob = git_blob(BASE, rel)
        head_blob = git_blob("HEAD", rel)

        if base_blob is None:
            errors.append(
                f"cannot resolve legacy base Git blob: {rel}"
            )
        elif head_blob is None:
            errors.append(
                f"cannot resolve legacy candidate Git blob: {rel}"
            )
        elif head_blob != base_blob:
            errors.append(
                f"legacy Git blob changed from base: {rel}"
            )

    return errors


def validate_corpus_doc(
    doc: dict[str, Any],
) -> list[str]:
    errors: list[str] = []

    if doc.get("schema") != "neoeng.dcore.golden-corpus.v1":
        errors.append("golden corpus schema mismatch")

    if doc.get("corpus_version") != 1:
        errors.append("golden corpus version mismatch")

    if doc.get("base_sha") != BASE:
        errors.append("golden corpus base SHA mismatch")

    if doc.get("scenario") != (
        "ev03-transition-rollback-evidence-v1"
    ):
        errors.append("golden scenario mismatch")

    if doc.get("canonical_world_format_version") != 1:
        errors.append("canonical world version mismatch")

    if doc.get("state_evidence_schema_version") != 1:
        errors.append("state evidence schema version mismatch")

    if doc.get("state_merkle_format_version") != 1:
        errors.append("Merkle format version mismatch")

    if doc.get("merkle_chunk_bodies") != 1:
        errors.append("golden Merkle chunk size mismatch")

    if doc.get("snapshot_strategies") != SNAPSHOT_STRATEGIES:
        errors.append("snapshot strategy corpus mismatch")

    if doc.get("snapshot_restore_all_pass") is not True:
        errors.append("snapshot restore oracle is not PASS")

    diagnostics = doc.get("diagnostics")
    if not isinstance(diagnostics, dict):
        errors.append("diagnostics block missing")
    else:
        maximum = diagnostics.get("maximum_frame", {})
        unknown = diagnostics.get("unknown_entity", {})

        if maximum != {
            "exception": "std::overflow_error",
            "message": "World frame maximum reached",
        }:
            errors.append("maximum-frame diagnostic mismatch")

        if unknown != {
            "exception": "std::out_of_range",
            "message": "Input references unknown EntityId",
        }:
            errors.append("unknown-entity diagnostic mismatch")

    evidence = doc.get("state_evidence")
    if not isinstance(evidence, dict):
        errors.append("state evidence block missing")
    else:
        if evidence.get("accepted") is not True:
            errors.append("accepted evidence oracle mismatch")
        if evidence.get("deterministic_rejection_reason") != (
            "canonical_digest_mismatch"
        ):
            errors.append("evidence rejection reason mismatch")
        if evidence.get("artifact") != "evidence_envelope.bin":
            errors.append("evidence artifact binding mismatch")

    expected_artifacts = {
        ("initial", "world_initial.bin"),
        ("after_transition", "world_after_transition.bin"),
        ("rollback_replay", "world_after_rollback_replay.bin"),
    }

    for key, artifact in expected_artifacts:
        row = doc.get(key)
        if not isinstance(row, dict):
            errors.append(f"missing corpus state: {key}")
            continue

        if row.get("artifact") != artifact:
            errors.append(f"{key} artifact mismatch")

        for digest_key in (
            "stable_hash",
            "canonical_sha256",
            "merkle_root_sha256",
        ):
            value = row.get(digest_key)
            if not isinstance(value, str) or not value:
                errors.append(f"{key} missing {digest_key}")

    replay = doc.get("rollback_replay", {})
    if isinstance(replay, dict):
        if replay.get("resimulated_frames") != 2:
            errors.append("rollback replay count mismatch")

    return errors


def corpus_schema_errors() -> list[str]:
    try:
        doc = read_json(CORPUS)
    except (ValueError, json.JSONDecodeError) as exc:
        return [str(exc)]

    return validate_corpus_doc(doc)


def validate_manifest_doc(
    doc: dict[str, Any],
    *,
    check_files: bool,
) -> list[str]:
    errors: list[str] = []

    if doc.get("schema") != (
        "neoeng.dcore.golden-corpus-manifest.v1"
    ):
        errors.append("golden manifest schema mismatch")

    if doc.get("corpus_schema") != (
        "neoeng.dcore.golden-corpus.v1"
    ):
        errors.append("golden manifest corpus schema mismatch")

    if doc.get("corpus_version") != 1:
        errors.append("golden manifest version mismatch")

    if doc.get("base_sha") != BASE:
        errors.append("golden manifest base SHA mismatch")

    if doc.get("hash_mode") != "raw-bytes-sha256":
        errors.append("golden manifest hash mode mismatch")

    artifact_rows = doc.get("artifacts")
    if not isinstance(artifact_rows, list):
        artifact_rows = []
        errors.append("manifest artifacts must be a list")

    artifact_map = {
        row.get("path"): row
        for row in artifact_rows
        if isinstance(row, dict)
        and isinstance(row.get("path"), str)
    }

    if set(artifact_map) != ARTIFACTS:
        errors.append("golden artifact identity mismatch")

    if check_files:
        for rel in sorted(ARTIFACTS):
            row = artifact_map.get(rel)
            if not isinstance(row, dict):
                continue

            path = ROOT / rel
            if not path.is_file():
                errors.append(f"golden artifact missing: {rel}")
            elif row.get("sha256") != sha256_file(rel):
                errors.append(f"golden artifact hash mismatch: {rel}")

    contract_rows = doc.get("contracts")
    if not isinstance(contract_rows, list):
        contract_rows = []
        errors.append("manifest contracts must be a list")

    contract_map = {
        row.get("path"): row
        for row in contract_rows
        if isinstance(row, dict)
        and isinstance(row.get("path"), str)
    }

    if set(contract_map) != CONTRACTS:
        errors.append("golden contract identity mismatch")

    if check_files:
        for rel in sorted(CONTRACTS):
            row = contract_map.get(rel)
            if not isinstance(row, dict):
                continue

            path = ROOT / rel
            if not path.is_file():
                errors.append(f"golden contract missing: {rel}")
            elif row.get("sha256") != sha256_file(rel):
                errors.append(f"golden contract hash mismatch: {rel}")

    producer_rows = doc.get("producers")
    if not isinstance(producer_rows, list):
        producer_rows = []
        errors.append("manifest producers must be a list")

    producer_map = {
        row.get("path"): row
        for row in producer_rows
        if isinstance(row, dict)
        and isinstance(row.get("path"), str)
    }

    if set(producer_map) != PRODUCERS:
        errors.append("golden producer identity mismatch")

    for rel in sorted(PRODUCERS):
        row = producer_map.get(rel)
        if not isinstance(row, dict):
            continue

        expected = git_blob(BASE, rel)

        if expected is None:
            errors.append(f"cannot resolve base producer blob: {rel}")
        elif row.get("base_blob") != expected:
            errors.append(f"producer base blob mismatch: {rel}")

    legacy_rows = doc.get("legacy_blob_bindings")
    if not isinstance(legacy_rows, list):
        legacy_rows = []
        errors.append("manifest legacy_blob_bindings must be a list")

    legacy_map = {
        row.get("path"): row
        for row in legacy_rows
        if isinstance(row, dict)
        and isinstance(row.get("path"), str)
    }

    if set(legacy_map) != LEGACY_BLOB_BINDINGS:
        errors.append("legacy Git blob binding identity mismatch")

    for rel in sorted(LEGACY_BLOB_BINDINGS):
        row = legacy_map.get(rel)
        if not isinstance(row, dict):
            continue

        expected = git_blob(BASE, rel)
        head_blob = git_blob("HEAD", rel)

        if row.get("binding_mode") != (
            "git-blob-sha1-cross-platform"
        ):
            errors.append(
                f"legacy binding mode mismatch: {rel}"
            )

        if expected is None:
            errors.append(
                f"cannot resolve legacy base blob: {rel}"
            )
        elif row.get("base_blob") != expected:
            errors.append(
                f"legacy manifest base blob mismatch: {rel}"
            )

        if head_blob is None:
            errors.append(
                f"cannot resolve legacy HEAD blob: {rel}"
            )
        elif expected is not None and head_blob != expected:
            errors.append(
                f"legacy candidate blob changed: {rel}"
            )

    return errors


def golden_manifest_errors() -> list[str]:
    try:
        doc = read_json(GOLDEN_MANIFEST)
    except (ValueError, json.JSONDecodeError) as exc:
        return [str(exc)]

    return validate_manifest_doc(
        doc,
        check_files=True,
    )


def contract_binding_errors() -> list[str]:
    errors: list[str] = []

    text = (ROOT / CONTRACT).read_text(encoding="utf-8")
    manifest = read_json(GOLDEN_MANIFEST)
    workflow = (ROOT / WORKFLOW).read_text(encoding="utf-8")

    for token in (
        BASE,
        "neoeng.dcore.golden-corpus.v1",
        "neoeng.dcore.golden-corpus-manifest.v1",
        "world_initial.bin",
        "world_after_transition.bin",
        "world_after_rollback_replay.bin",
        "evidence_envelope.bin",
        "canonical_digest_mismatch",
        "DEV-0011",
        "git-blob-sha1-cross-platform",
        "Property-based/model-based testing belongs to EV-04",
        "Semantic fuzzing/corruption campaigns belong to EV-05",
    ):
        if token not in text:
            errors.append(f"golden contract missing token: {token}")

    contract_map = {
        row.get("path"): row.get("sha256")
        for row in manifest.get("contracts", [])
        if isinstance(row, dict)
    }

    if contract_map.get(CONTRACT.as_posix()) != sha256_file(
        CONTRACT.as_posix()
    ):
        errors.append("golden contract is not hash-bound by manifest")

    if "--emit" in workflow:
        errors.append("qualifying workflow must never invoke --emit")

    if BRANCH not in workflow:
        errors.append("qualifying workflow branch identity mismatch")

    for token in (
        "name: CS020 validation",
        "name: CS020 cross-compiler golden corpus",
        "Compare cross-compiler golden corpus",
    ):
        if token not in workflow:
            errors.append(f"qualifying workflow missing token: {token}")

    return errors


def non_effects_errors() -> list[str]:
    errors: list[str] = []

    changed = changed_paths()

    forbidden_prefixes = (
        "include/neoeng/core/",
        "src/",
        "modules/host_sdk/",
    )

    for rel in sorted(changed):
        if rel.startswith(forbidden_prefixes):
            errors.append(f"product runtime/API path changed: {rel}")

    roadmap = read_json(ROADMAP)
    stages = {
        row.get("stage_id"): row
        for row in roadmap.get("stages", [])
        if isinstance(row, dict)
    }

    for sid in ("EV-04", "EV-05"):
        if stages.get(sid, {}).get("status") != "not_started":
            errors.append(f"{sid} lifecycle changed")

    if roadmap.get("release_authorized") is not False:
        errors.append("release authorization changed")

    prior_cmake = git_show(BASE, "CMakeLists.txt")
    if prior_cmake is None:
        errors.append("cannot read base CMakeLists.txt")
    else:
        current_cmake = (
            ROOT / "CMakeLists.txt"
        ).read_text(encoding="utf-8")

        if current_cmake.count(CMAKE_INSERT) != 1:
            errors.append("golden CMake insertion cardinality mismatch")
        else:
            restored = current_cmake.replace(
                CMAKE_INSERT,
                "",
                1,
            )

            if restored.encode("utf-8") != prior_cmake:
                errors.append(
                    "CMake changed beyond golden-test registration"
                )

    return errors


def self_test_errors() -> list[str]:
    failures: list[str] = []

    corpus = read_json(CORPUS)
    weak_corpus = copy.deepcopy(corpus)
    weak_corpus["base_sha"] = "0" * 40

    if not validate_corpus_doc(weak_corpus):
        failures.append("negative corpus base mutation accepted")

    manifest = read_json(GOLDEN_MANIFEST)
    weak_manifest = copy.deepcopy(manifest)
    weak_manifest["artifacts"] = weak_manifest.get(
        "artifacts",
        [],
    )[:-1]

    if not validate_manifest_doc(
        weak_manifest,
        check_files=False,
    ):
        failures.append("negative manifest artifact removal accepted")

    weak_legacy_manifest = copy.deepcopy(manifest)
    weak_legacy_manifest["legacy_blob_bindings"] = (
        weak_legacy_manifest.get(
            "legacy_blob_bindings",
            [],
        )[:-1]
    )

    if not validate_manifest_doc(
        weak_legacy_manifest,
        check_files=False,
    ):
        failures.append(
            "negative legacy Git blob binding removal accepted"
        )

    roadmap = read_json(ROADMAP)
    requirements = read_json(REQUIREMENTS)

    weak_roadmap = copy.deepcopy(roadmap)

    for row in weak_roadmap.get("stages", []):
        if isinstance(row, dict) and row.get("stage_id") == "EV-04":
            row["status"] = "in_progress"

    if not validate_planning_docs(
        weak_roadmap,
        requirements,
    ):
        failures.append("negative EV-04 advancement accepted")

    return failures


MODES = {
    "authority": authority_errors,
    "planning": planning_errors,
    "cs000k-preservation": cs000k_preservation_errors,
    "workflow-retirement": workflow_retirement_errors,
    "scope": scope_errors,
    "references": reference_errors,
    "corpus-schema": corpus_schema_errors,
    "golden-manifest": golden_manifest_errors,
    "contract-binding": contract_binding_errors,
    "non-effects": non_effects_errors,
}


def main() -> int:
    parser = argparse.ArgumentParser()

    parser.add_argument("--self-test", action="store_true")

    for name in MODES:
        parser.add_argument(
            f"--{name}",
            action="store_true",
        )

    parser.add_argument("--all", action="store_true")

    args = parser.parse_args()

    selected: list[tuple[str, Any]] = []

    if args.self_test:
        selected.append(
            ("self-test", self_test_errors)
        )

    for name, function in MODES.items():
        if args.all or getattr(
            args,
            name.replace("-", "_"),
        ):
            selected.append((name, function))

    if not selected:
        parser.error("select at least one verification mode")

    errors: list[str] = []

    for name, function in selected:
        current = function()

        if current:
            for error in current:
                errors.append(f"{name}: {error}")
        else:
            print(f"CS020 VERIFIER: PASS {name}")

    if errors:
        for error in errors:
            print(f"CS020 VERIFIER: FAIL {error}")
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
