#!/usr/bin/env python3
from __future__ import annotations

import argparse
import copy
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parents[1]

BASE = "e9e095e61d4de2995db704c51e9308850e1c929d"
R1_SOURCE = "4adccdb77607fc6d6f886369a842d2efa48a7028"
BRANCH = "agent/cs018-ev01-build-ci-governance-hardening"

PERMANENT = ".github/workflows/current-product-regression.yml"
QUALIFYING = ".github/workflows/cs018-validation.yml"
PLAN = "audit/validation/CS018/VALIDATION_PLAN.json"
PLAN_R2 = "audit/validation/CS018/VALIDATION_PLAN_R2.json"
ATTEMPT_R1 = "audit/validation/CS018/ATTEMPT_001_NONACCEPTANCE.json"
DISCOVERY = "docs/changesets/018/BUILD_CI_DISCOVERY.json"
CHANGESET_R1 = "docs/changesets/018/CHANGESET.md"
CHANGESET_R2 = "docs/changesets/018/CHANGESET_R2.md"
ROADMAP = "audit/EVOLUTION_ROADMAP.json"
REQS = "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json"
DESCRIPTOR = "audit/CURRENT_CHANGESET_VALIDATION.json"

HISTORICAL = (
    ".github/workflows/cs010-cross-compiler.yml",
    ".github/workflows/cs011-cross-compiler.yml",
    ".github/workflows/cs012-cross-compiler.yml",
    ".github/workflows/cs013-cross-compiler.yml",
    ".github/workflows/cs014-release-assurance.yml",
    ".github/workflows/cs015-final-acceptance.yml",
)

EXPECTED_SOURCE_PATHS = {
    ".github/workflows/current-product-regression.yml",
    ".github/workflows/cs018-validation.yml",
    "scripts/verify_build_ci_governance.py",
    "scripts/verify_evolution_plan.py",
    "audit/EVOLUTION_ROADMAP.json",
    "audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json",
    "audit/validation/CS018/VALIDATION_PLAN.json",
    "docs/changesets/018/CHANGESET.md",
    "docs/changesets/018/BUILD_CI_DISCOVERY.json",
    "MANIFEST.sha256",
}

EXPECTED_R2_SOURCE_PATHS = EXPECTED_SOURCE_PATHS | {
    ATTEMPT_R1,
    PLAN_R2,
    CHANGESET_R2,
}

SHA40 = re.compile(r"^[0-9a-f]{40}$")
OPTION = re.compile(r"\boption\s*\(\s*(NEOENG_[A-Z0-9_]+)")
CMAKE_USE = re.compile(r"-D(NEOENG_[A-Z0-9_]+)")
USES = re.compile(r"""uses:\s*["']?([^"'#\s]+)""")


def run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        args,
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


def git_show(ref: str, rel: str) -> bytes | None:
    p = subprocess.run(
        ["git", "show", f"{ref}:{rel}"],
        cwd=ROOT,
        capture_output=True,
        check=False,
    )
    return p.stdout if p.returncode == 0 else None


def load(path: str) -> dict:
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


def load_at(ref: str, path: str) -> dict:
    raw = git_show(ref, path)
    if raw is None:
        raise ValueError(f"cannot read {path} at {ref}")
    value = json.loads(raw.decode("utf-8"))
    if not isinstance(value, dict):
        raise ValueError(f"{path} root must be object")
    return value


def workflow_paths_current() -> list[str]:
    root = ROOT / ".github/workflows"
    return sorted(
        p.relative_to(ROOT).as_posix()
        for p in root.iterdir()
        if p.is_file() and p.suffix.lower() in {".yml", ".yaml"}
    )


def workflow_paths_at(ref: str) -> list[str]:
    p = run(
        "git", "ls-tree", "-r", "--name-only",
        ref, "--", ".github/workflows"
    )
    if p.returncode != 0:
        raise ValueError("cannot enumerate workflows at ref")
    return sorted(
        x.strip()
        for x in p.stdout.splitlines()
        if x.strip().endswith((".yml", ".yaml"))
    )


def workflow_text_current(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def workflow_text_at(ref: str, path: str) -> str:
    raw = git_show(ref, path)
    if raw is None:
        raise ValueError(f"cannot read workflow at {ref}: {path}")
    return raw.decode("utf-8")


def parse_on(text: str) -> tuple[set[str], list[str] | None]:
    lines = text.splitlines()
    start = None

    for i, line in enumerate(lines):
        if line == "on:":
            start = i + 1
            break
        if line.startswith("on:") and not line.startswith(" "):
            # Inline top-level event syntax is treated as current/fail-closed.
            return {"__inline__"}, None

    if start is None:
        return {"__missing__"}, None

    events: set[str] = set()
    push_branches: list[str] | None = None
    current_event: str | None = None
    collecting_push_branches = False

    for line in lines[start:]:
        if not line.strip() or line.lstrip().startswith("#"):
            continue

        indent = len(line) - len(line.lstrip(" "))

        if indent == 0:
            break

        stripped = line.strip()

        if indent == 2 and stripped.endswith(":"):
            current_event = stripped[:-1].strip()
            events.add(current_event)
            collecting_push_branches = False
            continue

        if current_event == "push" and indent == 4:
            if stripped.startswith("branches:"):
                collecting_push_branches = True
                push_branches = []
                rhs = stripped.split(":", 1)[1].strip()
                if rhs.startswith("[") and rhs.endswith("]"):
                    body = rhs[1:-1].strip()
                    if body:
                        push_branches.extend(
                            x.strip().strip("'\"")
                            for x in body.split(",")
                            if x.strip()
                        )
                continue

            collecting_push_branches = False

        if (
            current_event == "push"
            and collecting_push_branches
            and indent >= 6
            and stripped.startswith("- ")
        ):
            push_branches.append(stripped[2:].strip().strip("'\""))

    return events, push_branches


def historical_branch_literal(value: str) -> bool:
    if not value:
        return False
    if value in {"main", "refs/heads/main"}:
        return False
    if value.startswith("!"):
        return False
    if "${{" in value:
        return False
    if any(c in value for c in "*?[]"):
        return False
    return True


def classify(text: str) -> str:
    events, branches = parse_on(text)
    automatic = events - {"workflow_dispatch"}

    if not automatic:
        return "MANUAL_ONLY"

    if {
        "pull_request",
        "pull_request_target",
        "schedule",
    } & automatic:
        return "CURRENT"

    if automatic == {"push"}:
        if not branches:
            return "CURRENT"
        if all(historical_branch_literal(x) for x in branches):
            return "HISTORICAL_BRANCH"
        return "CURRENT"

    # Unknown automatic triggers are current/fail-closed.
    return "CURRENT"


def uses_values(text: str) -> list[str]:
    values: list[str] = []
    for line in text.splitlines():
        match = USES.search(line)
        if match:
            values.append(match.group(1))
    return values


def is_pinned_action(value: str) -> bool:
    if value.startswith("./"):
        return True
    if value.startswith("docker://"):
        return "@sha256:" in value

    if "@" not in value:
        return False

    _, ref = value.rsplit("@", 1)
    return SHA40.fullmatch(ref) is not None


def defined_options_current() -> set[str]:
    files = [ROOT / "CMakeLists.txt"]
    files.extend(ROOT.glob("cmake/**/*.cmake"))

    values: set[str] = set()
    for path in files:
        if path.is_file():
            values.update(OPTION.findall(path.read_text(encoding="utf-8")))
    return values


def defined_options_at(ref: str) -> set[str]:
    p = run(
        "git", "ls-tree", "-r", "--name-only",
        ref, "--", "CMakeLists.txt", "cmake"
    )
    if p.returncode != 0:
        raise ValueError("cannot enumerate CMake files at ref")

    values: set[str] = set()

    for rel in p.stdout.splitlines():
        rel = rel.strip()
        if not rel:
            continue
        if rel != "CMakeLists.txt" and not rel.endswith(".cmake"):
            continue
        raw = git_show(ref, rel)
        if raw is not None:
            values.update(OPTION.findall(raw.decode("utf-8")))

    return values


def is_regression_workflow(text: str) -> bool:
    return (
        ("cmake -S " in text or "cmake --preset " in text)
        and "cmake --build " in text
        and re.search(r"\bctest\b", text) is not None
    )


def analyze(
    paths: list[str],
    loader: Callable[[str], str],
    defined_options: set[str],
) -> dict:
    classifications: dict[str, str] = {}
    current_unpinned: list[dict[str, str]] = []
    historical_unpinned: list[dict[str, str]] = []
    current_unknown: list[dict[str, str]] = []
    historical_unknown: list[dict[str, str]] = []
    current_regression: list[str] = []

    for path in paths:
        text = loader(path)
        kind = classify(text)
        classifications[path] = kind

        if kind == "CURRENT" and is_regression_workflow(text):
            current_regression.append(path)

        for value in uses_values(text):
            if is_pinned_action(value):
                continue
            row = {"workflow": path, "uses": value}
            if kind == "CURRENT":
                current_unpinned.append(row)
            elif kind == "HISTORICAL_BRANCH":
                historical_unpinned.append(row)

        for option in CMAKE_USE.findall(text):
            if option in defined_options:
                continue
            row = {"workflow": path, "option": option}
            if kind == "CURRENT":
                current_unknown.append(row)
            elif kind == "HISTORICAL_BRANCH":
                historical_unknown.append(row)

    return {
        "classifications": classifications,
        "current_unpinned_actions": current_unpinned,
        "historical_unpinned_actions": historical_unpinned,
        "current_unknown_neoeng_options": current_unknown,
        "historical_unknown_neoeng_options": historical_unknown,
        "current_product_regression_workflows": current_regression,
    }


def baseline_analysis() -> dict:
    return analyze(
        workflow_paths_at(BASE),
        lambda path: workflow_text_at(BASE, path),
        defined_options_at(BASE),
    )


def current_analysis() -> dict:
    return analyze(
        workflow_paths_current(),
        workflow_text_current,
        defined_options_current(),
    )


def classification_errors() -> list[str]:
    errors: list[str] = []

    report = load(DISCOVERY)
    baseline = baseline_analysis()
    current = current_analysis()

    expected_baseline = {
        "current_unpinned_actions": 0,
        "current_unknown_neoeng_options": 0,
        "historical_unpinned_actions": 43,
        "historical_unknown_neoeng_options": 7,
        "current_product_regression_workflows": 0,
    }

    actual_baseline = {
        "current_unpinned_actions":
            len(baseline["current_unpinned_actions"]),
        "current_unknown_neoeng_options":
            len(baseline["current_unknown_neoeng_options"]),
        "historical_unpinned_actions":
            len(baseline["historical_unpinned_actions"]),
        "historical_unknown_neoeng_options":
            len(baseline["historical_unknown_neoeng_options"]),
        "current_product_regression_workflows":
            len(baseline["current_product_regression_workflows"]),
    }

    if actual_baseline != expected_baseline:
        errors.append(
            "base discovery counters differ: "
            + repr(actual_baseline)
        )

    if report.get("base_sha") != BASE:
        errors.append("discovery report base_sha mismatch")

    if report.get("baseline") != expected_baseline:
        errors.append("discovery report baseline counters mismatch")

    expected_candidate = {
        "current_unpinned_actions": 0,
        "current_unknown_neoeng_options": 0,
        "historical_unpinned_actions": 43,
        "historical_unknown_neoeng_options": 7,
        "current_product_regression_workflows": 1,
    }

    actual_candidate = {
        "current_unpinned_actions":
            len(current["current_unpinned_actions"]),
        "current_unknown_neoeng_options":
            len(current["current_unknown_neoeng_options"]),
        "historical_unpinned_actions":
            len(current["historical_unpinned_actions"]),
        "historical_unknown_neoeng_options":
            len(current["historical_unknown_neoeng_options"]),
        "current_product_regression_workflows":
            len(current["current_product_regression_workflows"]),
    }

    if actual_candidate != expected_candidate:
        errors.append(
            "candidate discovery counters differ: "
            + repr(actual_candidate)
        )

    if report.get("candidate_expected") != expected_candidate:
        errors.append("discovery report candidate counters mismatch")

    if current["classifications"].get(PERMANENT) != "CURRENT":
        errors.append("permanent regression workflow is not CURRENT")

    if current["classifications"].get(QUALIFYING) != "HISTORICAL_BRANCH":
        errors.append(
            "CS018 R2 qualifying workflow is not HISTORICAL_BRANCH"
        )

    for path in HISTORICAL:
        if (
            current["classifications"].get(path)
            != "HISTORICAL_BRANCH"
        ):
            errors.append(
                f"historical workflow classification changed: {path}"
            )

    unknown_names = {
        row["option"]
        for row in current["historical_unknown_neoeng_options"]
    }
    if unknown_names != {"NEOENG_DCORE_BUILD_VIEWER"}:
        errors.append(
            "historical unknown option set differs: "
            + repr(sorted(unknown_names))
        )

    return errors


def action_pinning_errors() -> list[str]:
    errors: list[str] = []
    analysis = current_analysis()

    if analysis["current_unpinned_actions"]:
        errors.append(
            "CURRENT unpinned actions: "
            + repr(analysis["current_unpinned_actions"])
        )

    qualifying = workflow_text_current(QUALIFYING)
    unpinned = [
        value
        for value in uses_values(qualifying)
        if not is_pinned_action(value)
    ]

    if unpinned:
        errors.append(
            "qualifying workflow has unpinned action refs: "
            + repr(unpinned)
        )

    return errors


def cmake_option_errors() -> list[str]:
    errors: list[str] = []
    defined = defined_options_current()
    analysis = current_analysis()

    if analysis["current_unknown_neoeng_options"]:
        errors.append(
            "CURRENT workflows use unknown NEOENG options: "
            + repr(analysis["current_unknown_neoeng_options"])
        )

    qualifying = workflow_text_current(QUALIFYING)
    unknown_qualifying = sorted(
        {
            option
            for option in CMAKE_USE.findall(qualifying)
            if option not in defined
        }
    )

    if unknown_qualifying:
        errors.append(
            "qualifying workflow uses unknown NEOENG options: "
            + repr(unknown_qualifying)
        )

    return errors


def regression_workflow_errors() -> list[str]:
    errors: list[str] = []
    text = workflow_text_current(PERMANENT)
    events, branches = parse_on(text)

    if classify(text) != "CURRENT":
        errors.append("permanent regression workflow is not CURRENT")

    if "pull_request" not in events:
        errors.append("permanent regression lacks pull_request")

    if "push" not in events or not branches or "main" not in branches:
        errors.append("permanent regression lacks push main")

    if "pull_request_target" in events:
        errors.append(
            "permanent product regression must not use pull_request_target"
        )

    required_tokens = (
        "uses: actions/checkout@11d5960a326750d5838078e36cf38b85af677262",
        "persist-credentials: false",
        "cmake -S . -B build/current-regression -G Ninja",
        "-DBUILD_TESTING=ON",
        "-DNEOENG_WARNINGS_AS_ERRORS=ON",
        "-DNEOENG_DCORE_BUILD_FULL_TOOLSET=OFF",
        "-DNEOENG_DCORE_BUILD_RELEASE_TOOLS=ON",
        "-DNEOENG_DCORE_BUILD_RESEARCH_TOOLS=OFF",
        "-DNEOENG_DCORE_BUILD_VIEW_LAB=OFF",
        "cmake --build build/current-regression --parallel 2",
        "ctest --test-dir build/current-regression",
        "--output-on-failure -L smoke",
    )

    for token in required_tokens:
        if token not in text:
            errors.append(
                f"permanent regression token missing: {token}"
            )

    analysis = current_analysis()

    if analysis["current_product_regression_workflows"] != [PERMANENT]:
        errors.append(
            "expected exactly the permanent workflow as current "
            "product regression"
        )

    return errors


def historical_boundary_errors() -> list[str]:
    errors: list[str] = []

    for path in HISTORICAL:
        prior = git_show(BASE, path)
        current = ROOT / path

        if prior is None:
            errors.append(f"historical workflow missing at base: {path}")
            continue

        if not current.is_file():
            errors.append(f"historical workflow missing now: {path}")
            continue

        if current.read_bytes() != prior:
            errors.append(f"historical workflow bytes changed: {path}")

    return errors



def r2_trigger_errors() -> list[str]:
    errors: list[str] = []
    text = workflow_text_current(QUALIFYING)
    events, branches = parse_on(text)

    if events != {"push"}:
        errors.append(
            "R2 qualifying workflow must have only push automatic trigger"
        )

    if branches != [BRANCH]:
        errors.append(
            "R2 qualifying workflow must target exact CS018 branch"
        )

    if classify(text) != "HISTORICAL_BRANCH":
        errors.append(
            "R2 qualifying workflow must classify HISTORICAL_BRANCH"
        )

    required_trigger_paths = (
        QUALIFYING,
        ATTEMPT_R1,
        PLAN_R2,
        CHANGESET_R2,
        "scripts/verify_build_ci_governance.py",
    )

    for path in required_trigger_paths:
        token = f"- '{path}'"
        if token not in text:
            errors.append(
                f"R2 qualifying push path missing: {path}"
            )

    if "workflow_dispatch:" in text:
        errors.append(
            "R2 qualifying workflow must not depend on workflow_dispatch"
        )

    if "name: CS018 R2 validation" not in text:
        errors.append("R2 evidence job name missing")

    return errors


def r1_preservation_errors() -> list[str]:
    errors: list[str] = []

    attempt = load(ATTEMPT_R1)

    expected = {
        "schema":
            "neoeng.dcore.changeset-validation-attempt-record.v1",
        "changeset": "CS018",
        "attempt": 1,
        "plan_path":
            "audit/validation/CS018/VALIDATION_PLAN.json",
        "plan_commit": R1_SOURCE,
        "source_sha": R1_SOURCE,
        "workflow_path": QUALIFYING,
        "execution_attempted": False,
        "dispatch_attempted": False,
        "workflow_run_created": False,
        "validation_state": "BLOCKED",
        "acceptance_decision": "NOT_ACCEPTED",
        "classification": "PRE_EXECUTION_PLATFORM_CONSTRAINT",
        "rerun_attempt1": False,
        "next_plan":
            "audit/validation/CS018/VALIDATION_PLAN_R2.json",
    }

    for key, value in expected.items():
        if attempt.get(key) != value:
            errors.append(f"R1 attempt record {key} mismatch")

    if attempt.get("run_id") is not None:
        errors.append("R1 blocked campaign must not invent run_id")

    if attempt.get("run_attempt") is not None:
        errors.append("R1 blocked campaign must not invent run_attempt")

    preserve = (
        PLAN,
        DISCOVERY,
        CHANGESET_R1,
        PERMANENT,
        ROADMAP,
        REQS,
        "scripts/verify_evolution_plan.py",
    )

    for rel in preserve:
        prior = git_show(R1_SOURCE, rel)
        current = ROOT / rel

        if prior is None:
            errors.append(f"cannot read R1 preserved file: {rel}")
            continue

        if not current.is_file():
            errors.append(f"R1 preserved file missing: {rel}")
            continue

        if current.read_bytes() != prior:
            errors.append(f"R1 preserved bytes changed: {rel}")

    r1_workflow = workflow_text_at(R1_SOURCE, QUALIFYING)

    if classify(r1_workflow) != "MANUAL_ONLY":
        errors.append(
            "R1 workflow history is not manual-only at R1 source"
        )

    ancestry = run(
        "git", "merge-base", "--is-ancestor", R1_SOURCE, "HEAD"
    )
    if ancestry.returncode != 0:
        errors.append("R1 source is not ancestor of current HEAD")

    return errors


def cs018_r2_scope_errors() -> list[str]:
    errors: list[str] = []

    p = run("git", "diff", "--name-only", f"{BASE}...HEAD")
    if p.returncode != 0:
        return ["cannot compute CS018 R2 source diff"]

    actual = {
        line.strip()
        for line in p.stdout.splitlines()
        if line.strip()
    }

    if actual != EXPECTED_R2_SOURCE_PATHS:
        extra = sorted(actual - EXPECTED_R2_SOURCE_PATHS)
        missing = sorted(EXPECTED_R2_SOURCE_PATHS - actual)

        if extra:
            errors.append(
                "paths outside CS018 R2 source scope: "
                + ", ".join(extra)
            )

        if missing:
            errors.append(
                "required CS018 R2 source paths missing: "
                + ", ".join(missing)
            )

    base_descriptor = git_show(BASE, DESCRIPTOR)

    if base_descriptor is None:
        errors.append("cannot read base validation descriptor")
    elif (ROOT / DESCRIPTOR).read_bytes() != base_descriptor:
        errors.append(
            "CURRENT_CHANGESET_VALIDATION changed before R2 result"
        )

    forbidden_prefixes = (
        "src/",
        "include/",
        "tests/",
        "apps/",
        "modules/",
        "cmake/",
        "tools/",
    )

    forbidden_exact = {
        "CMakeLists.txt",
        "CMakePresets.json",
        "vcpkg.json",
        "vcpkg-configuration.json",
    }

    product = sorted(
        path
        for path in actual
        if path in forbidden_exact
        or path.startswith(forbidden_prefixes)
    )

    if product:
        errors.append(
            "product/build-definition paths changed: "
            + ", ".join(product)
        )

    return errors


def cs018_ledger_errors() -> list[str]:
    errors: list[str] = []

    prior_roadmap = load_at(BASE, ROADMAP)
    current_roadmap = load(ROADMAP)
    expected_roadmap = copy.deepcopy(prior_roadmap)

    expected_roadmap["current_stage"] = "EV-01"
    ev01 = next(
        x
        for x in expected_roadmap["stages"]
        if x["stage_id"] == "EV-01"
    )
    ev01["status"] = "in_progress"

    if current_roadmap != expected_roadmap:
        errors.append(
            "roadmap differs from exact EV-01 start transition"
        )

    prior_reqs = load_at(BASE, REQS)
    current_reqs = load(REQS)
    expected_reqs = copy.deepcopy(prior_reqs)

    target = {
        "EVREQ-005",
        "EVREQ-006",
        "EVREQ-007",
        "EVREQ-008",
    }

    for row in expected_reqs["requirements"]:
        if row.get("requirement_id") in target:
            row["status"] = "in_progress"

    if current_reqs != expected_reqs:
        errors.append(
            "requirements differ from exact EVREQ-005..008 start transition"
        )

    if current_roadmap.get("release_authorized") is not False:
        errors.append("release_authorized changed")

    ev00 = next(
        x
        for x in current_roadmap["stages"]
        if x["stage_id"] == "EV-00"
    )

    if ev00 != next(
        x
        for x in prior_roadmap["stages"]
        if x["stage_id"] == "EV-00"
    ):
        errors.append("accepted EV-00 binding changed")

    return errors


def cs018_scope_errors() -> list[str]:
    errors: list[str] = []

    p = run("git", "diff", "--name-only", f"{BASE}...HEAD")
    if p.returncode != 0:
        return ["cannot compute CS018 source diff"]

    actual = {
        line.strip()
        for line in p.stdout.splitlines()
        if line.strip()
    }

    if actual != EXPECTED_SOURCE_PATHS:
        extra = sorted(actual - EXPECTED_SOURCE_PATHS)
        missing = sorted(EXPECTED_SOURCE_PATHS - actual)

        if extra:
            errors.append(
                "paths outside CS018 source scope: "
                + ", ".join(extra)
            )
        if missing:
            errors.append(
                "required CS018 source paths missing: "
                + ", ".join(missing)
            )

    base_descriptor = git_show(BASE, DESCRIPTOR)

    if base_descriptor is None:
        errors.append("cannot read base validation descriptor")
    elif (ROOT / DESCRIPTOR).read_bytes() != base_descriptor:
        errors.append(
            "CURRENT_CHANGESET_VALIDATION changed before CS018 result"
        )

    forbidden_prefixes = (
        "src/",
        "include/",
        "tests/",
        "apps/",
        "modules/",
        "cmake/",
        "tools/",
    )
    forbidden_exact = {
        "CMakeLists.txt",
        "CMakePresets.json",
        "vcpkg.json",
        "vcpkg-configuration.json",
    }

    product = sorted(
        path
        for path in actual
        if path in forbidden_exact
        or path.startswith(forbidden_prefixes)
    )

    if product:
        errors.append(
            "product/build-definition paths changed: "
            + ", ".join(product)
        )

    return errors


def plan_structure_errors() -> list[str]:
    errors: list[str] = []

    sys.path.insert(0, str(ROOT))
    from scripts.verify_changeset_validation import (  # noqa: PLC0415
        read_json,
        validate_plan,
    )

    plan = read_json(ROOT / PLAN)
    policy = read_json(ROOT / "audit/CHANGESET_VALIDATION_POLICY.json")

    errors.extend(validate_plan(plan, policy))

    expected = {
        "changeset": "CS018",
        "base_sha": BASE,
        "execution_workflow": QUALIFYING,
        "acceptance_requires_all_required_pass": True,
        "allow_test_removal_after_execution": False,
    }

    for key, value in expected.items():
        if plan.get(key) != value:
            errors.append(f"plan {key} mismatch")

    descriptor = load(DESCRIPTOR)

    if descriptor.get("plan_path") != (
        "audit/validation/CS000I/VALIDATION_PLAN.json"
    ):
        errors.append(
            "descriptor must remain on accepted CS000I before CS018 result"
        )

    if descriptor.get("result_path") != (
        "audit/validation/CS000I/VALIDATION_RESULT.json"
    ):
        errors.append(
            "descriptor result must remain on accepted CS000I "
            "before CS018 result"
        )

    return errors



def r2_plan_structure_errors() -> list[str]:
    errors: list[str] = []

    sys.path.insert(0, str(ROOT))
    from scripts.verify_changeset_validation import (
        read_json,
        validate_plan,
    )

    plan = read_json(ROOT / PLAN_R2)
    policy = read_json(
        ROOT / "audit/CHANGESET_VALIDATION_POLICY.json"
    )

    errors.extend(validate_plan(plan, policy))

    expected = {
        "changeset": "CS018",
        "base_sha": BASE,
        "execution_workflow": QUALIFYING,
        "acceptance_requires_all_required_pass": True,
        "allow_test_removal_after_execution": False,
    }

    for key, value in expected.items():
        if plan.get(key) != value:
            errors.append(f"R2 plan {key} mismatch")

    expected_ids = [
        "cs018.r2.verifier-self-test",
        "cs018.workflow-classification",
        "cs018.action-pinning",
        "cs018.cmake-options",
        "cs018.regression-contract",
        "cs018.historical-boundary",
        "cs018.r2.trigger-contract",
        "cs018.r1-preservation",
        "cs018.ledger-transition",
        "evolution.plan",
        "evolution.self-test",
        "changeset.policy-self-test",
        "cs018.r2.scope",
        "repository.manifest",
        "changeset.r2-plan-structure",
        "product.configure",
        "product.build",
        "product.smoke",
    ]

    actual_ids = [
        item.get("test_id")
        for item in plan.get("required_tests", [])
        if isinstance(item, dict)
    ]

    if actual_ids != expected_ids:
        errors.append("R2 required test inventory/order mismatch")

    frozen = set(plan.get("frozen_files", []))

    must_freeze = {
        PLAN,
        ATTEMPT_R1,
        QUALIFYING,
        "scripts/verify_build_ci_governance.py",
        PERMANENT,
        ROADMAP,
        REQS,
        CHANGESET_R1,
        CHANGESET_R2,
        DISCOVERY,
    }

    missing_frozen = sorted(must_freeze - frozen)

    if missing_frozen:
        errors.append(
            "R2 frozen_files missing: "
            + ", ".join(missing_frozen)
        )

    descriptor = load(DESCRIPTOR)

    if descriptor.get("plan_path") != (
        "audit/validation/CS000I/VALIDATION_PLAN.json"
    ):
        errors.append(
            "descriptor must remain CS000I before R2 result"
        )

    if descriptor.get("result_path") != (
        "audit/validation/CS000I/VALIDATION_RESULT.json"
    ):
        errors.append(
            "descriptor result must remain CS000I before R2 result"
        )

    return errors


def self_test_errors() -> list[str]:
    errors: list[str] = []

    manual = "name: x\n\non:\n  workflow_dispatch:\n"
    if classify(manual) != "MANUAL_ONLY":
        errors.append("manual-only classifier fixture failed")

    historical = (
        "name: x\n\non:\n"
        "  workflow_dispatch:\n"
        "  push:\n"
        "    branches: [changeset-013]\n"
    )
    if classify(historical) != "HISTORICAL_BRANCH":
        errors.append("historical classifier fixture failed")

    current_pr = "name: x\n\non:\n  pull_request:\n"
    if classify(current_pr) != "CURRENT":
        errors.append("pull_request classifier fixture failed")

    current_main = (
        "name: x\n\non:\n"
        "  push:\n"
        "    branches: [main]\n"
    )
    if classify(current_main) != "CURRENT":
        errors.append("main push classifier fixture failed")

    fail_closed = "name: x\n\non:\n  merge_group:\n"
    if classify(fail_closed) != "CURRENT":
        errors.append("unknown automatic trigger was not fail-closed")

    if not is_pinned_action(
        "actions/checkout@"
        "11d5960a326750d5838078e36cf38b85af677262"
    ):
        errors.append("pinned action fixture failed")

    if is_pinned_action("actions/checkout@v4"):
        errors.append("unpinned action fixture accepted")

    options = {"NEOENG_REAL"}
    text = "-DNEOENG_REAL=ON\n-DNEOENG_UNKNOWN=OFF\n"
    unknown = sorted(
        set(CMAKE_USE.findall(text)) - options
    )

    if unknown != ["NEOENG_UNKNOWN"]:
        errors.append("unknown CMake option fixture failed")

    return errors


def emit(label: str, errors: list[str]) -> int:
    if errors:
        print(f"BUILD/CI GOVERNANCE: REJECT — {label}")
        for item in errors:
            print(f"- {item}")
        return 1

    print(f"BUILD/CI GOVERNANCE: PASS — {label}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--classification", action="store_true")
    parser.add_argument("--action-pinning", action="store_true")
    parser.add_argument("--cmake-options", action="store_true")
    parser.add_argument("--regression-workflow", action="store_true")
    parser.add_argument("--historical-boundary", action="store_true")
    parser.add_argument("--r2-trigger", action="store_true")
    parser.add_argument("--r1-preservation", action="store_true")
    parser.add_argument("--cs018-ledger", action="store_true")
    parser.add_argument("--cs018-scope", action="store_true")
    parser.add_argument("--cs018-r2-scope", action="store_true")
    parser.add_argument("--plan-structure", action="store_true")
    parser.add_argument("--r2-plan-structure", action="store_true")
    args = parser.parse_args()

    selected = [
        args.self_test,
        args.classification,
        args.action_pinning,
        args.cmake_options,
        args.regression_workflow,
        args.historical_boundary,
        args.r2_trigger,
        args.r1_preservation,
        args.cs018_ledger,
        args.cs018_scope,
        args.cs018_r2_scope,
        args.plan_structure,
        args.r2_plan_structure,
    ]

    if sum(selected) != 1:
        parser.error("select exactly one verification mode")

    try:
        if args.self_test:
            return emit("self-test", self_test_errors())
        if args.classification:
            return emit(
                "workflow-classification",
                classification_errors(),
            )
        if args.action_pinning:
            return emit(
                "critical-action-pinning",
                action_pinning_errors(),
            )
        if args.cmake_options:
            return emit(
                "current-cmake-options",
                cmake_option_errors(),
            )
        if args.regression_workflow:
            return emit(
                "permanent-regression-contract",
                regression_workflow_errors(),
            )
        if args.historical_boundary:
            return emit(
                "historical-workflow-boundary",
                historical_boundary_errors(),
            )
        if args.r2_trigger:
            return emit(
                "r2-qualifying-trigger-contract",
                r2_trigger_errors(),
            )
        if args.r1_preservation:
            return emit(
                "r1-blocked-campaign-preservation",
                r1_preservation_errors(),
            )
        if args.cs018_ledger:
            return emit(
                "cs018-ledger-transition",
                cs018_ledger_errors(),
            )
        if args.cs018_scope:
            return emit(
                "cs018-source-scope",
                cs018_scope_errors(),
            )
        if args.cs018_r2_scope:
            return emit(
                "cs018-r2-source-scope",
                cs018_r2_scope_errors(),
            )
        if args.plan_structure:
            return emit(
                "validation-plan-structure",
                plan_structure_errors(),
            )

        return emit(
            "validation-plan-r2-structure",
            r2_plan_structure_errors(),
        )
    except Exception as exc:
        return emit("exception", [str(exc)])


if __name__ == "__main__":
    raise SystemExit(main())
