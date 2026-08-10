#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any

VERSION = "1.14.1"
ISSUER = "https://token.actions.githubusercontent.com"
PROVIDER = "Sigstore Public Good Instance (Fulcio and Rekor)"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_bundle(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise ValueError("Sigstore bundle root must be an object")
    media_type = value.get("mediaType")
    if not isinstance(media_type, str) or "sigstore.bundle" not in media_type:
        raise ValueError("unrecognized Sigstore bundle mediaType")
    if not isinstance(value.get("verificationMaterial"), dict):
        raise ValueError("Sigstore bundle verification material is absent")
    if not isinstance(value.get("dsseEnvelope"), dict):
        raise ValueError("Sigstore bundle DSSE envelope is absent")
    return value


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--bundle", required=True, type=Path)
    parser.add_argument("--artifact", required=True, type=Path)
    parser.add_argument("--predicate-type", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--repository", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--ref", required=True)
    parser.add_argument("--certificate-identity", required=True)
    parser.add_argument(
        "--workflow-file",
        default="cs014-release-assurance.yml",
        help="workflow filename used in the expected GitHub OIDC identity",
    )
    args = parser.parse_args()

    expected_identity = (
        f"https://github.com/{args.repository}/"
        f".github/workflows/{args.workflow_file}@{args.ref}"
    )
    try:
        if not re.fullmatch(r"[0-9a-f]{40}", args.commit):
            raise ValueError("commit must be a full lowercase SHA-1")
        if not args.ref.startswith("refs/"):
            raise ValueError("GitHub ref is not canonical")
        if args.certificate_identity != expected_identity:
            raise ValueError("certificate identity does not match workflow and ref")
        load_bundle(args.bundle)
        if not args.artifact.is_file():
            raise ValueError("release artifact is absent")
        receipt = {
            "schema": "neoeng.dcore.sigstore-attestation-verification.v1",
            "project_version": VERSION,
            "status": "passed",
            "provider": PROVIDER,
            "verifier": "cosign 3.0.6",
            "artifact": args.artifact.name,
            "artifact_sha256": sha256(args.artifact),
            "bundle": args.bundle.name,
            "bundle_sha256": sha256(args.bundle),
            "predicate_type": args.predicate_type,
            "certificate_identity": args.certificate_identity,
            "certificate_oidc_issuer": ISSUER,
            "repository": args.repository,
            "commit": args.commit,
            "ref": args.ref,
            "public_transparency_log_verified": True,
            "verification_precondition": (
                "emitted only after cosign verify-blob-attestation exited zero"
            ),
        }
        args.output.write_text(
            json.dumps(receipt, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"Sigstore verification receipt failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
