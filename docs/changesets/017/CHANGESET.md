# CS017 R15 — EV-00 dual-surface baseline laboratory certification

## Status

Prospective R15 campaign. R9-R14 remain preserved failed/nonqualifying history and are never reused as qualifying output.

R15 does not modify D-Core product/runtime/test/build-definition sources. The immutable source under test remains `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

## R14 finding carried forward

R14 physical run `ev00-20260822T135050Z-cdc30b2a` reached local terminal `PASSED`, with 54/54 exact normative supported-surface inventory and all recorded external commands passing. The independent verifier then rejected because it required `neoeng_dcore_replay_smoke` and `neoeng_dcore_history_smoke` inside the same 54-test research-OFF surface. Baseline CMake registers those tests only when research tools are enabled. R14 is therefore nonqualifying and not reusable.

Machine record: `R14_PHYSICAL_ATTEMPT1_FAILURE.json`.

## R15 correction

R15 separates two configurations without weakening either requirement:

1. **Supported surface — primary build**
   - `NEOENG_DCORE_BUILD_FULL_TOOLSET=OFF`
   - `NEOENG_DCORE_BUILD_RESEARCH_TOOLS=OFF`
   - `ctest -L dcore`
   - exact equality to the immutable CS015 Windows clang-cl 54-test inventory;
   - zero failures.

2. **Replay/history surface — isolated research build**
   - fresh sibling build directory `research-build` from the same immutable source worktree;
   - `NEOENG_DCORE_BUILD_FULL_TOOLSET=OFF`;
   - `NEOENG_DCORE_BUILD_RESEARCH_TOOLS=ON`;
   - explicit build targets `neoeng_dcore_preclosure` and `neoeng_temporal_closure_tests`;
   - anchored CTest allowlist requiring exactly `neoeng_dcore_replay_smoke`, `neoeng_dcore_history_smoke`, and `neoeng_temporal_closure_tests`;
   - exactly 3 tests, zero failures, exact names.

The independent verifier checks both configurations, command arguments, build-directory isolation, exact inventories and evidence manifest.

## Historical assurance

`HISTORICAL_ASSURANCE_PROVENANCE_PLAN.json` freezes provenance inputs for CS001-CS015 before physical execution. It deliberately does not pre-assign risk, reproducibility, rerun necessity or final historical disposition. The post-physical assurance result must be produced append-only from these verified inputs and satisfy the frozen verifier.

## Acceptance boundary

A local terminal `PASSED` is not acceptance. CS017/EV-00 remain unaccepted until committed physical evidence, independent verification, historical assurance, all required frozen validation-plan tests and the actual `Trusted ChangeSet validation gate` succeed.

No release is authorized and EV-01 does not start from this ChangeSet unless EV-00 is separately accepted through its governed closure.
