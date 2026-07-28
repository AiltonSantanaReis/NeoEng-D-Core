# NeoEng D-Core — public technical claims

Generated from the normative claims ledger for baseline `1.14.0`.

Only the statements and scopes below are authorized. Planned, unsupported, removed and prohibited claims are intentionally absent.

## CLAIM-AUTH-001

The D-Core is the sole authority over canonical state in the homologated architecture.

- Status: `implemented`
- Scope: official APIs and in-process integration contract
- Public use: `allowed_with_scope`
- Evidence:
  - `docs/architecture/HOST_SDK_BOUNDARY.md`
  - `docs/contracts/HOST_SDK_C_ABI_V1.md`

## CLAIM-DET-001

Canonical transitions are deterministic in the declared corpus on Linux x86_64 across GCC and Clang.

- Status: `verified`
- Scope: recorded GCC 14.2 and Clang 17 x86_64 corpus
- Public use: `allowed_with_environment`
- Evidence:
  - `docs/changesets/007/evidence/determinism-gcc.txt`
  - `docs/changesets/007/evidence/determinism-clang.txt`

## CLAIM-DIST-001

The product provides a two-instance reference synchronization flow over real UDP loopback transport.

- Status: `verified`
- Scope: single-authority correction, x86_64 campaign, UDP loopback reference; remote production transport, multiwriter ordering and consensus are out of scope
- Public use: `allowed_only_with_reference_loopback_and_single_authority_scope`
- Evidence:
  - `docs/contracts/DISTRIBUTED_REFERENCE_V1.md`
  - `neoeng_distributed_reference_tests`
  - `neoeng_distributed_reference_probe`
  - `docs/changesets/010/TEST_STATUS.md`
  - `docs/changesets/010/evidence/cross-compiler-20260726/cross-compiler-summary.json`
  - `docs/changesets/010/evidence/github-actions-run-30187433814.json`

## CLAIM-DIV-001

A requested comparison can locate the first observed divergence through stable hash, SHA-256, Merkle chunk and known semantic component.

- Status: `verified`
- Scope: all fields of the sole promised WorldState v1 schema; CS010 reference coordinator orchestrates fingerprint comparison and DCoreReplicaAdapter invokes semantic localization
- Public use: `allowed_with_orchestration_scope`
- Evidence:
  - `docs/changesets/005/CHANGESET.md`
  - `tests/observability_support_tests.cpp`
  - `neoeng_distributed_reference_probe`
  - `neoeng_temporal_closure_tests`
  - `docs/changesets/012/TEST_STATUS.md`

## CLAIM-HASH-001

Canonical SHA-256, Merkle roots, inclusion proofs and chained evidence are implemented and verified in the declared corpus.

- Status: `verified`
- Scope: GCC/Clang Linux x86_64 corpus
- Public use: `allowed_with_environment`
- Evidence:
  - `docs/contracts/STATE_EVIDENCE_V1.md`
  - `docs/changesets/004/evidence/state-evidence-gcc.txt`
  - `docs/changesets/004/evidence/state-evidence-clang.txt`

## CLAIM-REPLAY-001

The runtime supports deterministic replay and retained historical reconstruction.

- Status: `verified`
- Scope: WorldState v1, bounded live window and explicit append-only durable export recorder
- Public use: `allowed_with_retention_limits`
- Evidence:
  - `docs/TRACEABILITY_YEAR1.md`
  - `docs/VALIDATION_REPORT.md`
  - `docs/contracts/TEMPORAL_CLOSURE_V1.md`
  - `docs/changesets/012/TEST_STATUS.md`

## CLAIM-ROLLBACK-001

The runtime supports correction and deterministic resimulation across an eight-frame rollback window.

- Status: `verified`
- Scope: functional corpus; irreversible committed host effects are explicit boundaries; physical P1 latency qualification pending
- Public use: `allowed_without_unqualified_latency_claim`
- Evidence:
  - `docs/TRACEABILITY_YEAR1.md`
  - `docs/changesets/007/evidence/host-sdk-reference-gcc.txt`
  - `docs/contracts/TEMPORAL_CLOSURE_V1.md`
  - `neoeng_temporal_closure_tests`

## CLAIM-SCOPE-001

NeoEng D-Core is an independent product and not the implementation of the five-year engine program.

- Status: `verified`
- Scope: product governance baseline 1.14.0
- Public use: `allowed`
- Evidence:
  - `docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`

## CLAIM-SDK-001

An installable C Host SDK provides opaque-handle lifecycle and selected runtime capabilities.

- Status: `verified`
- Scope: static library; Linux x86_64 GCC/Clang installation test
- Public use: `allowed_with_platform_and_surface_limits`
- Evidence:
  - `docs/contracts/HOST_SDK_C_ABI_V1.md`
  - `docs/changesets/007/TEST_STATUS.md`

## CLAIM-SECTOR-001

The horizontal D-Core can be adapted to games, simulation, robotics, digital twins, defense, aerospace and finance.

- Status: `implemented`
- Scope: architectural applicability only; sector adapters and certifications are separate
- Public use: `allowed_as_architectural_applicability_only`
- Evidence:
  - `audit/PRODUCT_SCOPE_RESPONSIBILITY_MATRIX.json`
  - `docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`

## CLAIM-SUPPORT-001

Support bundles are integrity-checked and independently verifiable.

- Status: `verified`
- Scope: bundle integrity and tamper detection; confidentiality not included
- Public use: `allowed_with_confidentiality_limit`
- Evidence:
  - `docs/contracts/SUPPORT_BUNDLE_V1.md`
  - `scripts/verify_support_bundle.py`

## CLAIM-TT-001

Time-travel navigation, durable authorized export and semantic state diff are available for WorldState v1.

- Status: `verified`
- Scope: bounded in-memory history plus explicit durable recorder; WorldState v1 is the sole promised canonical schema
- Public use: `allowed_with_schema_and_retention_limits`
- Evidence:
  - `docs/changesets/001/OBSERVABILITY_TIME_TRAVEL.md`
  - `docs/changesets/005/CHANGESET.md`
  - `docs/contracts/TEMPORAL_CLOSURE_V1.md`
  - `docs/changesets/012/TEST_STATUS.md`

## Mandatory exclusions

This document does not authorize claims of unrestricted production readiness, ARM64 equivalence, contractual hardware performance, certification, independent audit, ROI, included asymmetric signing or universal sector readiness.
