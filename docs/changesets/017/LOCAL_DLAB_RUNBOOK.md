# CS017 R13 — Physical Windows EV-00 runbook

R13 may be executed only after the PR static workflow and generic ChangeSet diagnostic are green on the exact frozen plan commit.

Use a writable clone outside `Program Files` and a real x64 Visual Studio Developer PowerShell. `VCToolsInstallDir`, `LIB`, `INCLUDE` and `WindowsSdkDir` must be populated; `LIB` must resolve `msvcrtd.lib` and `oldnames.lib`. Ensure Visual Studio CMake/Ninja and LLVM clang-cl resolve before execution.

## Fetch and bind

```powershell
git fetch origin agent/cs017-ev00-baseline-certification-r13
$remote = 'origin/agent/cs017-ev00-baseline-certification-r13'
$planCommit = (git log -n 1 --format=%H $remote -- audit/validation/CS017/VALIDATION_PLAN.json).Trim()
git switch -c cs017-r13-local $planCommit
git rev-parse HEAD
git branch --show-current
git status --porcelain
```

The branch must be named, HEAD must equal the derived plan commit, and status must be empty.

## Preflight

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r13.ps1 `
  -Mode Preflight `
  -ControlRepo $repo
```

R13 Preflight validates plan binding, clean named branch, MSVC libraries, a real CMake+Ninja+clang-cl compile/link smoke test, the immutable product commit, physical-host detection and presence of the normative CS015 Windows reference.

## Qualify

Only after Preflight succeeds:

```powershell
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r13.ps1 `
  -Mode Qualify `
  -ControlRepo $repo
```

The CTest historical comparison remains strict against `docs/changesets/015/evidence/github-actions-run-30375982639/raw/windows-clang-cl-ctest.txt`: exact total, exact test-name inventory and zero failures.

If Qualify fails, do not rerun. Preserve the new workspace/evidence first.

Preserve prior workspaces: R9 `ev00-20260822T074919Z-47b2284e`, R10 `ev00-20260822T080349Z-7b051c0f`, R11 `ev00-20260822T093201Z-4a33e537`. R12 had no physical run.
