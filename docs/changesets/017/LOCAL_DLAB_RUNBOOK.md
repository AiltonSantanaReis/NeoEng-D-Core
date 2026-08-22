# CS017 R10 / EV-00 — Physical Windows D-Lab Runbook

Status: **PLAN-FROZEN CAMPAIGN ONLY — DO NOT USE R9 DETACHED-HEAD COMMANDS**

## Protected target

- D-Core release: `v1.14.1`
- D-Core product commit: `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`
- ChangeSet: `CS017`
- Stage: `EV-00`

R10 preserves the R9 runner bytes but invokes it through `scripts/dlab/ev00/invoke_ev00_r10.ps1`. The wrapper rejects detached HEAD and verifies that the control repository HEAD equals the commit that first introduced the current canonical `audit/validation/CS017/VALIDATION_PLAN.json`.

## Required local environment

Physical Windows host with PowerShell 7+, Git, Python, CMake/CTest, Ninja, clang-cl, MSVC linker (`link.exe`) and Windows SDK resource compiler (`rc.exe`). Use an x64 Visual Studio / Build Tools developer environment.

## Obtain the exact R10 plan commit

From the existing NeoEng-D-Core clone:

```powershell
git fetch origin agent/cs017-ev00-baseline-certification-r10
$remote = 'origin/agent/cs017-ev00-baseline-certification-r10'
$planCommit = (git log -n 1 --format=%H $remote -- audit/validation/CS017/VALIDATION_PLAN.json).Trim()
$planCommit
```

The value must be a 40-character commit SHA. Then create a new **named local branch** at exactly that commit:

```powershell
git switch -c cs017-r10-local $planCommit
git rev-parse HEAD
git branch --show-current
git status --porcelain
```

Required state:

- `git rev-parse HEAD` equals `$planCommit`;
- `git branch --show-current` is non-empty;
- `git status --porcelain` is empty.

Do not use detached HEAD for R10. Do not delete or modify the preserved R9 workspace `C:\Users\atnco\NeoEng-DLab\EV-00\runs\ev00-20260822T074919Z-47b2284e`.

## Preflight — non-qualifying

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile -File .\scripts\dlab\ev00\invoke_ev00_r10.ps1 -Mode Preflight -ControlRepo $repo
```

If Preflight fails, do not run Qualify.

## Qualification — single prospective attempt

Only after Preflight passes:

```powershell
pwsh -NoProfile -File .\scripts\dlab\ev00\invoke_ev00_r10.ps1 -Mode Qualify -ControlRepo $repo
```

The runner creates a fresh unique workspace under `%USERPROFILE%\NeoEng-DLab\EV-00\runs\<run-id>` and a repository evidence package under `docs/changesets/017/evidence/local-windows/<run-id>`.

If Qualify fails, **do not rerun it**. Preserve the created workspace and any repository evidence package exactly as produced and report the run id/error for prospective classification.

If Qualify reports `Terminal state: PASSED`, do not edit the evidence package. Its bytes will be bound to the later GitHub validation run.

## What this does not authorize

This runbook does not authorize product fixes, runtime changes, release, EV-01, reclassification of R9, deletion of failed evidence or weakening/removal of any frozen test.
