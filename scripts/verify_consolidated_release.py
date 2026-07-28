#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath
from typing import Any

VERSION = "1.14.0"
PACKAGE_NAME = f"NeoEng-D-Core-{VERSION}"
MANIFEST_PATTERN = re.compile(r"^([0-9a-f]{64})  ([^\r\n]+)$")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def load_json(value: bytes, name: str) -> dict[str, Any]:
    document = json.loads(value.decode("utf-8"))
    if not isinstance(document, dict):
        raise ValueError(f"{name} root is not an object")
    return document


def verify(directory: Path) -> dict[str, Any]:
    errors: list[str] = []
    archive = directory / f"{PACKAGE_NAME}.zip"
    digest_file = directory / f"{PACKAGE_NAME}.zip.sha256"
    artifact_record = directory / "release-artifacts.json"
    external_provenance = directory / "PROVENANCE.json"
    external_sbom = directory / "SBOM.spdx.json"
    if not archive.is_file():
        errors.append("consolidated archive missing")
    if not digest_file.is_file():
        errors.append("outer SHA-256 record missing")
    if not artifact_record.is_file():
        errors.append("release artifact record missing")
    if not external_provenance.is_file():
        errors.append("external provenance predicate missing")
    if not external_sbom.is_file():
        errors.append("external SBOM predicate missing")
    if errors:
        return result(errors, 0, 0)

    expected_outer = digest_file.read_text(encoding="utf-8").split()[0]
    actual_outer = sha256(archive)
    if expected_outer != actual_outer:
        errors.append("outer archive SHA-256 mismatch")
    try:
        artifacts = json.loads(artifact_record.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        errors.append(f"release artifact record invalid: {exc}")
        artifacts = {}
    if (
        artifacts.get("archive_sha256") != actual_outer
        or artifacts.get("project_version") != VERSION
        or artifacts.get("external_signed_attestation_required") is not True
        or artifacts.get("local_unsigned_candidate_publishable") is not False
    ):
        errors.append("release artifact record rejected")

    verified_entries = 0
    sbom_entries = 0
    try:
        with zipfile.ZipFile(archive, "r") as package:
            names = package.namelist()
            if len(names) != len(set(names)):
                errors.append("duplicate ZIP member")
            prefix = f"{PACKAGE_NAME}/"
            safe_names: list[str] = []
            for name in names:
                pure = PurePosixPath(name)
                if (
                    not name.startswith(prefix)
                    or pure.is_absolute()
                    or ".." in pure.parts
                    or "\\" in name
                ):
                    errors.append(f"unsafe ZIP member: {name}")
                else:
                    safe_names.append(name)
            required = {
                f"{prefix}CMakeLists.txt",
                f"{prefix}vcpkg.json",
                f"{prefix}NOTICE.md",
                f"{prefix}RELEASE_METADATA.json",
                f"{prefix}PROVENANCE.json",
                f"{prefix}SBOM.spdx.json",
                f"{prefix}PUBLIC_CLAIMS.md",
                f"{prefix}SHA256SUMS.txt",
                f"{prefix}docs/contracts/RELEASE_ASSURANCE_V1.md",
                f"{prefix}audit/PRODUCT_CLAIMS_LEDGER.json",
            }
            missing = sorted(required - set(names))
            if missing:
                errors.append(f"required package members missing: {missing}")

            manifest_name = f"{prefix}SHA256SUMS.txt"
            manifest_lines = package.read(manifest_name).decode("utf-8").splitlines()
            manifest_paths: set[str] = set()
            for line in manifest_lines:
                match = MANIFEST_PATTERN.fullmatch(line)
                if not match:
                    errors.append(f"invalid internal manifest line: {line!r}")
                    continue
                expected, relative = match.groups()
                member = f"{prefix}{relative}"
                if relative in manifest_paths:
                    errors.append(f"duplicate internal manifest path: {relative}")
                    continue
                manifest_paths.add(relative)
                if member not in names:
                    errors.append(f"manifest member missing: {relative}")
                elif sha256_bytes(package.read(member)) != expected:
                    errors.append(f"manifest digest mismatch: {relative}")
                else:
                    verified_entries += 1
            expected_manifest_paths = {
                name[len(prefix):] for name in safe_names
                if name != manifest_name and not name.endswith("/")
            }
            if manifest_paths != expected_manifest_paths:
                errors.append("internal manifest coverage mismatch")

            metadata = load_json(
                package.read(f"{prefix}RELEASE_METADATA.json"),
                "RELEASE_METADATA.json",
            )
            provenance = load_json(
                package.read(f"{prefix}PROVENANCE.json"),
                "PROVENANCE.json",
            )
            if (
                metadata.get("project_version") != VERSION
                or metadata.get("base_plus_patches") is not False
                or metadata.get("worktree_dirty") is not False
                or metadata.get("publishable_without_external_attestation")
                    is not False
                or not re.fullmatch(
                    r"[0-9a-f]{40}", str(metadata.get("source_commit", ""))
                )
            ):
                errors.append("release metadata rejected")
            if (
                provenance.get("project_version") != VERSION
                or provenance.get("source_commit") != metadata.get("source_commit")
                or provenance.get("source_tree") != metadata.get("source_tree")
                or provenance.get("worktree_dirty") is not False
                or provenance.get(
                    "external_attestation_required_for_publication"
                ) is not True
            ):
                errors.append("release provenance rejected")
            if load_json(
                external_provenance.read_bytes(), "external PROVENANCE.json"
            ) != provenance:
                errors.append("external provenance predicate diverges from archive")

            sbom = load_json(
                package.read(f"{prefix}SBOM.spdx.json"), "SBOM.spdx.json"
            )
            if load_json(
                external_sbom.read_bytes(), "external SBOM.spdx.json"
            ) != sbom:
                errors.append("external SBOM predicate diverges from archive")
            sbom_files = sbom.get("files", [])
            sbom_names = {
                str(row.get("fileName", "")).removeprefix("./")
                for row in sbom_files if isinstance(row, dict)
            }
            sbom_expected = {
                path for path in expected_manifest_paths
                if path not in {"SBOM.spdx.json"}
            }
            sbom_expected.discard("SHA256SUMS.txt")
            if (
                sbom.get("spdxVersion") != "SPDX-2.3"
                or sbom_names != sbom_expected
            ):
                errors.append("SPDX SBOM coverage mismatch")
            sbom_entries = len(sbom_names)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile,
            UnicodeDecodeError, json.JSONDecodeError) as exc:
        errors.append(f"archive verification failed: {exc}")
    return result(errors, verified_entries, sbom_entries)


def result(
    errors: list[str],
    verified_entries: int,
    sbom_entries: int,
) -> dict[str, Any]:
    return {
        "schema": "neoeng.dcore.consolidated-release-verification.v1",
        "project_version": VERSION,
        "status": "passed" if not errors else "failed",
        "errors": errors,
        "manifest_entries_verified": verified_entries,
        "sbom_entries_verified": sbom_entries,
        "external_attestation_verified_by_local_tool": False,
        "local_unsigned_candidate_publishable": False,
    }


def write_fixture(directory: Path) -> None:
    archive = directory / f"{PACKAGE_NAME}.zip"
    prefix = f"{PACKAGE_NAME}/"
    commit = "a" * 40
    tree = "b" * 40
    files: dict[str, bytes] = {
        "CMakeLists.txt": (
            "project(NeoEngDCore VERSION 1.14.0 LANGUAGES C CXX)\n"
        ).encode(),
        "vcpkg.json": b'{"version-string":"1.14.0"}\n',
        "NOTICE.md": b"fixture\n",
        "PUBLIC_CLAIMS.md": b"fixture\n",
        "docs/contracts/RELEASE_ASSURANCE_V1.md": b"fixture\n",
        "audit/PRODUCT_CLAIMS_LEDGER.json": b"{}\n",
        "RELEASE_METADATA.json": (
            json.dumps({
                "project_version": VERSION,
                "base_plus_patches": False,
                "worktree_dirty": False,
                "publishable_without_external_attestation": False,
                "source_commit": commit,
                "source_tree": tree,
            }) + "\n"
        ).encode(),
        "PROVENANCE.json": (
            json.dumps({
                "project_version": VERSION,
                "source_commit": commit,
                "source_tree": tree,
                "worktree_dirty": False,
                "external_attestation_required_for_publication": True,
            }) + "\n"
        ).encode(),
    }
    sbom_files = []
    for name, content in sorted(files.items()):
        sbom_files.append({
            "fileName": f"./{name}",
            "checksums": [{
                "algorithm": "SHA256",
                "checksumValue": sha256_bytes(content),
            }],
        })
    files["SBOM.spdx.json"] = (
        json.dumps({"spdxVersion": "SPDX-2.3", "files": sbom_files}) + "\n"
    ).encode()
    files["SHA256SUMS.txt"] = "".join(
        f"{sha256_bytes(content)}  {name}\n"
        for name, content in sorted(files.items())
    ).encode()
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
        for name, content in sorted(files.items()):
            package.writestr(prefix + name, content)
    (directory / "PROVENANCE.json").write_bytes(files["PROVENANCE.json"])
    (directory / "SBOM.spdx.json").write_bytes(files["SBOM.spdx.json"])
    digest = sha256(archive)
    (directory / f"{PACKAGE_NAME}.zip.sha256").write_text(
        f"{digest}  {archive.name}\n", encoding="utf-8"
    )
    (directory / "release-artifacts.json").write_text(
        json.dumps({
            "project_version": VERSION,
            "archive_sha256": digest,
            "external_signed_attestation_required": True,
            "local_unsigned_candidate_publishable": False,
        }) + "\n",
        encoding="utf-8",
    )


def self_test() -> bool:
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        write_fixture(directory)
        if verify(directory)["status"] != "passed":
            return False
        with (directory / f"{PACKAGE_NAME}.zip").open("ab") as handle:
            handle.write(b"tampered")
        report = verify(directory)
        return report["status"] == "failed" and any(
            "outer archive SHA-256 mismatch" in error
            for error in report["errors"]
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path, nargs="?")
    parser.add_argument("--write-report", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        passed = self_test()
        print(
            "consolidated_release_self_test="
            + ("passed" if passed else "failed")
        )
        return 0 if passed else 1
    if args.directory is None:
        parser.error("directory is required unless --self-test is used")
    report = verify(args.directory.resolve())
    if args.write_report:
        (args.directory / "independent-verification.json").write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
