# ChangeSet 017 R9 — EV-00 baseline laboratory certification

State: `PREPARING`  
Program: `POST_1_14_1`  
Stage: `EV-00`  
Control base: `3ebb989c5aaca65501ddbc5e552e1f751079e310`  
Source under test: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## Objective

Restart CS017 from the accepted post-CS000H `main` and certify the immutable `v1.14.1` D-Core baseline on the maintainer's physical Windows D-Lab before any post-baseline product evolution.

R9 is a fresh revision. It does not repair D-Core behavior and does not reuse any prior local build, test result or qualification state.

## Why R9 exists

R8 reached only a non-qualifying preparation preflight. Run `32546849264` failed in the canonical evolution-authorizer self-test before `start_stage`, `stage_operation`, harness publication or any physical qualifying command. CS000H preserved that failure and corrected only the lifecycle dependence of the self-test; CS000H was then accepted and merged as `main@3ebb989c5aaca65501ddbc5e552e1f751079e310`.

R9 therefore starts from that accepted control plane. R1-R8 remain historical evidence and are not reusable qualification state.

## Required ordering

1. Establish R9 `ACTION_SCOPE` from the current EV-00 maximum and transition only `EV-00.status` from `not_started` to `in_progress` in the candidate roadmap.
2. Pass the non-qualifying R9 preparation preflight. It must prove the exact base transition, current ACTION_SCOPE equality with the root maximum, canonical authorizer self-test, `start_stage`, intended future `stage_operation` paths, predecessor-history preservation and absence of product/runtime changes.
3. Only after that preflight may the R9 harness and EV-00 workflow be published under authorized stage paths.
4. Freeze `audit/validation/CS017/VALIDATION_PLAN.json` only after all qualifying harness/workflow/verifier bytes exist and before any physical `Qualify` command.
5. Execute the qualifying campaign on the maintainer's physical Windows machine in a fresh isolated workspace.
6. Commit exact terminal evidence without altering the frozen campaign files.
7. Validate committed evidence against the frozen ChangeSet plan.
8. Bind the exact qualifying source/run/attempt/workflow in `VALIDATION_RESULT.json`; EV-00 acceptance remains a separate closure operation.

## Laboratory boundary

The qualifying EV-00 D-Lab campaign must execute on the maintainer's physical Windows PC. GitHub-hosted runners may validate control-plane state and committed evidence, but they do not substitute for the physical campaign.

The D-Core source under test remains fixed at `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`. Any later product fix belongs to a later ChangeSet and cannot be folded into this baseline certification.

## Preserved predecessor history

- R1: failed before a qualifying product campaign (`31594048822`).
- R2: stopped before stage operation by DEV-0003 (`31598613467`).
- R3: stopped before stage operation by DEV-0004 (`31613924661`).
- R4: stopped before stage operation during the CS016E root/protection audit (`31618330920`).
- R5: local run `ev00-20260813T002607Z-80c3ae08` remains `FAILED` because vcpkg bootstrap invocation quoting failed.
- R6: local run `ev00-20260813T004225Z-961f7e1a` remains `FAILED` because PowerShell rejected an empty argument collection before the first determinism process.
- R7: preparation-only; qualification never started (`31655844950`).
- R8: non-qualifying preparation preflight failed (`32546849264`) in the authorizer self-test; `start_stage`, `stage_operation` and physical qualification did not execute.

R8's failure is preserved by `audit/validation/CS000H/R8_PREPARATION_FAILURE.json`; it is not rerun or reclassified.

## Non-goals and non-effects

R9 does not authorize:

- changes to `src/**`, `include/**`, product `tests/**`, CMake/runtime/ABI surfaces;
- fixes to defects or test gaps discovered by laboratory evidence;
- release authorization;
- EV-01 work;
- reuse of R5-R8 outputs as qualifying evidence;
- weakening or removal of a required test after observing a result.

Release remains unauthorized. EV-00 cannot become accepted until the complete R9 evidence and acceptance contract is satisfied.
