#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
VERSION = "1.14.0"
REPORT = ROOT / "audit/FINAL_ACCEPTANCE_VALIDATION.json"
CS014_EVIDENCE = (
    ROOT / "docs/changesets/014/evidence/github-actions-run-30367653644"
)
MANDATORY = {"core_mandatory", "product_module_mandatory"}
PUBLIC_STATUSES = {
    "implemented",
    "verified",
    "native_qualified",
    "independently_audited",
    "certified",
}

sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "scripts/qualification"))

import generate_public_claims as public_claims  # noqa: E402
import verify_product_assurance as product_assurance  # noqa: E402
import verify_product_contract as product_contract  # noqa: E402
import verify_release_assurance_evidence as release_evidence  # noqa: E402


def load(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"root must be an object: {path}")
    return value


def canonical_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def _rows(document: dict[str, Any], key: str) -> list[dict[str, Any]]:
    value = document.get(key)
    return [row for row in value if isinstance(row, dict)] if isinstance(value, list) else []


def evaluate(
    root: Path = ROOT,
    *,
    documents: dict[str, dict[str, Any]] | None = None,
    rendered_public_claims: str | None = None,
    previous_release_result: dict[str, Any] | None = None,
    final_evidence_result: dict[str, Any] | None = None,
) -> dict[str, Any]:
    errors: list[str] = []
    docs = documents
    if docs is None:
        docs, load_errors = product_contract.load_documents(root)
        errors.extend(load_errors)
    if set(docs) != set(product_contract.FILES):
        errors.append("not all final-acceptance documents were loaded")
        return _report("invalid", errors, [], [], 0, False, False)

    errors.extend(product_contract.validate_documents(root, docs))
    requirements = _rows(docs["requirements"], "requirements")
    assurance_rows = _rows(docs["assurance"], "requirements")
    campaigns = _rows(docs["campaigns"], "campaigns")
    claims = _rows(docs["claims"], "claims")
    limitations = _rows(docs["backlog"], "items")
    gates = _rows(docs["deferred"], "gates")

    errors.extend(
        product_assurance.validate(
            docs["requirements"], docs["assurance"], docs["campaigns"]
        )
    )

    req_by = {row.get("requirement_id"): row for row in requirements}
    assurance_by = {row.get("requirement_id"): row for row in assurance_rows}
    campaign_by = {row.get("campaign_id"): row for row in campaigns}
    acceptance_req = req_by.get("DCORE-ACCEPT-001", {})
    acceptance_assurance = assurance_by.get("DCORE-ACCEPT-001", {})
    acceptance_campaign = campaign_by.get("TEST-CS015-001", {})

    open_requirements = sorted(
        str(row.get("requirement_id"))
        for row in requirements
        if row.get("responsibility") in MANDATORY
        and row.get("status") in {"partial", "planned"}
    )
    open_limitations = sorted(
        str(row.get("limitation_id"))
        for row in limitations
        if row.get("category") == "internal_mandatory"
        and row.get("status") == "open"
    )
    if open_limitations:
        errors.append("open internal mandatory limitations remain")
    for row in requirements:
        if (
            row.get("responsibility") in MANDATORY
            and row.get("requirement_id") != "DCORE-ACCEPT-001"
            and row.get("status") != "complete"
        ):
            errors.append(
                f"mandatory prerequisite is not complete: {row.get('requirement_id')}"
            )
    for row in assurance_rows:
        if (
            row.get("responsibility") in MANDATORY
            and row.get("requirement_id") != "DCORE-ACCEPT-001"
            and row.get("assurance_status") != "evidenced"
        ):
            errors.append(
                f"mandatory prerequisite is not evidenced: {row.get('requirement_id')}"
            )

    if acceptance_req.get("status") == "planned":
        state = "closure_candidate"
        if open_requirements != ["DCORE-ACCEPT-001"]:
            errors.append("closure candidate must have only DCORE-ACCEPT-001 open")
        if (
            acceptance_req.get("implementation") != []
            or acceptance_req.get("tests_or_evidence") != []
            or acceptance_assurance.get("assurance_status") != "planned_or_partial"
            or acceptance_assurance.get("current_evidence") != []
            or acceptance_campaign.get("status") != "planned"
            or acceptance_campaign.get("evidence") != []
        ):
            errors.append("closure candidate contains premature acceptance evidence")
    elif acceptance_req.get("status") == "complete":
        state = "accepted"
        evidence = acceptance_req.get("tests_or_evidence")
        implementation = acceptance_req.get("implementation")
        campaign_evidence = acceptance_campaign.get("evidence")
        if open_requirements:
            errors.append("accepted state retains open mandatory requirements")
        if (
            not isinstance(implementation, list)
            or "audit/FINAL_ACCEPTANCE_POLICY.json" not in implementation
            or "scripts/verify_final_acceptance.py" not in implementation
        ):
            errors.append("accepted requirement lacks normative implementation")
        if (
            not isinstance(evidence, list)
            or len(evidence) != 1
            or evidence != campaign_evidence
            or acceptance_assurance.get("assurance_status") != "evidenced"
            or acceptance_assurance.get("current_evidence") != evidence
            or acceptance_campaign.get("status") == "planned"
        ):
            errors.append("accepted ledgers do not reference one common evidence package")
        elif isinstance(evidence[0], str):
            evidence_path = root / evidence[0]
            if final_evidence_result is None:
                try:
                    import verify_final_acceptance_evidence as final_evidence

                    final_evidence_result = final_evidence.verify(evidence_path)
                except (OSError, ValueError, json.JSONDecodeError) as exc:
                    errors.append(f"final evidence verification failed: {exc}")
            if (
                not isinstance(final_evidence_result, dict)
                or final_evidence_result.get("status") != "passed"
                or final_evidence_result.get("acceptance_state")
                not in {"closure_candidate", "accepted"}
            ):
                errors.append("final acceptance evidence package rejected")
    else:
        state = "invalid"
        errors.append("DCORE-ACCEPT-001 must be planned or complete")

    actual_public = (
        rendered_public_claims
        if rendered_public_claims is not None
        else (root / "docs/commercial/PUBLIC_CLAIMS.md").read_text(encoding="utf-8")
        if (root / "docs/commercial/PUBLIC_CLAIMS.md").is_file()
        else ""
    )
    expected_public = public_claims.render(docs["claims"])
    if actual_public != expected_public:
        errors.append("generated public claims are absent or divergent")
    leaked_claims = 0
    for row in claims:
        if (
            row.get("status") not in PUBLIC_STATUSES
            or str(row.get("public_use", "")).startswith("prohibited")
            or row.get("public_use") == "original_positive_claim_prohibited"
        ) and str(row.get("statement", "")) in actual_public:
            leaked_claims += 1
            errors.append(f"unsupported or prohibited public claim leaked: {row.get('claim_id')}")

    if previous_release_result is None:
        previous_release_result = release_evidence.verify(CS014_EVIDENCE)
    prior_release_verified = (
        isinstance(previous_release_result, dict)
        and previous_release_result.get("status") == "passed"
        and previous_release_result.get("external_signed_attestations_verified") is True
        and previous_release_result.get("commercial_product_complete") is False
    )
    if not prior_release_verified:
        errors.append("immutable CS014 release-assurance evidence rejected")

    for gate in gates:
        if gate.get("blocking_for_current_changeset") is not False:
            errors.append(f"deferred gate blocks CS015: {gate.get('gate_id')}")
        if (
            gate.get("category") == "native_validation_pending"
            and gate.get("blocking_for_profile_qualification") is not True
        ):
            errors.append(f"native profile gate was weakened: {gate.get('gate_id')}")

    policy = docs["acceptance"]
    if policy.get("allowed_acceptance_states") != ["closure_candidate", "accepted"]:
        errors.append("final acceptance state machine is not fail-closed")
    interpretation = policy.get("commercial_interpretation", {})
    prohibited = [
        key for key, value in interpretation.items()
        if key.endswith("_may_be_inferred")
        and key != "horizontal_product_baseline_may_be_accepted"
        and value is not False
    ]
    if prohibited:
        errors.append("final acceptance permits prohibited commercial inference")

    contract_report = product_contract.deterministic_report(root, docs)
    if contract_report.get("commercial_ready") is not (state == "accepted"):
        errors.append("product contract commercial-ready state diverges from acceptance")

    return _report(
        state,
        errors,
        open_requirements,
        open_limitations,
        leaked_claims,
        prior_release_verified,
        state == "accepted" and not errors,
    )


def _report(
    state: str,
    errors: list[str],
    open_requirements: list[str],
    open_limitations: list[str],
    leaked_claims: int,
    prior_release_verified: bool,
    accepted: bool,
) -> dict[str, Any]:
    return {
        "schema": "neoeng.dcore.final-acceptance-validation.v1",
        "project_version": VERSION,
        "status": "passed" if not errors else "failed",
        "acceptance_state": state,
        "horizontal_product_baseline_accepted": accepted,
        "accepted_scope": (
            "NeoEng D-Core horizontal product baseline 1.14.0 within generated "
            "public claims and recorded limitations"
        ),
        "open_internal_requirement_ids": open_requirements,
        "open_internal_limitation_ids": open_limitations,
        "unsupported_or_prohibited_public_claims": leaked_claims,
        "prior_release_assurance_evidence_verified": prior_release_verified,
        "unrestricted_production_readiness_inferred": False,
        "native_or_hardware_profile_qualification_inferred": False,
        "certification_or_external_audit_inferred": False,
        "performance_on_other_hardware_inferred": False,
        "errors": errors,
    }


def self_test() -> list[str]:
    docs, load_errors = product_contract.load_documents(ROOT)
    if load_errors:
        return load_errors
    failures: list[str] = []
    cases: list[tuple[str, dict[str, dict[str, Any]], str | None, dict[str, Any] | None]] = []

    def add(name: str, mutate, public: str | None = None, release: dict[str, Any] | None = None) -> None:
        mutated = copy.deepcopy(docs)
        mutate(mutated)
        cases.append((name, mutated, public, release))

    add(
        "open internal limitation",
        lambda d: next(
            row for row in d["backlog"]["items"]
            if row.get("category") == "internal_mandatory"
        ).update(status="open"),
    )
    add(
        "elevated production claim",
        lambda d: next(
            row for row in d["claims"]["claims"]
            if row.get("claim_id") == "CLAIM-PROD-READY-001"
        ).update(status="verified"),
    )
    add(
        "premature acceptance evidence",
        lambda d: next(
            row for row in d["requirements"]["requirements"]
            if row.get("requirement_id") == "DCORE-ACCEPT-001"
        ).update(tests_or_evidence=["invented"]),
    )
    add(
        "blocking deferred gate",
        lambda d: d["deferred"]["gates"][0].update(
            blocking_for_current_changeset=True
        ),
    )
    add(
        "divergent public claims",
        lambda d: None,
        public="unsupported public material\n",
    )
    add(
        "invalid prior release evidence",
        lambda d: None,
        release={"status": "failed"},
    )
    add(
        "weaken hardware non-inference",
        lambda d: d["acceptance"]["commercial_interpretation"].update(
            arm64_or_hardware_profile_qualification_may_be_inferred=True
        ),
    )
    for name, mutated, public_override, release_override in cases:
        result = evaluate(
            documents=mutated,
            rendered_public_claims=public_override,
            previous_release_result=release_override,
        )
        if result["status"] != "failed":
            failures.append(f"mutation accepted: {name}")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--write-report", action="store_true")
    parser.add_argument("--check-report", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        failures = self_test()
        if failures:
            print("\n".join(failures))
            return 1
        print("final_acceptance_self_test=passed; mutations_rejected=7")
        return 0
    result = evaluate()
    serialized = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.write_report:
        REPORT.write_text(serialized, encoding="utf-8", newline="\n")
    if args.check_report:
        if not REPORT.is_file() or REPORT.read_text(encoding="utf-8") != serialized:
            print("stored final-acceptance report diverges from recalculation")
            return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
