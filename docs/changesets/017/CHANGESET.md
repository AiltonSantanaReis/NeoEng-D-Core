# CS017 R21 — EV-00 auxiliary CMake oracle correction

## Objective

Preserve the immutable `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c` baseline, the physical runner contract, and the exact R20 independent verifier while correcting only the auxiliary static CMake registration oracle that blocked R20 before physical execution.

## Preserved R20 result

R20 static validation proved the canonical corrected verifier self-test and its clean-tree invariant. The subsequent independent CMake oracle failed because it selected the first global `NEOENG_DCORE_BUILD_RESEARCH_TOOLS` block, which belongs to research executable construction rather than the later test-registration section. R20 is closed/unmerged and non-reusable.

## R21 correction

The R20 verifier blob `e22d0e8b73264a993b912abd77379a53853ae50b` is preserved exactly. R21 changes only:

- revision-bound wrapper delegation to the preserved R20 wrapper;
- the auxiliary static oracle so it locates the first research-tools block after `neoeng_temporal_closure_tests` registration;
- assertions that replay/history test registrations are inside that later block and that temporal closure is registered/labeled `dcore` before it.

No physical runner, verifier semantics, product source, ABI, product tests or build definitions change.

## Fail-closed boundary

No physical Preflight is authorized unless the exact frozen R21 head passes scope/history, authorizer, verifier self-test, corrected CMake oracle, PowerShell syntax, parser-import regression, wrapper/verifier provenance, historical provenance, plan structure and final clean-tree checks.
