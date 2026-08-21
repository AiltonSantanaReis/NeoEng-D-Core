#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path
from typing import Iterable

PLAN_PATH = Path("audit/validation/CS000B/VALIDATION_PLAN.json")
URL_SCHEMES = ("http://", "https://", "mailto:", "tel:", "data:")
LINK_RE = re.compile(r"!?\[[^\]]*\]\(([^)]+)\)")
FENCE_RE = re.compile(r"^\s*(```|~~~)")


def run_git(root: Path, *args: str, binary: bool = False):
    return subprocess.run(
        ["git", "-C", str(root), *args],
        text=not binary,
        capture_output=True,
        check=False,
    )


def load_plan(root: Path) -> dict:
    return json.loads((root / PLAN_PATH).read_text(encoding="utf-8"))


def changed_entries(root: Path, base: str) -> list[tuple[str, str]]:
    proc = run_git(root, "diff", "--name-status", f"{base}...HEAD")
    if proc.returncode != 0:
        raise ValueError(proc.stderr.strip() or "git diff failed")
    entries: list[tuple[str, str]] = []
    for raw in proc.stdout.splitlines():
        if not raw.strip():
            continue
        parts = raw.split("\t")
        status = parts[0]
        if status.startswith("R") or status.startswith("C"):
            raise ValueError(f"rename/copy is not allowed in CS000B: {raw}")
        if len(parts) != 2:
            raise ValueError(f"unexpected git diff entry: {raw}")
        entries.append((status, parts[1]))
    return entries


def check_scope_entries(
    entries: Iterable[tuple[str, str]],
    allowed: set[str],
    required: set[str],
) -> list[str]:
    errors: list[str] = []
    seen: set[str] = set()
    for status, path in entries:
        seen.add(path)
        if status not in {"A", "M"}:
            errors.append(f"disallowed change status {status}: {path}")
        if path not in allowed:
            errors.append(f"path outside CS000B allowlist: {path}")
    for path in sorted(required - seen):
        errors.append(f"required candidate path missing from diff: {path}")
    return errors


def base_paths(root: Path, base: str) -> list[str]:
    proc = run_git(root, "ls-tree", "-r", "--name-only", base)
    if proc.returncode != 0:
        raise ValueError(proc.stderr.strip() or "git ls-tree failed")
    return [line for line in proc.stdout.splitlines() if line]


def git_show(root: Path, base: str, path: str) -> bytes | None:
    proc = run_git(root, "show", f"{base}:{path}", binary=True)
    return proc.stdout if proc.returncode == 0 else None


def check_protected_bytes(root: Path, plan: dict) -> list[str]:
    base = plan["base_sha"]
    exclusions = set(plan["protected_base_exclusions"])
    errors: list[str] = []
    checked = 0
    for path in base_paths(root, base):
        if path in exclusions:
            continue
        current = root / path
        prior = git_show(root, base, path)
        if prior is None:
            errors.append(f"cannot read protected base file: {path}")
            continue
        if not current.is_file():
            errors.append(f"protected base file missing: {path}")
            continue
        if current.read_bytes() != prior:
            errors.append(f"protected base file changed: {path}")
        checked += 1
    if not errors:
        print(f"protected base files byte-identical: {checked}")
    return errors


def markdown_link_target(raw: str) -> str | None:
    target = raw.strip()
    if not target:
        return None
    if target.startswith("<") and target.endswith(">"):
        target = target[1:-1].strip()
    if " " in target and not target.startswith(("http://", "https://")):
        target = target.split(" ", 1)[0]
    if not target or target.startswith("#") or target.startswith(URL_SCHEMES):
        return None
    if target.startswith("//"):
        return None
    return target.split("#", 1)[0].split("?", 1)[0]


def check_markdown_text(text: str, file_path: Path, root: Path) -> list[str]:
    errors: list[str] = []
    fence = None
    for line in text.splitlines():
        match = FENCE_RE.match(line)
        if not match:
            continue
        marker = match.group(1)
        if fence is None:
            fence = marker
        elif marker == fence:
            fence = None
    if fence is not None:
        errors.append(f"{file_path}: unbalanced Markdown fence")

    for match in LINK_RE.finditer(text):
        target = markdown_link_target(match.group(1))
        if target is None:
            continue
        if target.startswith("/"):
            errors.append(
                f"{file_path}: repository-relative link must not start with /: {target}"
            )
            continue
        resolved = (file_path.parent / target).resolve()
        try:
            resolved.relative_to(root.resolve())
        except ValueError:
            errors.append(f"{file_path}: link escapes repository: {target}")
            continue
        if not resolved.exists():
            errors.append(f"{file_path}: broken relative link: {target}")
    return errors


def check_markdown(root: Path, plan: dict) -> list[str]:
    errors: list[str] = []
    for rel in plan["documentation_paths"] + [plan["changeset_record"]]:
        path = root / rel
        if not path.is_file():
            errors.append(f"documentation path missing: {rel}")
            continue
        errors.extend(check_markdown_text(path.read_text(encoding="utf-8"), path, root))
    return errors


def check_status(root: Path, plan: dict) -> list[str]:
    errors: list[str] = []
    roadmap = json.loads(
        (root / "audit/EVOLUTION_ROADMAP.json").read_text(encoding="utf-8")
    )
    expected = plan["status_expectations"]

    if roadmap.get("project_version") != expected["project_version"]:
        errors.append("project_version differs from frozen documentation expectation")
    baseline = roadmap.get("baseline", {})
    if baseline.get("release_tag") != expected["release_tag"]:
        errors.append("release_tag differs from frozen documentation expectation")
    if baseline.get("release_commit") != expected["release_commit"]:
        errors.append("release_commit differs from frozen documentation expectation")
    if roadmap.get("program_id") != expected["program_id"]:
        errors.append("program_id differs from frozen documentation expectation")
    if roadmap.get("program_state") != expected["program_state"]:
        errors.append("program_state differs from frozen documentation expectation")
    if roadmap.get("current_stage") != expected["current_stage"]:
        errors.append("current_stage differs from frozen documentation expectation")
    if roadmap.get("release_authorized") is not expected["release_authorized"]:
        errors.append("release_authorized differs from frozen documentation expectation")

    stage = next(
        (
            item
            for item in roadmap.get("stages", [])
            if item.get("stage_id") == expected["current_stage"]
        ),
        None,
    )
    if not isinstance(stage, dict):
        errors.append("current stage missing from roadmap")
    else:
        if stage.get("status") != expected["current_stage_status"]:
            errors.append(
                "current stage status differs from frozen documentation expectation"
            )
        if stage.get("planned_changeset") != expected["planned_changeset"]:
            errors.append("planned ChangeSet differs from frozen documentation expectation")

    required_tokens = [
        expected["project_version"],
        expected["release_tag"],
        expected["release_commit"],
        expected["program_id"],
        expected["current_stage"],
        expected["planned_changeset"],
    ]
    for rel in ("README.md", "ABOUT.md", "docs/PROJECT_STATUS.md"):
        text = (root / rel).read_text(encoding="utf-8")
        for token in required_tokens:
            if token not in text:
                errors.append(f"{rel}: missing status identity {token}")
        if expected["release_authorized"] is False and "não autorizada" not in text.lower():
            errors.append(f"{rel}: must state that a new release is not authorized")
    return errors


def check_project_boundary(root: Path, plan: dict) -> list[str]:
    errors: list[str] = []
    required = {
        "README.md": ("D-Core", "D-Lab", "produto", "extern"),
        "ABOUT.md": ("D-Core", "D-Lab", "produto", "extern"),
        "docs/PROJECT_STATUS.md": ("D-Core", "D-Lab", "produto", "extern"),
        "docs/README.md": ("D-Core", "D-Lab"),
    }
    for rel, tokens in required.items():
        text = (root / rel).read_text(encoding="utf-8")
        lowered = text.lower()
        for token in tokens:
            if token.lower() not in lowered:
                errors.append(f"{rel}: missing project-boundary token {token!r}")

    presentation = "\n".join(
        (root / rel).read_text(encoding="utf-8")
        for rel in plan["documentation_paths"]
    ).lower()
    required_limit_markers = (
        "não substitui",
        "não autoriz",
        "arm64",
        "certifica",
        "desempenho universal",
    )
    for marker in required_limit_markers:
        if marker not in presentation:
            errors.append(f"presentation layer missing claim-limit marker: {marker}")
    return errors


def self_test() -> list[str]:
    errors: list[str] = []

    scope_errors = check_scope_entries(
        [("M", "README.md"), ("M", "src/runtime.cpp")],
        {"README.md"},
        {"README.md"},
    )
    if not any("outside CS000B allowlist" in item for item in scope_errors):
        errors.append("self-test: scope allowlist failed to reject product path")

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "docs").mkdir()
        good = root / "docs" / "good.md"
        target = root / "docs" / "target.md"
        target.write_text("# target\n", encoding="utf-8")
        good.write_text(
            "# good\n\n[ok](target.md)\n\n```text\nok\n```\n",
            encoding="utf-8",
        )
        if check_markdown_text(good.read_text(encoding="utf-8"), good, root):
            errors.append("self-test: valid Markdown fixture rejected")

        bad = root / "docs" / "bad.md"
        bad.write_text(
            "# bad\n\n[broken](missing.md)\n\n```text\n",
            encoding="utf-8",
        )
        bad_errors = check_markdown_text(
            bad.read_text(encoding="utf-8"), bad, root
        )
        if not any("broken relative link" in item for item in bad_errors):
            errors.append("self-test: broken link was not rejected")
        if not any("unbalanced Markdown fence" in item for item in bad_errors):
            errors.append("self-test: unbalanced fence was not rejected")

    return errors


def emit(errors: list[str], label: str) -> int:
    if errors:
        print(f"DCORE DOCUMENTATION MODERNIZATION: REJECT — {label}")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"DCORE DOCUMENTATION MODERNIZATION: PASS — {label}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--self-test", action="store_true")
    group.add_argument("--scope", action="store_true")
    group.add_argument("--protected-bytes", action="store_true")
    group.add_argument("--markdown", action="store_true")
    group.add_argument("--status", action="store_true")
    group.add_argument("--project-boundary", action="store_true")
    args = parser.parse_args()

    root = Path(".").resolve()
    if args.self_test:
        return emit(self_test(), "verifier-self-test")

    plan = load_plan(root)
    if args.scope:
        entries = changed_entries(root, plan["base_sha"])
        return emit(
            check_scope_entries(
                entries,
                set(plan["allowed_changed_paths"]),
                set(plan["required_candidate_paths"]),
            ),
            "documentation-scope",
        )
    if args.protected_bytes:
        return emit(check_protected_bytes(root, plan), "protected-base-bytes")
    if args.markdown:
        return emit(check_markdown(root, plan), "markdown-integrity")
    if args.status:
        return emit(check_status(root, plan), "status-binding")
    if args.project_boundary:
        return emit(check_project_boundary(root, plan), "project-boundary")
    raise AssertionError("unreachable")


if __name__ == "__main__":
    raise SystemExit(main())
