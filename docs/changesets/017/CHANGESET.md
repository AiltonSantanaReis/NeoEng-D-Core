# ChangeSet 017 R10 — EV-00 baseline laboratory certification

State: `IMPLEMENTED CANDIDATE / PHYSICAL QUALIFICATION NOT STARTED`

Control base: `3ebb989c5aaca65501ddbc5e552e1f751079e310`  
Protected source under test: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## Objective

Execute EV-00 prospectively after preserving the failed R9 physical harness-initialization attempt. R10 does not modify D-Core runtime, ABI, product tests or build definitions.

## R9 failure preserved

R9 physical Attempt 1 used the frozen plan commit `e12d3780eb95af076399ace7ef906dc6489948f3` and local run id `ev00-20260822T074919Z-47b2284e`. Preflight passed on the physical Windows host, but Qualify aborted while constructing `run-identity` because the preserved runner called `.Trim()` directly on the empty output of `git branch --show-current` in detached HEAD.

The machine-readable record is `docs/changesets/017/R9_PHYSICAL_ATTEMPT1_FAILURE.json`. The attempt is not rerun or reclassified and is not qualifying evidence.

## R10 correction

The R9 runner remains byte-for-byte unchanged at Git blob `00cc326fa1c8cd40402c525c877a1af21969ba42`. R10 adds `scripts/dlab/ev00/invoke_ev00_r10.ps1`, which:

1. derives the canonical plan commit from `audit/validation/CS017/VALIDATION_PLAN.json`;
2. requires current `HEAD` to equal that exact commit;
3. requires a named local branch, rejecting detached HEAD before the preserved runner is invoked;
4. requires a clean control tree;
5. delegates to the preserved R9 runner with explicit `-ControlRepo` and `-LabRoot`.

This is a control-harness correction only. The protected D-Core source under test remains immutable.

## Qualification ordering

1. R10 candidate must pass static GitHub validation, including authorizer checks, wrapper contract checks, verifier self-test and PowerShell syntax checks.
2. The canonical `VALIDATION_PLAN.json`, wrapper, preserved runner, verifier, workflow, runbook and stage scope are frozen in the same plan commit.
3. The physical PC must fetch that commit and create a **named local branch at exactly that commit**. Detached control checkout is prohibited in R10.
4. Run the R10 wrapper in `Preflight` mode.
5. Only after Preflight passes, run the R10 wrapper in `Qualify` mode once.
6. A failed physical attempt remains evidence and must be preserved before any later revision.
7. A PASSED package must be committed with `ACTIVE_LOCAL_RUN.json` and historical assurance evidence.
8. The frozen GitHub evidence workflow must pass every required test before a `VALIDATION_RESULT.json` can bind the run.
9. EV-00 acceptance is a separate closure operation; release and EV-01 remain unauthorized until then.

## Non-effects

- runtime: NONE;
- ABI: NONE;
- product tests: NONE;
- CMake/product build definitions: NONE;
- release: NOT AUTHORIZED;
- EV-01: NOT STARTED;
- R9 evidence rewrite: NONE.
