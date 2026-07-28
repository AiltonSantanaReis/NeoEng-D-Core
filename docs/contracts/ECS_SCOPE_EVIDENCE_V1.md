# NeoEng D-Core ECS Scope Evidence Contract v1

Schema: `neoeng.dcore.ecs-scope-evidence.v1`
Verifier schema: `neoeng.dcore.ecs-scope-evidence-verification.v1`
Workload: `Y1-O2-SPARSE-COMPONENT-MAINTENANCE-V1`
Status: active in NeoEng D-Core 1.10.0

## Purpose

This contract closes the previously incomplete Y1-O2 evidence boundary. A campaign must generate and independently verify four semantically mapped streams from the same benchmark execution:

1. general C++ heap allocation;
2. persistent epoch-arena behavior;
3. copy-on-write behavior;
4. sparse index/component maintenance.

Evidence completeness is not a performance qualification. In particular, a complete evidence set can truthfully show nonzero general allocation and therefore fail the P1 zero-allocation gate.

## Authoritative producer

`neoeng_ecs_maintenance_benchmark` is the only accepted producer for this contract. Arbitrary external files are not accepted as qualification evidence.

The producer emits:

- `summary.json` using `neoeng.dcore.ecs-maintenance-benchmark.v2`;
- `ecs_maintenance_samples.csv` for compatibility with the prior workload;
- `index_maintenance_samples.csv`;
- `general_allocation_samples.csv`;
- `arena_samples.csv`;
- `copy_on_write_samples.csv`.

## Independent verification

`scripts/qualification/verify_ecs_scope_evidence.py` recomputes from raw data:

- exact sample sequence and count;
- p50, p95, p99 and maximum duration;
- cross-stream mappings;
- active/inactive index semantics;
- copy-on-write allocation and reconstruction semantics;
- allocation-probe calibration and aggregate allocation decision;
- arena allocation, capacity and overflow semantics;
- SHA-256 of every source stream.

The saved report is accepted only when it is byte-for-byte semantically equivalent to the independent recomputation.

## P1 decision rule

For P1:

- `ecs_scope_evidence_complete` requires a passed independent report;
- `allocation_gate_passed` additionally requires zero measured general C++ heap allocation, no arena overflow, valid COW/index semantics and the rollback allocation gate;
- timing requires at least 1,000 measured samples and ECS p99 <= 100,000 ns;
- the campaign must still be `native_physical` in a compatible, locked environment.

No evidence-completeness result waives native execution, timing, allocation, thermal, clock or compatibility gates.

## Fail-closed behavior

Missing streams, altered mappings, uncalibrated allocation data, arena overflow, COW reconstruction, summary/raw disagreement or a forged saved report are rejected. The qualification runner cannot substitute user-supplied untyped artifacts for generated evidence.
