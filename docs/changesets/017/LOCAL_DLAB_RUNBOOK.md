# CS017 R14 local D-Lab runbook

R14 is prospective. R9-R13 remain failed/nonqualifying and must not be reused.

## Preconditions

Use a physical x64 Windows host, PowerShell 7+, a writable clone outside `Program Files`, full repository history, and an x64 Visual Studio Developer PowerShell environment. `VCToolsInstallDir`, `LIB`, `INCLUDE` and `WindowsSdkDir` must be populated. CMake, Ninja, clang-cl, link.exe and rc.exe must resolve from the intended toolchain.

The canonical plan commit is the first commit containing `audit/validation/CS017/VALIDATION_PLAN.json`. Use a named local branch exactly at that commit. Detached HEAD is rejected.

## Fetch and bind the frozen plan

```powershell
git fetch origin agent/cs017-ev00-baseline-certification-r14
$remote = 'origin/agent/cs017-ev00-baseline-certification-r14'
$planCommit = (git log -n 1 --format=%H $remote -- audit/validation/CS017/VALIDATION_PLAN.json).Trim()
$planCommit
git switch -c cs017-r14-local $planCommit
git rev-parse HEAD
git branch --show-current
git status --porcelain
```

The status output must be empty.

## Preflight

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile -File .\scripts\dlab\ev00\invoke_ev00_r14.ps1 -Mode Preflight -ControlRepo $repo
```

R14 Preflight validates the named-plan binding, clean writable control repository, physical host, complete MSVC environment, a real CMake+Ninja+clang-cl compile/link smoke, and the canonical runner's actual CTest parser against the immutable CS015 54-test Windows reference. It also verifies that an ambiguous historical summary below 100% without an explicit failure count is rejected.

Do not run Qualify unless Preflight passes and the frozen PR/static controls are green.

## Qualify

Exactly one physical Qualify attempt is allowed per frozen plan:

```powershell
pwsh -NoProfile -File .\scripts\dlab\ev00\invoke_ev00_r14.ps1 -Mode Qualify -ControlRepo $repo
```

If Qualify returns FAILED, do not rerun it under the same plan. Preserve the workspace and repository evidence package, then classify prospectively.

If Qualify returns PASSED, do not infer acceptance. Preserve the package and submit it for independent verifier and ChangeSet validation binding before any CS017/EV-00 acceptance decision.
