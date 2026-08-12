#!/usr/bin/env python3
from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any

SHA_RE = re.compile(r"^[0-9a-f]{40}$")
ROOT_TRUST = Path("audit/GOVERNANCE_ROOT_OF_TRUST.json")
MAXIMA = Path("audit/STAGE_SCOPE_MAXIMA.json")
CHAIN = Path("audit/GOVERNANCE_ACCEPTANCE_CHAIN.json")
AMENDMENTS = Path("audit/EVOLUTION_AMENDMENTS.json")
ROADMAP = Path("audit/EVOLUTION_ROADMAP.json")


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"missing required file: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError(f"JSON root must be object: {path}")
    return value


def run(repo: Path, args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(["git", "-C", str(repo), *args], text=True, capture_output=True, check=False)


def git_bytes(repo: Path, commit: str, rel: str) -> bytes | None:
    p = subprocess.run(["git", "-C", str(repo), "show", f"{commit}:{rel}"], capture_output=True, check=False)
    return p.stdout if p.returncode == 0 else None


def normalize_path(path: str) -> str | None:
    if not isinstance(path, str) or not path:
        return None
    value = path.replace("\\", "/")
    while value.startswith("./"):
        value = value[2:]
    if not value or value.startswith("/") or value.startswith("//") or re.match(r"^[A-Za-z]:/", value):
        return None
    parts = value.split("/")
    if any(x in {"", ".", ".."} for x in parts):
        return None
    return value


def allowed(path: str, allow: list[str], forbid: list[str]) -> bool:
    value = normalize_path(path)
    if value is None:
        return False
    if any(fnmatch.fnmatch(value, p) for p in forbid):
        return False
    return any(fnmatch.fnmatch(value, p) for p in allow)


def canonical_hash(obj: dict[str, Any]) -> str:
    payload = json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def validate_chain(candidate: Path, trusted_dir: Path | None = None) -> list[str]:
    errors: list[str] = []
    chain = load_json(candidate / CHAIN)
    if chain.get("schema") != "neoeng.dcore.governance-acceptance-chain.v1":
        return ["acceptance chain schema mismatch"]
    entries = chain.get("entries")
    if not isinstance(entries, list) or not entries:
        return ["acceptance chain has no entries"]
    previous = "GENESIS"
    ids: list[str] = []
    for idx, entry in enumerate(entries):
        if not isinstance(entry, dict):
            errors.append(f"acceptance chain entry {idx} is not object")
            continue
        cid = entry.get("changeset")
        if not isinstance(cid, str) or cid in ids:
            errors.append(f"invalid/duplicate acceptance chain id: {cid!r}")
        else:
            ids.append(cid)
        if entry.get("previous_entry_hash") != previous:
            errors.append(f"acceptance chain previous hash mismatch: {cid}")
        supplied = entry.get("entry_hash")
        body = dict(entry)
        body.pop("entry_hash", None)
        if supplied != canonical_hash(body):
            errors.append(f"acceptance chain entry hash mismatch: {cid}")
        previous = str(supplied)
        for key in ("qualifying_source_commit", "accepted_state_commit", "merge_commit"):
            if not isinstance(entry.get(key), str) or not SHA_RE.fullmatch(entry[key]):
                errors.append(f"acceptance chain {cid} invalid {key}")
        for key in ("qualifying_run_id", "accepted_state_run_id", "post_merge_run_id", "pr_number"):
            if not isinstance(entry.get(key), int) or entry[key] <= 0:
                errors.append(f"acceptance chain {cid} invalid {key}")
        if entry.get("conclusion") != "accepted_closed":
            errors.append(f"acceptance chain {cid} is not closed")
    if ids[:4] != ["CS016A", "CS016B", "CS016C", "CS016D"]:
        errors.append("historical acceptance chain prefix A/B/C/D changed")
    amendments = load_json(candidate / AMENDMENTS)
    rows = amendments.get("amendments", [])
    by_id = {r.get("changeset"): r for r in rows if isinstance(r, dict) and isinstance(r.get("changeset"), str)} if isinstance(rows, list) else {}
    for entry in entries:
        if not isinstance(entry, dict):
            continue
        cid = entry.get("changeset")
        row = by_id.get(cid)
        if not isinstance(row, dict):
            errors.append(f"acceptance chain entry has no amendment ledger row: {cid}")
            continue
        if row.get("status") != "accepted":
            errors.append(f"acceptance chain entry is not accepted in amendment ledger: {cid}")
        if row.get("accepted_source_commit") != entry.get("qualifying_source_commit"):
            errors.append(f"acceptance chain qualifying source differs from amendment ledger: {cid}")
    if trusted_dir is not None and (trusted_dir / CHAIN).is_file() and trusted_dir.resolve() != candidate.resolve():
        trusted_entries = load_json(trusted_dir / CHAIN).get("entries")
        if not isinstance(trusted_entries, list):
            errors.append("trusted acceptance chain entries missing")
        elif len(entries) < len(trusted_entries):
            errors.append("candidate truncates trusted acceptance chain")
        else:
            for idx, trusted_entry in enumerate(trusted_entries):
                if entries[idx] != trusted_entry:
                    errors.append(f"candidate rewrites trusted acceptance chain entry {idx}")
    return errors


def current_work(candidate: Path) -> tuple[str | None, str | None, str | None]:
    amendments = load_json(candidate / AMENDMENTS)
    roadmap = load_json(candidate / ROADMAP)
    rows = amendments.get("amendments")
    if isinstance(rows, list):
        in_progress = [r for r in rows if isinstance(r, dict) and r.get("status") == "in_progress"]
        if in_progress:
            row = in_progress[-1]
            cid = row.get("changeset")
            if isinstance(cid, str):
                return "governance_amendment", cid, row.get("required_before_stage") if isinstance(row.get("required_before_stage"), str) else None
    stages = roadmap.get("stages")
    cur = roadmap.get("current_stage")
    if isinstance(stages, list) and isinstance(cur, str):
        row = next((r for r in stages if isinstance(r, dict) and r.get("stage_id") == cur), None)
        if isinstance(row, dict) and row.get("status") in {"in_progress", "accepted"} and isinstance(row.get("planned_changeset"), str):
            return "stage", row["planned_changeset"], cur
    return None, None, None


def scope_path(changeset: str) -> Path:
    return Path(f"docs/changesets/{changeset.removeprefix('CS')}/ACTION_SCOPE.json")


def stage_maximum(maxima: dict[str, Any], stage: str) -> dict[str, Any] | None:
    rows = maxima.get("stages")
    if not isinstance(rows, list):
        return None
    return next((r for r in rows if isinstance(r, dict) and r.get("stage_id") == stage), None)


def validate_scope_contract(scope: dict[str, Any], maximum: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    a = scope.get("allowed_paths")
    f = scope.get("forbidden_paths")
    ma = maximum.get("allowed_patterns")
    mf = maximum.get("mandatory_forbidden_patterns")
    if not isinstance(a, list) or not all(isinstance(x, str) for x in a):
        return ["ACTION_SCOPE allowed_paths invalid"]
    if not isinstance(f, list) or not all(isinstance(x, str) for x in f):
        return ["ACTION_SCOPE forbidden_paths invalid"]
    if not isinstance(ma, list) or not isinstance(mf, list):
        return ["root maximum invalid"]
    extra = sorted(set(a) - set(ma))
    if extra:
        errors.append("ACTION_SCOPE expands trusted maximum: " + ", ".join(extra))
    missing = sorted(set(mf) - set(f))
    if missing:
        errors.append("ACTION_SCOPE drops mandatory forbidden patterns: " + ", ".join(missing))
    if "**" in a or "*" in a:
        errors.append("ACTION_SCOPE contains unbounded wildcard")
    return errors


def determine_base(repo: Path, explicit: str | None) -> tuple[str | None, list[str]]:
    if explicit:
        if not SHA_RE.fullmatch(explicit):
            return None, ["invalid explicit base sha"]
        return explicit, []
    proc = run(repo, ["rev-parse", "origin/main"])
    if proc.returncode != 0:
        return None, ["cannot resolve trusted origin/main base"]
    base = proc.stdout.strip()
    return (base, []) if SHA_RE.fullmatch(base) else (None, ["resolved origin/main is not full SHA"])


def changed_paths(repo: Path, base: str, head: str) -> tuple[list[str], list[str]]:
    p = run(repo, ["diff", "--name-only", f"{base}...{head}"])
    if p.returncode != 0:
        return [], ["cannot enumerate candidate diff: " + p.stderr.strip()]
    return [x.strip() for x in p.stdout.splitlines() if x.strip()], []


def validate_immutable_history(candidate: Path, base: str, trusted: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    for rel in trusted.get("immutable_historical_documents", []):
        if not isinstance(rel, str):
            continue
        expected = git_bytes(candidate, base, rel)
        path = candidate / rel
        if expected is None or not path.is_file():
            errors.append(f"immutable historical document missing: {rel}")
        elif path.read_bytes() != expected:
            errors.append(f"immutable historical document changed: {rel}")
    return errors


def validate_monotonic_root(trusted_root: dict[str, Any], candidate_root: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    for key in ("fail_closed", "self_authorizing_scope_forbidden", "candidate_cannot_select_diff_base", "candidate_cannot_expand_root_maximum", "scope_sealed_after_stage_start", "write_actions_require_explicit_paths", "live_github_provenance_required", "cryptographic_attestation_required_for_release"):
        if trusted_root.get(key) is True and candidate_root.get(key) is not True:
            errors.append(f"root-of-trust weakening forbidden: {key}")
    for key in ("program_id", "protected_branch", "ratchet_mode", "trusted_root_workflow", "trusted_root_verifier", "live_evidence_verifier", "repository_protection_verifier", "acceptance_chain", "stage_scope_maxima", "repository_protection_policy", "release_attestation_verifier"):
        if trusted_root.get(key) != candidate_root.get(key):
            errors.append(f"root-of-trust identity field is immutable: {key}")
    old = trusted_root.get("governance_amendment_maximum", {})
    new = candidate_root.get("governance_amendment_maximum", {})
    if isinstance(old, dict) and isinstance(new, dict):
        if not set(new.get("allowed_patterns", [])).issubset(set(old.get("allowed_patterns", []))):
            errors.append("candidate expands governance root maximum")
        if not set(old.get("mandatory_forbidden_patterns", [])).issubset(set(new.get("mandatory_forbidden_patterns", []))):
            errors.append("candidate weakens governance mandatory forbidden set")
    return errors


def validate_stage_maxima_monotonic(trusted: dict[str, Any], candidate: dict[str, Any], root: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    global_forbid = set(root.get("stage_scope_global_mandatory_forbidden_patterns", []))
    old_rows = {r.get("stage_id"): r for r in trusted.get("stages", []) if isinstance(r, dict) and isinstance(r.get("stage_id"), str)}
    new_rows = {r.get("stage_id"): r for r in candidate.get("stages", []) if isinstance(r, dict) and isinstance(r.get("stage_id"), str)}
    for sid, old in old_rows.items():
        new = new_rows.get(sid)
        if not isinstance(new, dict):
            errors.append(f"candidate removes defined stage maximum: {sid}")
            continue
        if not set(new.get("allowed_patterns", [])).issubset(set(old.get("allowed_patterns", []))):
            errors.append(f"candidate expands existing stage maximum: {sid}")
        if not set(new.get("preparation_allowed_patterns", [])).issubset(set(old.get("preparation_allowed_patterns", []))):
            errors.append(f"candidate expands existing preparation maximum: {sid}")
        if not set(old.get("mandatory_forbidden_patterns", [])).issubset(set(new.get("mandatory_forbidden_patterns", []))):
            errors.append(f"candidate weakens existing stage forbidden set: {sid}")
    for sid, row in new_rows.items():
        a = row.get("allowed_patterns", [])
        p = row.get("preparation_allowed_patterns", [])
        f = set(row.get("mandatory_forbidden_patterns", []))
        if not isinstance(a, list) or not isinstance(p, list) or not all(isinstance(x, str) for x in a + p):
            errors.append(f"stage maximum path lists invalid: {sid}")
            continue
        if any(x in {"*", "**"} for x in a + p):
            errors.append(f"stage maximum contains unbounded wildcard: {sid}")
        missing = sorted(global_forbid - f)
        if missing:
            errors.append(f"stage maximum omits global governance forbidden paths {sid}: " + ", ".join(missing))
    return errors


def validate_scope_seal(repo: Path, base: str, head: str, rel: str) -> list[str]:
    p = run(repo, ["log", "--reverse", "--diff-filter=A", "--format=%H", f"{base}..{head}", "--", rel])
    commits = [x.strip() for x in p.stdout.splitlines() if SHA_RE.fullmatch(x.strip())] if p.returncode == 0 else []
    if not commits:
        return [f"in-progress stage scope was not introduced after trusted base: {rel}"]
    expected = git_bytes(repo, commits[0], rel)
    current = (repo / rel).read_bytes() if (repo / rel).is_file() else None
    return [] if expected is not None and current is not None and expected == current else [f"ACTION_SCOPE changed after its introduction commit: {rel}"]


def validate_accepted_amendment_immutability(candidate: Path, trusted_dir: Path) -> list[str]:
    errors: list[str] = []
    if trusted_dir.resolve() == candidate.resolve() or not (trusted_dir / AMENDMENTS).is_file():
        return errors
    rows = load_json(trusted_dir / AMENDMENTS).get("amendments", [])
    if not isinstance(rows, list):
        return ["trusted amendment ledger invalid"]
    for row in rows:
        if not isinstance(row, dict) or row.get("status") != "accepted":
            continue
        cid = row.get("changeset")
        if not isinstance(cid, str):
            continue
        rels: list[str] = []
        for key in ("amendment_document", "deviation_record", "evidence_manifest"):
            value = row.get(key)
            if isinstance(value, str):
                rels.append(value)
        n = cid.removeprefix("CS")
        rels.extend([f"docs/changesets/{n}/ACTION_SCOPE.json", f"docs/changesets/{n}/CHANGESET.md", f"docs/changesets/{n}/TEST_STATUS.md"])
        for rel in rels:
            tp = trusted_dir / rel
            if not tp.is_file():
                continue
            cp = candidate / rel
            if not cp.is_file():
                errors.append(f"accepted governance artifact removed: {rel}")
            elif cp.read_bytes() != tp.read_bytes():
                errors.append(f"accepted governance artifact rewritten: {rel}")
    return errors


def validate_pinned_actions(candidate: Path) -> list[str]:
    errors: list[str] = []
    for rel in (Path(".github/workflows/evolution-governance.yml"), Path(".github/workflows/governance-root.yml")):
        path = candidate / rel
        if not path.is_file():
            errors.append(f"required governance workflow missing: {rel}")
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            s = line.strip()
            if s.startswith("uses:") or s.startswith("- uses:"):
                ref = s.split("uses:", 1)[1].strip().split("#", 1)[0].strip()
                if ref.startswith("./"):
                    continue
                if "@" not in ref or not SHA_RE.fullmatch(ref.rsplit("@", 1)[1]):
                    errors.append(f"governance workflow action is not pinned to full SHA: {rel}: {ref}")
    return errors


def validate_candidate(candidate: Path, trusted_dir: Path, base_sha: str | None, head_sha: str | None) -> list[str]:
    errors: list[str] = []
    trusted_root_path = trusted_dir / ROOT_TRUST
    candidate_root_path = candidate / ROOT_TRUST
    if not candidate_root_path.is_file():
        return ["candidate governance root of trust missing"]
    candidate_root = load_json(candidate_root_path)
    trusted_root = load_json(trusted_root_path) if trusted_root_path.is_file() else candidate_root
    if candidate_root.get("fail_closed") is not True:
        errors.append("candidate root is not fail-closed")
    if trusted_root_path.is_file() and trusted_dir.resolve() != candidate.resolve():
        errors.extend(validate_monotonic_root(trusted_root, candidate_root))
    base, base_errors = determine_base(candidate, base_sha)
    errors.extend(base_errors)
    if base is None:
        return errors
    if head_sha is None:
        p = run(candidate, ["rev-parse", "HEAD"])
        head_sha = p.stdout.strip() if p.returncode == 0 else None
    if not isinstance(head_sha, str) or not SHA_RE.fullmatch(head_sha):
        return errors + ["cannot resolve candidate HEAD"]
    if run(candidate, ["merge-base", "--is-ancestor", base, head_sha]).returncode != 0:
        errors.append("trusted base is not ancestor of candidate HEAD")
    errors.extend(validate_chain(candidate, trusted_dir))
    errors.extend(validate_immutable_history(candidate, base, trusted_root))
    errors.extend(validate_accepted_amendment_immutability(candidate, trusted_dir))
    errors.extend(validate_pinned_actions(candidate))
    if (trusted_dir / MAXIMA).is_file() and trusted_dir.resolve() != candidate.resolve():
        errors.extend(validate_stage_maxima_monotonic(load_json(trusted_dir / MAXIMA), load_json(candidate / MAXIMA), trusted_root))
    else:
        errors.extend(validate_stage_maxima_monotonic(load_json(candidate / MAXIMA), load_json(candidate / MAXIMA), candidate_root))
    work_type, changeset, stage = current_work(candidate)
    changed, diff_errors = changed_paths(candidate, base, head_sha)
    errors.extend(diff_errors)
    if (trusted_dir / ROOT_TRUST).is_file() and trusted_dir.resolve() != candidate.resolve():
        immutable = trusted_root.get("steady_state_immutable_paths", [])
        if isinstance(immutable, list):
            touched = sorted(path for path in changed if path in immutable)
            if touched:
                errors.append("candidate modifies steady-state immutable root files: " + ", ".join(touched))
    if work_type is None:
        if changed:
            errors.append("candidate changes exist without an active governance amendment or in-progress stage")
        return errors
    rel = scope_path(str(changeset))
    if not (candidate / rel).is_file():
        return errors + [f"missing ACTION_SCOPE for current work: {changeset}"]
    scope = load_json(candidate / rel)
    if scope.get("changeset") != changeset:
        errors.append("ACTION_SCOPE changeset mismatch")
    if scope.get("control_base_commit") not in {None, base}:
        errors.append("candidate-selected control_base_commit differs from trusted base")
    if work_type == "governance_amendment":
        maximum_key = "bootstrap_governance_amendment_maximum" if changeset == trusted_root.get("bootstrap_changeset") else "governance_amendment_maximum"
        maximum = trusted_root.get(maximum_key)
        if not isinstance(maximum, dict):
            return errors + [f"trusted governance maximum missing: {maximum_key}"]
        errors.extend(validate_scope_contract(scope, maximum))
        root_allow = maximum.get("allowed_patterns", [])
        root_forbid = maximum.get("mandatory_forbidden_patterns", [])
    else:
        maxima = load_json(trusted_dir / MAXIMA if (trusted_dir / MAXIMA).is_file() else candidate / MAXIMA)
        maximum = stage_maximum(maxima, str(stage))
        if maximum is None:
            return errors + [f"stage maximum undefined: {stage}"]
        errors.extend(validate_scope_contract(scope, maximum))
        root_allow = maximum.get("allowed_patterns", [])
        root_forbid = maximum.get("mandatory_forbidden_patterns", [])
        roadmap = load_json(candidate / ROADMAP)
        rows = roadmap.get("stages", [])
        row = next((r for r in rows if isinstance(r, dict) and r.get("stage_id") == stage), None) if isinstance(rows, list) else None
        if isinstance(row, dict) and row.get("status") == "in_progress":
            errors.extend(validate_scope_seal(candidate, base, head_sha, rel.as_posix()))
    scope_allow = scope.get("allowed_paths", [])
    scope_forbid = scope.get("forbidden_paths", [])
    for path in changed:
        if not allowed(path, root_allow, root_forbid):
            errors.append(f"diff path exceeds trusted root maximum: {path}")
        if not allowed(path, scope_allow, scope_forbid):
            errors.append(f"diff path exceeds ACTION_SCOPE: {path}")
    return errors


def self_test() -> list[str]:
    failures: list[str] = []
    maximum = {"allowed_patterns": ["docs/changesets/017/**", "scripts/dlab/**"], "mandatory_forbidden_patterns": ["src/**", "include/**"]}
    good = {"allowed_paths": ["docs/changesets/017/**"], "forbidden_paths": ["src/**", "include/**"]}
    if validate_scope_contract(good, maximum):
        failures.append("valid narrow scope rejected")
    broad = {"allowed_paths": ["**"], "forbidden_paths": ["src/**", "include/**"]}
    if not validate_scope_contract(broad, maximum):
        failures.append("broad self-authorizing scope was not rejected")
    weak = {"allowed_paths": ["docs/changesets/017/**"], "forbidden_paths": ["src/**"]}
    if not validate_scope_contract(weak, maximum):
        failures.append("missing mandatory forbidden pattern was not rejected")
    stage_root = {"stage_scope_global_mandatory_forbidden_patterns": ["audit/GOVERNANCE_ROOT_OF_TRUST.json"]}
    bad_stage = {"stages": [{"stage_id": "EV-X", "allowed_patterns": ["**"], "preparation_allowed_patterns": [], "mandatory_forbidden_patterns": []}]}
    if not validate_stage_maxima_monotonic({"stages": []}, bad_stage, stage_root):
        failures.append("unsafe new stage maximum was not rejected")
    old = {"fail_closed": True, "self_authorizing_scope_forbidden": True, "candidate_cannot_select_diff_base": True, "candidate_cannot_expand_root_maximum": True, "scope_sealed_after_stage_start": True, "write_actions_require_explicit_paths": True, "live_github_provenance_required": True, "cryptographic_attestation_required_for_release": True, "governance_amendment_maximum": maximum}
    new = json.loads(json.dumps(old))
    new["fail_closed"] = False
    if not validate_monotonic_root(old, new):
        failures.append("root weakening was not rejected")
    return failures


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--candidate-root", default=".")
    ap.add_argument("--trusted-root")
    ap.add_argument("--base-sha")
    ap.add_argument("--head-sha")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        errors = self_test()
    else:
        candidate = Path(args.candidate_root).resolve()
        trusted = Path(args.trusted_root).resolve() if args.trusted_root else candidate
        try:
            errors = validate_candidate(candidate, trusted, args.base_sha, args.head_sha)
        except ValueError as exc:
            errors = [str(exc)]
    if errors:
        print("GOVERNANCE ROOT VERIFICATION: REJECT")
        for e in errors:
            print(f"- {e}")
        return 1
    print("GOVERNANCE ROOT VERIFICATION: ACCEPT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
