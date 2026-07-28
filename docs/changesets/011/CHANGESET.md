# ChangeSet 011 — numerical closure

Target baseline: 1.11.0

Requirements: `DCORE-NUM-001`, `DCORE-NUM-002`, `DCORE-NUM-003`

Limitations: `LIM-002`, `LIM-003`, `LIM-004`

Campaign: `TEST-CS011-001`

State: concluded in 1.11.0

## Decision

CS011 closes unsupported numerical promises by narrowing them to evidence that
the implementation can actually sustain:

- Y1-O4 is rejected as a runtime claim and retained only as historical
  uncertainty/RAA research;
- the global composed containment/overflow claim is rejected;
- Q32.32 primitives receive an explicit exact-intermediate and fail-closed
  contract;
- oblique solver evidence is separated into exact continuous small-tree,
  finite-grid, residual-only and operational non-certified scopes.

No canonical state, transition, serialization, hash, fixed tick or rollback
format is changed.

## Exit evidence

- Windows clang-cl configured regression: 24/24;
- immutable Windows numeric campaign: passed;
- Linux GCC and Clang campaigns: passed;
- semantic GCC/Clang result: byte-identical;
- independent evidence verifier and tamper self-test: passed;
- ARM64, universal hardware timing and external mathematical audit: not
  inferred.
