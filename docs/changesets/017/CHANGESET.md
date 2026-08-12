# ChangeSet 017 — EV-00 baseline laboratory certification

State: `in_progress`  
Program: `POST_1_14_1`  
Stage: `EV-00`  
Control base: `de55e0882c6400a0409b5cf881c6ee796a975cdf`  
Source under test: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## Objective

Reproduce, characterize and record the immutable accepted `v1.14.1` baseline in the authorized D-Lab before any post-baseline behavior change.

This ChangeSet certifies only what is actually executed and evidenced in the recorded environment. It does not improve or modify product behavior.

## Normative inputs

Read-only inputs include the Source of Truth, Source of Truth Index, Plan 1.0, Amendments 1.1 through 1.4, D-Lab Validation Standard, evolution roadmap/requirements/invariants/amendments, D-Lab execution policy and scenario catalog.

No normative input is modified to obtain approval.

## Required baseline surface

The EV-00 campaign must cover:

- exact historical SHA checkout;
- environment identity;
- fresh build;
- supported CTest surface;
- determinism probe;
- Host SDK integration boundary;
- replay/rollback;
- state evidence;
- support bundle;
- hashes and evidence manifest;
- comparison with available historical evidence;
- historical assurance revalidation of CS001-CS015 under Amendment 1.1;
- scenario/oracle discipline, permanent regressions and independent evidence verification.

## Prior attempts

R1, R2 and R3 remain preserved historical attempts and are not qualifying evidence. R2 stopped before `stage_operation` because of DEV-0003. R3 stopped before `stage_operation` because of DEV-0004. CS016C and CS016D were separately accepted, merged and post-merge validated before R4 was created.

## Non-goals

CS017 does not authorize runtime/product source changes, ABI changes, replay/rollback/snapshot/serialization semantic changes, EV-01 build/CI hardening, claim expansion, release authorization, physical-hardware/ARM64/power-loss/WAN/certification claims without matching evidence, or reuse of prior builds as qualification.

## Preparation authorization

Post-CS016D `main` workflow run `31617502522`, job `94183881160`, executed the Action Authorization Gate on exact source `de55e0882c6400a0409b5cf881c6ee796a975cdf` and returned `prepare_stage_changeset(CS017, EV-00) => AUTHORIZED` with CS016A/B/C/D accepted.

Machine-readable record: `PREPARATION_AUTHORIZATION.json`.

## Current boundary

This preparation only creates CS017 control records and moves EV-00 to `in_progress` on this candidate branch. No D-Lab harness or qualifying baseline command is executed by preparation itself.

Before harness/workflow publication or any qualifying baseline command:

1. the start state must pass the existing governance workflow;
2. `start_stage(CS017)` must return `AUTHORIZED`;
3. `stage_operation` must be evaluated on the exact stable branch state for the exact intended harness/workflow paths and return `AUTHORIZED`.

`REJECT`, unexplained failure or missing evidence stops progress.
