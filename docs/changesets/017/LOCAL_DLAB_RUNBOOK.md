# CS017 R9 / EV-00 — Local Windows D-Lab Runbook

Status: **HARNESS PUBLISHED — QUALIFY FORBIDDEN UNTIL CANONICAL PLAN FREEZE**

## Purpose

EV-00 certifies the immutable D-Core `v1.14.1` baseline on the maintainer's physical Windows PC. GitHub-hosted runners validate the control plane, harness syntax and committed evidence; they do not replace the physical qualifying environment.

Protected product under test:

- release: `v1.14.1`;
- commit: `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

R9 control base: `main@3ebb989c5aaca65501ddbc5e552e1f751079e310`.

## Harness identity and relocation rule

The R9 runner and evidence verifier preserve the corrected R7 bytes, but are published under the current authorized namespace:

- `scripts/dlab/ev00/run_ev00_dlab_windows.ps1`;
- `scripts/dlab/ev00/verify_ev00_dlab_evidence.py`.

Because the PowerShell runner was originally authored one directory level shallower, R9 commands MUST pass `-ControlRepo` explicitly. Do not invoke the relocated runner without that parameter.

## Required local environment

Use a physical Windows host with:

- PowerShell 7+ (`pwsh`);
- Git;
- Python;
- CMake and CTest;
- Ninja;
- `clang-cl`;
- MSVC linker environment (`link.exe`);
- Windows SDK resource compiler (`rc.exe`);
- full repository history containing the protected `v1.14.1` commit.

For `clang-cl`, `link.exe` and `rc.exe` together, start from an x64 Visual Studio / Build Tools developer environment and launch `pwsh` there.

## Local preflight — non-qualifying

Run only after checking out the exact R9 branch/head requested by the frozen plan:

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile -File .\scripts\dlab\ev00\run_ev00_dlab_windows.ps1 -Mode Preflight -ControlRepo $repo
```

Preflight does not qualify the product. It checks host/tool prerequisites, repository cleanliness, protected historical source availability and physical-host heuristics.

A failed preflight is `BLOCKED`; do not weaken checks to obtain a pass.

## Qualification hold point

**Do not run `Qualify` yet.** Qualification is authorized only after all of the following are true:

1. the complete R9 harness/workflow/verifier surface is committed;
2. `audit/validation/CS017/VALIDATION_PLAN.json` exists and is frozen in a dedicated plan commit;
3. the branch HEAD still equals that plan commit when the local run begins;
4. the working tree is clean;
5. the exact plan/test inventory has not been changed after observing any result.

Then execute exactly:

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile -File .\scripts\dlab\ev00\run_ev00_dlab_windows.ps1 -Mode Qualify -ControlRepo $repo
```

The runner creates a unique workspace under:

```text
%USERPROFILE%\NeoEng-DLab\EV-00\runs\<run-id>\
```

with independent `source/`, `build/`, `install/`, `deps/` and `evidence/` directories. The product source is a detached worktree at exactly `e3fff973...`; prior build output is not reused.

## Evidence preservation

Every relevant command records executable/arguments, working directory, UTC timing, exit code, stdout, stderr and PASS/FAIL classification. The terminal evidence package receives a SHA-256 manifest and is copied to:

```text
docs/changesets/017/evidence/local-windows/<run-id>/
```

A failed qualification package is still evidence. Preserve and commit it before any prospective retry; never delete or reclassify it.

For a successful terminal package, a later authorized evidence commit must add a pointer:

```text
docs/changesets/017/ACTIVE_LOCAL_RUN.json
```

and the historical assurance record:

```text
docs/changesets/017/HISTORICAL_ASSURANCE_RESULT.json
```

The GitHub workflow `.github/workflows/ev00-dlab.yml` independently verifies the committed package, including the plan-commit/harness-SHA binding.

## What is not EV-00 acceptance

None of the following alone accepts EV-00:

- preparation preflight PASS;
- local preflight PASS;
- compilation success;
- a local terminal `PASSED` JSON without independent verification;
- CI green;
- historical R1-R8 evidence;
- `NOT_TESTED`, `PARTIAL`, `BLOCKED`, missing evidence or a skipped required step.

EV-00 acceptance requires the canonical frozen CS017 validation plan, physical terminal evidence, independent verification, exact GitHub run binding and the later explicit stage-acceptance closure. Release remains unauthorized.
