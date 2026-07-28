#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import functools
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_VERSION = "1.14.0"
FILES = {
    "index": Path("audit/SOURCE_OF_TRUTH_INDEX.json"),
    "requirements": Path("audit/PRODUCT_REQUIREMENTS_TRACEABILITY.json"),
    "claims": Path("audit/PRODUCT_CLAIMS_LEDGER.json"),
    "responsibility": Path("audit/PRODUCT_SCOPE_RESPONSIBILITY_MATRIX.json"),
    "backlog": Path("audit/PRODUCT_CLOSURE_BACKLOG.json"),
    "assurance": Path("audit/PRODUCT_ASSURANCE_MATRIX.json"),
    "campaigns": Path("audit/PRODUCT_TEST_CAMPAIGNS.json"),
    "capabilities": Path("audit/PRODUCT_CAPABILITY_SURFACE.json"),
    "release": Path("audit/RELEASE_ASSURANCE_POLICY.json"),
    "deferred": Path("audit/DEFERRED_VALIDATION_GATES.json"),
}
REPORT = ROOT / "audit/PRODUCT_CONTRACT_VALIDATION.json"

CLAIM_STATUSES = {
    "implemented", "verified", "native_qualified", "independently_audited",
    "certified", "planned", "unsupported", "removed",
}
REQUIREMENT_STATUSES = {
    "complete", "partial", "planned", "externally_blocked",
    "accepted_boundary", "out_of_scope", "removed",
}
RESPONSIBILITY_CLASSES = {
    "core_mandatory", "product_module_mandatory", "host_responsibility",
    "deployment_responsibility", "native_qualification", "external_assurance",
    "optional_vertical", "accepted_boundary", "out_of_scope",
}
BACKLOG_CATEGORIES = {
    "internal_mandatory", "native_validation", "external_assurance",
    "host_responsibility", "deployment_responsibility", "optional_vertical",
    "accepted_boundary", "out_of_scope",
}
ELEVATED_CLAIMS = {"verified", "native_qualified", "independently_audited", "certified"}


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"missing required file: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"root JSON value must be an object: {path}")
    return value


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def cmake_version(root: Path) -> str:
    text = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(NeoEngDCore VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES C CXX\)", text)
    if not match:
        raise ValueError("unable to resolve NeoEngDCore project version from CMakeLists.txt")
    return match.group(1)


@functools.lru_cache(maxsize=1)
def cmake_registry_text(root: Path) -> str:
    excluded = {"build", ".git", ".deps"}
    return "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in root.rglob("CMakeLists.txt")
        if not excluded.intersection(path.relative_to(root).parts)
    )


def evidence_reference_exists(root: Path, reference: str) -> bool:
    if not reference or not isinstance(reference, str):
        return False
    candidate = root / reference
    if candidate.exists():
        return True
    # Simple identifiers are allowed only when they are registered in CMake/CTest.
    if "/" not in reference and "\\" not in reference and "." not in reference:
        return reference in cmake_registry_text(root)
    return False


def load_documents(root: Path) -> tuple[dict[str, dict[str, Any]], list[str]]:
    docs: dict[str, dict[str, Any]] = {}
    errors: list[str] = []
    for key, rel in FILES.items():
        try:
            docs[key] = load_json(root / rel)
        except ValueError as exc:
            errors.append(str(exc))
    return docs, errors


def validate_documents(root: Path, docs: dict[str, dict[str, Any]]) -> list[str]:
    errors: list[str] = []
    if set(docs) != set(FILES):
        return ["not all required governance documents were loaded"]

    expected_schemas = {
        "index": "neoeng.dcore.source-of-truth-index.v1",
        "requirements": "neoeng.dcore.product-requirements-traceability.v1",
        "claims": "neoeng.dcore.product-claims-ledger.v1",
        "responsibility": "neoeng.dcore.product-scope-responsibility-matrix.v1",
        "backlog": "neoeng.dcore.product-closure-backlog.v1",
        "assurance": "neoeng.dcore.product-assurance-matrix.v1",
        "campaigns": "neoeng.dcore.product-test-campaigns.v1",
        "capabilities": "neoeng.dcore.product-capability-surface.v1",
        "release": "neoeng.dcore.release-assurance-policy.v1",
        "deferred": "neoeng.dcore.deferred-validation-gates.v1",
    }
    for key, schema in expected_schemas.items():
        if docs[key].get("schema") != schema:
            errors.append(f"schema mismatch for {key}: expected {schema}")
        if docs[key].get("project_version") != EXPECTED_VERSION:
            errors.append(f"project_version mismatch in {key}")

    try:
        if cmake_version(root) != EXPECTED_VERSION:
            errors.append("CMake project version does not match governance baseline")
    except ValueError as exc:
        errors.append(str(exc))
    try:
        if load_json(root / "vcpkg.json").get("version-string") != EXPECTED_VERSION:
            errors.append("vcpkg version-string does not match governance baseline")
    except ValueError as exc:
        errors.append(str(exc))

    index = docs["index"]
    primary = index.get("primary_document")
    if primary != "docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md":
        errors.append("unexpected primary source-of-truth document")
    if index.get("required_for_every_changeset") is not True:
        errors.append("source of truth is not mandatory for every ChangeSet")
    precedence = index.get("precedence")
    if not isinstance(precedence, list) or len(precedence) < 10:
        errors.append("precedence must contain the complete normative hierarchy")
    else:
        if precedence[0] != primary:
            errors.append("primary source of truth must be first in precedence")
        if len(precedence) != len(set(precedence)):
            errors.append("precedence contains duplicate entries")
        for rel in precedence[:10]:
            if not (root / rel).exists():
                errors.append(f"precedence entry does not exist: {rel}")
    scope = str(index.get("scope_statement", "")).lower()
    for token in ("independent product", "not the five-year engine program"):
        if token not in scope:
            errors.append(f"scope statement missing: {token}")

    primary_path = root / str(primary)
    if not primary_path.is_file():
        errors.append("primary source-of-truth file is missing")
        primary_text = ""
    else:
        primary_text = primary_path.read_text(encoding="utf-8")
    for token in (
        "produto independente",
        "nao e a implementacao do programa completo de cinco anos",
        "Nenhum ChangeSet pode declarar capacidade concluida com lacuna interna obrigatoria conhecida",
        "scripts/verify_product_contract.py",
        "scripts/verify_product_assurance.py",
    ):
        if token not in primary_text:
            errors.append(f"primary source of truth missing normative token: {token}")
    if "baseline 1.14.0" not in primary_text.lower():
        errors.append("source of truth does not identify the active 1.14.0 baseline")
    if "cs009" not in primary_text.lower() or "evidencia ecs" not in primary_text.lower():
        errors.append("source of truth does not record the CS009 ECS closure")

    req_rows = docs["requirements"].get("requirements")
    if not isinstance(req_rows, list) or len(req_rows) != 36:
        errors.append("requirements ledger must contain exactly 36 requirements")
        req_rows = []
    req_ids: set[str] = set()
    open_mandatory_stages: list[tuple[str, str]] = []
    for row in req_rows:
        if not isinstance(row, dict):
            errors.append("requirement row must be an object")
            continue
        rid = row.get("requirement_id")
        if not isinstance(rid, str) or not re.fullmatch(r"DCORE-[A-Z]+-[0-9]{3}", rid):
            errors.append(f"invalid requirement id: {rid!r}")
            continue
        if rid in req_ids:
            errors.append(f"duplicate requirement id: {rid}")
        req_ids.add(rid)
        status = row.get("status")
        resp = row.get("responsibility")
        stage = row.get("closure_stage")
        if status not in REQUIREMENT_STATUSES:
            errors.append(f"invalid requirement status {status!r}: {rid}")
        if resp not in RESPONSIBILITY_CLASSES:
            errors.append(f"invalid requirement responsibility {resp!r}: {rid}")
        if not isinstance(stage, str) or not stage:
            errors.append(f"requirement missing closure_stage: {rid}")
        impl = row.get("implementation")
        evidence = row.get("tests_or_evidence")
        if not isinstance(impl, list) or not isinstance(evidence, list):
            errors.append(f"implementation/evidence must be lists: {rid}")
            continue
        if status == "complete":
            if not impl or not evidence:
                errors.append(f"complete requirement lacks implementation or evidence: {rid}")
            for rel in impl:
                if not isinstance(rel, str) or not (root / rel).exists():
                    errors.append(f"complete requirement implementation path missing: {rid}: {rel}")
            for ref in evidence:
                if not evidence_reference_exists(root, ref):
                    errors.append(f"complete requirement evidence missing/unregistered: {rid}: {ref}")
        if resp in {"core_mandatory", "product_module_mandatory"} and status in {"partial", "planned"}:
            open_mandatory_stages.append((rid, str(stage)))

    claim_rows = docs["claims"].get("claims")
    if not isinstance(claim_rows, list) or len(claim_rows) != 20:
        errors.append("claims ledger must contain exactly 20 claims")
        claim_rows = []
    claim_ids: set[str] = set()
    for row in claim_rows:
        cid = row.get("claim_id") if isinstance(row, dict) else None
        if not isinstance(cid, str) or not re.fullmatch(r"CLAIM-[A-Z0-9-]+-[0-9]{3}", cid):
            errors.append(f"invalid claim id: {cid!r}")
            continue
        if cid in claim_ids:
            errors.append(f"duplicate claim id: {cid}")
        claim_ids.add(cid)
        status = row.get("status")
        evidence = row.get("evidence")
        if status not in CLAIM_STATUSES:
            errors.append(f"invalid claim status {status!r}: {cid}")
        if not isinstance(evidence, list):
            errors.append(f"claim evidence must be a list: {cid}")
            evidence = []
        if status in ELEVATED_CLAIMS and not evidence:
            errors.append(f"elevated claim lacks evidence: {cid}")
        for ref in evidence:
            if not evidence_reference_exists(root, ref):
                errors.append(f"claim evidence missing/unregistered: {cid}: {ref}")
        if not row.get("scope") or not row.get("public_use"):
            errors.append(f"claim missing scope/public_use: {cid}")

    claims_by_id = {r.get("claim_id"): r for r in claim_rows if isinstance(r, dict)}
    xarch = claims_by_id.get("CLAIM-XARCH-001", {})
    if xarch.get("status") not in {"planned", "unsupported"}:
        errors.append("cross-architecture claim is elevated without native ARM64 evidence")
    prod = claims_by_id.get("CLAIM-PROD-READY-001", {})
    if prod.get("status") not in {"unsupported", "planned"}:
        errors.append("production-readiness claim is elevated while mandatory work remains")
    sign = claims_by_id.get("CLAIM-SIGN-001", {})
    if sign.get("status") not in {"unsupported", "planned", "removed"}:
        errors.append("asymmetric signature provider claim exceeds implementation")
    if sign.get("status") == "removed" and (
        "not included" not in str(sign.get("statement", "")).lower()
        or sign.get("public_use") != "original_positive_claim_prohibited"
    ):
        errors.append("removed asymmetric signature claim is not fail-closed")

    classes = docs["responsibility"].get("classes")
    if not isinstance(classes, dict):
        errors.append("responsibility classes must be an object")
    else:
        missing = sorted(RESPONSIBILITY_CLASSES - set(classes))
        if missing:
            errors.append(f"responsibility classes missing: {', '.join(missing)}")

    backlog_rows = docs["backlog"].get("items")
    if not isinstance(backlog_rows, list) or len(backlog_rows) != 41:
        errors.append("closure backlog must contain exactly 41 known limitations")
        backlog_rows = []
    limitation_ids: set[str] = set()
    open_internal_stages: set[str] = set()
    for row in backlog_rows:
        lid = row.get("limitation_id") if isinstance(row, dict) else None
        if not isinstance(lid, str) or not re.fullmatch(r"LIM-[0-9]{3}", lid):
            errors.append(f"invalid limitation id: {lid!r}")
            continue
        if lid in limitation_ids:
            errors.append(f"duplicate limitation id: {lid}")
        limitation_ids.add(lid)
        resp = row.get("responsibility")
        category = row.get("category")
        status = row.get("status")
        if resp not in RESPONSIBILITY_CLASSES:
            errors.append(f"invalid limitation responsibility {resp!r}: {lid}")
        if category not in BACKLOG_CATEGORIES:
            errors.append(f"invalid limitation category {category!r}: {lid}")
        if not row.get("normative_decision"):
            errors.append(f"limitation lacks normative decision: {lid}")
        if category == "internal_mandatory":
            if status not in {"open", "closed"}:
                errors.append(f"internal mandatory limitation has invalid status: {lid}")
            if status == "open":
                stage = row.get("closure_stage")
                if not isinstance(stage, str) or not stage.startswith("CS"):
                    errors.append(f"open internal limitation lacks ChangeSet closure stage: {lid}")
                else:
                    open_internal_stages.add(stage)
            else:
                evidence = row.get("closure_evidence")
                if not isinstance(evidence, list) or not evidence:
                    errors.append(f"closed internal limitation lacks closure evidence: {lid}")
                else:
                    for ref in evidence:
                        if not evidence_reference_exists(root, ref):
                            errors.append(f"closed limitation evidence missing: {lid}: {ref}")

    for rid, stage in open_mandatory_stages:
        if rid == "DCORE-ACCEPT-001" and stage == "CS015":
            closure_plan = (root / "docs/governance/PRODUCT_CLOSURE_PLAN.md").read_text(encoding="utf-8", errors="replace")
            if "CS015 - Aceitacao final" not in closure_plan:
                errors.append("final acceptance requirement lacks CS015 closure-plan stage")
            continue
        if stage not in open_internal_stages:
            errors.append(f"open mandatory requirement has no matching internal backlog stage: {rid} -> {stage}")

    gates = docs["deferred"].get("gates")
    if not isinstance(gates, list) or not gates:
        errors.append("deferred validation ledger must contain gates")
        gates = []
    gate_ids = {g.get("gate_id") for g in gates if isinstance(g, dict)}
    for required_gate in ("NATIVE-ARM64-001", "PROFILE-P1-NVIDIA-001", "CRYPTO-ASSURANCE-001", "ECS-SCOPE-COMPLETE-001"):
        if required_gate not in gate_ids:
            errors.append(f"required deferred gate missing: {required_gate}")
    for gate in gates:
        if gate.get("blocking_for_current_changeset") is not False:
            errors.append(f"deferred gate incorrectly blocks the current implementation ChangeSet: {gate.get('gate_id')}")
        if gate.get("category") == "native_validation_pending" and gate.get("blocking_for_profile_qualification") is not True:
            errors.append(f"native gate does not block profile qualification: {gate.get('gate_id')}")

    # CS009 closes implementation scope only; native qualification remains separate.
    req_by_id = {r.get("requirement_id"): r for r in req_rows if isinstance(r, dict)}
    for rid in ("DCORE-ECS-002", "DCORE-ECS-003", "DCORE-ECS-004", "DCORE-GOV-001"):
        if req_by_id.get(rid, {}).get("status") != "complete":
            errors.append(f"CS009 required closure is not complete: {rid}")
    backlog_by_id = {r.get("limitation_id"): r for r in backlog_rows if isinstance(r, dict)}
    for lid in ("LIM-001", "LIM-019"):
        row = backlog_by_id.get(lid, {})
        if row.get("status") != "closed" or not row.get("closure_evidence"):
            errors.append(f"CS009 limitation closure is incomplete: {lid}")
    ecs_gate = next((g for g in gates if g.get("gate_id") == "ECS-SCOPE-COMPLETE-001"), {})
    if ecs_gate.get("category") != "native_validation_pending" or ecs_gate.get("blocking_for_profile_qualification") is not True:
        errors.append("ECS implementation closure does not preserve the native P1 qualification gate")

    # CS010 closes the internal reference-distribution scope without elevating
    # native ARM64 or production-transport claims.
    for rid in ("DCORE-DIST-001", "DCORE-NET-002"):
        if req_by_id.get(rid, {}).get("status") != "complete":
            errors.append(f"CS010 required closure is not complete: {rid}")
    for lid in ("LIM-005", "LIM-020", "LIM-023"):
        row = backlog_by_id.get(lid, {})
        if row.get("status") != "closed" or not row.get("closure_evidence"):
            errors.append(f"CS010 limitation closure is incomplete: {lid}")
    distributed_claim = claims_by_id.get("CLAIM-DIST-001", {})
    if (
        distributed_claim.get("status") != "verified"
        or "loopback" not in str(distributed_claim.get("scope", "")).lower()
    ):
        errors.append("CS010 distributed claim exceeds or omits its reference-loopback scope")

    return errors


def deterministic_report(root: Path, docs: dict[str, dict[str, Any]]) -> dict[str, Any]:
    req_rows = docs["requirements"]["requirements"]
    claim_rows = docs["claims"]["claims"]
    backlog_rows = docs["backlog"]["items"]
    open_req = [
        r["requirement_id"] for r in req_rows
        if r["responsibility"] in {"core_mandatory", "product_module_mandatory"}
        and r["status"] in {"partial", "planned"}
    ]
    open_lim = [r["limitation_id"] for r in backlog_rows if r["category"] == "internal_mandatory" and r["status"] == "open"]
    return {
        "schema": "neoeng.dcore.product-contract-validation.v1",
        "project_version": EXPECTED_VERSION,
        "status": "passed",
        "source_of_truth_files": {FILES[k].as_posix(): sha256(root / FILES[k]) for k in sorted(FILES)},
        "counts": {
            "requirements": len(req_rows),
            "claims": len(claim_rows),
            "known_limitations": len(backlog_rows),
            "open_internal_requirements": len(open_req),
            "open_internal_limitations": len(open_lim),
        },
        "open_internal_requirement_ids": open_req,
        "open_internal_limitation_ids": open_lim,
        "commercial_ready": False,
        "reason": "CS013 is closed by immutable Windows and Linux GCC/Clang evidence with explicit provider, trust and hardware non-claims; seven mandatory requirements in CS014/CS015, native ARM64/profile qualification and external assurance remain open.",
    }


def run_self_test(root: Path, docs: dict[str, dict[str, Any]]) -> list[str]:
    failures: list[str] = []
    mutations: list[tuple[str, Any]] = []

    def add(name: str, mutate):
        mutated = copy.deepcopy(docs)
        mutate(mutated)
        mutations.append((name, mutated))

    add("verified claim without evidence", lambda d: d["claims"]["claims"][0].update(status="verified", evidence=[]))
    add("duplicate requirement id", lambda d: d["requirements"]["requirements"][1].update(requirement_id=d["requirements"]["requirements"][0]["requirement_id"]))
    add("remove ECS closure evidence", lambda d: next(r for r in d["requirements"]["requirements"] if r["requirement_id"] == "DCORE-ECS-002").update(tests_or_evidence=[]))
    add("close limitation without evidence", lambda d: next(r for r in d["backlog"]["items"] if r["limitation_id"] == "LIM-001").update(closure_evidence=[]))
    add("elevate ARM64 claim", lambda d: next(r for r in d["claims"]["claims"] if r["claim_id"] == "CLAIM-XARCH-001").update(status="verified"))
    add("wrong baseline version", lambda d: d["index"].update(project_version="1.8.0"))
    add(
        "missing backlog stage",
        lambda d: next(
            row
            for row in d["backlog"]["items"]
            if row.get("category") == "internal_mandatory"
            and row.get("status") == "open"
        ).update(closure_stage=""),
    )

    for name, mutated in mutations:
        if not validate_documents(root, mutated):
            failures.append(f"self-test failed to reject mutation: {name}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify NeoEng D-Core normative product contract.")
    parser.add_argument("--write-report", action="store_true")
    parser.add_argument("--check-report", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    docs, load_errors = load_documents(ROOT)
    if load_errors:
        print("\n".join(load_errors))
        return 1
    if args.self_test:
        failures = run_self_test(ROOT, docs)
        if failures:
            print("\n".join(failures))
            return 1
        print("OK: 7 fail-closed product-contract mutations were rejected.")
        return 0

    errors = validate_documents(ROOT, docs)
    if errors:
        print("\n".join(errors))
        return 1
    report = deterministic_report(ROOT, docs)
    serialized = json.dumps(report, indent=2, ensure_ascii=False) + "\n"
    if args.write_report:
        REPORT.write_text(serialized, encoding="utf-8")
    if args.check_report:
        if not REPORT.is_file() or REPORT.read_text(encoding="utf-8") != serialized:
            print("stored product-contract validation report diverges from recalculation")
            return 1
    print(
        "OK: source of truth verified; "
        f"requirements={report['counts']['requirements']}, "
        f"claims={report['counts']['claims']}, "
        f"limitations={report['counts']['known_limitations']}, "
        f"open_internal_requirements={report['counts']['open_internal_requirements']}."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
