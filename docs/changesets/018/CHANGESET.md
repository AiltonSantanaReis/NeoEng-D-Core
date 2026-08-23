# ChangeSet 018 — EV-01 build, CI and operational governance hardening

## Identity

- ChangeSet: `CS018`
- Evolution stage: `EV-01`
- Protected base: `e9e095e61d4de2995db704c51e9308850e1c929d`
- Stage objective: harden build, CI and operational governance without changing product semantics.

## Pre-freeze findings

The protected base had no current PR/main product regression workflow.

The corrected workflow classification found:

- current unpinned external Action refs: `0`;
- current unknown `NEOENG_*` CMake options: `0`;
- historical-branch unpinned Action refs: `43`;
- historical unknown `NEOENG_DCORE_BUILD_VIEWER` occurrences: `7`;
- current permanent product regression workflows: `0`.

The historical CS010-CS015 workflows remain historical evidence and are not rewritten merely to zero historical counters.

The real current CMake option is `NEOENG_DCORE_BUILD_VIEW_LAB`.

## Pre-freeze governance defect discovered on Windows

`scripts/verify_evolution_plan.py` used host-native `str(Path(...))` values when comparing repository-relative normative paths.

On Windows this produced backslashes and caused false mismatches for:

- `active_evolution_program.master_plan`;
- `active_evolution_program.roadmap`;
- `roadmap.normative_document`.

CS018 changes those repository-relative comparisons to `Path.as_posix()`.

The same pre-freeze exercise exposed two mutation self-test fixtures whose negative assumptions had become stale after EV-00 acceptance. Their fixtures are repaired without weakening the production validation rules.

## Current permanent control plane added

`.github/workflows/current-product-regression.yml` becomes the permanent current product regression surface for:

- `pull_request`;
- push to `main`;
- explicit manual diagnostics.

It performs current workflow classification, critical Action pinning verification, current CMake-option verification, historical-boundary verification, configure, build and smoke regression.

Candidate product code is executed only under `pull_request`/push/manual execution, never `pull_request_target`.

## CS018 qualifying campaign

`.github/workflows/cs018-validation.yml` is manual-only and is the exact execution workflow named by the CS018 validation plan.

The plan is committed before the qualifying execution.

`audit/CURRENT_CHANGESET_VALIDATION.json` intentionally remains bound to accepted CS000I at the plan/source commit because CS018 has no result yet. After a qualifying run is bound into `VALIDATION_RESULT.json`, the descriptor may be repointed in the result-closure commit.

## Ledger transition

At the CS018 plan/source candidate:

- `current_stage`: `EV-00` -> `EV-01`;
- EV-00 remains accepted with all accepted bindings unchanged;
- EV-01: `not_started` -> `in_progress`;
- EVREQ-005 through EVREQ-008: `planned` -> `in_progress`;
- `release_authorized` remains `false`;
- EV-02 and later stages remain unchanged.

## Non-effects

CS018 does not modify:

- `src/`;
- `include/`;
- `tests/`;
- `apps/`;
- `modules/`;
- `cmake/`;
- `CMakeLists.txt`;
- `CMakePresets.json`;
- vcpkg manifests;
- release authorization;
- the accepted EV-00 evidence binding.

The six CS010-CS015 historical workflow files remain byte-identical to protected base `e9e095e61d4de2995db704c51e9308850e1c929d`.
