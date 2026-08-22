# CS017 R20 Local D-Lab Runbook

## Frozen-boundary rule

No physical command is authorized until the R20 validation plan is frozen and both static workflows pass on that exact SHA. The commit that first introduces `audit/validation/CS017/VALIDATION_PLAN.json` is the only eligible R20 `harness_sha`.

## Physical host prerequisites

Use a fresh writable Windows clone on a named local branch, PowerShell 7+, x64 Visual Studio Developer environment, CMake, Ninja, clang-cl, link.exe and rc.exe. The immutable source-under-test commit is `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

## Preflight

After static authorization, execute only:

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile -File .\scripts\dlab\ev00\invoke_ev00_r20.ps1 -Mode Preflight -ControlRepo $repo
```

A valid Preflight must report the exact frozen R20 head, clean control tree, protected product commit present, physical host and expected toolchain. The control tree must remain clean after return.

## Qualify hold point

`Qualify` is forbidden until the Preflight output is reviewed and exactly one attempt is explicitly authorized. Never execute a second Qualify under the same frozen R20 plan, regardless of outcome.

When authorized, execute only:

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile -File .\scripts\dlab\ev00\invoke_ev00_r20.ps1 -Mode Qualify -ControlRepo $repo
$qualifyExit = $LASTEXITCODE
"QUALIFY EXIT CODE: $qualifyExit"
git status --porcelain --untracked-files=all
```

Preserve the actual run id printed by the runner. A local terminal `PASSED` is not acceptance.

## R20 verifier contract

Independent verification must run with Python bytecode disabled. The canonical verifier must:

- require Build A research-OFF/full-toolset-OFF and exact CS015 normative 54/0 inventory;
- allow and require `neoeng_temporal_closure_tests` as part of that normative inventory;
- reject `neoeng_dcore_replay_smoke` or `neoeng_dcore_history_smoke` in Build A;
- require Build B research-ON/full-toolset-OFF in an isolated `research-build` directory;
- require exact Build-B tests `neoeng_dcore_replay_smoke`, `neoeng_dcore_history_smoke`, `neoeng_temporal_closure_tests`, exactly 3/0;
- preserve all source/environment/build/determinism/Host SDK/state/support/release/manifest/historical/terminal checks;
- preserve Historical Assurance CS001-CS015 requirements.

## Acceptance boundary

Even after local evidence and independent verification pass, CS017/EV-00 remain unaccepted until the physical evidence package is committed with exact plan binding, `ACTIVE_LOCAL_RUN.json` and governed `HISTORICAL_ASSURANCE_RESULT.json` are present, every frozen required test passes, and the actual `Trusted ChangeSet validation gate` succeeds. Release remains unauthorized until then.
