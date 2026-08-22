# CS017 R21 Local D-Lab Runbook

R21 is frozen-plan governed. The commit that first introduces `audit/validation/CS017/VALIDATION_PLAN.json` is the only eligible R21 `harness_sha`.

No physical command is authorized until both static workflows pass on that exact SHA.

## Preflight

After explicit authorization, from a fresh writable Windows clone on a named local branch and x64 Visual Studio Developer environment:

```powershell
$repo = (Resolve-Path .).Path
pwsh -NoProfile -File .\scripts\dlab\ev00\invoke_ev00_r21.ps1 -Mode Preflight -ControlRepo $repo
```

The Preflight must preserve a clean control tree and report the exact frozen head, protected product commit, physical host and intended toolchain.

## Qualify

Qualify remains forbidden until the Preflight output is reviewed. If one attempt is later authorized, execute it exactly once; no same-plan second attempt is allowed regardless of result.

## R21 verifier contract

The canonical verifier is byte-identical to R20. It must continue to:

- accept only the exact normative supported 54/0 inventory with `neoeng_temporal_closure_tests` included;
- forbid replay/history from the supported surface;
- require exact isolated Build-B replay/history/temporal-closure 3/0;
- preserve all other EV-00 physical, manifest, historical-comparison, terminal and Historical Assurance checks.

R21 changes only the static CMake proof that independently validates the immutable baseline registration boundary.

A local PASSED result is not acceptance. Committed evidence binding, CS001-CS015 Historical Assurance, all frozen required tests and the actual Trusted ChangeSet validation gate remain mandatory.
