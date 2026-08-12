#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
POLICY = Path("audit/REPOSITORY_PROTECTION_POLICY.json")


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict): raise ValueError(f"JSON root must be object: {path}")
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
        try: body = json.loads(exc.read().decode("utf-8"))
        except Exception: body = {}
        return exc.code, body


def verify(repo: str, token: str, phase: str) -> list[str]:
    errors: list[str] = []
    policy = load_json(ROOT / POLICY)
    branch = str(policy.get("branch", "main"))
    status, info = api(f"https://api.github.com/repos/{repo}/branches/{branch}", token)
    if status != 200 or not isinstance(info, dict): return [f"cannot read branch metadata: HTTP {status}"]
    if info.get("protected") is not True: errors.append(f"branch {branch} is not protected")

    status, protection = api(f"https://api.github.com/repos/{repo}/branches/{branch}/protection", token)
    if status != 200 or not isinstance(protection, dict):
        errors.append(f"cannot verify detailed branch protection: HTTP {status}")
        return errors
    if protection.get("allow_force_pushes", {}).get("enabled") is True: errors.append("force pushes are enabled")
    if protection.get("allow_deletions", {}).get("enabled") is True: errors.append("branch deletion is enabled")
    reviews = protection.get("required_pull_request_reviews")
    if not isinstance(reviews, dict): errors.append("pull request reviews are not required")
    else:
        if int(reviews.get("required_approving_review_count") or 0) < int(policy.get("minimum_approvals", 1)):
            errors.append("required approving review count below policy")
        if policy.get("dismiss_stale_reviews") is True and reviews.get("dismiss_stale_reviews") is not True:
            errors.append("stale approvals are not dismissed")
    checks = protection.get("required_status_checks")
    contexts: set[str] = set()
    if isinstance(checks, dict):
        for c in checks.get("contexts", []) or []:
            if isinstance(c, str): contexts.add(c)
        for c in checks.get("checks", []) or []:
            if isinstance(c, dict) and isinstance(c.get("context"), str): contexts.add(c["context"])
    else: errors.append("required status checks are not configured")
    required = policy.get("bootstrap_required_checks" if phase == "bootstrap" else "steady_state_required_checks", [])
    missing = sorted(set(required) - contexts)
    if missing: errors.append("required status checks missing: " + ", ".join(missing))
    if policy.get("require_enforce_admins") is True and protection.get("enforce_admins", {}).get("enabled") is not True:
        errors.append("branch protection does not enforce administrators")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--phase", choices=["bootstrap", "steady"], required=True)
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"))
    ap.add_argument("--token-env", default="GOVERNANCE_ADMIN_TOKEN")
    args = ap.parse_args()
    if not args.repo: errors = ["repository identity missing"]
    elif not os.environ.get(args.token_env): errors = [f"required administrative read token environment variable missing: {args.token_env}"]
    else:
        try: errors = verify(args.repo, os.environ[args.token_env], args.phase)
        except (ValueError, json.JSONDecodeError) as exc: errors = [str(exc)]
    if errors:
        print("REPOSITORY PROTECTION VERIFICATION: REJECT")
        for e in errors: print(f"- {e}")
        return 1
    print("REPOSITORY PROTECTION VERIFICATION: ACCEPT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
