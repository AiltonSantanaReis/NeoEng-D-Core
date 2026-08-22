# CS017 R22 local D-Lab runbook

R22 is a fresh-from-main successor to failed R21. The only authorized physical entrypoint is `scripts/dlab/ev00/invoke_ev00_r22.ps1` at the exact commit that first introduces `audit/validation/CS017/VALIDATION_PLAN.json`.

## Preconditions

Use a fresh writable Windows clone in x64 Visual Studio Developer PowerShell. The clone must be on a named local branch at the exact R22 plan commit and `git status --porcelain --untracked-files=all` must be empty. The protected product commit `e3fff973554a2e56b8bd7afdc1132f75f3ec337c` must exist locally. `VCToolsInstallDir`, `LIB`, `INCLUDE` and `WindowsSdkDir` must be populated; x64 `msvcrtd.lib` and `oldnames.lib` must be reachable through `LIB`; `cmake`, `ninja` and `clang-cl` must resolve.

## Preflight

Run only:

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile -File .\scripts\dlab\ev00\invoke_ev00_r22.ps1 -Mode Preflight -ControlRepo $repo
$preflightExit = $LASTEXITCODE
$preflightExit
git status --porcelain --untracked-files=all
```

Stop after Preflight and review the complete output. `Qualify` is not authorized merely because Preflight returns zero.

## Qualify hold point

Exactly one Qualify may be authorized only after the frozen R22 Preflight is reviewed and accepted. Never execute a second Qualify under the same plan, regardless of outcome.

## Acceptance boundary

A local runner `PASSED` is not EV-00 acceptance. Acceptance still requires the independent verifier over committed evidence, exact plan/harness binding, Historical Assurance result, frozen ChangeSet validation result and the repository's required `Trusted ChangeSet validation gate`.
