#!/usr/bin/env python3
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def source_texts(root: Path) -> tuple[str, str, str]:
    root_cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    module_cmake = (
        root / "modules/distributed_reference/CMakeLists.txt"
    ).read_text(encoding="utf-8")
    canonical = "\n".join(
        path.read_text(encoding="utf-8", errors="replace")
        for directory in (root / "include/neoeng/core", root / "src")
        for path in directory.rglob("*")
        if path.is_file() and path.suffix in {".hpp", ".cpp"}
    )
    return root_cmake, module_cmake, canonical


def validate(root_cmake: str, module_cmake: str, canonical: str) -> list[str]:
    errors: list[str] = []
    if "project(NeoEngDCore VERSION 1.10.0 LANGUAGES C CXX)" not in root_cmake:
        errors.append("unexpected project baseline")
    if "add_subdirectory(modules/distributed_reference)" not in root_cmake:
        errors.append("distributed reference module is not integrated")
    if "add_library(neoeng_dcore_distributed_reference STATIC" not in module_cmake:
        errors.append("distributed reference target missing")
    if (
        "target_link_libraries(neoeng_dcore_distributed_reference PUBLIC neoeng_dcore)"
        not in module_cmake
    ):
        errors.append("module does not depend one-way on neoeng_dcore")
    if "target_link_libraries(neoeng_dcore " in module_cmake:
        errors.append("reverse dependency from neoeng_dcore detected")
    lowered_canonical = canonical.lower()
    for forbidden in (
        "neoeng/distributed_reference",
        "neoeng/dcore_replica_adapter",
        "<winsock2.h>",
        "<sys/socket.h>",
    ):
        if forbidden in lowered_canonical:
            errors.append(f"canonical core imports distributed concern: {forbidden}")
    required = (
        ROOT / "modules/distributed_reference/include/neoeng/distributed_reference.hpp",
        ROOT / "docs/architecture/DISTRIBUTED_REFERENCE_BOUNDARY.md",
        ROOT / "docs/contracts/DISTRIBUTED_REFERENCE_V1.md",
    )
    for path in required:
        if not path.is_file():
            errors.append(f"required distributed boundary artifact missing: {path}")
    return errors


def self_test(root_cmake: str, module_cmake: str, canonical: str) -> bool:
    mutations = (
        (root_cmake.replace(
            "add_subdirectory(modules/distributed_reference)", ""
        ), module_cmake, canonical),
        (root_cmake, module_cmake.replace(
            "target_link_libraries(neoeng_dcore_distributed_reference PUBLIC neoeng_dcore)",
            "target_link_libraries(neoeng_dcore PUBLIC neoeng_dcore_distributed_reference)",
        ), canonical),
        (root_cmake, module_cmake, canonical + "\n#include <winsock2.h>\n"),
    )
    return all(validate(*mutation) for mutation in mutations)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    root_cmake, module_cmake, canonical = source_texts(ROOT)
    if args.self_test:
        passed = self_test(root_cmake, module_cmake, canonical)
        print(
            "distributed_reference_boundary_self_test="
            + ("passed" if passed else "failed")
        )
        return 0 if passed else 1
    errors = validate(root_cmake, module_cmake, canonical)
    if errors:
        print("\n".join(errors))
        return 1
    print(
        "OK: distributed reference remains a one-way companion; "
        "socket/coordinator state is absent from the canonical core."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
