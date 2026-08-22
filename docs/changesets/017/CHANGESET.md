# ChangeSet 017 R11 — EV-00 baseline laboratory certification

State: `PLAN_FREEZE_PENDING / PHYSICAL_QUALIFICATION_NOT_STARTED / EV-00_NOT_ACCEPTED`

Base: `3ebb989c5aaca65501ddbc5e552e1f751079e310`

Protected product source: `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## Objective

Execute a fresh physical Windows EV-00 baseline certification after preserving the non-qualifying/failed R5-R10 history. R11 is prospective and does not rerun or repair R9/R10 in place.

## Triggering R10 failure

R10 physical Attempt 1 used plan `9c88def414f6313c5e4235cb063f4aa66dc8225a`, local run `ev00-20260822T080349Z-7b051c0f`, and terminated `FAILED` at `cmake-configure` while CMake tested compiler ABI/linking. `lld-link` could not locate `msvcrtd.lib` and `oldnames.lib`. Post-run inspection showed `VCToolsInstallDir` empty and `LIB` without an x64 MSVC library directory. No D-Core build or product tests started.

Machine-readable record: `docs/changesets/017/R10_PHYSICAL_ATTEMPT1_FAILURE.json`.

## R11 correction

R11 preserves the runner blob `00cc326fa1c8cd40402c525c877a1af21969ba42` and evidence verifier blob `b8a87f899350215a80d4eb7dd1dc6cc7e07823f7`.

New canonical entry point: `scripts/dlab/ev00/invoke_ev00_r11.ps1`.

Before delegating to the preserved runner it requires exact plan-commit HEAD, named local branch, clean tree, non-empty `VCToolsInstallDir`/`LIB`/`INCLUDE`/`WindowsSdkDir`, an x64 MSVC library directory in `LIB`, reachability of `msvcrtd.lib` and `oldnames.lib`, and a real temporary CMake+Ninja+`clang-cl` compile/link smoke test that produces an executable.

Any of these failures occurs in Preflight before a qualifying workspace is created by the preserved runner.

## Non-effects

- runtime/ABI/product tests/build definitions: unchanged;
- protected product source: immutable;
- EV-00 acceptance: none;
- CS017 acceptance: none;
- EV-01: not started;
- release: not authorized;
- historical evidence rewrite: none.
