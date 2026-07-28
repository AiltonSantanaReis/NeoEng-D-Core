#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def source_texts(root: Path) -> tuple[str, str, str, str, str]:
    return (
        (root / "CMakeLists.txt").read_text(encoding="utf-8"),
        (root / "include/neoeng/core/temporal_contract.hpp").read_text(
            encoding="utf-8"
        ),
        (root / "src/temporal_contract.cpp").read_text(encoding="utf-8"),
        (root / "docs/contracts/TEMPORAL_CLOSURE_V1.md").read_text(
            encoding="utf-8"
        ),
        (root / "tests/temporal_closure_tests.cpp").read_text(encoding="utf-8"),
    )


def validate(
    cmake: str, header: str, source: str, contract: str, tests: str
) -> list[str]:
    errors: list[str] = []
    version = re.search(
        r"project\(NeoEngDCore VERSION (\d+)\.(\d+)\.(\d+) LANGUAGES C CXX\)",
        cmake,
    )
    if version is None or tuple(map(int, version.groups())) < (1, 12, 0):
        errors.append("unexpected project baseline")
    for required in (
        "src/temporal_contract.cpp",
        "neoeng_temporal_closure_tests",
        "neoeng_temporal_closure_probe",
    ):
        if required not in cmake:
            errors.append(f"temporal integration missing: {required}")
    for required in (
        "DurableTimelineRecorder",
        "ExternalEffectLedger",
        "CommittedEffectCrossedRollback",
        "mandatory_operational_paths_v1",
        "canonical_world_v1_fields",
    ):
        if required not in header:
            errors.append(f"temporal public contract missing: {required}")
    for required in (
        "stable_flush_file",
        "SequenceMismatch",
        "ChainMismatch",
        "IdempotencyConflict",
        "NotConfirmed",
    ):
        if required not in source:
            errors.append(f"fail-closed implementation missing: {required}")
    normalized = " ".join(contract.lower().split())
    for required in (
        "append-only",
        "confirmed horizon",
        "rollback cannot undo",
        "external trust anchor",
        "worldstate v1",
        "machines with lower or higher capability",
    ):
        if required not in normalized:
            errors.append(f"normative decision missing: {required}")
    for required in (
        "one-byte tampering",
        "IdempotencyConflict",
        "CommittedEffectCrossedRollback",
        "BudgetId::EcsMaintenance",
        "BudgetId::DivergenceLocalization",
    ):
        if required not in tests:
            errors.append(f"required test coverage missing: {required}")
    return errors


def self_test(texts: tuple[str, str, str, str, str]) -> bool:
    cmake, header, source, contract, tests = texts
    mutations = (
        (cmake.replace("src/temporal_contract.cpp", ""), header, source, contract, tests),
        (
            cmake,
            header.replace("DurableTimelineRecorder", "RemovedRecorder"),
            source,
            contract,
            tests,
        ),
        (
            cmake,
            header,
            source.replace("IdempotencyConflict", "ConflictRemoved"),
            contract,
            tests,
        ),
        (
            cmake,
            header,
            source,
            contract.replace("Rollback cannot undo", "Rollback may undo"),
            tests,
        ),
        (
            cmake,
            header,
            source,
            contract,
            tests.replace("one-byte tampering", "tampering omitted"),
        ),
    )
    return all(validate(*mutation) for mutation in mutations)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    texts = source_texts(ROOT)
    if args.self_test:
        passed = self_test(texts)
        print(
            "temporal_closure_boundary_self_test="
            + ("passed" if passed else "failed")
        )
        return 0 if passed else 1
    errors = validate(*texts)
    if errors:
        print("\n".join(errors))
        return 1
    print(
        "OK: durable temporal records, canonical coverage, mandatory "
        "instrumentation and external-effect boundaries are explicit."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
