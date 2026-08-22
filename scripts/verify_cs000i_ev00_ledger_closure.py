#!/usr/bin/env python3
from __future__ import annotations

import copy
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

BASE = "89c134795e41357cc204265f7cd96ddadd804c57"
PREVIOUS_MAIN = "3ebb989c5aaca65501ddbc5e552e1f751079e310"
CS017_HEAD = "047cca5ac296af4e83f44b70fbec64458ba49ea4"
CS017_EVIDENCE = "36a186520091adc4df799ac0668c9ca9939b8c36"
CS017_PLAN = "9325c9940f1059c57cbd6f4994edbce7d525a270"
PRODUCT = "e3fff973554a2e56b8bd7afdc1132f75f3ec337c"
PHYSICAL_RUN = "ev00-20260822T212835Z-d3eb8773"
DLAB_RUN_ID = 32601277637
DLAB_RUN_ATTEMPT = 1
TRUSTED_APP_ID = 15368

ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")
REQS = Path("audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json")
TARGET_WORKFLOW = Path(".github/workflows/ev00-dlab.yml")
SELF_WORKFLOW = Path(".github/workflows/cs000i-ev00-ledger-closure-validation.yml")
PLAN = Path("audit/validation/CS000I/VALIDATION_PLAN.json")
RESULT = Path("audit/validation/CS000I/VALIDATION_RESULT.json")
DESCRIPTOR = Path("audit/CURRENT_CHANGESET_VALIDATION.json")
CHANGESET = Path("docs/changesets/000I/CHANGESET.md")
ACCEPTANCE_MANIFEST = Path("docs/changesets/000I/evidence/EV00_ACCEPTANCE_MANIFEST.json")
DECISION = Path("docs/records/evolution/DEV-0007.md")
GLOBAL_MANIFEST = Path("MANIFEST.sha256")

RUN_ROOT = (
    "docs/changesets/017/evidence/local-windows/"
    + PHYSICAL_RUN
)

EXPECTED_TRIGGER_PATHS = {
    ".github/workflows/ev00-dlab.yml",
    ".github/workflows/cs000i-ev00-ledger-closure-validation.yml",
    "scripts/verify_cs000i_ev00_ledger_closure.py",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/validation/CS000I/VALIDATION_PLAN.json",
    "docs/changesets/000I/CHANGESET.md",
    "docs/changesets/000I/evidence/EV00_ACCEPTANCE_MANIFEST.json",
    "docs/records/evolution/DEV-0007.md",
    "MANIFEST.sha256",
}

CANDIDATE_REQUIRED = EXPECTED_TRIGGER_PATHS | {
    "audit/CURRENT_CHANGESET_VALIDATION.json",
}

CLOSURE_ALLOWED = CANDIDATE_REQUIRED | {
    "audit/validation/CS000I/VALIDATION_RESULT.json",
}

REQ_EVIDENCE = {
    "EVREQ-001": [
        f"{RUN_ROOT}/run-identity.json",
        f"{RUN_ROOT}/evidence-manifest.json",
        f"{RUN_ROOT}/historical-comparison.json",
        "docs/changesets/017/HISTORICAL_ASSURANCE_RESULT.json",
        "audit/validation/CS017/VALIDATION_RESULT.json",
    ],
    "EVREQ-002": [
        f"{RUN_ROOT}/environment.json",
        f"{RUN_ROOT}/raw/commands/cmake-configure.json",
        f"{RUN_ROOT}/raw/logs/cmake-configure.stdout.txt",
    ],
    "EVREQ-003": [
        f"{RUN_ROOT}/raw/logs/cmake-configure.stdout.txt",
        f"{RUN_ROOT}/raw/logs/cmake-build.stdout.txt",
        f"{RUN_ROOT}/raw/logs/ctest-dcore.stdout.txt",
        f"{RUN_ROOT}/historical-comparison.json",
    ],
    "EVREQ-004": [
        f"{RUN_ROOT}/raw/logs/determinism-1.stdout.txt",
        f"{RUN_ROOT}/raw/logs/determinism-2.stdout.txt",
        f"{RUN_ROOT}/raw/logs/ctest-host-sdk.stdout.txt",
        f"{RUN_ROOT}/raw/logs/ctest-replay-rollback.stdout.txt",
        f"{RUN_ROOT}/replay-rollback-validation.json",
        f"{RUN_ROOT}/raw/logs/state-evidence-probe.stdout.txt",
        f"{RUN_ROOT}/raw/logs/support-bundle-probe.stdout.txt",
    ],
}

def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )

def load(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))

def git_show(ref: str, path: str) -> bytes | None:
    p = subprocess.run(
        ["git", "show", f"{ref}:{path}"],
        cwd=ROOT,
        capture_output=True,
        check=False,
    )
    return p.stdout if p.returncode == 0 else None

def changed_paths() -> set[str]:
    paths: set[str] = set()
    for args in [
        ("git", "diff", "--name-only", f"{BASE}...HEAD"),
        ("git", "diff", "--name-only", "HEAD"),
        ("git", "diff", "--cached", "--name-only"),
    ]:
        p = run(*args)
        if p.returncode != 0:
            raise RuntimeError(p.stderr.strip() or "git diff failed")
        paths.update(x.strip() for x in p.stdout.splitlines() if x.strip())
    return paths

def sha256_lf(path: Path) -> str:
    return hashlib.sha256(
        path.read_bytes().replace(b"\r\n", b"\n")
    ).hexdigest()

def top_level_on_keys(text: str) -> set[str]:
    lines = text.splitlines()
    try:
        start = lines.index("on:") + 1
    except ValueError as exc:
        raise ValueError("missing top-level on: block") from exc
    keys: set[str] = set()
    for line in lines[start:]:
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if not line.startswith(" "):
            break
        if line.startswith("  ") and not line.startswith("    "):
            token = line.strip()
            if token.endswith(":"):
                keys.add(token[:-1])
    return keys

def suffix_from_permissions(text: str) -> str:
    marker = "\npermissions:\n"
    pos = text.find(marker)
    if pos < 0:
        raise ValueError("permissions marker missing")
    return text[pos + 1:]

def trigger_paths(text: str) -> set[str]:
    values: set[str] = set()
    active = False
    for line in text.splitlines():
        if line == "    paths:":
            active = True
            continue
        if active:
            if line.startswith("      - "):
                values.add(line.split("- ", 1)[1].strip().strip("'\""))
                continue
            if line.strip() and not line.startswith("      "):
                break
    return values

def emit(label: str, errors: list[str]) -> int:
    if errors:
        print(f"CS000I EV-00 LEDGER CLOSURE: REJECT — {label}")
        for item in errors:
            print(f"- {item}")
        return 1
    print(f"CS000I EV-00 LEDGER CLOSURE: PASS — {label}")
    return 0

def self_test_errors() -> list[str]:
    errors: list[str] = []
    manual = (
        "name: x\n\non:\n  workflow_dispatch:\n\n"
        "permissions:\n  contents: read\n"
    )
    if top_level_on_keys(manual) != {"workflow_dispatch"}:
        errors.append("manual-only parser fixture failed")

    scoped = (
        "name: x\n\non:\n  pull_request:\n"
        "    branches: [main]\n    paths:\n"
        "      - 'a'\n      - 'b'\n\npermissions:\n  contents: read\n"
    )
    if trigger_paths(scoped) != {"a", "b"}:
        errors.append("path-scope parser fixture failed")

    bad = set(CANDIDATE_REQUIRED)
    bad.add("src/forbidden.cpp")
    if not (bad - CLOSURE_ALLOWED):
        errors.append("forbidden scope fixture was not rejected")

    return errors

def integration_errors() -> list[str]:
    errors: list[str] = []

    parents = run("git", "rev-list", "--parents", "-n", "1", BASE)
    expected = [BASE, PREVIOUS_MAIN, CS017_HEAD]
    if parents.returncode != 0 or parents.stdout.split() != expected:
        errors.append("accepted merge parents mismatch")

    tree = run("git", "diff", "--quiet", CS017_HEAD, BASE, "--")
    if tree.returncode != 0:
        errors.append("accepted merge tree differs from CS017 accepted head")

    result = load(ROOT / "audit/validation/CS017/VALIDATION_RESULT.json")
    checks = {
        "changeset": "CS017",
        "plan_commit": CS017_PLAN,
        "source_sha": CS017_EVIDENCE,
        "run_id": DLAB_RUN_ID,
        "run_attempt": DLAB_RUN_ATTEMPT,
        "validation_state": "VALIDATED",
        "acceptance_decision": "ACCEPTED",
    }
    for key, value in checks.items():
        if result.get(key) != value:
            errors.append(f"CS017 validation result mismatch: {key}")

    tests = result.get("tests")
    if not isinstance(tests, list) or len(tests) != 29:
        errors.append("CS017 validation result must contain exactly 29 tests")
    elif any(x.get("status") != "PASS" for x in tests if isinstance(x, dict)):
        errors.append("CS017 validation result contains non-PASS test")

    identity = load(ROOT / RUN_ROOT / "run-identity.json")
    if identity.get("run_id") != PHYSICAL_RUN:
        errors.append("physical run_id mismatch")
    if identity.get("product_sha") != PRODUCT:
        errors.append("physical product SHA mismatch")
    if identity.get("harness_sha") != CS017_PLAN:
        errors.append("physical harness SHA mismatch")

    terminal = load(ROOT / RUN_ROOT / "terminal-state.json")
    if terminal.get("state") != "PASSED":
        errors.append("physical terminal state is not PASSED")

    return errors

def github_get(path: str):
    token = os.environ.get("GITHUB_TOKEN", "")
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    if not token or not repo:
        raise ValueError("GITHUB_TOKEN/GITHUB_REPOSITORY missing")
    req = urllib.request.Request(
        f"https://api.github.com/repos/{repo}{path}",
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {token}",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "neoeng-cs000i-verifier",
        },
    )
    with urllib.request.urlopen(req, timeout=20) as response:
        return json.loads(response.read().decode("utf-8"))

def github_errors() -> list[str]:
    errors: list[str] = []

    pr = github_get("/pulls/50")
    if pr.get("merged") is not True:
        errors.append("PR #50 is not merged")
    if pr.get("merge_commit_sha") != BASE:
        errors.append("PR #50 merge commit mismatch")
    if pr.get("head", {}).get("sha") != CS017_HEAD:
        errors.append("PR #50 head mismatch")
    if pr.get("base", {}).get("sha") != PREVIOUS_MAIN:
        errors.append("PR #50 base mismatch")

    trusted = github_get(f"/commits/{CS017_HEAD}/check-runs?per_page=100")
    trusted_ok = any(
        x.get("name") == "Trusted ChangeSet validation gate"
        and x.get("conclusion") == "success"
        and x.get("app", {}).get("id") == TRUSTED_APP_ID
        for x in trusted.get("check_runs", [])
        if isinstance(x, dict)
    )
    if not trusted_ok:
        errors.append("successful trusted ChangeSet gate not found on CS017 head")

    main = github_get(f"/commits/{BASE}/check-runs?per_page=100")
    main_ok = any(
        x.get("name") == "Main ChangeSet validation"
        and x.get("conclusion") == "success"
        and x.get("app", {}).get("id") == TRUSTED_APP_ID
        for x in main.get("check_runs", [])
        if isinstance(x, dict)
    )
    if not main_ok:
        errors.append("successful Main ChangeSet validation not found on merge")

    return errors

def workflow_retirement_errors() -> list[str]:
    errors: list[str] = []

    prior_bytes = git_show(BASE, TARGET_WORKFLOW.as_posix())
    if prior_bytes is None:
        return ["cannot read accepted R22 workflow from base"]

    prior = prior_bytes.decode("utf-8")
    current = (ROOT / TARGET_WORKFLOW).read_text(encoding="utf-8")

    if top_level_on_keys(current) != {"workflow_dispatch"}:
        errors.append("EV-00 R22 workflow is not manual-only")

    if suffix_from_permissions(prior) != suffix_from_permissions(current):
        errors.append("EV-00 workflow body changed from permissions onward")

    self_text = (ROOT / SELF_WORKFLOW).read_text(encoding="utf-8")
    if top_level_on_keys(self_text) != {"pull_request"}:
        errors.append("CS000I workflow must be pull_request-only")

    paths = trigger_paths(self_text)
    if paths != EXPECTED_TRIGGER_PATHS:
        errors.append("CS000I workflow trigger paths differ from exact bounded set")

    if "audit/CURRENT_CHANGESET_VALIDATION.json" in paths:
        errors.append("CS000I workflow must not become descriptor-global")
    if "audit/validation/CS000I/VALIDATION_RESULT.json" in paths:
        errors.append("CS000I workflow must not become result-global")

    return errors

def ledger_errors() -> list[str]:
    errors: list[str] = []

    prior_raw = git_show(BASE, ROADMAP.as_posix())
    if prior_raw is None:
        return ["cannot read base roadmap"]

    prior = json.loads(prior_raw.decode("utf-8"))
    current = load(ROOT / ROADMAP)

    expected = copy.deepcopy(prior)
    ev00 = next(x for x in expected["stages"] if x["stage_id"] == "EV-00")
    ev00.update({
        "status": "accepted",
        "accepted_commit": BASE,
        "evidence_manifest": ACCEPTANCE_MANIFEST.as_posix(),
        "decision_record": DECISION.as_posix(),
    })

    if current != expected:
        errors.append("roadmap differs from exact EV-00 acceptance transition")

    ev01 = next(x for x in current["stages"] if x["stage_id"] == "EV-01")
    if current.get("current_stage") != "EV-00":
        errors.append("current_stage advanced during closure")
    if current.get("release_authorized") is not False:
        errors.append("release_authorized changed")
    if ev01.get("status") != "not_started":
        errors.append("EV-01 started during EV-00 closure")

    return errors

def requirements_errors() -> list[str]:
    prior_raw = git_show(BASE, REQS.as_posix())
    if prior_raw is None:
        return ["cannot read base requirements ledger"]

    prior = json.loads(prior_raw.decode("utf-8"))
    current = load(ROOT / REQS)

    expected = copy.deepcopy(prior)
    for row in expected["requirements"]:
        rid = row.get("requirement_id")
        if rid in REQ_EVIDENCE:
            row["status"] = "verified"
            row["evidence"] = REQ_EVIDENCE[rid]

    if current != expected:
        return ["requirements ledger differs from exact EVREQ-001..004 closure"]
    return []

def evidence_errors() -> list[str]:
    errors: list[str] = []

    doc = load(ROOT / ACCEPTANCE_MANIFEST)
    if doc.get("schema") != "neoeng.dcore.evolution-evidence-manifest.v1":
        errors.append("acceptance manifest schema mismatch")
    if doc.get("stage") != "EV-00":
        errors.append("acceptance manifest stage mismatch")
    if doc.get("changeset") != "CS017":
        errors.append("acceptance manifest ChangeSet mismatch")
    if doc.get("closure_changeset") != "CS000I":
        errors.append("acceptance manifest closure ChangeSet mismatch")
    if doc.get("source_commit") != BASE:
        errors.append("acceptance manifest source_commit mismatch")
    if doc.get("hash_mode") != "lf-normalized-text":
        errors.append("acceptance manifest hash mode mismatch")

    rows = doc.get("files")
    if not isinstance(rows, list) or not rows:
        errors.append("acceptance manifest has no files")
        rows = []

    seen: set[str] = set()

    for row in rows:
        if not isinstance(row, dict):
            errors.append("invalid acceptance manifest row")
            continue
        rel = row.get("path")
        digest = row.get("sha256")
        if not isinstance(rel, str):
            errors.append("invalid acceptance evidence path")
            continue
        if rel in seen:
            errors.append(f"duplicate acceptance evidence path: {rel}")
        seen.add(rel)

        path = ROOT / rel
        if not path.is_file():
            errors.append(f"acceptance evidence missing: {rel}")
            continue

        base_bytes = git_show(BASE, rel)
        if base_bytes is None:
            errors.append(f"acceptance evidence absent from accepted merge: {rel}")
            continue
        if base_bytes != path.read_bytes():
            errors.append(f"accepted evidence changed after merge: {rel}")

        if digest != sha256_lf(path):
            errors.append(f"acceptance evidence hash mismatch: {rel}")

    decision = (ROOT / DECISION).read_text(encoding="utf-8")
    tokens = [
        f"Accepted integrated commit: `{BASE}`",
        f"Accepted CS017 head: `{CS017_HEAD}`",
        f"R22 plan/harness commit: `{CS017_PLAN}`",
        f"Committed physical evidence source: `{CS017_EVIDENCE}`",
        f"Physical qualifying run: `{PHYSICAL_RUN}`",
        "`EV-00.status = accepted`",
        "`EV-01` remains `not_started`",
        "`release_authorized` remains `false`",
    ]
    for token in tokens:
        if token not in decision:
            errors.append(f"decision record token missing: {token}")

    return errors

def scope_errors() -> list[str]:
    paths = changed_paths()
    if paths == CANDIDATE_REQUIRED:
        return []
    if paths == CLOSURE_ALLOWED:
        return []
    extra = sorted(paths - CLOSURE_ALLOWED)
    missing = sorted(CANDIDATE_REQUIRED - paths)
    errors: list[str] = []
    if extra:
        errors.append("paths outside CS000I scope: " + ", ".join(extra))
    if missing:
        errors.append("required CS000I paths missing: " + ", ".join(missing))
    if not errors:
        errors.append("CS000I path set is neither candidate nor result-closure shape")
    return errors

def non_effect_errors() -> list[str]:
    errors: list[str] = []
    paths = changed_paths()

    forbidden_prefixes = (
        "src/", "include/", "tests/", "cmake/",
        "modules/", "apps/", "tools/",
    )
    forbidden_exact = {"CMakeLists.txt", "vcpkg.json", "vcpkg-configuration.json"}

    bad = sorted(
        p for p in paths
        if p in forbidden_exact or p.startswith(forbidden_prefixes)
    )
    if bad:
        errors.append("product/runtime paths changed: " + ", ".join(bad))

    roadmap = load(ROOT / ROADMAP)
    ev01 = next(x for x in roadmap["stages"] if x["stage_id"] == "EV-01")
    if roadmap.get("release_authorized") is not False:
        errors.append("release authorization changed")
    if ev01.get("status") != "not_started":
        errors.append("EV-01 is not not_started")

    tag = run("git", "rev-list", "-n", "1", "v1.14.1")
    if tag.returncode != 0 or tag.stdout.strip() != PRODUCT:
        errors.append("v1.14.1 product identity changed")

    return errors

def manifest_errors() -> list[str]:
    p = run(sys.executable, "scripts/generate_manifest.py", "--check")
    if p.returncode != 0:
        return [
            "MANIFEST.sha256 mismatch: "
            + (p.stdout + p.stderr).strip()
        ]
    return []

def main() -> int:
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--integration", action="store_true")
    parser.add_argument("--github", action="store_true")
    parser.add_argument("--workflow-retirement", action="store_true")
    parser.add_argument("--ledger", action="store_true")
    parser.add_argument("--requirements", action="store_true")
    parser.add_argument("--evidence", action="store_true")
    parser.add_argument("--scope", action="store_true")
    parser.add_argument("--non-effects", action="store_true")
    parser.add_argument("--manifest", action="store_true")
    args = parser.parse_args()

    choices = [
        args.self_test,
        args.integration,
        args.github,
        args.workflow_retirement,
        args.ledger,
        args.requirements,
        args.evidence,
        args.scope,
        args.non_effects,
        args.manifest,
    ]
    if sum(choices) != 1:
        parser.error("select exactly one check")

    try:
        if args.self_test:
            return emit("verifier-self-test", self_test_errors())
        if args.integration:
            return emit("accepted-CS017-integration", integration_errors())
        if args.github:
            return emit("live-GitHub-acceptance-provenance", github_errors())
        if args.workflow_retirement:
            return emit("R22-workflow-retirement", workflow_retirement_errors())
        if args.ledger:
            return emit("EV-00-ledger", ledger_errors())
        if args.requirements:
            return emit("EVREQ-001-004", requirements_errors())
        if args.evidence:
            return emit("acceptance-evidence", evidence_errors())
        if args.scope:
            return emit("administrative-scope", scope_errors())
        if args.non_effects:
            return emit("non-effects", non_effect_errors())
        return emit("tracked-manifest", manifest_errors())
    except Exception as exc:
        return emit("exception", [str(exc)])

if __name__ == "__main__":
    raise SystemExit(main())
