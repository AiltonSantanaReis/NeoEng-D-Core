#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
RECORD = Path("audit/RELEASE_ATTESTATION.json")


def load_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"release attestation record missing: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid release attestation JSON: {exc}") from exc
    if not isinstance(value, dict):
        raise ValueError("release attestation root must be object")
    return value


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def verify(root: Path, repo: str) -> list[str]:
    errors: list[str] = []
    doc = load_json(root / RECORD)
    if doc.get("schema") != "neoeng.dcore.release-attestation.v1":
        return ["release attestation schema mismatch"]
    if doc.get("repository") != repo:
        errors.append("release attestation repository identity mismatch")
    rel = doc.get("artifact_path")
    digest = doc.get("sha256")
    signer_workflow = doc.get("signer_workflow")
    if not isinstance(rel, str) or not rel:
        errors.append("release artifact_path missing")
        return errors
    artifact = (root / rel).resolve()
    try:
        artifact.relative_to(root.resolve())
    except ValueError:
        errors.append("release artifact path escapes repository root")
        return errors
    if not artifact.is_file():
        errors.append("release artifact missing")
        return errors
    actual = sha256(artifact)
    if not isinstance(digest, str) or digest != actual:
        errors.append("release artifact sha256 mismatch")
    if not isinstance(signer_workflow, str) or not signer_workflow.startswith(repo + "/.github/workflows/"):
        errors.append("release signer_workflow must identify an exact workflow in this repository")
    gh = shutil.which("gh")
    if gh is None:
        errors.append("GitHub CLI is required for cryptographic artifact-attestation verification")
        return errors
    cmd = [gh, "attestation", "verify", str(artifact), "--repo", repo, "--format", "json"]
    if isinstance(signer_workflow, str):
        cmd += ["--signer-workflow", signer_workflow]
    env = os.environ.copy()
    proc = subprocess.run(cmd, cwd=root, text=True, capture_output=True, check=False, env=env)
    if proc.returncode != 0:
        errors.append("cryptographic artifact attestation verification failed: " + (proc.stderr.strip() or proc.stdout.strip()))
        return errors
    try:
        rows = json.loads(proc.stdout)
    except json.JSONDecodeError:
        errors.append("gh attestation verify did not return valid JSON")
        return errors
    if not isinstance(rows, list) or not rows:
        errors.append("no verified artifact attestation returned")
    return errors


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(ROOT))
    ap.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY"))
    args = ap.parse_args()
    if not args.repo:
        errors = ["repository identity missing"]
    else:
        try:
            errors = verify(Path(args.root).resolve(), args.repo)
        except ValueError as exc:
            errors = [str(exc)]
    if errors:
        print("RELEASE ATTESTATION VERIFICATION: REJECT")
        for e in errors:
            print(f"- {e}")
        return 1
    print("RELEASE ATTESTATION VERIFICATION: ACCEPT")
    return 0


if __name__ == "__main__":
    sys.exit(main())
