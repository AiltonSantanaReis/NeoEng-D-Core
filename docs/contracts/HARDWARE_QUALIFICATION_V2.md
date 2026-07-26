# NeoEng D-Core Hardware Qualification Contract v2

Schema: `neoeng.dcore.hardware-qualification.v2`

## Purpose

This contract qualifies the NeoEng D-Core canonical runtime on a recorded hardware profile. It does not qualify the complete NeoEng product, a renderer, a final Year-2 scene, a commercial SLA or a platform that was not executed.

The profile definitions are measurement and comparison targets, not universal minimum system requirements for running, integrating or testing NeoEng D-Core. A result applies only to the exact recorded environment and campaign conditions. A machine with lower or higher nominal specifications may perform better or worse because CPU architecture, memory behavior, drivers, firmware, power policy, thermals and background load all affect wall-clock measurements. No cross-machine result may be inferred from component specifications alone.

## Non-negotiable decision rule

A profile can return `passed` only when all of the following are true:

1. execution is declared `native_physical`;
2. automatic host observation does not detect a virtualization conflict;
3. the declared architecture matches the executing host;
4. the environment is complete, locked and reviewed against the profile contract;
5. the complete registered CTest suite passes;
6. determinism and serialization/evidence probes pass;
7. benchmark summaries and raw samples are present;
8. binaries are embedded and hashed, and the non-build source tree is manifested directly at campaign execution time;
9. hardware, thermal and clock policies are recorded;
10. the evidence manifest verifies;
11. every profile-specific requirement passes.

Virtualized and containerized campaigns are always `unqualified`. A complete one is classified as `engineering_baseline`; it is useful evidence but never a hardware qualification.

## Profiles

- **P0** - fixed reference laboratory machine for reproducibility and daily regression.
- **P1** - NVIDIA primary D-Core reference performance target. Qualification under the name P1 requires the target class declared in the project plan, 1,000 rollback samples, 1,000 ECS samples, rollback p99 <= 2,000,000 ns, ECS maintenance p99 <= 100,000 ns, zero-allocation semantic gate and the complete ECS evidence scope. These constraints do not prohibit execution or evidence collection on other hardware.
- **P2** - AMD semantic portability and separately qualified performance. P1 limits are not inherited.
- **P3** - native ARM64 determinism and serialization compatibility. Performance is reported separately.
- **P4** - approximately 8 GiB compatibility and safe-degradation evidence. P1 limits are not inherited.

## Complete ECS evidence scope for P1

The project plan requires the Y1-O2 metric to distinguish general allocation, arena, copy-on-write and index maintenance. NeoEng D-Core 1.9.0 implements the accepted `neoeng.dcore.ecs-scope-evidence.v1` contract.

The campaign itself generates all four streams from one execution and invokes `scripts/qualification/verify_ecs_scope_evidence.py`. User-supplied candidate files are rejected. The verifier recomputes sample counts, percentiles, cross-stream mappings, allocation-probe calibration, arena overflow and COW/index semantics from raw CSV files.

A verified complete scope removes `EcsScopeIncomplete`; it does not make P1 pass. P1 still requires native physical execution, the declared hardware/environment, at least 1,000 samples, rollback p99 <= 2,000,000 ns, ECS p99 <= 100,000 ns and a true zero-allocation gate. A complete report that observes allocations correctly fails `AllocationGateFailed` rather than being mislabeled as incomplete evidence.

Runs outside the P1 target class remain valid environment-specific engineering evidence when their machine inventory, binaries, parameters and limitations are recorded. They must be reported as observed results for that machine rather than generalized as P1 performance or as a promise for other machines.

The detailed contract is `docs/contracts/ECS_SCOPE_EVIDENCE_V1.md`.

## Workload identity

The automated ECS timing workload is `Y1-O2-SPARSE-COMPONENT-MAINTENANCE-V1`:

- 10,000 bodies;
- 100 active bodies;
- ComponentSoA page size 64;
- 128 warmup samples;
- 1,000 measured samples.

The existing rollback workload is `Y1-O3-CANONICAL-ROLLBACK-8-V1`:

- 10,000 bodies;
- 5,000 contacts;
- correction and resimulation of 8 frames;
- 16 warmup samples;
- 1,000 measured samples for qualification.

The machine-readable registry is `config/qualification_workloads.v1.json`. Its incomplete experiments are explicitly marked and are not silently promoted to complete.

## Integrity boundary

The harness embeds the five executed probe/benchmark binaries, generates `source-MANIFEST.sha256` directly from the non-build project tree at campaign execution time, hashes the complete campaign, and an independent verifier rejects missing, additional or modified files. The campaign does not copy a potentially stale release manifest while a ChangeSet is under construction. The verifier also recomputes benchmark percentiles, sample counts, compatibility rules, failure masks, status and evidence disposition instead of trusting the emitted decision. This provides internal package integrity and semantic consistency. It does not replace an external signature, WORM store, TPM/HSM or independent laboratory custody.
