#!/usr/bin/env python3
"""Independent structural and semantic verifier for D-Core qualification campaigns.

The verifier recomputes the qualification decision from the request, command
results, raw samples and evidence records. It does not trust the emitted result
or summary. Internal SHA-256 integrity is not a substitute for external custody
or signature, but silent edits and inconsistent decisions are rejected.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import sys
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any

PROJECT_VERSION = "1.12.0"
PROFILES = {"P0", "P1", "P2", "P3", "P4"}
EXECUTION_KINDS = {"virtualized", "native_physical", "containerized"}

# Must remain synchronized with include/neoeng/core/hardware_profile.hpp.
FAILURE = {
    "MissingBaseline": 1 << 0,
    "EnvironmentMismatch": 1 << 1,
    "MissingMeasurement": 1 << 2,
    "RollbackBudgetExceeded": 1 << 3,
    "EcsBudgetExceeded": 1 << 4,
    "DeterminismFailed": 1 << 5,
    "SerializationFailed": 1 << 6,
    "NativeExecutionRequired": 1 << 7,
    "MissingFullTestReport": 1 << 8,
    "MissingRawSamples": 1 << 9,
    "MissingBinaryHashes": 1 << 10,
    "MissingSourceManifest": 1 << 11,
    "MissingHardwareInventory": 1 << 12,
    "MissingThermalRecord": 1 << 13,
    "CampaignVerificationFailed": 1 << 14,
    "ProfileCompatibilityFailed": 1 << 15,
    "InsufficientSamples": 1 << 16,
    "MissingBenchmarkReport": 1 << 17,
    "ClockPolicyMissing": 1 << 18,
    "CpuMigrationDetected": 1 << 19,
    "AllocationGateFailed": 1 << 20,
    "FullTestSuiteFailed": 1 << 21,
    "EcsScopeIncomplete": 1 << 22,
}
INCOMPLETE_MASK = sum(
    FAILURE[name]
    for name in (
        "MissingBaseline",
        "EnvironmentMismatch",
        "MissingMeasurement",
        "NativeExecutionRequired",
        "MissingFullTestReport",
        "MissingRawSamples",
        "MissingBinaryHashes",
        "MissingSourceManifest",
        "MissingHardwareInventory",
        "MissingThermalRecord",
        "CampaignVerificationFailed",
        "ProfileCompatibilityFailed",
        "InsufficientSamples",
        "MissingBenchmarkReport",
        "ClockPolicyMissing",
        "EcsScopeIncomplete",
    )
)


class VerificationError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise VerificationError(f"JSON root must be an object: {path.name}")
    return value


def safe_relative(value: str) -> Path:
    normalized = value.replace("\\", "/")
    path = Path(normalized)
    if not value or path.is_absolute() or ".." in path.parts or normalized.startswith("/"):
        raise VerificationError(f"unsafe manifest path: {value}")
    return path


def require_bool(mapping: dict[str, Any], key: str) -> bool:
    value = mapping.get(key)
    if not isinstance(value, bool):
        raise VerificationError(f"{key} must be boolean")
    return value


def require_text(mapping: dict[str, Any], key: str) -> str:
    value = mapping.get(key)
    if not isinstance(value, str) or not value.strip():
        raise VerificationError(f"{key} must be a non-empty string")
    return value.strip()


def require_nonnegative_int(mapping: dict[str, Any], key: str) -> int:
    value = mapping.get(key)
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise VerificationError(f"{key} must be a non-negative integer")
    return value


def require_positive_int(mapping: dict[str, Any], key: str) -> int:
    value = require_nonnegative_int(mapping, key)
    if value == 0:
        raise VerificationError(f"{key} must be positive")
    return value


def normalize_architecture(value: str) -> str:
    return value.strip().lower().replace("amd64", "x86_64").replace("arm64", "aarch64")


def verify_manifest_document(
    root: Path,
    document_path: Path,
    schema: str,
    *,
    exact_file_set: bool,
    excluded_actual: set[str] | None = None,
) -> set[str]:
    document = load_json(document_path)
    if document.get("schema") != schema:
        raise VerificationError(f"manifest schema mismatch: {document_path.name}")
    entries = document.get("entries")
    if not isinstance(entries, list) or not entries:
        raise VerificationError(f"manifest entries are missing: {document_path.name}")
    expected: set[str] = set()
    for row in entries:
        if not isinstance(row, dict):
            raise VerificationError(f"invalid manifest row: {document_path.name}")
        relative = safe_relative(str(row.get("path", ""))).as_posix()
        if relative in expected or relative == document_path.name:
            raise VerificationError(f"duplicate or self-referential manifest path: {relative}")
        expected.add(relative)
        path = root / relative
        if not path.is_file():
            raise VerificationError(f"missing campaign file: {relative}")
        size = row.get("size")
        if not isinstance(size, int) or isinstance(size, bool) or size < 0:
            raise VerificationError(f"invalid size in manifest: {relative}")
        if path.stat().st_size != size:
            raise VerificationError(f"size mismatch: {relative}")
        digest = row.get("sha256")
        if not isinstance(digest, str) or len(digest) != 64:
            raise VerificationError(f"invalid SHA-256 field: {relative}")
        if sha256_file(path) != digest:
            raise VerificationError(f"SHA-256 mismatch: {relative}")
    if exact_file_set:
        excluded = excluded_actual or set()
        actual = {
            path.relative_to(root).as_posix()
            for path in root.rglob("*")
            if path.is_file()
            and path.relative_to(root).as_posix() not in excluded
            and path.name != document_path.name
        }
        if actual != expected:
            raise VerificationError(
                f"campaign file-set mismatch; missing={sorted(expected - actual)}; "
                f"extra={sorted(actual - expected)}"
            )
    return expected


def verify_source_manifest(path: Path) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines:
        raise VerificationError("source manifest is empty")
    seen: set[str] = set()
    for line in lines:
        if len(line) < 67 or line[64:66] != "  ":
            raise VerificationError("source manifest line format is invalid")
        digest, relative = line[:64], line[66:]
        if any(character not in "0123456789abcdef" for character in digest):
            raise VerificationError("source manifest digest is invalid")
        safe = safe_relative(relative).as_posix()
        if safe in seen:
            raise VerificationError(f"duplicate source manifest path: {safe}")
        seen.add(safe)


def verify_binary_hashes(root: Path) -> None:
    document = load_json(root / "binary-hashes.json")
    if document.get("schema") != "neoeng.dcore.qualification-binary-hashes.v1":
        raise VerificationError("binary-hash schema mismatch")
    rows = document.get("binaries")
    if not isinstance(rows, list):
        raise VerificationError("binary hashes are missing")
    expected_names = {
        "hardware_probe",
        "ecs_benchmark",
        "rollback_benchmark",
        "determinism_probe",
        "state_evidence_probe",
    }
    names: set[str] = set()
    for row in rows:
        if not isinstance(row, dict):
            raise VerificationError("invalid binary-hash row")
        name = row.get("name")
        if not isinstance(name, str) or name in names:
            raise VerificationError("duplicate or invalid binary name")
        names.add(name)
        relative = safe_relative(str(row.get("path", ""))).as_posix()
        if not relative.startswith("binaries/"):
            raise VerificationError(f"binary is not embedded in campaign: {name}")
        path = root / relative
        if not path.is_file():
            raise VerificationError(f"embedded binary missing: {name}")
        if path.stat().st_size != row.get("size") or sha256_file(path) != row.get("sha256"):
            raise VerificationError(f"embedded binary hash mismatch: {name}")
    if names != expected_names:
        raise VerificationError(f"binary set mismatch: {sorted(names)}")


def command_map(root: Path) -> dict[str, dict[str, Any]]:
    document = load_json(root / "command-results.json")
    if document.get("schema") != "neoeng.dcore.qualification-command-results.v1":
        raise VerificationError("command-results schema mismatch")
    rows = document.get("commands")
    if not isinstance(rows, list):
        raise VerificationError("campaign command list is missing")
    result: dict[str, dict[str, Any]] = {}
    for row in rows:
        if not isinstance(row, dict):
            raise VerificationError("invalid campaign command row")
        name = row.get("name")
        if not isinstance(name, str) or name in result:
            raise VerificationError("duplicate or invalid campaign command name")
        return_code = row.get("return_code")
        passed = row.get("passed")
        if not isinstance(return_code, int) or isinstance(return_code, bool):
            raise VerificationError(f"invalid return code: {name}")
        if not isinstance(passed, bool) or passed != (return_code == 0):
            raise VerificationError(f"inconsistent passed flag: {name}")
        for key in ("stdout_file", "stderr_file"):
            relative = safe_relative(str(row.get(key, ""))).as_posix()
            if "/" in relative or not (root / relative).is_file():
                raise VerificationError(f"missing or invalid command output: {name}/{key}")
        command = row.get("command")
        if not isinstance(command, list) or not command or not all(isinstance(item, str) for item in command):
            raise VerificationError(f"invalid command vector: {name}")
        result[name] = row
    required = {
        "ctest",
        "determinism-probe",
        "state-evidence-probe",
        "ecs-benchmark",
        "ecs-scope-verifier",
        "rollback-benchmark",
        "hardware-profile-probe",
    }
    if set(result) != required:
        raise VerificationError(f"campaign command set mismatch: {sorted(result)}")
    return result


def percentile_rank(values: list[int], numerator: int, denominator: int) -> int:
    if not values:
        raise VerificationError("cannot compute percentile from empty samples")
    ordered = sorted(values)
    rank = (numerator * len(ordered) + denominator - 1) // denominator
    return ordered[max(1, rank) - 1]


def load_ecs_scope_verifier(project_root: Path):
    verifier_path = project_root / "scripts" / "qualification" / "verify_ecs_scope_evidence.py"
    spec = importlib.util.spec_from_file_location("neoeng_ecs_scope_verifier", verifier_path)
    if spec is None or spec.loader is None:
        raise VerificationError("cannot load independent ECS scope verifier")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_ecs_benchmark(root: Path) -> tuple[dict[str, Any], dict[str, Any], int, int]:
    project_root = Path(__file__).resolve().parents[2]
    verifier = load_ecs_scope_verifier(project_root)
    try:
        report = verifier.verify_directory(root / "ecs-benchmark", verify_saved_report=True)
    except Exception as error:  # verifier owns its precise structural/semantic exceptions
        raise VerificationError(f"ECS scope verification failed: {error}") from error
    summary = load_json(root / "ecs-benchmark" / "summary.json")
    measured = require_positive_int(summary, "measured_samples")
    if report.get("sample_count") != measured:
        raise VerificationError("ECS scope report sample count mismatch")
    p99 = require_nonnegative_int(report, "p99_ns")
    if require_nonnegative_int(summary, "p99_ns") != p99:
        raise VerificationError("ECS summary p99 differs from independent scope verification")
    if report.get("final_hash") != summary.get("final_hash"):
        raise VerificationError("ECS final hash differs from independent scope verification")
    return summary, report, measured, p99

def read_rollback_benchmark(root: Path) -> tuple[dict[str, Any], int, int, bool, bool]:
    summary = load_json(root / "rollback-benchmark" / "summary.json")
    csv_path = root / "rollback-benchmark" / "rollback_samples.csv"
    durations: list[Decimal] = []
    migration = False
    with csv_path.open("r", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream)
        expected_columns = {
            "sample", "duration_ms", "cpp_allocations", "c_allocations", "cpu_before", "cpu_after"
        }
        if set(reader.fieldnames or []) != expected_columns:
            raise VerificationError("rollback sample columns mismatch")
        for index, row in enumerate(reader):
            if int(row["sample"]) != index:
                raise VerificationError("rollback sample sequence mismatch")
            try:
                duration = Decimal(row["duration_ms"])
            except InvalidOperation as error:
                raise VerificationError("invalid rollback duration") from error
            if duration < 0:
                raise VerificationError("negative rollback duration")
            durations.append(duration)
            if int(row["cpu_before"]) != int(row["cpu_after"]):
                migration = True
    measured = require_positive_int(summary, "measured_samples")
    if measured != len(durations):
        raise VerificationError("rollback measured-sample count mismatch")
    ordered = sorted(durations)
    # Match apps/v28_bare_metal_rollback.cpp exactly:
    # floor(fraction * (sample_count - 1)).
    index = (99 * (len(ordered) - 1)) // 100
    p99_ms = ordered[index]
    if Decimal(str(summary.get("p99_ms"))) != p99_ms:
        raise VerificationError("rollback p99 does not match raw samples")
    if Decimal(str(summary.get("maximum_ms"))) != max(durations):
        raise VerificationError("rollback maximum does not match raw samples")
    if bool(summary.get("cpu_migration_detected")) != migration:
        raise VerificationError("rollback CPU-migration flag mismatch")
    allocation_gate = bool(summary.get("semantic_gate_passed")) and bool(
        summary.get("allocation_probe_calibrated")
    )
    return summary, measured, int(p99_ms * Decimal(1_000_000)), migration, allocation_gate


def profile_compatibility(profile: str, environment: dict[str, Any], inventory: dict[str, Any]) -> bool:
    reviewed = require_bool(environment, "profile_compatibility_reviewed")
    architecture = normalize_architecture(require_text(environment, "architecture"))
    observed = inventory.get("observed")
    if not isinstance(observed, dict):
        raise VerificationError("observed inventory is missing")
    observed_arch = normalize_architecture(require_text(observed, "architecture"))
    if architecture != observed_arch:
        return False
    cores = require_positive_int(environment, "physical_cpu_cores")
    gpu_vendor = require_text(environment, "gpu_vendor").upper()
    gpu_memory = require_nonnegative_int(environment, "gpu_memory_bytes")
    storage_class = require_text(environment, "storage_class")
    compatible = reviewed
    if profile == "P1":
        compatible = compatible and gpu_vendor == "NVIDIA" and gpu_memory >= 16 * 1024**3 \
            and cores >= 16 and storage_class == "nvme_pcie_4_or_5"
    elif profile == "P2":
        compatible = compatible and gpu_vendor in {"AMD", "ADVANCED MICRO DEVICES"} \
            and gpu_memory >= 16 * 1024**3 and cores >= 16 \
            and storage_class == "nvme_pcie_4_or_5"
    elif profile == "P3":
        compatible = compatible and architecture == "aarch64"
    elif profile == "P4":
        compatible = compatible and 7 * 1024**3 <= gpu_memory <= 9 * 1024**3
    virtualization = observed.get("virtualization")
    if not isinstance(virtualization, dict) or not isinstance(virtualization.get("detected"), bool):
        raise VerificationError("virtualization observation is invalid")
    return compatible


def baseline_complete(environment: dict[str, Any]) -> bool:
    fields = (
        "environment_id", "cpu_sku", "gpu_sku", "driver_version", "os_build",
        "power_profile", "architecture", "memory_configuration", "storage_configuration",
        "firmware_version", "thermal_policy",
    )
    return all(isinstance(environment.get(key), str) and bool(environment[key].strip()) for key in fields)


def recompute_decision(root: Path) -> tuple[int, str, str, dict[str, Any]]:
    request = load_json(root / "campaign-request.json")
    result = load_json(root / "qualification-result.json")
    summary = load_json(root / "campaign-summary.json")
    inventory = load_json(root / "hardware-inventory.json")
    thermal = load_json(root / "thermal-record.json")
    clock = load_json(root / "clock-policy.json")
    commands = command_map(root)

    if request.get("schema") != "neoeng.dcore.qualification-campaign-request.v1":
        raise VerificationError("request schema mismatch")
    if request.get("project_version") != PROJECT_VERSION:
        raise VerificationError("request project version mismatch")
    if result.get("schema") != "neoeng.dcore.hardware-qualification.v2":
        raise VerificationError("qualification result schema mismatch")
    if result.get("project_version") != PROJECT_VERSION \
            or result.get("independent_verification_required") is not True:
        raise VerificationError("qualification result version/verification policy mismatch")
    if summary.get("schema") != "neoeng.dcore.qualification-campaign-summary.v1" \
            or summary.get("project_version") != PROJECT_VERSION:
        raise VerificationError("campaign summary schema/version mismatch")
    profile = require_text(request, "profile")
    execution = require_text(request, "execution_kind")
    if profile not in PROFILES or execution not in EXECUTION_KINDS:
        raise VerificationError("invalid profile or execution kind")
    environment = request.get("environment")
    campaign = request.get("campaign")
    if not isinstance(environment, dict) or not isinstance(campaign, dict):
        raise VerificationError("request environment/campaign is invalid")
    if inventory.get("declared") != environment:
        raise VerificationError("inventory declaration differs from request")
    if inventory.get("profile") != profile or inventory.get("execution_kind_declared") != execution:
        raise VerificationError("inventory profile/execution mismatch")

    ecs_summary, ecs_scope_report, ecs_samples, ecs_p99 = read_ecs_benchmark(root)
    rollback_summary, rollback_samples, rollback_p99, migration, rollback_allocation_gate = \
        read_rollback_benchmark(root)
    ecs_scope_complete = bool(ecs_scope_report.get("scope_complete")) \
        and ecs_scope_report.get("status") == "passed"
    ecs_general_allocation_zero = bool(ecs_scope_report.get("general_allocation_zero"))
    ecs_arena_overflow_zero = bool(ecs_scope_report.get("arena_overflow_zero"))
    ecs_cow_valid = bool(ecs_scope_report.get("copy_on_write_semantics_valid"))
    ecs_index_valid = bool(ecs_scope_report.get("index_maintenance_semantics_valid"))
    allocation_gate = rollback_allocation_gate and ecs_general_allocation_zero \
        and ecs_arena_overflow_zero and ecs_cow_valid and ecs_index_valid

    ctest_scope = require_text(campaign, "ctest_scope")
    full_report_present = (root / commands["ctest"]["stdout_file"]).is_file() and \
        (root / commands["ctest"]["stderr_file"]).is_file()
    full_tests_passed = commands["ctest"]["return_code"] == 0 and ctest_scope == "full"
    determinism_passed = commands["determinism-probe"]["return_code"] == 0
    serialization_passed = commands["state-evidence-probe"]["return_code"] == 0
    benchmark_reports_present = (root / "ecs-benchmark" / "summary.json").is_file() and \
        (root / "ecs-benchmark" / "ecs_scope_verification.json").is_file() and \
        (root / "rollback-benchmark" / "summary.json").is_file()
    ecs_streams = (
        "ecs_maintenance_samples.csv",
        "index_maintenance_samples.csv",
        "general_allocation_samples.csv",
        "arena_samples.csv",
        "copy_on_write_samples.csv",
    )
    raw_samples_present = all((root / "ecs-benchmark" / name).is_file() for name in ecs_streams) and \
        (root / "rollback-benchmark" / "rollback_samples.csv").is_file()
    binary_hashes_present = (root / "binary-hashes.json").is_file()
    source_manifest_present = (root / "source-MANIFEST.sha256").is_file()
    hardware_inventory_present = (root / "hardware-inventory.json").is_file()
    thermal_method = thermal.get("measurement_method")
    thermal_present = isinstance(thermal_method, str) and bool(thermal_method.strip())
    if execution == "native_physical":
        thermal_present = thermal_present and isinstance(thermal.get("temperature_before_c"), (int, float)) \
            and not isinstance(thermal.get("temperature_before_c"), bool) \
            and isinstance(thermal.get("temperature_after_c"), (int, float)) \
            and not isinstance(thermal.get("temperature_after_c"), bool) \
            and isinstance(thermal.get("throttling_observed"), bool) \
            and "unavailable" not in thermal_method.lower()
    clock_present = require_bool(clock, "frequency_policy_recorded") and \
        require_bool(clock, "background_services_policy_recorded")
    evidence_manifest_verified = True  # verification already completed before this function.
    compatible = profile_compatibility(profile, environment, inventory)
    if bool(inventory.get("profile_compatibility_confirmed")) != compatible:
        raise VerificationError("inventory compatibility decision mismatch")
    native_conflict = inventory.get("native_claim_conflict") is True
    if native_conflict != (execution == "native_physical" and bool(
            inventory.get("observed", {}).get("virtualization", {}).get("detected"))):
        raise VerificationError("native-claim conflict flag mismatch")

    mask = 0
    if not baseline_complete(environment):
        mask |= FAILURE["MissingBaseline"]
    if result.get("environment_id") != environment.get("environment_id"):
        mask |= FAILURE["EnvironmentMismatch"]
    if not require_bool(environment, "environment_lock_recorded"):
        mask |= FAILURE["EnvironmentMismatch"]
    if not compatible or native_conflict:
        mask |= FAILURE["ProfileCompatibilityFailed"]
    if execution != "native_physical":
        mask |= FAILURE["NativeExecutionRequired"]
    if not full_report_present:
        mask |= FAILURE["MissingFullTestReport"]
    elif not full_tests_passed:
        mask |= FAILURE["FullTestSuiteFailed"]
    if profile == "P1" and not ecs_scope_complete:
        mask |= FAILURE["EcsScopeIncomplete"]
    if not benchmark_reports_present:
        mask |= FAILURE["MissingBenchmarkReport"]
    if not raw_samples_present:
        mask |= FAILURE["MissingRawSamples"]
    if not binary_hashes_present:
        mask |= FAILURE["MissingBinaryHashes"]
    if not source_manifest_present:
        mask |= FAILURE["MissingSourceManifest"]
    if not hardware_inventory_present:
        mask |= FAILURE["MissingHardwareInventory"]
    if not thermal_present:
        mask |= FAILURE["MissingThermalRecord"]
    if not evidence_manifest_verified:
        mask |= FAILURE["CampaignVerificationFailed"]
    if not clock_present:
        mask |= FAILURE["ClockPolicyMissing"]
    if migration:
        mask |= FAILURE["CpuMigrationDetected"]
    if profile == "P1" and not allocation_gate:
        mask |= FAILURE["AllocationGateFailed"]
    if profile == "P1":
        if rollback_samples <= 0 or ecs_samples <= 0:
            mask |= FAILURE["MissingMeasurement"]
        else:
            if rollback_samples < 1000 or ecs_samples < 1000:
                mask |= FAILURE["InsufficientSamples"]
            if rollback_p99 > 2_000_000:
                mask |= FAILURE["RollbackBudgetExceeded"]
            if ecs_p99 > 100_000:
                mask |= FAILURE["EcsBudgetExceeded"]
    if not determinism_passed:
        mask |= FAILURE["DeterminismFailed"]
    if not serialization_passed:
        mask |= FAILURE["SerializationFailed"]

    common_evidence = baseline_complete(environment) \
        and require_bool(environment, "environment_lock_recorded") \
        and compatible and full_report_present and benchmark_reports_present \
        and raw_samples_present and binary_hashes_present and source_manifest_present \
        and hardware_inventory_present and thermal_present and clock_present \
        and evidence_manifest_verified
    profile_evidence = profile != "P1" or (rollback_samples > 0 and ecs_samples > 0 and ecs_scope_complete)
    if not common_evidence or not profile_evidence:
        disposition = "incomplete"
    elif execution == "native_physical":
        disposition = "qualification_candidate"
    else:
        disposition = "engineering_baseline"

    if mask & INCOMPLETE_MASK:
        status = "unqualified"
    elif mask:
        status = "failed"
    else:
        status = "passed"

    expected = {
        "profile": profile,
        "execution_kind": execution,
        "rollback_samples": rollback_samples,
        "ecs_samples": ecs_samples,
        "rollback_p99_ns": rollback_p99,
        "ecs_maintenance_p99_ns": ecs_p99,
        "full_tests_passed": full_tests_passed,
        "determinism_passed": determinism_passed,
        "serialization_passed": serialization_passed,
        "ecs_scope_evidence_complete": ecs_scope_complete,
        "allocation_gate_passed": allocation_gate,
        "ecs_general_allocation_zero": ecs_general_allocation_zero,
        "ecs_arena_overflow_zero": ecs_arena_overflow_zero,
        "ecs_copy_on_write_semantics_valid": ecs_cow_valid,
        "ecs_index_maintenance_semantics_valid": ecs_index_valid,
    }
    # Suppress unused local warning by making workload IDs part of consistency checks.
    if ecs_summary.get("workload_id") != "Y1-O2-SPARSE-COMPONENT-MAINTENANCE-V1" \
            or rollback_summary.get("workload") != "canonical_10000_bodies_5000_contacts_rollback_8_frames":
        raise VerificationError("benchmark workload identity mismatch")
    return mask, status, disposition, expected


def verify_semantics(root: Path) -> None:
    result = load_json(root / "qualification-result.json")
    summary = load_json(root / "campaign-summary.json")
    request = load_json(root / "campaign-request.json")
    commands = command_map(root)
    mask, status, disposition, expected = recompute_decision(root)

    for key in ("profile", "execution_kind", "rollback_samples", "ecs_samples",
                "rollback_p99_ns", "ecs_maintenance_p99_ns"):
        if result.get(key) != expected[key]:
            raise VerificationError(f"qualification result differs from recomputed {key}")
    if result.get("failure_mask") != mask or result.get("status") != status \
            or result.get("evidence_disposition") != disposition:
        raise VerificationError("qualification decision differs from independently recomputed result")
    if summary.get("profile") != expected["profile"] or summary.get("execution_kind") != expected["execution_kind"]:
        raise VerificationError("campaign summary identity mismatch")
    if summary.get("failure_mask") != mask or summary.get("status") != status \
            or summary.get("evidence_disposition") != disposition:
        raise VerificationError("campaign summary decision mismatch")
    for key in ("rollback_samples", "ecs_samples", "rollback_p99_ns", "ecs_maintenance_p99_ns",
                "full_tests_passed", "determinism_passed", "serialization_passed",
                "ecs_scope_evidence_complete"):
        if summary.get(key) != expected[key]:
            raise VerificationError(f"campaign summary differs from recomputed {key}")
    for key in ("ecs_general_allocation_zero", "ecs_arena_overflow_zero",
                "ecs_copy_on_write_semantics_valid", "ecs_index_maintenance_semantics_valid"):
        if summary.get(key) != expected[key]:
            raise VerificationError(f"campaign summary differs from recomputed {key}")
    if summary.get("native_profile_qualified") is not (status == "passed"):
        raise VerificationError("native_profile_qualified does not match status")
    if summary.get("independent_verification_required") is not True:
        raise VerificationError("campaign summary omits independent-verification requirement")
    expected_scope_status = "verified_complete_v1" if expected["ecs_scope_evidence_complete"] \
        else "verification_failed"
    if summary.get("ecs_scope_acceptance_status") != expected_scope_status:
        raise VerificationError("ECS-scope acceptance status is incorrect")
    if summary.get("allocation_gate_passed") != expected["allocation_gate_passed"]:
        raise VerificationError("campaign summary allocation gate differs from independent recomputation")

    probe_return = commands["hardware-profile-probe"]["return_code"]
    expected_probe_return = 0 if status == "passed" else 1
    if probe_return != expected_probe_return:
        raise VerificationError("hardware-profile-probe exit code differs from qualification status")
    probe_stdout = (root / commands["hardware-profile-probe"]["stdout_file"]).read_text(encoding="utf-8")
    try:
        if json.loads(probe_stdout) != result:
            raise VerificationError("saved qualification result differs from probe stdout")
    except json.JSONDecodeError as error:
        raise VerificationError("hardware-profile-probe stdout is not valid JSON") from error

    # A complete virtualized run must remain an engineering baseline.
    if request.get("execution_kind") != "native_physical" and status == "passed":
        raise VerificationError("non-native execution passed qualification")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("campaign_directory", type=Path)
    args = parser.parse_args()
    root = args.campaign_directory.resolve()
    if not root.is_dir():
        raise VerificationError(f"campaign directory not found: {root}")
    required = {
        "campaign-request.json",
        "hardware-inventory.json",
        "thermal-record.json",
        "clock-policy.json",
        "binary-hashes.json",
        "source-MANIFEST.sha256",
        "command-results.json",
        "evidence-manifest.json",
        "qualification-result.json",
        "campaign-summary.json",
        "SHA256SUMS.json",
    }
    absent = sorted(name for name in required if not (root / name).is_file())
    if absent:
        raise VerificationError(f"required campaign files are missing: {absent}")

    verify_manifest_document(
        root,
        root / "SHA256SUMS.json",
        "neoeng.dcore.qualification-checksums.v1",
        exact_file_set=True,
    )
    evidence_paths = verify_manifest_document(
        root,
        root / "evidence-manifest.json",
        "neoeng.dcore.qualification-evidence-manifest.v1",
        exact_file_set=False,
    )
    required_predecision = {
        "campaign-request.json", "hardware-inventory.json", "thermal-record.json",
        "clock-policy.json", "binary-hashes.json", "source-MANIFEST.sha256",
        "ctest.stdout.txt", "ctest.stderr.txt", "determinism-probe.stdout.txt",
        "state-evidence-probe.stdout.txt", "ecs-scope-verifier.stdout.txt",
        "ecs-scope-verifier.stderr.txt", "ecs-benchmark/summary.json",
        "ecs-benchmark/ecs_scope_verification.json",
        "ecs-benchmark/ecs_maintenance_samples.csv",
        "ecs-benchmark/index_maintenance_samples.csv",
        "ecs-benchmark/general_allocation_samples.csv",
        "ecs-benchmark/arena_samples.csv",
        "ecs-benchmark/copy_on_write_samples.csv",
        "rollback-benchmark/summary.json", "rollback-benchmark/rollback_samples.csv",
    }
    if not required_predecision.issubset(evidence_paths):
        raise VerificationError(
            f"predecision evidence manifest is incomplete: {sorted(required_predecision - evidence_paths)}"
        )
    verify_source_manifest(root / "source-MANIFEST.sha256")
    verify_binary_hashes(root)
    verify_semantics(root)
    print("qualification_campaign_verification=passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (VerificationError, OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        print(f"qualification_campaign_verification=failed: {error}", file=sys.stderr)
        raise SystemExit(1)
