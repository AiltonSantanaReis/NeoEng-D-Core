# ChangeSet 017 R8 — EV-00 baseline laboratory certification

State: `PREPARING`  
Program: `POST_1_14_1`  
Stage: `EV-00`  
Control base: `ed3661ee3aad366d639d1a3de5934e53c507c135`  
Source under test: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## Objective

Rebuild CS017 from the current accepted `main` and certify the immutable `v1.14.1` D-Core baseline on the maintainer's physical Windows D-Lab before any post-baseline product evolution.

This revision does not improve or modify D-Core behavior. It prepares, executes and validates laboratory evidence only.

## Why R8 is a new revision

R1-R7 belong to older control-plane states and are historical evidence, not reusable qualification state. R8 begins from the post-CS000G `main`, after the ChangeSet validation transition, evolution-authorizer reconciliation and retirement/bounding of stale specific workflows.

R5 and R6 remain failed local campaigns. R7 remains preparation-only with qualification never started. Their records are imported byte-for-byte into this lineage.

## Required ordering

1. Establish R8 `ACTION_SCOPE` from the current EV-00 maximum and transition the EV-00 roadmap state from `not_started` to `in_progress` using preparation-only paths.
2. Pass the non-qualifying R8 preparation preflight. It must prove canonical authorizer self-test, `start_stage`, intended future `stage_operation` paths, action-scope equality with the current maximum, historical-record preservation and absence of product/runtime changes.
3. Only after that preflight may the R8 harness and EV-00 workflow be published under the currently authorized stage paths (`scripts/dlab/**` and `.github/workflows/ev00-dlab.yml`).
4. Freeze `audit/validation/CS017/VALIDATION_PLAN.json` after all harness/workflow/verifier files exist and before any physical `Qualify` command.
5. Run the qualifying campaign on the maintainer's physical Windows PC in a fresh workspace.
6. Commit the exact local evidence package and active-run pointer without changing frozen campaign files.
7. Run the GitHub evidence-validation workflow and require every test in the frozen plan to PASS.
8. Bind the exact GitHub source/run/attempt/workflow in `VALIDATION_RESULT.json`. EV-00 acceptance and any subsequent roadmap advancement remain separate closure operations.

## Laboratory boundary

The qualifying D-Lab campaign MUST execute on the maintainer's physical Windows PC. GitHub-hosted runners may perform static/preflight checks and independently validate committed evidence, but they do not substitute for the physical campaign.

The baseline source is fixed at `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`. The local run must use a fresh isolated source worktree, build tree, dependency tree and evidence root.

## Required evidence surface

At minimum R8 retains the R7 intended surface:

- exact historical source identity;
- physical Windows environment identity;
- fresh isolated build;
- supported CTest surface;
- deterministic repeat probe;
- Host SDK boundary;
- replay/rollback surface;
- state-evidence probe;
- support-bundle probe;
- historical release-gate revalidation;
- SHA-256 evidence manifest and independent verification;
- historical CTest comparison;
- CS001-CS015 historical assurance review;
- explicit terminal state.

No required test may be removed after the R8 validation plan is frozen.

## Preserved predecessor history

- R1: failed before a qualifying product campaign (`31594048822`).
- R2: stopped before stage operation by DEV-0003 (`31598613467`).
- R3: stopped before stage operation by DEV-0004 (`31613924661`).
- R4: stopped before stage operation during the CS016E root/protection audit (`31618330920`).
- R5: local run `ev00-20260813T002607Z-80c3ae08` remains `FAILED`; harness invocation quoting prevented vcpkg bootstrap execution.
- R6: local run `ev00-20260813T004225Z-961f7e1a` remains `FAILED`; PowerShell rejected an empty argument collection before the first determinism process started.
- R7: preflight run `31655844950` passed static preparation checks, but `qualification_state=NOT_STARTED`; no R7 qualifying run occurred.

None of those outcomes is reclassified or reused as R8 evidence.

## Non-goals and non-effects

R8 does not authorize:

- changes to `src/**`, `include/**`, product `tests/**`, CMake/runtime/ABI surfaces;
- product fixes discovered by the laboratory campaign;
- release authorization;
- EV-01 work;
- claims outside the exact executed environment;
- reuse of R5/R6/R7 build or evidence output as qualification;
- weakening/removal of a required test after observing a result.

Release remains unauthorized. EV-00 cannot become accepted until the complete R8 acceptance contract is satisfied.
