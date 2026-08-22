# CS017 R19 — EV-00 static Python hygiene campaign

## Purpose

R19 is a prospective correction after R18 static validation proved that ordinary authorizer Python invocations generated an untracked bytecode cache before the intended verifier no-side-effect check. R18 remains closed, unmerged, nonqualifying and non-reusable.

R19 starts fresh from accepted `main@3ebb989c5aaca65501ddbc5e552e1f751079e310` and keeps the protected source under test immutable at `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

## Narrow correction

The physical adapter semantics remain the R18 design: require exact plan/named clean branch, preserve the exact R17 wrapper, set `PYTHONDONTWRITEBYTECODE=1`, and require a clean tree before/after delegation.

The R19 correction is in static validation hygiene:

1. all EV-00 authorizer/verifier Python script invocations use `python3 -B`;
2. the independent verifier self-test requires a clean tree before and after execution;
3. a final static clean-tree gate runs after parser, adapter, provenance and plan checks and before any physical-evidence resolution;
4. the R18 failure record is preserved exactly.

The dual-surface runner and independent verifier runtime bytes remain unchanged.

## Non-effects

No product source, ABI, public header, product test, CMake/build definition, release claim or release authorization is changed. EV-00 remains unaccepted until committed physical evidence, independent verification, Historical Assurance, all frozen required tests and the actual Trusted ChangeSet validation gate pass.
