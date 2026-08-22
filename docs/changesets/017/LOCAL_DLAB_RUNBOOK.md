# CS017 R15 local D-Lab runbook

R15 is prospective. R9-R14 remain failed/nonqualifying or independently rejected and must not be reused as qualifying evidence.

## Preconditions

Use a physical x64 Windows host, PowerShell 7+, a writable clone outside `Program Files`, full repository history, and an x64 Visual Studio Developer PowerShell environment. `VCToolsInstallDir`, `LIB`, `INCLUDE` and `WindowsSdkDir` must be populated. CMake, Ninja, clang-cl, link.exe and rc.exe must resolve from the intended toolchain.

The protected source under test remains exactly `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

The canonical R15 plan commit is the first commit containing `audit/validation/CS017/VALIDATION_PLAN.json`. Use a named local branch exactly at that commit. Detached HEAD is rejected.

## Fetch and bind the frozen plan

```powershell
git fetch origin agent/cs017-ev00-baseline-certification-r15
$remote = 'origin/agent/cs017-ev00-baseline-certification-r15'
$planCommit = (git log -n 1 --format=%H $remote -- audit/validation/CS017/VALIDATION_PLAN.json).Trim()
$planCommit

git switch -c cs017-r15-local $planCommit
git rev-parse HEAD
git branch --show-current
git status --porcelain
```

The branch must be `cs017-r15-local`, HEAD must equal the frozen R15 plan commit, and status output must be empty.

## R15 validation model

R15 intentionally uses two separate build trees from the same immutable source worktree:

1. primary `build` with `NEOENG_DCORE_BUILD_RESEARCH_TOOLS=OFF`, which must reproduce exactly the immutable CS015 Windows clang-cl supported `dcore` inventory of 54 tests with zero failures;
2. sibling `research-build` with `NEOENG_DCORE_BUILD_RESEARCH_TOOLS=ON`, used only to build `neoeng_dcore_preclosure` and `neoeng_temporal_closure_tests` and execute exactly three replay/history/temporal-closure tests.

The second build does not alter or enlarge the normative supported-surface oracle. The independent verifier checks both surfaces, their CMake arguments, isolated build directories, explicit target allowlist, exact anchored CTest selector and exact inventories.

## Preflight

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r15.ps1 `
  -Mode Preflight `
  -ControlRepo $repo
```

R15 Preflight validates the named-plan binding, clean writable control repository, physical host, complete MSVC environment, real CMake+Ninja+clang-cl compile/link smoke, CTest parser compatibility, the immutable CS015 54-test oracle, baseline CMake research gating, and the canonical runner/verifier dual-surface contract.

Do not run Qualify unless this Preflight passes and both frozen static PR controls are green on the exact plan commit.

## Qualify

Exactly one physical Qualify attempt is allowed per frozen R15 plan:

```powershell
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r15.ps1 `
  -Mode Qualify `
  -ControlRepo $repo
```

If Qualify returns FAILED, do not rerun it under the same plan. Preserve the complete workspace and repository evidence package, then classify prospectively.

If Qualify returns PASSED, do not infer acceptance and do not immediately commit/push. First run the frozen independent verifier against every physical evidence check. Historical Assurance CS001-CS015 is a separate governed closure and must be materialized from the frozen provenance plan without rewriting historical outcomes.

## Preservation and acceptance boundary

Never delete or overwrite R9-R14 workspaces/evidence as part of R15. R15 output cannot retroactively change any predecessor disposition.

A local terminal `PASSED` is only bounded laboratory evidence. CS017/EV-00 remain unaccepted until the exact evidence package is committed and bound, independent verification passes, the Historical Assurance result passes its frozen verifier, all required validation-plan tests pass, and the actual `Trusted ChangeSet validation gate` succeeds.
