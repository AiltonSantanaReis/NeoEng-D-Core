# CS017 R16 local D-Lab runbook

R16 is prospective. R9-R15 remain failed/nonqualifying and must not be reused as qualifying evidence.

## Preconditions

Use a physical x64 Windows host, PowerShell 7+, a writable clone outside `Program Files`, full repository history, and an x64 Visual Studio Developer PowerShell environment. `VCToolsInstallDir`, `LIB`, `INCLUDE` and `WindowsSdkDir` must be populated. CMake, Ninja, clang-cl, link.exe and rc.exe must resolve from the intended toolchain.

The protected source under test remains exactly `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

The canonical R16 plan commit is the first commit containing `audit/validation/CS017/VALIDATION_PLAN.json`. Use a named local branch exactly at that commit. Detached HEAD is rejected.

## Fetch and bind the frozen plan

```powershell
git fetch origin agent/cs017-ev00-baseline-certification-r16
$remote = 'origin/agent/cs017-ev00-baseline-certification-r16'
$planCommit = (git log -n 1 --format=%H $remote -- audit/validation/CS017/VALIDATION_PLAN.json).Trim()
$planCommit

git switch -c cs017-r16-local $planCommit
git rev-parse HEAD
git branch --show-current
git status --porcelain
```

The branch must be `cs017-r16-local`, HEAD must equal the frozen R16 plan commit, and status output must be empty.

## Validation model

R16 preserves R15 runtime behavior exactly:

1. primary `build` with `NEOENG_DCORE_BUILD_RESEARCH_TOOLS=OFF`, exact CS015 Windows clang-cl 54-test supported `dcore` inventory, zero failures;
2. sibling `research-build` with `NEOENG_DCORE_BUILD_RESEARCH_TOOLS=ON`, explicit targets `neoeng_dcore_preclosure` and `neoeng_temporal_closure_tests`, and exactly the three replay/history/temporal-closure tests.

The independent runtime verifier semantics are delegated to the byte-preserved R15 verifier. R16 changes only the synthetic verifier self-test fixture so command records contain all fields required by the preserved verifier contract.

## Preflight

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r16.ps1 `
  -Mode Preflight `
  -ControlRepo $repo
```

R16 Preflight validates named-plan binding, clean repository, physical host, complete MSVC environment, real CMake+Ninja+clang-cl compile/link smoke, CTest parser compatibility, dual-surface contract, and executes `verify_ev00_dlab_evidence.py --self-test` before delegating to the unchanged runner.

Do not run Qualify unless this Preflight passes and both frozen static PR controls are green on the exact plan commit.

## Qualify

Exactly one physical Qualify attempt is allowed per frozen R16 plan:

```powershell
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r16.ps1 `
  -Mode Qualify `
  -ControlRepo $repo
```

If Qualify returns FAILED, do not rerun under the same plan. Preserve the complete workspace and repository evidence package.

If Qualify returns PASSED, do not infer acceptance and do not immediately commit/push. First run every frozen independent physical-evidence check. Historical Assurance CS001-CS015 is a separate governed closure based on the frozen provenance plan.

## Acceptance boundary

A local terminal `PASSED` is bounded laboratory evidence only. CS017/EV-00 remain unaccepted until the exact evidence package is committed and bound, independent verification passes, Historical Assurance passes its frozen verifier, all required validation-plan tests pass, and the actual Trusted ChangeSet validation gate succeeds.
