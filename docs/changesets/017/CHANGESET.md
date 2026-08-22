# ChangeSet 017 R12 — EV-00 baseline laboratory certification

State: `IMPLEMENTED CANDIDATE / PLAN FREEZE PENDING / PHYSICAL QUALIFICATION NOT STARTED / EV-00 NOT ACCEPTED`

Base: `3ebb989c5aaca65501ddbc5e552e1f751079e310`  
Protected product baseline: `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## Objective

Execute EV-00 against the immutable v1.14.1 product baseline on a physical Windows host while preserving all failed R5–R11 history and correcting only the historical CTest comparison oracle revealed by R11.

## R11 triggering failure

R11 physical Attempt 1:

- plan/harness commit: `4ceac7e8ec213e91f1d79a669773e226e93f671c`;
- local run: `ev00-20260822T093201Z-4a33e537`;
- terminal state: `FAILED`;
- local D-Core supported-surface result: 54 tests, 0 failed;
- historical comparator used: supplemental 89-test local CS015 build;
- classification: `FAILED_TERMINAL_HARNESS_HISTORICAL_COMPARISON_ORACLE_DEFECT`.

Machine-readable preservation:

`docs/changesets/017/R11_PHYSICAL_ATTEMPT1_FAILURE.json`

R11 is never rerun or reclassified as qualifying evidence.

## Prospective R12 correction

The R11 runner is preserved byte-for-byte as:

`scripts/dlab/ev00/run_ev00_dlab_windows_r11.ps1`

Expected R11 runner Git blob:

`00cc326fa1c8cd40402c525c877a1af21969ba42`

R12 changes only the historical CTest reference used by the canonical runner from the supplemental local 89-test file to the immutable CS015 acceptance package Windows clang-cl supported-surface file:

`docs/changesets/015/evidence/github-actions-run-30375982639/raw/windows-clang-cl-ctest.txt`

That normative file records 54/54 and exact test names. R12 still requires exact inventory equality plus zero local and historical failures.

The R11 evidence verifier is preserved byte-for-byte as:

`scripts/dlab/ev00/verify_ev00_dlab_evidence_r11.py`

Expected blob:

`b8a87f899350215a80d4eb7dd1dc6cc7e07823f7`

The R12 verifier delegates to that preserved implementation while overriding only its historical CTest constant to the normative immutable CS015 Windows reference.

R12 also retains the R11 named-branch, clean-tree and MSVC compile/link Preflight protections.

## Non-effects

- no `src/**`, `include/**`, product `tests/**`, CMake or build-definition modification;
- no runtime/ABI change;
- no public-claim expansion;
- no release authorization;
- no historical failure rewrite;
- EV-00 remains unaccepted until all frozen required tests pass and a formal result is bound;
- EV-01 remains not started.
