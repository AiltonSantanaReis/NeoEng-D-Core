#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import functools
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_VERSION = "1.13.0"
REQUIREMENTS = ROOT / "audit/PRODUCT_REQUIREMENTS_TRACEABILITY.json"
MATRIX = ROOT / "audit/PRODUCT_ASSURANCE_MATRIX.json"
CAMPAIGNS = ROOT / "audit/PRODUCT_TEST_CAMPAIGNS.json"
REPORT = ROOT / "audit/PRODUCT_ASSURANCE_VALIDATION.json"
ALLOWED_ASSURANCE = {"evidenced", "planned_or_partial", "external_or_native_pending", "boundary_accepted"}
MANDATORY = {"core_mandatory", "product_module_mandatory"}
FUTURE_CLASSES = {
    "positive", "negative", "adversarial", "fault_injection", "recovery", "long_run",
    "cross_compiler", "cross_architecture_if_applicable", "independent_verification", "evidence_integrity",
}
COMPLETE_CLASSES = {"positive", "negative", "fail_closed", "regression", "reproducibility"}
MANDATORY_ARTIFACTS = {
    "source-identity", "build-identity", "configuration", "raw-results",
    "result-summary", "sha256-manifest", "independent-verification", "limitations",
}


def load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"root must be object: {path}")
    return value


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


@functools.lru_cache(maxsize=1)
def cmake_registry_text() -> str:
    excluded = {"build", ".git", ".deps"}
    return "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for path in ROOT.rglob("CMakeLists.txt")
        if not excluded.intersection(path.relative_to(ROOT).parts)
    )


def ref_exists(ref: str) -> bool:
    if (ROOT / ref).exists():
        return True
    if "/" not in ref and "\\" not in ref and "." not in ref:
        return ref in cmake_registry_text()
    return False


def validate(req: dict[str, Any], matrix: dict[str, Any], campaigns: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    schemas = {
        "requirements": (req, "neoeng.dcore.product-requirements-traceability.v1"),
        "matrix": (matrix, "neoeng.dcore.product-assurance-matrix.v1"),
        "campaigns": (campaigns, "neoeng.dcore.product-test-campaigns.v1"),
    }
    for name, (doc, schema) in schemas.items():
        if doc.get("schema") != schema:
            errors.append(f"{name} schema mismatch")
        if doc.get("project_version") != EXPECTED_VERSION:
            errors.append(f"{name} project version mismatch")

    policy = matrix.get("policy")
    if not isinstance(policy, dict):
        errors.append("assurance policy missing")
        policy = {}
    for flag in (
        "meta_validation_is_not_capability_proof",
        "complete_requires_existing_evidence_paths",
        "open_mandatory_requires_future_test_classes",
        "native_and_external_results_cannot_be_inferred",
    ):
        if policy.get(flag) is not True:
            errors.append(f"assurance policy must enable {flag}")

    campaign_policy = campaigns.get("policy")
    if not isinstance(campaign_policy, dict):
        errors.append("test campaign policy missing")
        campaign_policy = {}
    for flag in (
        "all_requirements_must_be_covered",
        "planned_campaign_is_not_execution_evidence",
        "raw_evidence_and_independent_verification_required",
        "native_or_external_results_cannot_be_inferred",
    ):
        if campaign_policy.get(flag) is not True:
            errors.append(f"test campaign policy must enable {flag}")

    req_rows = req.get("requirements")
    matrix_rows = matrix.get("requirements")
    campaign_rows = campaigns.get("campaigns")
    if not isinstance(req_rows, list) or not isinstance(matrix_rows, list) or not isinstance(campaign_rows, list):
        return errors + ["requirements, assurance rows and campaigns must be lists"]

    req_by = {r.get("requirement_id"): r for r in req_rows if isinstance(r, dict)}
    matrix_by = {r.get("requirement_id"): r for r in matrix_rows if isinstance(r, dict)}
    if len(req_by) != len(req_rows):
        errors.append("requirements contain duplicate or invalid ids")
    if len(matrix_by) != len(matrix_rows):
        errors.append("assurance matrix contains duplicate or invalid ids")
    if set(req_by) != set(matrix_by):
        errors.append("assurance matrix does not cover exactly every requirement")

    for rid in sorted(set(req_by) & set(matrix_by)):
        r = req_by[rid]
        a = matrix_by[rid]
        expected_fields = {
            "requirement_status": r["status"],
            "responsibility": r["responsibility"],
            "closure_stage": r["closure_stage"],
        }
        for field, expected in expected_fields.items():
            if a.get(field) != expected:
                errors.append(f"assurance field mismatch {rid}: {field}")
        status = r["status"]
        responsibility = r["responsibility"]
        assurance_status = a.get("assurance_status")
        classes = a.get("required_test_classes")
        current = a.get("current_evidence")
        if assurance_status not in ALLOWED_ASSURANCE:
            errors.append(f"invalid assurance status: {rid}: {assurance_status}")
        if not isinstance(classes, list) or not classes or len(classes) != len(set(classes)):
            errors.append(f"required test classes missing or duplicated: {rid}")
            classes = []
        if not isinstance(current, list):
            errors.append(f"current evidence must be a list: {rid}")
            current = []
        if current != r.get("tests_or_evidence", []):
            errors.append(f"current evidence diverges from requirements ledger: {rid}")
        if not a.get("closure_rule"):
            errors.append(f"closure rule missing: {rid}")

        if status == "complete":
            if assurance_status != "evidenced":
                errors.append(f"complete requirement is not evidenced: {rid}")
            if not COMPLETE_CLASSES.issubset(set(classes)):
                errors.append(f"complete requirement lacks minimum assurance classes: {rid}")
            if not current:
                errors.append(f"complete requirement lacks current evidence: {rid}")
            for ref in current:
                if not ref_exists(ref):
                    errors.append(f"complete assurance evidence missing or unregistered: {rid}: {ref}")
        elif responsibility in MANDATORY and status in {"partial", "planned"}:
            if assurance_status != "planned_or_partial":
                errors.append(f"open mandatory requirement assurance state invalid: {rid}")
            if not FUTURE_CLASSES.issubset(set(classes)):
                errors.append(f"open mandatory requirement lacks rigorous future test classes: {rid}")
        elif status == "externally_blocked" or responsibility in {"native_qualification", "external_assurance"}:
            if assurance_status != "external_or_native_pending":
                errors.append(f"native or external requirement assurance state invalid: {rid}")
            if not FUTURE_CLASSES.issubset(set(classes)):
                errors.append(f"native or external requirement lacks campaign test classes: {rid}")
        elif status in {"accepted_boundary", "out_of_scope", "removed"}:
            if assurance_status != "boundary_accepted":
                errors.append(f"boundary requirement assurance state invalid: {rid}")

    campaign_ids: set[str] = set()
    coverage: dict[str, list[str]] = {rid: [] for rid in req_by}
    for row in campaign_rows:
        if not isinstance(row, dict):
            errors.append("campaign row must be an object")
            continue
        cid = row.get("campaign_id")
        if not isinstance(cid, str) or not cid.startswith("TEST-"):
            errors.append(f"invalid campaign id: {cid!r}")
            continue
        if cid in campaign_ids:
            errors.append(f"duplicate campaign id: {cid}")
        campaign_ids.add(cid)
        ids = row.get("requirement_ids")
        classes = row.get("required_test_classes")
        artifacts = row.get("mandatory_artifacts")
        evidence = row.get("evidence")
        if not isinstance(ids, list) or not ids:
            errors.append(f"campaign has no requirements: {cid}")
            ids = []
        if not isinstance(classes, list) or not classes:
            errors.append(f"campaign has no test classes: {cid}")
            classes = []
        if not isinstance(artifacts, list) or not MANDATORY_ARTIFACTS.issubset(set(artifacts)):
            errors.append(f"campaign lacks mandatory artifacts: {cid}")
        if not isinstance(evidence, list):
            errors.append(f"campaign evidence must be a list: {cid}")
            evidence = []
        if row.get("native_or_external_results_may_be_inferred") is not False:
            errors.append(f"campaign allows inferred native or external results: {cid}")
        if not row.get("acceptance_rule"):
            errors.append(f"campaign acceptance rule missing: {cid}")
        status = row.get("status")
        if status == "planned" and evidence:
            errors.append(f"planned campaign must not present execution evidence: {cid}")
        if status != "planned":
            if not evidence:
                errors.append(f"executed campaign lacks evidence: {cid}")
            for ref in evidence:
                if not ref_exists(ref):
                    errors.append(f"campaign evidence missing: {cid}: {ref}")
        stage = row.get("closure_stage")
        for rid in ids:
            if rid not in req_by:
                errors.append(f"campaign references unknown requirement: {cid}: {rid}")
                continue
            coverage[rid].append(cid)
            if req_by[rid]["closure_stage"] != stage:
                errors.append(f"campaign stage mismatch: {cid}: {rid}")
        # Any campaign that covers an open mandatory requirement must carry the rigorous class set.
        if any(
            rid in req_by and req_by[rid]["responsibility"] in MANDATORY and req_by[rid]["status"] in {"partial", "planned"}
            for rid in ids
        ) and not FUTURE_CLASSES.issubset(set(classes)):
            errors.append(f"campaign for open mandatory work lacks full rigorous classes: {cid}")

    uncovered = sorted(rid for rid, cids in coverage.items() if not cids)
    if uncovered:
        errors.append(f"requirements not covered by a test campaign: {', '.join(uncovered)}")

    return errors


def report(req: dict[str, Any], matrix: dict[str, Any], campaigns: dict[str, Any]) -> dict[str, Any]:
    rows = matrix["requirements"]
    campaign_rows = campaigns["campaigns"]
    by_status: dict[str, int] = {}
    for row in rows:
        by_status[row["assurance_status"]] = by_status.get(row["assurance_status"], 0) + 1
    campaign_status: dict[str, int] = {}
    for row in campaign_rows:
        campaign_status[row["status"]] = campaign_status.get(row["status"], 0) + 1
    return {
        "schema": "neoeng.dcore.product-assurance-validation.v1",
        "project_version": EXPECTED_VERSION,
        "status": "passed",
        "requirements_sha256": sha256(REQUIREMENTS),
        "assurance_matrix_sha256": sha256(MATRIX),
        "test_campaigns_sha256": sha256(CAMPAIGNS),
        "requirements_covered": len(rows),
        "campaigns": len(campaign_rows),
        "assurance_status_counts": dict(sorted(by_status.items())),
        "campaign_status_counts": dict(sorted(campaign_status.items())),
        "capability_proof_completed_by_this_meta_test": False,
        "reason": "The verifier proves traceability, campaign coverage and fail-closed test obligations, not unexecuted technical campaigns.",
    }


def self_test(req: dict[str, Any], matrix: dict[str, Any], campaigns: dict[str, Any]) -> list[str]:
    failures: list[str] = []
    cases: list[tuple[str, dict[str, Any], dict[str, Any], dict[str, Any]]] = []

    def add(name, mutate):
        r, m, c = copy.deepcopy(req), copy.deepcopy(matrix), copy.deepcopy(campaigns)
        mutate(r, m, c)
        cases.append((name, r, m, c))

    add("remove a requirement row", lambda r, m, c: m["requirements"].pop())
    add("mark partial requirement evidenced", lambda r, m, c: next(x for x in m["requirements"] if x["requirement_status"] == "partial").update(assurance_status="evidenced"))
    add("drop adversarial future class", lambda r, m, c: next(x for x in m["requirements"] if x["assurance_status"] == "planned_or_partial")["required_test_classes"].remove("adversarial"))
    add("claim meta-test is capability proof", lambda r, m, c: m["policy"].update(meta_validation_is_not_capability_proof=False))
    add("diverge current evidence", lambda r, m, c: m["requirements"][0].update(current_evidence=[]))
    add("duplicate assurance row", lambda r, m, c: m["requirements"].append(copy.deepcopy(m["requirements"][0])))
    add("remove requirement from all campaigns", lambda r, m, c: [x["requirement_ids"].remove("DCORE-DIST-001") for x in c["campaigns"] if "DCORE-DIST-001" in x["requirement_ids"]])
    add("planned campaign presents evidence", lambda r, m, c: next(x for x in c["campaigns"] if x["status"] == "planned").update(evidence=["README.md"]))
    add("allow inferred native results", lambda r, m, c: c["campaigns"][0].update(native_or_external_results_may_be_inferred=True))

    for name, r, m, c in cases:
        if not validate(r, m, c):
            failures.append(f"self-test failed to reject mutation: {name}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify requirement-to-test assurance obligations and campaigns.")
    parser.add_argument("--write-report", action="store_true")
    parser.add_argument("--check-report", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        req, matrix, campaigns = load(REQUIREMENTS), load(MATRIX), load(CAMPAIGNS)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(exc)
        return 1
    if args.self_test:
        failures = self_test(req, matrix, campaigns)
        if failures:
            print("\n".join(failures))
            return 1
        print("OK: 9 fail-closed product-assurance mutations were rejected.")
        return 0
    errors = validate(req, matrix, campaigns)
    if errors:
        print("\n".join(errors))
        return 1
    value = report(req, matrix, campaigns)
    serialized = json.dumps(value, indent=2, ensure_ascii=False) + "\n"
    if args.write_report:
        REPORT.write_text(serialized, encoding="utf-8")
    if args.check_report:
        if not REPORT.is_file() or REPORT.read_text(encoding="utf-8") != serialized:
            print("stored product-assurance report diverges from recalculation")
            return 1
    print(
        f"OK: assurance matrix covers {value['requirements_covered']} requirements across "
        f"{value['campaigns']} campaigns; meta-test does not claim capability proof."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
