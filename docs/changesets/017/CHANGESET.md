# CS017 R14 — EV-00 baseline laboratory certification

## Status

Prospective revision. R9-R13 remain failed/nonqualifying historical evidence and are not reused.

## Objective

Run a new physical Windows EV-00 qualification of immutable D-Core v1.14.1 after correcting the R13 CTest-summary parser compatibility defect.

## R13 disposition

R13 physical run `ev00-20260822T103002Z-409935dc` is terminal `FAILED`. All 17 recorded external commands were `PASS`; failure occurred only when the harness attempted to parse the immutable CS015 Windows clang-cl historical reference. The R13 parser accepted only the modern explicit-failure summary, while the normative CS015 file uses `100% tests passed out of 54`.

R13 classification: `FAILED_TERMINAL_HARNESS_CTEST_SUMMARY_PARSER_COMPATIBILITY_DEFECT`.

## R14 correction

R14 preserves the exact R13 runner as `scripts/dlab/ev00/run_ev00_dlab_windows_r13.ps1` and changes only `Parse-CtestInventory` in the canonical runner.

Accepted parser forms:

1. modern explicit-failure form: `<percent>% tests passed, <failed> tests failed out of <total>`;
2. historical all-pass form: `100% tests passed out of <total>`, interpreted strictly as `failed=0`.

A historical summary below 100% without an explicit failure count remains rejected.

The independent verifier receives the same compatibility rule. Exact total, exact sorted test-name inventory, and zero failures remain mandatory.

The R14 physical Preflight extracts and executes the canonical runner's actual `Parse-CtestInventory` function against the normative CS015 Windows file and requires 100%, zero failed, total 54 and 54 unique test names before Qualify can start.

## Non-effects

No runtime, ABI, product test, CMake/build definition, product claim, release authorization or EV-01 effect is introduced by this revision. Passing R14 physical evidence alone does not accept EV-00 or CS017.
