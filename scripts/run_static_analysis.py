#!/usr/bin/env python3
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCOPES = (
    ROOT / "src",
    ROOT / "modules/host_sdk/src",
    ROOT / "modules/distributed_reference/src",
)
CHECKS = (
    "-*,clang-analyzer-core.*,clang-analyzer-cplusplus.*,"
    "clang-analyzer-deadcode.*,clang-analyzer-security.*"
)
HEADER_FILTER = (
    r"[\\/](include[\\/]neoeng|modules[\\/]"
    r"(host_sdk|distributed_reference)[\\/](include|src))[\\/]"
)
EXCLUDE_HEADER_FILTER = r"[\\/](vcpkg_installed|usr[\\/]include)[\\/]"
SOURCE_CHECK_SUPPRESSIONS = {
    "src/exact_oblique_tree_oracle.cpp": (
        "clang-analyzer-security.ArrayBound",
    ),
}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--clang-tidy", default="clang-tidy")
    parser.add_argument(
        "--jobs",
        type=int,
        default=min(4, max(1, os.cpu_count() or 1)),
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be at least 1")
    executable = shutil.which(args.clang_tidy)
    if not executable:
        print(f"clang-tidy not found: {args.clang_tidy}", file=sys.stderr)
        return 1
    compile_commands = args.build_dir / "compile_commands.json"
    if not compile_commands.is_file():
        print(f"compile_commands.json missing: {compile_commands}", file=sys.stderr)
        return 1
    sources = sorted(
        path for scope in SCOPES for path in scope.glob("*.cpp")
        if path.is_file()
    )
    args.output.mkdir(parents=True, exist_ok=True)
    def analyze(source: Path) -> tuple[str, list[str], int]:
        relative = source.relative_to(ROOT).as_posix()
        suppressed = SOURCE_CHECK_SUPPRESSIONS.get(relative, ())
        source_checks = CHECKS + "".join(
            f",-{check}" for check in suppressed
        )
        line_filter = json.dumps(
            [{
                "name": str(source.resolve()),
                "lines": [[1, len(source.read_text(
                    encoding="utf-8", errors="replace"
                ).splitlines())]],
            }],
            separators=(",", ":"),
        )
        command = [
            executable,
            str(source),
            f"--checks={source_checks}",
            f"--header-filter={HEADER_FILTER}",
            f"--exclude-header-filter={EXCLUDE_HEADER_FILTER}",
            f"--line-filter={line_filter}",
            "--warnings-as-errors=*",
            "--quiet",
            "-p",
            str(args.build_dir),
        ]
        completed = subprocess.run(
            command,
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        return (
            relative,
            [
                f"$ {' '.join(command)}",
                completed.stdout,
                completed.stderr,
                f"exit_code={completed.returncode}",
            ],
            completed.returncode,
        )

    with concurrent.futures.ThreadPoolExecutor(
        max_workers=args.jobs
    ) as executor:
        results = sorted(executor.map(analyze, sources))
    raw_lines = [
        line for _, lines, _ in results for line in lines
    ]
    failures = [
        relative for relative, _, returncode in results if returncode != 0
    ]
    (args.output / "raw-static-analysis.txt").write_text(
        "\n".join(raw_lines), encoding="utf-8", newline="\n"
    )
    summary = {
        "schema": "neoeng.dcore.static-analysis-summary.v1",
        "project_version": "1.14.0",
        "status": "passed" if not failures else "failed",
        "engine": Path(executable).name,
        "checks": CHECKS,
        "header_filter": HEADER_FILTER,
        "exclude_header_filter": EXCLUDE_HEADER_FILTER,
        "diagnostic_scope": "supported implementation source files",
        "documented_suppressions": [{
            "source": source,
            "checks": list(checks),
            "reason": (
                "LLVM 22 false positive is emitted inside the Boost "
                "multiprecision unchecked cpp_int implementation; all other "
                "analyzer checks remain blocking for this translation unit"
            ),
        } for source, checks in sorted(SOURCE_CHECK_SUPPRESSIONS.items())],
        "sources_analyzed": len(sources),
        "parallel_jobs": args.jobs,
        "failed_sources": failures,
        "blocking": True,
    }
    (args.output / "static-analysis-summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
