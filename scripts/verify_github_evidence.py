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
CHAIN = Path("audit/GOVERNANCE_ACCEPTANCE_CHAIN.json")
EXPECTED_WORKFLOW = ".github/workflows/evolution-governance.yml"


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {path}")
    return value


def api_json(url: str, token: str) -> dict[str, Any]:
    req = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "X-GitHub-Api-Version": "2026-03-10",
        "User-Agent": "NeoEng-DCore-governance-evidence-verifier",
    })
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            value = json.load(r)
    except urllib.error.HTTPError as exc:
        raise ValueError(f"GitHub API HTTP {exc.code} for {url}") from exc
    except urllib.error.URLError as exc:
        raise ValueError(f"GitHub API unavailable for {url}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"GitHub API response is not object: {url}")
    return value


def verify_run(repo: str, token: str, run_id: int, expected_sha: str, label: str) -> list[str]:
    errors: list[str] = []
    run = api_json(f"https://api.github.com/repos/{repo}/actions/runs/{run_id}", token)
    if run.get("id") != run_id: errors.append(f"{label}: run id mismatch expected={run_id} actual={run.get('id')!r}")
    if run.get("head_sha") != expected_sha: errors.append(f"{label}: head_sha mismatch expected={expected_sha} actual={run.get('head_sha')!r}")
    if run.get("status") != "completed": errors.append(f"{label}: run not completed actual={run.get('status')!r}")
    if run.get("conclusion") != "success": errors.append(f"{label}: conclusion is not success actual={run.get('conclusion')!r}")
    if run.get("path") != EXPECTED_WORKFLOW: errors.append(f"{label}: unexpected workflow path {run.get('path')!r}")
    if run.get("event") not in {"push", "pull_request"}: errors.append(f"{label}: unexpected event {run.get('event')!r}")
    repository = run.get("repository")
    if isinstance(repository, dict) and repository.get("full_name") != repo:
        errors.append(f"{label}: repository identity mismatch expected={repo} actual={repository.get('full_name')!r}")
    return errors


def verify_pr(repo: str, token: str, pr_number: int, expected_head: str, expected_merge: str) -> list[str]:
    errors: list[str] = []
    pr = api_json(f"https://api.github.com/repos/{repo}/pulls/{pr_number}", token)
    head = pr.get("head")
    actual_head = head.get("sha") if isinstance(head, dict) else None
    actual_merge = pr.get("merge_commit_sha")
    if actual_head != expected_head:
        errors.append(f"PR #{pr_number}: head SHA mismatch expected={expected_head} actual={actual_head!r}")
    if pr.get("merged") is not True:
        errors.append(f"PR #{pr_number}: not merged actual={pr.get('merged')!r}")
    if actual_merge != expected_merge:
        errors.append(f"PR #{pr_number}: merge SHA mismatch expected={expected_merge} actual={actual_merge!r}")
    base = pr.get("base")
    actual_base = base.get("ref") if isinstance(base, dict) else None
    if actual_base != "main":
        errors.append(f"PR #{pr_number}: base is not main actual={actual_base!r}")
    return errors


def verify_chain(repo: str, token: str, root: Path) -> list[str]:
    errors: list[str] = []
    chain = load_json(root / CHAIN)
    entries = chain.get("entries")
    if not isinstance(entries, list):
        return ["acceptance chain entries missing"]
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        cid = str(entry.get("changeset"))
        errors.extend(verify_run(repo, token, int(entry["qualifying_run_id"]), str(entry["qualifying_source_commit"]), f"{cid} qualifying"))
        errors.extend(verify_run(repo, token, int(entry["accepted_state_run_id"]), str(entry["accepted_state_commit"]), f"{cid} accepted-state"))
        errors.extend(verify_pr(repo, token, int(entry["pr_number"]), str(entry["accepted_state_commit"]), str(entry["merge_commit"])))
        errors.extend(verify_run(repo, token, int(entry["post_merge_run_id"]), str(entry["merge_commit"]), f"{cid} post-merge"))
    return errors


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"))
    ap.add_argument("--token-env", default="GITHUB_TOKEN")
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--offline-schema-only", action="store_true")
    args = ap.parse_args()
    if not args.repo:
        print("GITHUB EVIDENCE VERIFICATION: REJECT\n- repository identity missing")
        return 1
    try:
        if args.offline_schema_only:
            doc = load_json(Path(args.root) / CHAIN)
            errors = [] if isinstance(doc.get("entries"), list) and doc["entries"] else ["acceptance chain entries missing"]
        else:
            token = os.environ.get(args.token_env)
            if not token:
                errors = [f"required token environment variable missing: {args.token_env}"]
            else:
                errors = verify_chain(args.repo, token, Path(args.root))
    except (ValueError, KeyError, TypeError, json.JSONDecodeError) as exc:
        errors = [str(exc)]
    if errors:
        print("GITHUB EVIDENCE VERIFICATION: REJECT")
        for e in errors:
            print(f"- {e}")
        return 1
    print("GITHUB EVIDENCE VERIFICATION: ACCEPT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
