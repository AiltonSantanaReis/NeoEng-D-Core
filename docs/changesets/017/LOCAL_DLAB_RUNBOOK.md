# CS017 R12 — Physical Windows EV-00 runbook

R12 may be executed only after the PR static workflow and generic ChangeSet diagnostic are green on the exact frozen plan commit.

## 1. Use a real x64 Visual Studio Developer PowerShell

Do not reconstruct only `PATH`. The shell must expose the Visual C++ developer environment, including non-empty `VCToolsInstallDir`, `LIB`, `INCLUDE` and `WindowsSdkDir`. `LIB` must include the x64 MSVC library directory and resolve `msvcrtd.lib` and `oldnames.lib`.

The R12 wrapper performs a real temporary CMake + Ninja + clang-cl compile/link smoke test before delegating to the EV-00 runner.

## 2. Use a writable clone

Do not use a clone under `Program Files`. Recommended control repository location:

`$env:USERPROFILE\src\NeoEng-D-Core-R12`

## 3. Fetch and bind to the exact R12 plan commit

```powershell
git fetch origin agent/cs017-ev00-baseline-certification-r12
$remote = 'origin/agent/cs017-ev00-baseline-certification-r12'
$planCommit = (git log -n 1 --format=%H $remote -- audit/validation/CS017/VALIDATION_PLAN.json).Trim()
git switch -c cs017-r12-local $planCommit
git rev-parse HEAD
git branch --show-current
git status --porcelain
```

The branch output must be non-empty and `git status --porcelain` must be empty.

## 4. Preflight

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r12.ps1 `
  -Mode Preflight `
  -ControlRepo $repo
```

R12 Preflight checks the plan binding, named branch, clean tree, MSVC environment, compile/link smoke test, normative CS015 Windows reference presence, physical host, required tools and immutable product commit.

## 5. Qualify

Only after Preflight succeeds:

```powershell
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r12.ps1 `
  -Mode Qualify `
  -ControlRepo $repo
```

R12 compares the newly generated `ctest -L dcore` inventory strictly against:

`docs/changesets/015/evidence/github-actions-run-30375982639/raw/windows-clang-cl-ctest.txt`

The comparison still requires exact test-name inventory equality and zero failures. The supplemental 89-test local corroboration file is not an acceptance oracle.

If Qualify fails, do not rerun. Preserve the new `ev00-*` workspace and repository evidence package before any prospective correction.

R9 workspace `ev00-20260822T074919Z-47b2284e`, R10 workspace `ev00-20260822T080349Z-7b051c0f`, and R11 workspace `ev00-20260822T093201Z-4a33e537` remain preservation-required.
