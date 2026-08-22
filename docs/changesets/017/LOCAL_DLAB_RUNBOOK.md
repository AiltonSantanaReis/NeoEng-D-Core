# CS017 R18 local D-Lab runbook

R18 is prospective. R9-R17 remain failed/nonqualifying and must not be reused as qualifying evidence.

## Preconditions

Use a fresh writable physical x64 Windows clone, PowerShell 7+, full repository history, and an x64 Visual Studio Developer PowerShell environment. `VCToolsInstallDir`, `LIB`, `INCLUDE` and `WindowsSdkDir` must be populated. CMake, Ninja, clang-cl, link.exe and rc.exe must resolve from the intended toolchain.

The protected source under test remains exactly `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

The canonical R18 plan commit is the first commit containing `audit/validation/CS017/VALIDATION_PLAN.json`. Use a named local branch exactly at that commit. Detached HEAD is rejected.

## Fetch and bind the frozen plan

```powershell
git fetch origin agent/cs017-ev00-baseline-certification-r18
$remote = 'origin/agent/cs017-ev00-baseline-certification-r18'
$planCommit = (git log -n 1 --format=%H $remote -- audit/validation/CS017/VALIDATION_PLAN.json).Trim()
$planCommit

git switch -c cs017-r18-local $planCommit
git rev-parse HEAD
git branch --show-current
git status --porcelain --untracked-files=all
```

The branch must be `cs017-r18-local`, HEAD must equal the frozen R18 plan commit, and status output must be empty.

## R18 wrapper model

R18 is a thin no-side-effect adapter over the byte-preserved R17 wrapper. It verifies the preserved R17 wrapper hash, sets `PYTHONDONTWRITEBYTECODE=1`, requires a clean control tree immediately before delegation, invokes the R17 wrapper, and requires the tree to remain clean after delegation. Any wrapper/runner side effect is fail-closed.

R17 behavior remains preserved underneath:

1. real MSVC environment and CMake+Ninja+clang-cl link smoke;
2. script-scope CTest parser import and normative CS015 parser check;
3. strict dual-surface contract;
4. independent verifier self-test;
5. preserved runner Preflight/Qualify behavior.

Build A remains research-OFF and must exactly match the normative CS015 Windows clang-cl 54-test supported inventory with zero failures. Build B remains isolated research-ON and must execute exactly `neoeng_dcore_replay_smoke`, `neoeng_dcore_history_smoke`, and `neoeng_temporal_closure_tests`, with zero failures.

## Preflight

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r18.ps1 `
  -Mode Preflight `
  -ControlRepo $repo
```

Do not run Qualify unless this Preflight passes and both frozen static PR controls are green on the exact plan commit.

## Qualify

Exactly one physical Qualify attempt is allowed per frozen R18 plan:

```powershell
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r18.ps1 `
  -Mode Qualify `
  -ControlRepo $repo
```

If Qualify returns FAILED, do not rerun under the same plan. Preserve all evidence. If Qualify returns PASSED, do not infer acceptance and do not immediately commit/push. First run every frozen independent physical-evidence check and complete Historical Assurance CS001-CS015 from the frozen provenance plan.

## Acceptance boundary

A local terminal `PASSED` is bounded laboratory evidence only. CS017/EV-00 remain unaccepted until the exact evidence package is committed and bound, independent verification passes, Historical Assurance passes its frozen verifier, all required validation-plan tests pass, and the actual Trusted ChangeSet validation gate succeeds.
