#!/usr/bin/env python3
"""Run a reproducible NeoEng D-Core hardware qualification campaign.

This tool never upgrades a virtualized/containerized execution to a qualified
profile. It records those runs as engineering baselines. Native qualification
also requires a complete, verified evidence set and the profile-specific gate.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable

SCHEMA = "neoeng.dcore.qualification-campaign-request.v1"
PROJECT_VERSION = "1.11.0"
PROFILES = {"P0", "P1", "P2", "P3", "P4"}
EXECUTION_KINDS = {"virtualized", "native_physical", "containerized"}


class CampaignError(RuntimeError):
    pass


def canonical_json(value: Any) -> str:
    return json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path: Path, value: Any) -> None:
    path.write_text(canonical_json(value), encoding="utf-8", newline="\n")


def require_mapping(value: Any, name: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise CampaignError(f"{name} must be an object")
    return value


def require_text(mapping: dict[str, Any], key: str) -> str:
    value = mapping.get(key)
    if not isinstance(value, str) or not value.strip():
        raise CampaignError(f"{key} must be a non-empty string")
    return value.strip()


def require_bool(mapping: dict[str, Any], key: str) -> bool:
    value = mapping.get(key)
    if not isinstance(value, bool):
        raise CampaignError(f"{key} must be boolean")
    return value


def require_positive_int(mapping: dict[str, Any], key: str) -> int:
    value = mapping.get(key)
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise CampaignError(f"{key} must be a positive integer")
    return value


def normalize_architecture(value: str) -> str:
    normalized = value.strip().lower().replace("amd64", "x86_64").replace("arm64", "aarch64")
    return normalized


def detect_virtualization() -> dict[str, Any]:
    indicators: list[str] = []
    if sys.platform.startswith("linux"):
        detector = shutil.which("systemd-detect-virt")
        if detector:
            completed = subprocess.run(
                [detector], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
            )
            value = completed.stdout.strip()
            if completed.returncode == 0 and value and value != "none":
                indicators.append(f"systemd-detect-virt:{value}")
        for candidate in (
            Path("/sys/class/dmi/id/product_name"),
            Path("/sys/class/dmi/id/sys_vendor"),
        ):
            try:
                text = candidate.read_text(encoding="utf-8", errors="replace").strip()
            except OSError:
                continue
            lower = text.lower()
            if any(token in lower for token in ("virtual", "vmware", "kvm", "qemu", "hyper-v", "xen")):
                indicators.append(f"{candidate.name}:{text}")
    return {"detected": bool(indicators), "indicators": sorted(set(indicators))}


def profile_compatibility(profile: str, environment: dict[str, Any]) -> tuple[bool, list[str]]:
    reasons: list[str] = []
    reviewed = require_bool(environment, "profile_compatibility_reviewed")
    if not reviewed:
        reasons.append("profile compatibility was not reviewed")
    cores = require_positive_int(environment, "physical_cpu_cores")
    gpu_vendor = require_text(environment, "gpu_vendor").upper()
    gpu_memory = environment.get("gpu_memory_bytes")
    if not isinstance(gpu_memory, int) or isinstance(gpu_memory, bool) or gpu_memory < 0:
        raise CampaignError("gpu_memory_bytes must be a non-negative integer")
    architecture = normalize_architecture(require_text(environment, "architecture"))
    storage_class = require_text(environment, "storage_class")

    if profile == "P1":
        if gpu_vendor != "NVIDIA": reasons.append("P1 requires NVIDIA GPU vendor")
        if gpu_memory < 16 * 1024**3: reasons.append("P1 requires at least 16 GiB GPU memory")
        if cores < 16: reasons.append("P1 requires at least 16 physical CPU cores")
        if storage_class != "nvme_pcie_4_or_5": reasons.append("P1 requires NVMe PCIe 4.0/5.0 storage")
    elif profile == "P2":
        if gpu_vendor not in {"AMD", "ADVANCED MICRO DEVICES"}:
            reasons.append("P2 requires AMD GPU vendor")
        if gpu_memory < 16 * 1024**3: reasons.append("P2 requires at least 16 GiB GPU memory")
        if cores < 16: reasons.append("P2 requires at least 16 physical CPU cores")
        if storage_class != "nvme_pcie_4_or_5": reasons.append("P2 requires NVMe PCIe 4.0/5.0 storage")
    elif profile == "P3":
        if architecture != "aarch64": reasons.append("P3 requires ARM64/AArch64 architecture")
    elif profile == "P4":
        if not (7 * 1024**3 <= gpu_memory <= 9 * 1024**3):
            reasons.append("P4 requires a declared GPU-memory class between 7 and 9 GiB")

    return reviewed and not reasons, reasons


def find_executable(build_dir: Path, name: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    candidates = [build_dir / f"{name}{suffix}"]
    candidates.extend(build_dir.rglob(f"{name}{suffix}"))
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise CampaignError(f"required executable not found under {build_dir}: {name}{suffix}")


@dataclass
class CommandResult:
    name: str
    command: list[str]
    return_code: int
    stdout_file: str
    stderr_file: str

    @property
    def passed(self) -> bool:
        return self.return_code == 0

    def as_json(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "command": self.command,
            "return_code": self.return_code,
            "passed": self.passed,
            "stdout_file": self.stdout_file,
            "stderr_file": self.stderr_file,
        }


def run_command(name: str, command: list[str], output_dir: Path, cwd: Path | None = None) -> CommandResult:
    stdout_path = output_dir / f"{name}.stdout.txt"
    stderr_path = output_dir / f"{name}.stderr.txt"
    completed = subprocess.run(
        command,
        cwd=cwd,
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    stdout_path.write_text(completed.stdout, encoding="utf-8", newline="\n")
    stderr_path.write_text(completed.stderr, encoding="utf-8", newline="\n")
    return CommandResult(
        name=name,
        command=command,
        return_code=completed.returncode,
        stdout_file=stdout_path.name,
        stderr_file=stderr_path.name,
    )


def manifest_rows(root: Path, excluded: Iterable[str] = ()) -> list[dict[str, Any]]:
    excluded_set = set(excluded)
    rows: list[dict[str, Any]] = []
    for path in sorted(root.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(root).as_posix()
        if relative in excluded_set:
            continue
        rows.append({"path": relative, "size": path.stat().st_size, "sha256": sha256_file(path)})
    return rows


def write_source_manifest(project_root: Path, destination: Path) -> None:
    """Capture the exact non-build project tree used by the campaign.

    The project-level MANIFEST may represent a release baseline and can be stale
    while a ChangeSet is under construction. Qualification evidence therefore
    computes its source identity directly at campaign execution time.
    """
    excluded_parts = {"build", ".deps", ".git", "__pycache__"}
    excluded_files = {"MANIFEST.sha256"}
    rows: list[str] = []
    for path in sorted(project_root.rglob("*")):
        if not path.is_file():
            continue
        relative_path = path.relative_to(project_root)
        if any(part in excluded_parts for part in relative_path.parts):
            continue
        if path.name in excluded_files or path.suffix == ".pyc":
            continue
        relative = relative_path.as_posix()
        rows.append(f"{sha256_file(path)}  {relative}")
    if not rows:
        raise CampaignError("source tree manifest would be empty")
    destination.write_text("\n".join(rows) + "\n", encoding="ascii", newline="\n")


def bool_word(value: bool) -> str:
    return "1" if value else "0"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--request", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()

    project_root = Path(__file__).resolve().parents[2]
    request = json.loads(args.request.read_text(encoding="utf-8"))
    if request.get("schema") != SCHEMA:
        raise CampaignError(f"request schema must be {SCHEMA}")
    if request.get("project_version") != PROJECT_VERSION:
        raise CampaignError(f"request project_version must be {PROJECT_VERSION}")
    profile = require_text(request, "profile")
    if profile not in PROFILES:
        raise CampaignError("profile must be P0, P1, P2, P3 or P4")
    execution_kind = require_text(request, "execution_kind")
    if execution_kind not in EXECUTION_KINDS:
        raise CampaignError("execution_kind is invalid")
    environment = require_mapping(request.get("environment"), "environment")
    thermal = require_mapping(request.get("thermal_record"), "thermal_record")
    clock = require_mapping(request.get("clock_policy"), "clock_policy")
    campaign = require_mapping(request.get("campaign"), "campaign")

    output_dir = args.output_dir.resolve()
    if output_dir.exists() and any(output_dir.iterdir()):
        raise CampaignError(f"output directory must be absent or empty: {output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)
    build_dir = args.build_dir.resolve()
    if not build_dir.is_dir():
        raise CampaignError(f"build directory not found: {build_dir}")

    declared_arch = normalize_architecture(require_text(environment, "architecture"))
    observed_arch = normalize_architecture(platform.machine())
    architecture_matches_host = declared_arch == observed_arch
    virtualization = detect_virtualization()
    native_claim_conflict = execution_kind == "native_physical" and virtualization["detected"]
    compatibility_ok, compatibility_reasons = profile_compatibility(profile, environment)
    if not architecture_matches_host:
        compatibility_reasons.append(
            f"declared architecture {declared_arch} differs from observed host {observed_arch}"
        )
        compatibility_ok = False
    if native_claim_conflict:
        compatibility_reasons.append("native_physical was requested but virtualization was detected")
        compatibility_ok = False

    inventory = {
        "schema": "neoeng.dcore.hardware-inventory.v1",
        "collected_at_utc": datetime.now(timezone.utc).isoformat(),
        "profile": profile,
        "execution_kind_declared": execution_kind,
        "observed": {
            "platform": platform.platform(),
            "system": platform.system(),
            "release": platform.release(),
            "architecture": observed_arch,
            "python": platform.python_version(),
            "virtualization": virtualization,
        },
        "declared": environment,
        "architecture_matches_host": architecture_matches_host,
        "native_claim_conflict": native_claim_conflict,
        "profile_compatibility_confirmed": compatibility_ok,
        "profile_compatibility_reasons": compatibility_reasons,
    }
    write_json(output_dir / "hardware-inventory.json", inventory)
    write_json(output_dir / "thermal-record.json", thermal)
    write_json(output_dir / "clock-policy.json", clock)
    write_json(output_dir / "campaign-request.json", request)
    write_source_manifest(project_root, output_dir / "source-MANIFEST.sha256")

    # ChangeSet 009 makes benchmark-generated, independently verified evidence
    # authoritative. Arbitrary externally supplied files are rejected so a P1
    # decision cannot be influenced by untyped or semantically unmapped data.
    legacy_ecs_scope = campaign.get("ecs_scope_artifacts")
    if legacy_ecs_scope not in (None, {}):
        raise CampaignError(
            "ecs_scope_artifacts is deprecated and must be omitted; "
            "the campaign generates and independently verifies all ECS streams"
        )

    executables = {
        "hardware_probe": find_executable(build_dir, "neoeng_hardware_profile_probe"),
        "ecs_benchmark": find_executable(build_dir, "neoeng_ecs_maintenance_benchmark"),
        "rollback_benchmark": find_executable(build_dir, "neoeng_v28_bare_metal_rollback"),
        "determinism_probe": find_executable(build_dir, "neoeng_determinism_probe"),
        "state_evidence_probe": find_executable(build_dir, "neoeng_state_evidence_probe"),
    }
    embedded_binary_dir = output_dir / "binaries"
    embedded_binary_dir.mkdir(parents=True, exist_ok=True)
    embedded_binaries: dict[str, Path] = {}
    binary_rows: list[dict[str, Any]] = []
    for name, source in sorted(executables.items()):
        destination = embedded_binary_dir / source.name
        shutil.copy2(source, destination)
        source_hash = sha256_file(source)
        embedded_hash = sha256_file(destination)
        if source_hash != embedded_hash or source.stat().st_size != destination.stat().st_size:
            raise CampaignError(f"embedded binary copy mismatch: {name}")
        embedded_binaries[name] = destination
        binary_rows.append({
            "name": name,
            "path": destination.relative_to(output_dir).as_posix(),
            "sha256": embedded_hash,
            "size": destination.stat().st_size,
        })
    binary_hashes = {
        "schema": "neoeng.dcore.qualification-binary-hashes.v1",
        "binaries": binary_rows,
    }
    write_json(output_dir / "binary-hashes.json", binary_hashes)

    results: list[CommandResult] = []
    ctest_scope = require_text(campaign, "ctest_scope")
    ctest_command = ["ctest", "--test-dir", str(build_dir), "--output-on-failure"]
    if ctest_scope == "smoke":
        ctest_command.extend(["-L", "smoke"])
    elif ctest_scope != "full":
        raise CampaignError("ctest_scope must be full or smoke")
    results.append(run_command("ctest", ctest_command, output_dir))
    results.append(run_command("determinism-probe", [str(executables["determinism_probe"])], output_dir))
    results.append(run_command("state-evidence-probe", [str(executables["state_evidence_probe"])], output_dir))

    ecs_output = output_dir / "ecs-benchmark"
    results.append(run_command(
        "ecs-benchmark",
        [
            str(executables["ecs_benchmark"]),
            str(ecs_output),
            str(require_positive_int(campaign, "ecs_body_count")),
            str(require_positive_int(campaign, "ecs_active_body_count")),
            str(require_positive_int(campaign, "ecs_samples")),
            str(require_positive_int(campaign, "ecs_warmup_samples")),
        ],
        output_dir,
    ))
    ecs_scope_verifier = project_root / "scripts" / "qualification" / "verify_ecs_scope_evidence.py"
    results.append(run_command(
        "ecs-scope-verifier",
        [sys.executable, str(ecs_scope_verifier), str(ecs_output), "--write-report"],
        output_dir,
    ))

    rollback_output = output_dir / "rollback-benchmark"
    rollback_command = [
        str(executables["rollback_benchmark"]),
        str(rollback_output),
        str(require_positive_int(campaign, "rollback_samples")),
    ]
    affinity_requested = require_bool(clock, "cpu_affinity_requested")
    if affinity_requested:
        cpu_index = clock.get("cpu_index")
        if not isinstance(cpu_index, int) or isinstance(cpu_index, bool) or cpu_index < 0:
            raise CampaignError("cpu_index must be a non-negative integer")
        rollback_command.append(str(cpu_index))
    results.append(run_command("rollback-benchmark", rollback_command, output_dir))
    ecs_summary_path = ecs_output / "summary.json"
    ecs_scope_report_path = ecs_output / "ecs_scope_verification.json"
    rollback_summary_path = rollback_output / "summary.json"
    ecs_summary = json.loads(ecs_summary_path.read_text(encoding="utf-8")) if ecs_summary_path.is_file() else {}
    ecs_scope_report = json.loads(ecs_scope_report_path.read_text(encoding="utf-8")) \
        if ecs_scope_report_path.is_file() else {}
    rollback_summary = json.loads(rollback_summary_path.read_text(encoding="utf-8")) if rollback_summary_path.is_file() else {}

    thermal_method = thermal.get("measurement_method")
    thermal_record_present = isinstance(thermal_method, str) and bool(thermal_method.strip())
    if execution_kind == "native_physical":
        thermal_record_present = thermal_record_present and isinstance(thermal.get("temperature_before_c"), (int, float)) \
            and isinstance(thermal.get("temperature_after_c"), (int, float)) \
            and isinstance(thermal.get("throttling_observed"), bool) \
            and "unavailable" not in thermal_method.lower()

    clock_policy_recorded = require_bool(clock, "frequency_policy_recorded") \
        and require_bool(clock, "background_services_policy_recorded")
    full_test_report_present = (output_dir / results[0].stdout_file).is_file() \
        and (output_dir / results[0].stderr_file).is_file()
    full_tests_passed = results[0].passed and ctest_scope == "full"
    determinism_passed = results[1].passed
    serialization_passed = results[2].passed
    benchmark_reports_present = ecs_summary_path.is_file() and ecs_scope_report_path.is_file() \
        and rollback_summary_path.is_file()
    ecs_raw_streams = (
        "ecs_maintenance_samples.csv",
        "index_maintenance_samples.csv",
        "general_allocation_samples.csv",
        "arena_samples.csv",
        "copy_on_write_samples.csv",
    )
    raw_samples_present = all((ecs_output / name).is_file() for name in ecs_raw_streams) \
        and (rollback_output / "rollback_samples.csv").is_file()
    campaign_inputs_complete = all(path.is_file() for path in (
        output_dir / "hardware-inventory.json",
        output_dir / "thermal-record.json",
        output_dir / "clock-policy.json",
        output_dir / "binary-hashes.json",
        output_dir / "source-MANIFEST.sha256",
    ))

    evidence_manifest = {
        "schema": "neoeng.dcore.qualification-evidence-manifest.v1",
        "entries": manifest_rows(output_dir, excluded={
            "evidence-manifest.json", "command-results.json", "qualification-result.json",
            "campaign-summary.json", "SHA256SUMS.json"
        }),
    }
    write_json(output_dir / "evidence-manifest.json", evidence_manifest)
    evidence_manifest_verified = all(
        (output_dir / row["path"]).is_file() and sha256_file(output_dir / row["path"]) == row["sha256"]
        for row in evidence_manifest["entries"]
    )

    rollback_p99_ns = int(round(float(rollback_summary.get("p99_ms", 0.0)) * 1_000_000.0))
    rollback_samples = int(rollback_summary.get("measured_samples", 0))
    ecs_p99_ns = int(ecs_summary.get("p99_ns", 0))
    ecs_samples = int(ecs_summary.get("measured_samples", 0))
    cpu_migration = bool(rollback_summary.get("cpu_migration_detected", False))
    ecs_scope_complete = bool(ecs_scope_report.get("scope_complete", False)) \
        and ecs_scope_report.get("status") == "passed" \
        and results[4].passed
    ecs_general_allocation_zero = bool(ecs_scope_report.get("general_allocation_zero", False))
    ecs_arena_overflow_zero = bool(ecs_scope_report.get("arena_overflow_zero", False))
    ecs_cow_valid = bool(ecs_scope_report.get("copy_on_write_semantics_valid", False))
    ecs_index_valid = bool(ecs_scope_report.get("index_maintenance_semantics_valid", False))
    allocation_gate = bool(rollback_summary.get("semantic_gate_passed", False)) \
        and bool(rollback_summary.get("allocation_probe_calibrated", False)) \
        and ecs_general_allocation_zero and ecs_arena_overflow_zero \
        and ecs_cow_valid and ecs_index_valid

    probe_options = [
        "--profile", profile,
        "--environment-id", require_text(environment, "environment_id"),
        "--cpu-sku", require_text(environment, "cpu_sku"),
        "--gpu-sku", require_text(environment, "gpu_sku"),
        "--driver-version", require_text(environment, "driver_version"),
        "--os-build", require_text(environment, "os_build"),
        "--power-profile", require_text(environment, "power_profile"),
        "--execution-kind", execution_kind,
        "--architecture", declared_arch,
        "--memory", require_text(environment, "memory_configuration"),
        "--storage", require_text(environment, "storage_configuration"),
        "--firmware", require_text(environment, "firmware_version"),
        "--thermal-policy", require_text(environment, "thermal_policy"),
        "--profile-compatible", bool_word(compatibility_ok),
        "--environment-locked", bool_word(require_bool(environment, "environment_lock_recorded")),
        "--rollback-p99-ns", str(rollback_p99_ns),
        "--rollback-samples", str(rollback_samples),
        "--ecs-p99-ns", str(ecs_p99_ns),
        "--ecs-samples", str(ecs_samples),
        "--determinism-passed", bool_word(determinism_passed),
        "--serialization-passed", bool_word(serialization_passed),
        "--full-test-report-present", bool_word(full_test_report_present),
        "--full-tests-passed", bool_word(full_tests_passed),
        "--ecs-scope-complete", bool_word(ecs_scope_complete),
        "--benchmark-report-present", bool_word(benchmark_reports_present),
        "--raw-samples-present", bool_word(raw_samples_present),
        "--binary-hashes-present", bool_word((output_dir / "binary-hashes.json").is_file()),
        "--source-manifest-present", bool_word((output_dir / "source-MANIFEST.sha256").is_file()),
        "--hardware-inventory-present", bool_word((output_dir / "hardware-inventory.json").is_file()),
        "--thermal-record-present", bool_word(thermal_record_present),
        "--campaign-verified", bool_word(evidence_manifest_verified and campaign_inputs_complete),
        "--clock-policy-recorded", bool_word(clock_policy_recorded),
        "--cpu-migration-detected", bool_word(cpu_migration),
        "--allocation-gate-passed", bool_word(allocation_gate),
    ]
    probe_result = run_command(
        "hardware-profile-probe",
        [str(executables["hardware_probe"]), *probe_options],
        output_dir,
    )
    probe_stdout = (output_dir / probe_result.stdout_file).read_text(encoding="utf-8")
    try:
        qualification = json.loads(probe_stdout)
    except json.JSONDecodeError as error:
        raise CampaignError(f"hardware profile probe did not emit valid JSON: {error}") from error
    write_json(output_dir / "qualification-result.json", qualification)
    results_with_probe = [*results, probe_result]
    write_json(output_dir / "command-results.json", {
        "schema": "neoeng.dcore.qualification-command-results.v1",
        "commands": [result.as_json() for result in results_with_probe],
    })

    summary = {
        "schema": "neoeng.dcore.qualification-campaign-summary.v1",
        "project_version": PROJECT_VERSION,
        "profile": profile,
        "execution_kind": execution_kind,
        "status": qualification.get("status"),
        "evidence_disposition": qualification.get("evidence_disposition"),
        "failure_mask": qualification.get("failure_mask"),
        "profile_compatibility_confirmed": compatibility_ok,
        "profile_compatibility_reasons": compatibility_reasons,
        "full_test_report_present": full_test_report_present,
        "full_tests_passed": full_tests_passed,
        "ecs_scope_evidence_complete": ecs_scope_complete,
        "ecs_scope_acceptance_status": "verified_complete_v1" if ecs_scope_complete else "verification_failed",
        "ecs_scope_verification_report": "ecs-benchmark/ecs_scope_verification.json",
        "ecs_general_allocation_zero": ecs_general_allocation_zero,
        "ecs_arena_overflow_zero": ecs_arena_overflow_zero,
        "ecs_copy_on_write_semantics_valid": ecs_cow_valid,
        "ecs_index_maintenance_semantics_valid": ecs_index_valid,
        "allocation_gate_passed": allocation_gate,
        "determinism_passed": determinism_passed,
        "serialization_passed": serialization_passed,
        "benchmarks_completed": benchmark_reports_present and raw_samples_present,
        "rollback_p99_ns": rollback_p99_ns,
        "rollback_samples": rollback_samples,
        "ecs_maintenance_p99_ns": ecs_p99_ns,
        "ecs_samples": ecs_samples,
        "native_profile_qualified": qualification.get("status") == "passed",
        "independent_verification_required": True,
        "note": "Virtualized and containerized runs are engineering baselines and cannot qualify a profile. Complete ECS evidence does not waive native P1 timing, zero-allocation or environment gates.",
    }
    write_json(output_dir / "campaign-summary.json", summary)

    checksums = {
        "schema": "neoeng.dcore.qualification-checksums.v1",
        "entries": manifest_rows(output_dir, excluded={"SHA256SUMS.json"}),
    }
    write_json(output_dir / "SHA256SUMS.json", checksums)

    verifier = project_root / "scripts" / "qualification" / "verify_qualification_campaign.py"
    verified = subprocess.run(
        [sys.executable, str(verifier), str(output_dir)],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    sys.stdout.write(verified.stdout)
    sys.stderr.write(verified.stderr)
    if verified.returncode != 0:
        return 1
    if not all(result.passed for result in results):
        return 1
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (CampaignError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"qualification campaign failed: {error}", file=sys.stderr)
        raise SystemExit(2)
