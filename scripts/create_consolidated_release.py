#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
VERSION = "1.14.0"
PACKAGE_NAME = f"NeoEng-D-Core-{VERSION}"


class ReleaseError(RuntimeError):
    pass


def run(*command: str) -> str:
    result = subprocess.run(
        command,
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if result.returncode != 0:
        raise ReleaseError(
            f"command failed ({result.returncode}): {' '.join(command)}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result.stdout.strip()


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path: Path, value: dict[str, Any]) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def tracked_files() -> list[Path]:
    output = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    ).stdout
    paths = [
        ROOT / item.decode("utf-8")
        for item in output.split(b"\0")
        if item
    ]
    files = sorted(path for path in paths if path.is_file())
    if not files:
        raise ReleaseError("tracked source set is empty")
    return files


def spdx_file_id(relative: str) -> str:
    digest = hashlib.sha256(relative.encode("utf-8")).hexdigest()[:20]
    return f"SPDXRef-File-{digest}"


def build_sbom(stage: Path, source_files: list[Path], commit: str) -> dict[str, Any]:
    files = []
    relationships = []
    for path in sorted(source_files):
        relative = path.relative_to(stage).as_posix()
        file_id = spdx_file_id(relative)
        files.append({
            "SPDXID": file_id,
            "fileName": f"./{relative}",
            "checksums": [{
                "algorithm": "SHA256",
                "checksumValue": sha256(path),
            }],
        })
        relationships.append({
            "spdxElementId": "SPDXRef-Package-NeoEng-D-Core",
            "relationshipType": "CONTAINS",
            "relatedSpdxElement": file_id,
        })
    relationships.append({
        "spdxElementId": "SPDXRef-Package-NeoEng-D-Core",
        "relationshipType": "DEPENDS_ON",
        "relatedSpdxElement": "SPDXRef-Package-Boost",
    })
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"{PACKAGE_NAME}-SBOM",
        "documentNamespace":
            f"https://neoeng.example/spdx/{PACKAGE_NAME}/{commit}",
        "creationInfo": {
            "created": "1980-01-01T00:00:00Z",
            "creators": ["Tool: NeoEng-D-Core-create_consolidated_release"],
        },
        "packages": [
            {
                "name": "NeoEng-D-Core",
                "SPDXID": "SPDXRef-Package-NeoEng-D-Core",
                "versionInfo": VERSION,
                "downloadLocation": "NOASSERTION",
                "filesAnalyzed": True,
                "licenseConcluded": "NOASSERTION",
                "licenseDeclared": "NOASSERTION",
                "copyrightText": "NOASSERTION",
            },
            {
                "name": "Boost",
                "SPDXID": "SPDXRef-Package-Boost",
                "versionInfo": ">=1.80",
                "downloadLocation": "https://www.boost.org/",
                "filesAnalyzed": False,
                "licenseConcluded": "BSL-1.0",
                "licenseDeclared": "BSL-1.0",
                "copyrightText": "NOASSERTION",
            },
        ],
        "files": files,
        "relationships": relationships,
    }


def write_archive(stage: Path, archive: Path) -> None:
    with zipfile.ZipFile(
        archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as output:
        for path in sorted(stage.rglob("*")):
            if not path.is_file():
                continue
            relative = Path(PACKAGE_NAME) / path.relative_to(stage)
            info = zipfile.ZipInfo(relative.as_posix(), (1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            output.writestr(info, path.read_bytes())


def create(output_dir: Path, compiler_id: str, build_id: str) -> dict[str, Any]:
    if run("git", "status", "--porcelain"):
        raise ReleaseError("release source worktree is not clean")
    commit = run("git", "rev-parse", "HEAD")
    tree = run("git", "rev-parse", "HEAD^{tree}")
    output_dir.mkdir(parents=True, exist_ok=True)
    archive = output_dir / f"{PACKAGE_NAME}.zip"
    digest_file = output_dir / f"{PACKAGE_NAME}.zip.sha256"
    artifact_record = output_dir / "release-artifacts.json"
    external_provenance = output_dir / "PROVENANCE.json"
    external_sbom = output_dir / "SBOM.spdx.json"
    for path in (
        archive,
        digest_file,
        artifact_record,
        external_provenance,
        external_sbom,
    ):
        if path.exists():
            raise ReleaseError(f"refusing to overwrite release artifact: {path}")

    with tempfile.TemporaryDirectory(prefix="neoeng-release-") as temp:
        stage = Path(temp) / PACKAGE_NAME
        stage.mkdir()
        tracked = tracked_files()
        for source in tracked:
            relative = source.relative_to(ROOT)
            destination = stage / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)

        public_claims = ROOT / "docs/commercial/PUBLIC_CLAIMS.md"
        if not public_claims.is_file():
            raise ReleaseError("generated public claims are missing")
        shutil.copyfile(public_claims, stage / "PUBLIC_CLAIMS.md")

        metadata = {
            "schema": "neoeng.dcore.consolidated-release-metadata.v1",
            "project_version": VERSION,
            "package_name": PACKAGE_NAME,
            "distribution": "single cumulative source archive",
            "base_plus_patches": False,
            "source_commit": commit,
            "source_tree": tree,
            "worktree_dirty": False,
            "publication_status": "candidate_unsigned",
            "publishable_without_external_attestation": False,
            "external_attestation_required": True,
            "arm64_or_hardware_result_inferred": False,
            "commercial_product_complete": False,
        }
        provenance = {
            "schema": "neoeng.dcore.deterministic-source-provenance.v1",
            "project_version": VERSION,
            "source_commit": commit,
            "source_tree": tree,
            "worktree_dirty": False,
            "archive_epoch": "1980-01-01T00:00:00Z",
            "generator": "scripts/create_consolidated_release.py",
            "variable_builder_metadata_in_archive": False,
            "external_attestation_required_for_publication": True,
        }
        write_json(stage / "RELEASE_METADATA.json", metadata)
        write_json(stage / "PROVENANCE.json", provenance)
        shutil.copyfile(stage / "PROVENANCE.json", external_provenance)

        pre_sbom = [
            path for path in stage.rglob("*")
            if path.is_file()
            and path not in {
                stage / "SBOM.spdx.json",
                stage / "SHA256SUMS.txt",
            }
        ]
        write_json(stage / "SBOM.spdx.json", build_sbom(stage, pre_sbom, commit))
        shutil.copyfile(stage / "SBOM.spdx.json", external_sbom)

        manifest_files = sorted(
            path for path in stage.rglob("*")
            if path.is_file() and path != stage / "SHA256SUMS.txt"
        )
        manifest = "".join(
            f"{sha256(path)}  {path.relative_to(stage).as_posix()}\n"
            for path in manifest_files
        )
        (stage / "SHA256SUMS.txt").write_text(
            manifest, encoding="utf-8", newline="\n"
        )
        write_archive(stage, archive)

    archive_digest = sha256(archive)
    digest_file.write_text(
        f"{archive_digest}  {archive.name}\n",
        encoding="utf-8",
        newline="\n",
    )
    result = {
        "schema": "neoeng.dcore.release-artifacts.v1",
        "project_version": VERSION,
        "status": "created",
        "archive": archive.name,
        "archive_sha256": archive_digest,
        "archive_bytes": archive.stat().st_size,
        "source_commit": commit,
        "source_tree": tree,
        "compiler_id": compiler_id,
        "build_id": build_id,
        "builder": platform.node() or "unknown",
        "operating_system": platform.platform(),
        "python": platform.python_version(),
        "external_signed_attestation_required": True,
        "local_unsigned_candidate_publishable": False,
    }
    write_json(artifact_record, result)
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--compiler-id", required=True)
    parser.add_argument("--build-id", required=True)
    args = parser.parse_args()
    try:
        result = create(
            args.output_dir.resolve(), args.compiler_id, args.build_id
        )
    except (OSError, ReleaseError, subprocess.SubprocessError) as exc:
        print(f"release creation failed: {exc}", file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
