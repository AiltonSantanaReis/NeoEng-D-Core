#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

SHA40 = re.compile(r"^[0-9a-f]{40}$")
PASS = "PASS"
TEST_STATES = {"PASS", "FAIL", "SKIPPED", "NOT_TESTED", "BLOCKED", "PARTIAL"}


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"missing file: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {path}")
    return value


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git(root: Path, *args: str, binary: bool = False):
    return subprocess.run(
        ["git", "-C", str(root), *args],
        text=not binary,
        capture_output=True,
        check=False,
    )


def git_show(root: Path, commit: str, rel: str) -> bytes | None:
    proc = git(root, "show", f"{commit}:{rel}", binary=True)
    return proc.stdout if proc.returncode == 0 else None


def is_ancestor(root: Path, older: str, newer: str) -> bool:
    return git(root, "merge-base", "--is-ancestor", older, newer).returncode == 0


def validate_policy(policy: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    expected_true = (
        "required_test_nonpass_blocks_validation",
        "test_inventory_frozen_before_execution",
        "exact_run_binding_required",
        "failure_preservation_required",
        "accepted_requires_validated",
        "all_required_tests_must_pass",
        "trusted_base_verifier_required_for_pr_acceptance",
        "historical_records_are_immutable",
        "release_is_separate_from_changeset_acceptance",
    )
    if policy.get("schema") != "neoeng.dcore.changeset-validation-policy.v1":
        errors.append("policy schema mismatch")
    if policy.get("lifecycle_states") != ["PLANNED", "IMPLEMENTED", "VALIDATED", "ACCEPTED"]:
        errors.append("lifecycle must be PLANNED -> IMPLEMENTED -> VALIDATED -> ACCEPTED")
    if policy.get("ci_green_is_acceptance") is not False:
        errors.append("ci_green_is_acceptance must be false")
    if policy.get("allow_test_removal_after_execution") is not False:
        errors.append("policy must forbid test removal after execution")
    if policy.get("candidate_verifier_is_authoritative") is not False:
        errors.append("candidate verifier cannot be authoritative")
    for key in expected_true:
        if policy.get(key) is not True:
            errors.append(f"policy must require {key}=true")
    return errors


def validate_plan(plan: dict[str, Any], policy: dict[str, Any]) -> list[str]:
    errors = validate_policy(policy)
    if plan.get("schema") != "neoeng.dcore.changeset-validation-plan.v1":
        errors.append("plan schema mismatch")
    changeset = plan.get("changeset")
    if not isinstance(changeset, str) or not re.fullmatch(r"CS\d{3}[A-Z]?", changeset):
        errors.append("invalid changeset id")
    base = plan.get("base_sha")
    if not isinstance(base, str) or not SHA40.fullmatch(base):
        errors.append("plan base_sha must be exact 40-char lowercase SHA")
    workflow = plan.get("execution_workflow")
    if not isinstance(workflow, str) or not workflow.startswith(".github/workflows/"):
        errors.append("execution_workflow must be a repository workflow path")

    tests = plan.get("required_tests")
    if not isinstance(tests, list) or not tests:
        return errors + ["required_tests must be a non-empty list"]
    seen: set[str] = set()
    for index, test in enumerate(tests):
        if not isinstance(test, dict):
            errors.append(f"required_tests[{index}] is not object")
            continue
        test_id = test.get("test_id")
        if not isinstance(test_id, str) or not re.fullmatch(r"[a-z0-9][a-z0-9._-]{1,63}", test_id):
            errors.append(f"invalid test_id at index {index}")
        elif test_id in seen:
            errors.append(f"duplicate test_id: {test_id}")
        else:
            seen.add(test_id)
        if test.get("required") is not True:
            errors.append(f"required test must set required=true: {test_id}")
        if not isinstance(test.get("evidence_job"), str) or not test.get("evidence_job"):
            errors.append(f"required test missing evidence_job: {test_id}")
        step = test.get("evidence_step")
        if step is not None and (not isinstance(step, str) or not step):
            errors.append(f"invalid evidence_step: {test_id}")

    frozen = plan.get("frozen_files")
    if not isinstance(frozen, list) or not all(
        isinstance(item, str)
        and item
        and not item.startswith("/")
        and ".." not in Path(item).parts
        for item in frozen
    ):
        errors.append("frozen_files must be safe repository-relative paths")
    elif len(set(frozen)) != len(frozen):
        errors.append("frozen_files contains duplicates")
    if plan.get("acceptance_requires_all_required_pass") is not True:
        errors.append("plan must require all required tests PASS")
    if plan.get("allow_test_removal_after_execution") is not False:
        errors.append("plan must forbid test removal after execution")
    return errors


def validate_result(
    root: Path,
    plan_path: Path,
    result_path: Path,
    policy_path: Path,
    *,
    head_sha: str | None,
    require_accepted: bool,
    expected_base_sha: str | None,
) -> list[str]:
    policy = read_json(policy_path)
    plan = read_json(plan_path)
    result = read_json(result_path)
    errors = validate_plan(plan, policy)
    if expected_base_sha is not None and plan.get("base_sha") != expected_base_sha:
        errors.append("plan base_sha differs from trusted PR base")
    if result.get("schema") != "neoeng.dcore.changeset-validation-result.v1":
        errors.append("result schema mismatch")
    if result.get("changeset") != plan.get("changeset"):
        errors.append("result changeset differs from plan")

    plan_commit = result.get("plan_commit")
    source_sha = result.get("source_sha")
    run_id = result.get("run_id")
    run_attempt = result.get("run_attempt")
    if not isinstance(plan_commit, str) or not SHA40.fullmatch(plan_commit):
        errors.append("result plan_commit must be exact SHA")
    if not isinstance(source_sha, str) or not SHA40.fullmatch(source_sha):
        errors.append("result source_sha must be exact SHA")
    if not isinstance(run_id, int) or run_id <= 0:
        errors.append("result run_id must be positive integer")
    if not isinstance(run_attempt, int) or run_attempt <= 0:
        errors.append("result run_attempt must be positive integer")
    if result.get("workflow_path") != plan.get("execution_workflow"):
        errors.append("result workflow_path differs from plan")
    if result.get("plan_sha256") != sha256_file(plan_path):
        errors.append("result plan_sha256 does not match current plan bytes")

    if isinstance(plan_commit, str) and SHA40.fullmatch(plan_commit):
        plan_rel = plan_path.relative_to(root).as_posix()
        prior_plan = git_show(root, plan_commit, plan_rel)
        if prior_plan is None:
            errors.append("cannot read validation plan at plan_commit")
        elif prior_plan != plan_path.read_bytes():
            errors.append("validation plan changed after plan_commit")
        for rel in plan.get("frozen_files", []):
            if not isinstance(rel, str):
                continue
            prior = git_show(root, plan_commit, rel)
            current = root / rel
            if prior is None or not current.is_file():
                errors.append(f"frozen file missing at plan/current state: {rel}")
            elif prior != current.read_bytes():
                errors.append(f"frozen file changed after plan_commit: {rel}")

    if isinstance(plan_commit, str) and SHA40.fullmatch(plan_commit) and isinstance(source_sha, str) and SHA40.fullmatch(source_sha):
        if not is_ancestor(root, plan_commit, source_sha):
            errors.append("plan_commit is not ancestor of source_sha")
    if head_sha and SHA40.fullmatch(head_sha) and isinstance(source_sha, str) and SHA40.fullmatch(source_sha):
        if not is_ancestor(root, source_sha, head_sha):
            errors.append("source_sha is not ancestor of current HEAD")

    raw_results = result.get("tests")
    results: dict[str, dict[str, Any]] = {}
    if not isinstance(raw_results, list):
        errors.append("result tests must be a list")
        raw_results = []
    for item in raw_results:
        if not isinstance(item, dict):
            errors.append("result test entry is not object")
            continue
        test_id = item.get("test_id")
        if not isinstance(test_id, str) or test_id in results:
            errors.append(f"invalid or duplicate result test_id: {test_id!r}")
            continue
        results[test_id] = item
        if item.get("status") not in TEST_STATES:
            errors.append(f"invalid status for {test_id}: {item.get('status')!r}")

    required = [item for item in plan.get("required_tests", []) if isinstance(item, dict)]
    declared = {item.get("test_id") for item in required if isinstance(item.get("test_id"), str)}
    extras = sorted(set(results) - declared)
    if extras:
        errors.append("result contains undeclared tests: " + ", ".join(extras))
    for required_test in required:
        test_id = required_test.get("test_id")
        actual = results.get(test_id)
        if actual is None:
            errors.append(f"required test missing from result: {test_id}")
            continue
        if actual.get("status") != PASS:
            errors.append(f"required test is not PASS: {test_id}={actual.get('status')}")
        if actual.get("evidence_job") != required_test.get("evidence_job"):
            errors.append(f"evidence job mismatch for {test_id}")
        if actual.get("evidence_step") != required_test.get("evidence_step"):
            errors.append(f"evidence step mismatch for {test_id}")

    if result.get("validation_state") != "VALIDATED":
        errors.append("result validation_state must be VALIDATED")
    accepted = result.get("acceptance_decision") == "ACCEPTED"
    if require_accepted and not accepted:
        errors.append("acceptance_decision must be ACCEPTED")
    if accepted and errors:
        errors.append("accepted result has blocking validation errors")
    return errors


def github_get(repo: str, path: str, token: str) -> Any:
    request = urllib.request.Request(
        f"https://api.github.com/repos/{repo}{path}",
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "neoeng-dcore-changeset-validator",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=20) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        raise ValueError(f"GitHub API {exc.code} for {path}: {body[:300]}") from exc


def validate_live(plan: dict[str, Any], result: dict[str, Any], repo: str, token: str) -> list[str]:
    errors: list[str] = []
    run_id, attempt = result.get("run_id"), result.get("run_attempt")
    if not isinstance(run_id, int) or not isinstance(attempt, int):
        return ["cannot verify GitHub evidence without run_id/run_attempt"]
    run = github_get(repo, f"/actions/runs/{run_id}/attempts/{attempt}", token)
    if run.get("head_sha") != result.get("source_sha"):
        errors.append("GitHub run head_sha differs from result source_sha")
    if run.get("run_attempt") != attempt:
        errors.append("GitHub run_attempt differs from result")
    if run.get("status") != "completed" or run.get("conclusion") != "success":
        errors.append(f"GitHub run is not completed/success: {run.get('status')}/{run.get('conclusion')}")
    if run.get("path") != plan.get("execution_workflow"):
        errors.append("GitHub workflow path differs from plan")
    if run.get("repository", {}).get("full_name") != repo:
        errors.append("GitHub run repository mismatch")

    jobs_doc = github_get(repo, f"/actions/runs/{run_id}/attempts/{attempt}/jobs?per_page=100", token)
    jobs = {job.get("name"): job for job in jobs_doc.get("jobs", []) if isinstance(job, dict)}
    for test in plan.get("required_tests", []):
        if not isinstance(test, dict):
            continue
        job = jobs.get(test.get("evidence_job"))
        test_id = test.get("test_id")
        if not isinstance(job, dict):
            errors.append(f"GitHub evidence job not found for {test_id}: {test.get('evidence_job')}")
            continue
        if job.get("conclusion") != "success":
            errors.append(f"GitHub evidence job not success for {test_id}: {job.get('conclusion')}")
        step_name = test.get("evidence_step")
        if step_name:
            step = next((item for item in job.get("steps", []) if isinstance(item, dict) and item.get("name") == step_name), None)
            if not isinstance(step, dict):
                errors.append(f"GitHub evidence step not found for {test_id}: {step_name}")
            elif step.get("conclusion") != "success":
                errors.append(f"GitHub evidence step not success for {test_id}: {step.get('conclusion')}")
    return errors


def self_test() -> list[str]:
    policy = {
        "schema": "neoeng.dcore.changeset-validation-policy.v1",
        "lifecycle_states": ["PLANNED", "IMPLEMENTED", "VALIDATED", "ACCEPTED"],
        "ci_green_is_acceptance": False,
        "allow_test_removal_after_execution": False,
        "candidate_verifier_is_authoritative": False,
        "required_test_nonpass_blocks_validation": True,
        "test_inventory_frozen_before_execution": True,
        "exact_run_binding_required": True,
        "failure_preservation_required": True,
        "accepted_requires_validated": True,
        "all_required_tests_must_pass": True,
        "trusted_base_verifier_required_for_pr_acceptance": True,
        "historical_records_are_immutable": True,
        "release_is_separate_from_changeset_acceptance": True,
    }
    plan = {
        "schema": "neoeng.dcore.changeset-validation-plan.v1",
        "changeset": "CS017",
        "base_sha": "a" * 40,
        "execution_workflow": ".github/workflows/cs017.yml",
        "required_tests": [{"test_id": "build.windows", "required": True, "evidence_job": "Windows", "evidence_step": None}],
        "frozen_files": [".github/workflows/cs017.yml"],
        "acceptance_requires_all_required_pass": True,
        "allow_test_removal_after_execution": False,
    }
    failures: list[str] = []
    if validate_plan(plan, policy):
        failures.append("valid plan rejected")
    weak = dict(policy)
    weak["ci_green_is_acceptance"] = True
    if not validate_policy(weak):
        failures.append("CI-green-only policy was not rejected")
    empty = json.loads(json.dumps(plan))
    empty["required_tests"] = []
    if not validate_plan(empty, policy):
        failures.append("empty required test inventory was not rejected")
    relaxed = json.loads(json.dumps(plan))
    relaxed["allow_test_removal_after_execution"] = True
    if not validate_plan(relaxed, policy):
        failures.append("post-execution test removal was not rejected")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--policy-only", action="store_true")
    parser.add_argument("--root", default=".")
    parser.add_argument("--policy", default="audit/CHANGESET_VALIDATION_POLICY.json")
    parser.add_argument("--descriptor", default="audit/CURRENT_CHANGESET_VALIDATION.json")
    parser.add_argument("--plan")
    parser.add_argument("--result")
    parser.add_argument("--head-sha")
    parser.add_argument("--expected-base-sha")
    parser.add_argument("--require-accepted", action="store_true")
    parser.add_argument("--github-repo")
    args = parser.parse_args()

    if args.self_test:
        errors = self_test()
    else:
        root = Path(args.root).resolve()
        policy_path = (root / args.policy).resolve()
        if args.policy_only:
            errors = validate_policy(read_json(policy_path))
        else:
            plan_arg, result_arg = args.plan, args.result
            if not plan_arg:
                descriptor = read_json(root / args.descriptor)
                if descriptor.get("schema") != "neoeng.dcore.current-changeset-validation.v1":
                    errors = ["descriptor schema mismatch"]
                    plan_arg = None
                else:
                    plan_arg = descriptor.get("plan_path")
                    result_arg = descriptor.get("result_path")
                    errors = []
            else:
                errors = []
            if not isinstance(plan_arg, str) or not plan_arg:
                errors.append("descriptor/argument plan_path missing")
            else:
                plan_path = root / plan_arg
                policy = read_json(policy_path)
                errors.extend(validate_plan(read_json(plan_path), policy))
                if args.expected_base_sha and read_json(plan_path).get("base_sha") != args.expected_base_sha:
                    errors.append("plan base_sha differs from trusted PR base")
                if result_arg:
                    result_path = root / result_arg
                    errors = validate_result(
                        root,
                        plan_path,
                        result_path,
                        policy_path,
                        head_sha=args.head_sha,
                        require_accepted=args.require_accepted,
                        expected_base_sha=args.expected_base_sha,
                    )
                    if args.github_repo and not errors:
                        token = os.environ.get("GITHUB_TOKEN", "")
                        if not token:
                            errors.append("GITHUB_TOKEN missing for live evidence verification")
                        else:
                            errors.extend(validate_live(read_json(plan_path), read_json(result_path), args.github_repo, token))
                elif args.require_accepted:
                    errors.append("accepted validation requires result_path")

    if errors:
        print("CHANGESET VALIDATION: REJECT")
        for error in errors:
            print(f"- {error}")
        return 1
    print("CHANGESET VALIDATION: ACCEPT")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
