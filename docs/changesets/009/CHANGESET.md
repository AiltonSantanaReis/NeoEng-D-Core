# ChangeSet 009 — Complete Y1-O2 ECS Evidence Scope and Contract Reconciliation

## Identity

- Baseline: NeoEng D-Core 1.8.0
- Result: NeoEng D-Core 1.9.0
- Source of truth consulted: `docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`
- Normative requirements closed: `DCORE-ECS-002`, `DCORE-ECS-003`, `DCORE-ECS-004`, `DCORE-GOV-001`
- Known limitations closed: `LIM-001`, `LIM-019`

## Purpose

Close the Y1-O2 evidence-completeness gap without changing canonical state or transition semantics. The four required evidence classes are produced by the same benchmark execution and independently recomputed:

1. general allocation;
2. deterministic arena;
3. copy-on-write;
4. index maintenance.

Evidence completeness is deliberately separate from qualification. A complete report may prove that allocation occurred and must then fail the allocation gate instead of being mislabeled as incomplete evidence.

Hardware profile constraints are likewise separate from general project compatibility. P1 and P2 describe named reference targets for comparable campaigns; they are not universal minimum system requirements. Results produced on another recorded machine remain valid for that exact environment, but cannot be extrapolated to machines with nominally lower or higher specifications.

## Existing capabilities confirmed before implementation

The project already contained the ECS workload, arena implementation, copy-on-write behavior, maintenance timing, P0–P4 qualification harness and fail-closed decision engine. They were reused. This ChangeSet did not create duplicate ECS, rollback, hashing, state, qualification or Host SDK subsystems.

## Implementation

### Benchmark evidence contract

`neoeng_ecs_maintenance_benchmark` emits schema `neoeng.dcore.ecs-maintenance-benchmark.v2` and the scope contract `neoeng.dcore.ecs-scope-evidence.v1`:

- `general_allocation_samples.csv`;
- `arena_samples.csv`;
- `copy_on_write_samples.csv`;
- `index_maintenance_samples.csv`;
- compatibility stream `ecs_maintenance_samples.csv`;
- `summary.json`.

The benchmark calibrates its allocation probe before measurements and reports allocation count and bytes. Arena overflow, copy-on-write reconstruction and active/inactive index semantics are recorded explicitly.

### Independent verifier

`scripts/qualification/verify_ecs_scope_evidence.py` does not trust the benchmark decision. It recomputes:

- exact sample sequences and counts;
- p50, p95, p99 and maximum;
- legacy/index stream equivalence;
- active and inactive entity semantics;
- allocation calibration and totals;
- arena capacity and overflow semantics;
- copy-on-write reconstruction;
- final deterministic hash;
- SHA-256 of all input streams;
- the complete saved verification report.

Its self-test rejects six independently altered evidence classes.

### Qualification integration

The campaign runner and verifier require the raw ECS streams and the independent report. Arbitrary candidate files are no longer accepted as semantic evidence. The runner manifests the exact non-build project tree at execution time instead of copying a potentially stale release manifest.

For P1:

- complete evidence removes `EcsScopeIncomplete`;
- observed general allocation produces `AllocationGateFailed`;
- virtualized execution produces `NativeExecutionRequired`;
- fewer than 1,000 samples produces `InsufficientSamples`;
- absent clock-policy evidence produces `ClockPolicyMissing`;
- environment mismatch remains explicit;
- no timing, allocation or native gate is waived.

### Contract reconciliation

`docs/records/ADR-009-YEAR1-CONTRACT-VS-HOST-ABI.md` resolves the ambiguity between:

- the internal Year 1 replay/schema source contract; and
- the public Host C ABI 1.0.

The internal replay schema is frozen at version 1. The public Host ABI remains independently versioned as ABI 1.0. Product runtime metadata advances to 1.9.0.

## Canonical invariants

All 58 canonical implementation units under `src/` remain byte-for-byte identical to 1.8.0. Of 64 canonical public headers, only `include/neoeng/core/year1_contract.hpp` changed, solely to reconcile contract governance. No state, transition, numeric, rollback, serialization, hash or evidence algorithm was changed.

The complete comparison is in `audit/CHANGESET_009_CORE_INVARIANT_LEDGER.json`.

## Verified results

### Compiler and sanitizer matrices

- GCC 14.2.0 Release: 64/64 CTest passed in three runs (10.36 s, 9.13 s and 9.25 s).
- Clang 17.0.0 Release: 64/64 CTest passed in three runs (9.06 s, 7.59 s and 7.51 s).
- Clang 17.0.0 ASan + UBSan exact selected critical scope: 17/17 passed in the final 2.68 s run.
- LeakSanitizer was disabled; no leak-coverage claim is made.

### P0 virtualized campaign

- disposition: `incomplete`;
- status: `unqualified`;
- ECS scope complete: true;
- ECS p99: 5,200 ns;
- rollback p99: 1,202,243 ns;
- samples: 32;
- failure mask: 262274;
- failures: `EnvironmentMismatch`, `NativeExecutionRequired`, `ClockPolicyMissing`.

### P1 virtualized campaign

- disposition: `incomplete`;
- status: `unqualified`;
- ECS scope complete: true;
- `EcsScopeIncomplete`: false;
- ECS p99: 6,763 ns;
- rollback p99: 1,205,388 ns;
- samples: 32, below the contractual minimum of 1,000;
- general allocation events: 672;
- general allocation bytes: 901,376;
- arena overflow zero: true;
- copy-on-write semantics valid: true;
- index semantics valid: true;
- final deterministic ECS hash: `0x3562ED2079D86CF7`;
- failure mask: 1409154;
- profile compatibility: false;
- incompatibilities: NVIDIA GPU required, at least 16 GiB GPU memory, at least 16 physical CPU cores, and NVMe PCIe 4.0/5.0 storage;
- failures: `EnvironmentMismatch`, `NativeExecutionRequired`, `ProfileCompatibilityFailed`, `InsufficientSamples`, `ClockPolicyMissing`, `AllocationGateFailed`.

The observed timing values are diagnostic only because the campaigns were virtualized, had 32 samples, lacked native clock/thermal qualification and failed the allocation gate.

## Honest product status

The ECS evidence pipeline is complete, independently verifiable and fail-closed. The current campaign does not prove the public P1 ECS claim and does not satisfy zero allocation. No native P0–P4, Windows 1.9.0, ARM64, certification or external audit is claimed.

After CS009:

- requirements in the ledger: 36;
- claims in the ledger: 20;
- known limitations: 41;
- open mandatory internal requirements: 18;
- open mandatory internal limitations: 23;
- commercial ready: false.

## Rebuilt-package portability correction

The rebuilt distribution revision corrects the manifest generator itself. Path enumeration is ordered by the tuple of relative path components rather than by the host-specific `Path` ordering or by the complete POSIX path string. Manifest paths are still serialized with `/`, and the manifest is compared and written as explicit UTF-8 bytes with LF endings.

This correction changes no D-Core runtime, state, transition, rollback, numeric or Host ABI semantics. It exists to make the same 1,281-file tree generate byte-identical `MANIFEST.sha256` content under POSIX and Windows path models. Physical Windows execution remains an independent acceptance gate.
