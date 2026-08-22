# CS017 R17 local D-Lab runbook

R17 is prospective. R9-R16 remain failed/nonqualifying and must not be reused as qualifying evidence.

## Preconditions

Use a physical x64 Windows host, PowerShell 7+, a writable clone outside `Program Files`, full repository history, and an x64 Visual Studio Developer PowerShell environment. `VCToolsInstallDir`, `LIB`, `INCLUDE` and `WindowsSdkDir` must be populated. CMake, Ninja, clang-cl, link.exe and rc.exe must resolve from the intended toolchain.

The protected source under test remains exactly `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

The canonical R17 plan commit is the first commit containing `audit/validation/CS017/VALIDATION_PLAN.json` on the R17 branch. Use a named local branch exactly at that commit. Detached HEAD is rejected.

## R16 boundary

R16 static validation passed, but its physical Preflight failed before runner delegation because `Import-CtestParser` defined `Parse-CtestInventory` only in the importer's local function scope. No R16 EV-00 run ID or qualifying evidence package exists, and Qualify was not executed. Do not rerun R16.

R17 preserves the R16 runner/verifier runtime bytes and promotes the extracted parser definition explicitly into wrapper script scope. Static validation executes that import and invokes the parser against the normative CS015 Windows CTest reference before physical Preflight is authorized.

## Fetch and bind the frozen plan

```powershell
git fetch origin agent/cs017-ev00-baseline-certification-r17
$remote = 'origin/agent/cs017-ev00-baseline-certification-r17'
$planCommit = (git log -n 1 --format=%H $remote -- audit/validation/CS017/VALIDATION_PLAN.json).Trim()
$planCommit

git switch -c cs017-r17-local $planCommit
git rev-parse HEAD
git branch --show-current
git status --porcelain
```

The branch must be `cs017-r17-local`, HEAD must equal the frozen R17 plan commit, and status output must be empty.

## Validation model

R17 preserves R16 runtime behavior exactly:

1. primary `build` with `NEOENG_DCORE_BUILD_RESEARCH_TOOLS=OFF`, exact CS015 Windows clang-cl 54-test supported `dcore` inventory, zero failures;
2. sibling `research-build` with `NEOENG_DCORE_BUILD_RESEARCH_TOOLS=ON`, explicit targets `neoeng_dcore_preclosure` and `neoeng_temporal_closure_tests`, and exactly the three replay/history/temporal-closure tests.

The independent runtime verifier is byte-for-byte identical to the R16 verifier adapter, whose self-test passed on R16 static validation.

## Preflight

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r17.ps1 `
  -Mode Preflight `
  -ControlRepo $repo
```

R17 Preflight validates named-plan binding, clean repository, complete MSVC environment, real CMake+Ninja+clang-cl compile/link smoke, CTest parser compatibility, dual-surface contract, and executes `verify_ev00_dlab_evidence.py --self-test` before delegating to the preserved runner.

Do not run Qualify unless this Preflight passes and both frozen static PR controls are green on the exact plan commit.

## Qualify

Exactly one physical Qualify attempt is allowed per frozen R17 plan:

```powershell
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r17.ps1 `
  -Mode Qualify `
  -ControlRepo $repo
```

If Qualify returns FAILED, do not rerun under the same plan. Preserve the complete workspace and repository evidence package.

If Qualify returns PASSED, do not infer acceptance and do not immediately commit/push. First run every frozen independent physical-evidence check. Historical Assurance CS001-CS015 is a separate governed closure based on the frozen provenance plan.

## Acceptance boundary

A local terminal `PASSED` is bounded laboratory evidence only. CS017/EV-00 remain unaccepted until the exact evidence package is committed and bound, independent verification passes, Historical Assurance passes its frozen verifier, all required validation-plan tests pass, and the actual Trusted ChangeSet validation gate succeeds.
