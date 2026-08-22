# CS017 R19 local D-Lab runbook

R19 is prospective. R9-R18 remain failed/nonqualifying and must not be reused as qualifying evidence.

## Preconditions

Use a fresh writable physical x64 Windows clone, PowerShell 7+, full repository history, and an x64 Visual Studio Developer PowerShell environment. `VCToolsInstallDir`, `LIB`, `INCLUDE` and `WindowsSdkDir` must be populated. CMake, Ninja, clang-cl, link.exe and rc.exe must resolve from the intended toolchain.

The protected source under test remains exactly `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

The canonical R19 plan commit is the first commit containing `audit/validation/CS017/VALIDATION_PLAN.json`. Use a named local branch exactly at that commit. Detached HEAD is rejected.

## Fetch and bind the frozen plan

```powershell
git fetch origin agent/cs017-ev00-baseline-certification-r19
$remote = 'origin/agent/cs017-ev00-baseline-certification-r19'
$planCommit = (git log -n 1 --format=%H $remote -- audit/validation/CS017/VALIDATION_PLAN.json).Trim()
$planCommit

git switch -c cs017-r19-local $planCommit
git rev-parse HEAD
git branch --show-current
git status --porcelain --untracked-files=all
```

The branch must be `cs017-r19-local`, HEAD must equal the frozen R19 plan commit, and status output must be empty.

## R19 validation model

The physical adapter is the R18 no-side-effect model: preserve exact R17 wrapper bytes, set `PYTHONDONTWRITEBYTECODE=1`, require a clean tree before delegation, invoke the preserved R17 wrapper, and require the tree to remain clean after delegation.

Static validation additionally runs every authorizer/verifier Python control with `-B` and requires the complete static checkout to remain clean before physical Preflight can be authorized.

The preserved runtime remains:

1. Build A research-OFF, exact normative CS015 Windows clang-cl 54-test inventory, zero failures;
2. Build B isolated research-ON, exact three replay/history/temporal-closure tests, zero failures;
3. independent runtime verifier byte-identical to R16-R18.

## Preflight

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r19.ps1 `
  -Mode Preflight `
  -ControlRepo $repo
```

Do not run Qualify unless this Preflight passes and both frozen static PR controls are green on the exact plan commit.

## Qualify

Exactly one physical Qualify attempt is allowed per frozen R19 plan:

```powershell
pwsh -NoProfile `
  -File .\scripts\dlab\ev00\invoke_ev00_r19.ps1 `
  -Mode Qualify `
  -ControlRepo $repo
```

If Qualify returns FAILED, do not rerun under the same plan. If it returns PASSED, do not infer acceptance and do not immediately commit/push. First run every frozen independent physical-evidence check and complete Historical Assurance CS001-CS015.

## Acceptance boundary

A local terminal `PASSED` is bounded laboratory evidence only. CS017/EV-00 remain unaccepted until exact committed evidence, independent verification, Historical Assurance, every required validation-plan test and the actual Trusted ChangeSet validation gate pass.
