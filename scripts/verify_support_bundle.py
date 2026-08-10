#!/usr/bin/env python3
"""Independent verifier for neoeng.dcore.support-bundle.v1 directories."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path, PurePosixPath

SCHEMA = "neoeng.dcore.support-bundle.v1"
GATE_SCHEMA = "neoeng.dcore.deferred-validation-gates.v1"
REQUIRED = {
    "metadata.json",
    "traces.json",
    "evidence-chain.json",
    "deferred-validation-gates.json",
    "redaction-report.json",
}
HEX64 = re.compile(r"^[0-9a-f]{64}$")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_relative(path: str) -> bool:
    if not path or "\\" in path or "\x00" in path:
        return False
    pure = PurePosixPath(path)
    if pure.is_absolute() or any(part in {"", ".", ".."} for part in pure.parts):
        return False
    return all(re.fullmatch(r"[A-Za-z0-9._-]+", part) for part in pure.parts)


def fail(message: str) -> int:
    print(f"ERROR: {message}", file=sys.stderr)
    return 1

def json_keys(value: object) -> set[str]:
    keys: set[str] = set()
    if isinstance(value, dict):
        for key, child in value.items():
            if isinstance(key, str):
                keys.add(key)
            keys.update(json_keys(child))
    elif isinstance(value, list):
        for child in value:
            keys.update(json_keys(child))
    return keys


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=Path)
    parser.add_argument("--reject-extra-files", action="store_true", default=True)
    args = parser.parse_args()
    root = args.directory.resolve()
    if not root.is_dir():
        return fail("bundle directory is missing")
    symlinks = sorted(
        item.relative_to(root).as_posix() for item in root.rglob("*")
        if item.is_symlink()
    )
    if symlinks:
        return fail(f"symbolic links are not allowed: {symlinks}")
    manifest_path = root / "manifest.json"
    digest_path = root / "manifest.sha256"
    if not manifest_path.is_file() or not digest_path.is_file():
        return fail("manifest.json or manifest.sha256 is missing")

    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except Exception as exc:
        return fail(f"manifest is invalid JSON: {exc}")
    if manifest.get("schema") != SCHEMA:
        return fail("manifest schema mismatch")
    entries = manifest.get("entries")
    if not isinstance(entries, list) or manifest.get("entry_count") != len(entries):
        return fail("manifest entry_count mismatch")

    declared: set[str] = set()
    total_bytes = 0
    for index, item in enumerate(entries):
        if not isinstance(item, dict):
            return fail(f"entry {index} is not an object")
        path = item.get("path")
        expected_size = item.get("size")
        expected_hash = item.get("sha256")
        if not isinstance(path, str) or not safe_relative(path):
            return fail(f"unsafe path at entry {index}")
        if path in declared:
            return fail(f"duplicate path: {path}")
        declared.add(path)
        if not isinstance(expected_size, int) or expected_size < 0:
            return fail(f"invalid size for {path}")
        if not isinstance(expected_hash, str) or not HEX64.fullmatch(expected_hash):
            return fail(f"invalid SHA-256 for {path}")
        target = root / path
        if target.is_symlink() or not target.is_file():
            return fail(f"missing or symbolic-link entry: {path}")
        try:
            target.resolve(strict=True).relative_to(root)
        except (OSError, ValueError):
            return fail(f"entry escapes bundle directory: {path}")
        actual_size = target.stat().st_size
        if actual_size != expected_size:
            return fail(f"size mismatch for {path}: {actual_size} != {expected_size}")
        actual_hash = sha256(target)
        if actual_hash != expected_hash:
            return fail(f"SHA-256 mismatch for {path}")
        total_bytes += actual_size

    missing = REQUIRED - declared
    if missing:
        return fail(f"required entries missing: {sorted(missing)}")

    expected_manifest_hash = digest_path.read_text(encoding="ascii").split()[0].lower()
    if not HEX64.fullmatch(expected_manifest_hash):
        return fail("manifest.sha256 is malformed")
    actual_manifest_hash = sha256(manifest_path)
    if actual_manifest_hash != expected_manifest_hash:
        return fail("manifest SHA-256 mismatch")

    redaction = json.loads((root / "redaction-report.json").read_text(encoding="utf-8"))
    forbidden_true = (
        "session_keys_included",
        "authentication_secrets_included",
        "private_signing_material_included",
        "raw_subject_ids_included",
    )
    for field in forbidden_true:
        if redaction.get(field) is not False:
            return fail(f"redaction invariant failed: {field}")
    payload_keys: set[str] = set()
    trace_keys: set[str] = set()
    for relative in sorted(declared - {"redaction-report.json"}):
        candidate = root / relative
        try:
            parsed = json.loads(candidate.read_text(encoding="utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            continue
        parsed_keys = json_keys(parsed)
        payload_keys.update(parsed_keys)
        if relative == "traces.json":
            trace_keys.update(parsed_keys)
    if (
        not redaction.get("session_keys_included")
        and payload_keys & {"session_key", "session_keys"}
    ):
        return fail("redaction report contradicts session-key payload")
    if (
        not redaction.get("authentication_secrets_included")
        and payload_keys & {"authentication_secret", "authentication_secrets"}
    ):
        return fail("redaction report contradicts authentication-secret payload")
    if (
        not redaction.get("private_signing_material_included")
        and payload_keys & {"private_signing_material", "private_signing_key"}
    ):
        return fail("redaction report contradicts private-signing payload")
    if (
        not redaction.get("raw_subject_ids_included")
        and payload_keys & {"raw_subject_id", "raw_subject_ids", "subject_id", "subject_ids"}
    ):
        return fail("redaction report contradicts raw-subject payload")
    if not redaction.get("monotonic_timestamps_included") and "monotonic_time_ns" in trace_keys:
        return fail("redaction report contradicts monotonic-timestamp payload")

    gates = json.loads((root / "deferred-validation-gates.json").read_text(encoding="utf-8"))
    if gates.get("schema") != GATE_SCHEMA or not isinstance(gates.get("gates"), list):
        return fail("deferred-validation gate schema mismatch")

    allowed_files = declared | {"manifest.json", "manifest.sha256"}
    actual_files = {
        item.relative_to(root).as_posix()
        for item in root.rglob("*") if item.is_file() and not item.is_symlink()
    }
    extras = actual_files - allowed_files
    if args.reject_extra_files and extras:
        return fail(f"undeclared files present: {sorted(extras)}")

    print(
        f"OK: schema={SCHEMA}; entries={len(entries)}; "
        f"bytes={total_bytes}; manifest_sha256={actual_manifest_hash}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
