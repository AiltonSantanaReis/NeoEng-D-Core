#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

AUTHORITATIVE_RUNTIME = (
    "src/simulation.cpp",
    "src/rollback.cpp",
    "src/component_world.cpp",
    "src/contact_solver.cpp",
    "src/general_lcp_solver.cpp",
    "src/atomic_temporal_physics.cpp",
)


def source_texts(root: Path) -> tuple[str, str, str, str]:
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    header = (root / "include/neoeng/core/numeric_contract.hpp").read_text(
        encoding="utf-8"
    )
    contract = (root / "docs/contracts/NUMERIC_CLOSURE_V1.md").read_text(
        encoding="utf-8"
    )
    authoritative = "\n".join(
        (root / relative).read_text(encoding="utf-8", errors="replace")
        for relative in AUTHORITATIVE_RUNTIME
    )
    return cmake, header, contract, authoritative


def validate(
    cmake: str, header: str, contract: str, authoritative: str
) -> list[str]:
    errors: list[str] = []
    version = re.search(
        r"project\(NeoEngDCore VERSION (\d+)\.(\d+)\.(\d+) LANGUAGES C CXX\)",
        cmake,
    )
    if version is None or tuple(map(int, version.groups())) < (1, 11, 0):
        errors.append("unexpected project baseline")
    for required in (
        "src/numeric_contract.cpp",
        "neoeng_numeric_closure_tests",
        "neoeng_numeric_closure_probe",
    ):
        if required not in cmake:
            errors.append(f"numeric closure integration missing: {required}")
    for required in (
        "y1_o4_runtime_claim_allowed{}",
        "global_composed_numeric_certificate_claim_allowed{}",
        "exact_oblique_maximum_bodies{10U}",
        "raa_maximum_terms{16U}",
    ):
        if required not in header:
            errors.append(f"numeric policy invariant missing: {required}")
    lowered_contract = " ".join(contract.lower().split())
    for required in (
        "y1-o4",
        "rejected as a runtime product claim",
        "global certificate",
        "connected coordinate fallback",
        "operational, but explicitly non-certified",
        "arm64",
    ):
        if required not in lowered_contract:
            errors.append(f"numeric contract decision missing: {required}")
    lowered_runtime = authoritative.lower()
    for forbidden in (
        "run_uncertainty_lab",
        "run_fixed_raa_microkernel",
        "run_fixed_raa_active_island_shadow",
        "fixed_raa_",
    ):
        if forbidden in lowered_runtime:
            errors.append(
                f"research numerical path reached authoritative runtime: {forbidden}"
            )
    return errors


def self_test(cmake: str, header: str, contract: str, authoritative: str) -> bool:
    mutations = (
        (cmake.replace("src/numeric_contract.cpp", ""), header, contract, authoritative),
        (
            cmake,
            header.replace(
                "y1_o4_runtime_claim_allowed{}",
                "y1_o4_runtime_claim_allowed{true}",
            ),
            contract,
            authoritative,
        ),
        (
            cmake,
            header,
            contract.replace("Y1-O4", "removed"),
            authoritative,
        ),
        (cmake, header, contract, authoritative + "\nrun_fixed_raa_microkernel();\n"),
    )
    return all(validate(*mutation) for mutation in mutations)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    texts = source_texts(ROOT)
    if args.self_test:
        passed = self_test(*texts)
        print("numeric_closure_boundary_self_test=" + ("passed" if passed else "failed"))
        return 0 if passed else 1
    errors = validate(*texts)
    if errors:
        print("\n".join(errors))
        return 1
    print(
        "OK: unsupported global numerical claims are rejected; "
        "certificates remain confined to their declared scopes."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
