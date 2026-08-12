# ChangeSet 017 — EV-00 baseline laboratory certification

State: `in_progress`  
Program: `POST_1_14_1`  
Stage: `EV-00`  
Control base: `7393b32d2be3fd2e65eab6a738a0066c13848f6c`  
Source under test: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## Objective

Reproduce, characterize and record the immutable accepted `v1.14.1` baseline in
the authorized D-Lab before any post-baseline behavior change.

This ChangeSet certifies only what is actually executed and evidenced in the
recorded environment. It does not improve or modify product behavior.

## Normative inputs

Read-only inputs include:

- `docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`;
- `audit/SOURCE_OF_TRUTH_INDEX.json`;
- `docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md`;
- Amendments 1.1, 1.2 and 1.3;
- `docs/governance/DLAB_VALIDATION_STANDARD.md`;
- evolution roadmap, requirements, invariants and amendments ledgers;
- D-Lab execution policy and scenario catalog.

No normative input is modified to obtain approval.

## Required baseline surface

The EV-00 campaign must cover the mandatory Plan 1.0 surface:

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
- comparison with available historical evidence.

Amendment 1.1 additionally requires historical assurance revalidation of
CS001-CS015, scenario/oracle discipline, reproducible run identity, fresh
workspace/build, preservation of failures and independent evidence verification.

## Prior attempts

R1 and R2 remain preserved historical attempts and are not qualifying evidence.
R2 was stopped before `stage_operation` and before any qualifying product
campaign when `DEV-0003` identified the path-canonicalization governance defect.
CS016C corrected and accepted that governance defect before this R3 branch was
created.

## Non-goals

CS017 does not authorize:

- product/runtime source changes;
- ABI changes;
- replay/rollback/snapshot/serialization semantic changes;
- build-system fixes or CI hardening assigned to EV-01;
- claim expansion;
- release authorization;
- physical hardware, ARM64, power-loss, WAN or certification claims without
  matching executed evidence;
- reuse of a previous build as qualification for a new run.

## Write scope

Exact path authorization is recorded in `ACTION_SCOPE.json`.
Allowlisting alone is insufficient: all stage operations require an explicit
`stage_operation` authorization on the exact branch state.

## Preparation authorization

The post-CS016C `main` workflow run `31613040252`, job `94168915002`, executed
the Action Authorization Gate on exact source
`7393b32d2be3fd2e65eab6a738a0066c13848f6c` and returned
`prepare_stage_changeset(CS017, EV-00) => AUTHORIZED` with CS016A/B/C accepted.
The machine-readable record is `PREPARATION_AUTHORIZATION.json`.

## Current boundary

This preparation commit only creates the ChangeSet control records and moves
EV-00 to `in_progress` on this candidate branch. No D-Lab harness or qualifying
baseline command is executed by preparation itself.

Before any harness/workflow publication or qualifying baseline command:

1. the start state must pass the existing governance workflow;
2. `start_stage(CS017)` must be `AUTHORIZED`;
3. a `stage_operation` request listing the exact harness/workflow paths must be
   evaluated and return `AUTHORIZED`.

A `REJECT`, unexplained failure or missing evidence stops progress.
