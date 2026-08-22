# CS017 R11 — Physical Windows EV-00 runbook

R11 may be executed only after the PR static workflow and generic ChangeSet diagnostic are green.

## 1. Start from a real Visual Studio x64 developer environment

Do not reconstruct only `PATH`. The shell must have the Visual C++ environment, including `VCToolsInstallDir`, `LIB`, `INCLUDE` and `WindowsSdkDir`.

Recommended: open **Developer PowerShell for Visual Studio** / **x64 Native Tools** and then enter the repository.

Before R11:

```powershell
$env:VCToolsInstallDir
$env:WindowsSdkDir
$env:LIB
$env:INCLUDE
```

All must be non-empty. `LIB` must include an x64 MSVC path similar to `...\VC\Tools\MSVC\<version>\lib\x64`.

## 2. Fetch and bind to the exact R11 plan commit

```powershell
git fetch origin agent/cs017-ev00-baseline-certification-r11
$remote = 'origin/agent/cs017-ev00-baseline-certification-r11'
$planCommit = (git log -n 1 --format=%H $remote -- audit/validation/CS017/VALIDATION_PLAN.json).Trim()
git switch -c cs017-r11-local $planCommit
git rev-parse HEAD
git branch --show-current
git status --porcelain
```

The branch output must be non-empty and `git status --porcelain` must be empty.

## 3. Preflight

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r11.ps1 `
  -Mode Preflight `
  -ControlRepo $repo
```

R11 Preflight first proves the MSVC environment and runs a real temporary CMake/Ninja/clang-cl compile-link smoke test. Only then does it delegate to the preserved EV-00 runner Preflight.

## 4. Qualify

Only after Preflight succeeds:

```powershell
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r11.ps1 `
  -Mode Qualify `
  -ControlRepo $repo
```

If Qualify fails, do not rerun. Preserve the new `ev00-*` workspace and evidence package before any prospective correction.

R9 workspace `ev00-20260822T074919Z-47b2284e` and R10 workspace `ev00-20260822T080349Z-7b051c0f` remain preservation-required.
