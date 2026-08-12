#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
POLICY = Path("audit/REPOSITORY_PROTECTION_POLICY.json")
EXPECTED_SCHEMA = "neoeng.dcore.repository-protection-policy.v1"


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {path}")
    return value


def api(url: str, token: str) -> tuple[int, Any]:
    req = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "X-GitHub-Api-Version": "2026-03-10",
        "User-Agent": "NeoEng-DCore-repository-protection-verifier",
    })
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            return r.status, json.load(r)
    except urllib.error.HTTPError as exc:
        try:
            body = json.loads(exc.read().decode("utf-8"))
        except Exception:
            body = {}
        return exc.code, body


def _bypass_entries(reviews: dict[str, Any]) -> list[str]:
    allowances = reviews.get("bypass_pull_request_allowances")
    if not isinstance(allowances, dict):
        return []
    found: list[str] = []
    for kind in ("users", "teams", "apps"):
        rows = allowances.get(kind)
        if not isinstance(rows, list):
            continue
        for row in rows:
            if isinstance(row, dict):
                name = row.get("login") or row.get("slug") or row.get("name") or row.get("id")
                found.append(f"{kind}:{name}")
            else:
                found.append(f"{kind}:{row}")
    return found


def evaluate(protection: dict[str, Any], policy: dict[str, Any], phase: str) -> list[str]:
    errors: list[str] = []

    if policy.get("schema") != EXPECTED_SCHEMA:
        errors.append("repository protection policy schema mismatch")

    model = policy.get("maintainer_model")
    if model not in {"single_maintainer", "multi_maintainer"}:
        errors.append(f"unsupported maintainer model: {model!r}")

    if policy.get("allow_force_pushes") is False:
        if protection.get("allow_force_pushes", {}).get("enabled") is True:
            errors.append("force pushes are enabled")
    if policy.get("allow_deletions") is False:
        if protection.get("allow_deletions", {}).get("enabled") is True:
            errors.append("branch deletion is enabled")

    reviews = protection.get("required_pull_request_reviews")
    if policy.get("require_pull_request") is True and not isinstance(reviews, dict):
        errors.append("pull request requirement is not enforced by branch protection")
        reviews = {}

    if isinstance(reviews, dict):
        actual_approvals = int(reviews.get("required_approving_review_count") or 0)
        minimum = int(policy.get("minimum_approvals") or 0)

        if model == "single_maintainer":
            if policy.get("human_review_required") is not False:
                errors.append("single-maintainer policy must not claim human review as required")
            if minimum != 0:
                errors.append("single-maintainer policy minimum_approvals must be exactly 0")
            if actual_approvals != 0:
                errors.append(
                    f"single-maintainer protection still requires human approvals: {actual_approvals}"
                )
            if reviews.get("require_code_owner_reviews") is True:
                errors.append("single-maintainer protection requires code-owner review")
            if reviews.get("require_last_push_approval") is True:
                errors.append("single-maintainer protection requires approval of the last push")
        elif model == "multi_maintainer":
            if policy.get("human_review_required") is not True:
                errors.append("multi-maintainer policy must explicitly require human review")
            if minimum < 1:
                errors.append("multi-maintainer minimum_approvals must be at least 1")
            if actual_approvals < minimum:
                errors.append("required approving review count below policy")
            if policy.get("dismiss_stale_reviews") is True and reviews.get("dismiss_stale_reviews") is not True:
                errors.append("stale approvals are not dismissed")

        if policy.get("require_empty_pull_request_bypass_allowances") is True:
            bypass = _bypass_entries(reviews)
            if bypass:
                errors.append("pull-request bypass allowances are not empty: " + ", ".join(sorted(bypass)))

    checks = protection.get("required_status_checks")
    contexts: set[str] = set()
    app_bindings: dict[str, set[int | None]] = {}
    if isinstance(checks, dict):
        if policy.get("require_strict_status_checks") is True and checks.get("strict") is not True:
            errors.append("required status checks are not strict/up-to-date")
        for c in checks.get("contexts", []) or []:
            if isinstance(c, str):
                contexts.add(c)
        for c in checks.get("checks", []) or []:
            if isinstance(c, dict) and isinstance(c.get("context"), str):
                name = c["context"]
                contexts.add(name)
                app_id = c.get("app_id")
                app_bindings.setdefault(name, set()).add(app_id if isinstance(app_id, int) else None)
    else:
        errors.append("required status checks are not configured")

    required_key = "bootstrap_required_checks" if phase == "bootstrap" else "steady_state_required_checks"
    required = policy.get(required_key, [])
    if not isinstance(required, list) or not all(isinstance(x, str) for x in required):
        errors.append(f"{required_key} is invalid")
        required = []
    missing = sorted(set(required) - contexts)
    if missing:
        errors.append("required status checks missing: " + ", ".join(missing))

    expected_apps = policy.get("required_check_apps", {})
    if not isinstance(expected_apps, dict):
        errors.append("required_check_apps is invalid")
        expected_apps = {}
    for check_name in required:
        expected = expected_apps.get(check_name)
        if not isinstance(expected, int):
            errors.append(f"required check has no pinned GitHub App id: {check_name}")
            continue
        if expected not in app_bindings.get(check_name, set()):
            errors.append(f"required check app binding mismatch: {check_name} expected app_id={expected}")

    if policy.get("require_enforce_admins") is True and protection.get("enforce_admins", {}).get("enabled") is not True:
        errors.append("branch protection does not enforce administrators")

    if model == "single_maintainer":
        if policy.get("bootstrap_human_review_exception_changeset") != "CS016E":
            errors.append("single-maintainer bootstrap exception must be limited to CS016E")
        if policy.get("bootstrap_closure_requires_post_merge_trusted_root") is not True:
            errors.append("single-maintainer bootstrap must require post-merge trusted-root validation")
        if policy.get("bootstrap_closure_requires_followup_pr") is not True:
            errors.append("single-maintainer bootstrap must require a separate closure PR")
        if policy.get("future_root_replacement_without_external_authority") != "BLOCKED":
            errors.append("root replacement must remain BLOCKED without external independent authority")
        steady = policy.get("steady_state_required_checks")
        if not isinstance(steady, list) or "Trusted governance root gate" not in steady:
            errors.append("single-maintainer steady state must require Trusted governance root gate")

    return errors


def run_self_test() -> list[str]:
    errors: list[str] = []
    policy: dict[str, Any] = {
        "schema": EXPECTED_SCHEMA,
        "branch": "main",
        "maintainer_model": "single_maintainer",
        "declared_maintainer": "fixture",
        "human_review_required": False,
        "minimum_approvals": 0,
        "dismiss_stale_reviews": False,
        "require_pull_request": True,
        "require_strict_status_checks": True,
        "require_empty_pull_request_bypass_allowances": True,
        "require_enforce_admins": True,
        "allow_force_pushes": False,
        "allow_deletions": False,
        "required_check_apps": {
            "Evolution governance gate": 15368,
            "Trusted governance root gate": 15368,
        },
        "bootstrap_required_checks": ["Evolution governance gate"],
        "steady_state_required_checks": [
            "Evolution governance gate",
            "Trusted governance root gate",
        ],
        "bootstrap_human_review_exception_changeset": "CS016E",
        "bootstrap_closure_requires_post_merge_trusted_root": True,
        "bootstrap_closure_requires_followup_pr": True,
        "future_root_replacement_without_external_authority": "BLOCKED",
    }
    protection: dict[str, Any] = {
        "allow_force_pushes": {"enabled": False},
        "allow_deletions": {"enabled": False},
        "enforce_admins": {"enabled": True},
        "required_pull_request_reviews": {
            "required_approving_review_count": 0,
            "dismiss_stale_reviews": False,
            "require_code_owner_reviews": False,
            "require_last_push_approval": False,
            "bypass_pull_request_allowances": {"users": [], "teams": [], "apps": []},
        },
        "required_status_checks": {
            "strict": True,
            "contexts": ["Evolution governance gate"],
            "checks": [{"context": "Evolution governance gate", "app_id": 15368}],
        },
    }

    if evaluate(protection, policy, "bootstrap"):
        errors.append("valid single-maintainer fixture was rejected")

    bad = copy.deepcopy(protection)
    bad["required_pull_request_reviews"]["required_approving_review_count"] = 1
    if not any("requires human approvals" in x for x in evaluate(bad, policy, "bootstrap")):
        errors.append("required human approval was not rejected in single-maintainer mode")

    bad = copy.deepcopy(protection)
    bad["required_status_checks"]["strict"] = False
    if not any("not strict" in x for x in evaluate(bad, policy, "bootstrap")):
        errors.append("non-strict required status checks were not rejected")

    bad = copy.deepcopy(protection)
    bad["required_status_checks"]["checks"][0]["app_id"] = 99999
    if not any("app binding mismatch" in x for x in evaluate(bad, policy, "bootstrap")):
        errors.append("wrong check-app binding was not rejected")

    bad = copy.deepcopy(protection)
    bad["allow_force_pushes"]["enabled"] = True
    if "force pushes are enabled" not in evaluate(bad, policy, "bootstrap"):
        errors.append("force-push enablement was not rejected")

    bad = copy.deepcopy(protection)
    bad["required_pull_request_reviews"]["bypass_pull_request_allowances"]["users"] = [{"login": "fixture"}]
    if not any("bypass allowances" in x for x in evaluate(bad, policy, "bootstrap")):
        errors.append("pull-request bypass allowance was not rejected")

    bad_policy = copy.deepcopy(policy)
    bad_policy["future_root_replacement_without_external_authority"] = "ALLOWED"
    if not any("root replacement" in x for x in evaluate(protection, bad_policy, "bootstrap")):
        errors.append("single-maintainer root replacement without external authority was not rejected")

    return errors


def verify(repo: str, token: str, phase: str) -> list[str]:
    errors = run_self_test()
    if errors:
        return ["repository-protection self-test failed: " + e for e in errors]

    policy = load_json(ROOT / POLICY)
    branch = str(policy.get("branch", "main"))
    if policy.get("maintainer_model") == "single_maintainer":
        decision = policy.get("single_maintainer_decision_record")
        if not isinstance(decision, str) or not decision:
            return ["single-maintainer decision record is not bound in repository protection policy"]
        decision_path = ROOT / decision
        if not decision_path.is_file():
            return [f"single-maintainer decision record missing: {decision}"]

    # Least-privilege boundary: query the detailed protection endpoint directly.
    # A fine-grained PAT with Administration:read is sufficient for this endpoint;
    # Contents:read is intentionally not required by this verifier.
    status, protection = api(
        f"https://api.github.com/repos/{repo}/branches/{branch}/protection",
        token,
    )
    if status != 200 or not isinstance(protection, dict):
        detail = protection.get("message") if isinstance(protection, dict) else None
        suffix = f" ({detail})" if isinstance(detail, str) and detail else ""
        return [f"cannot verify detailed branch protection: HTTP {status}{suffix}"]

    return evaluate(protection, policy, phase)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", choices=["bootstrap", "steady"])
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"))
    ap.add_argument("--token-env", default="GOVERNANCE_ADMIN_TOKEN")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        errors = run_self_test()
        if errors:
            print("REPOSITORY PROTECTION SELF-TEST: REJECT")
            for e in errors:
                print(f"- {e}")
            return 1
        print("REPOSITORY PROTECTION SELF-TEST: ACCEPT")
        return 0

    if not args.phase:
        errors = ["--phase is required unless --self-test is used"]
    elif not args.repo:
        errors = ["repository identity missing"]
    elif not os.environ.get(args.token_env):
        errors = [f"required administrative read token environment variable missing: {args.token_env}"]
    else:
        try:
            errors = verify(args.repo, os.environ[args.token_env], args.phase)
        except (ValueError, json.JSONDecodeError) as exc:
            errors = [str(exc)]

    if errors:
        print("REPOSITORY PROTECTION VERIFICATION: REJECT")
        for e in errors:
            print(f"- {e}")
        return 1
    print("REPOSITORY PROTECTION VERIFICATION: ACCEPT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
