# ChangeSet 019 — EV-02 Fundamental Contract Hardening

State: **PLANNED**

Base: `bf3051abdb084273540e6caeb72329eafa0a2eea`

Stage: `EV-02 — Hardening dos contratos fundamentais`

## Purpose

CS019 closes EVREQ-009 through EVREQ-012 without broad architectural change.

The implementation phase is limited to explicit fundamental-transition behavior:

- advancing a `WorldState` already at `UINT64_MAX` must reject fail-closed;
- an `InputCommand` targeting an entity absent from the canonical world must reject explicitly;
- rejected fundamental transitions must not commit rollback input-history state before validation succeeds;
- the existing Q32.32 contract remains authoritative: overflow and narrowing reject, division by zero rejects, and multiplication/division round by truncation toward zero;
- the existing Host SDK ABI 1.0 status classes are reused rather than extended.

## Source-phase maximum

The complete source candidate is limited to 15 paths.

Seven semantic paths may change during IMPLEMENTED:

- `docs/contracts/FUNDAMENTAL_TRANSITION_V1.md`
- `include/neoeng/core/simulation.hpp`
- `src/simulation.cpp`
- `src/rollback.cpp`
- `tests/test_main.cpp`
- `tests/numeric_closure_tests.cpp`
- `modules/host_sdk/tests/host_sdk_tests.cpp`

Eight campaign-control paths comprise the PLANNED control plane:

- `.github/workflows/cs019-ev02-fundamental-contract-hardening-validation.yml`
- `MANIFEST.sha256`
- `audit/CURRENT_CHANGESET_VALIDATION.json`
- `audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json`
- `audit/EVOLUTION_ROADMAP.json`
- `audit/validation/CS019/VALIDATION_PLAN.json`
- `docs/changesets/019/CHANGESET.md`
- `scripts/verify_cs019_ev02_fundamental_contracts.py`

## Explicit non-effects

CS019 does not authorize:

- Host SDK ABI layout or status-number changes;
- changes to `modules/host_sdk/include/neoeng/dcore_host.h`;
- changes to `modules/host_sdk/src/dcore_host.cpp`;
- changes to Q32.32 implementation files;
- changes to CMake build definitions or test registration;
- product-version or release changes;
- EV-03 entry;
- release authorization.

`audit/STAGE_SCOPE_MAXIMA.json` remains preserved historical CS016E control-plane material. Prospective ChangeSet authority is `CHANGESET_VALIDATION`.

## Q32.32

`docs/contracts/NUMERIC_CLOSURE_V1.md` and the current `Fixed` implementation already define the required arithmetic semantics. CS019 adds explicit regression assertions but does not alter the arithmetic implementation.

## Host SDK

Existing exception translation is reused:

- `std::overflow_error` -> `NEOENG_DCORE_STATUS_NUMERIC_OVERFLOW`;
- `std::out_of_range` -> `NEOENG_DCORE_STATUS_NOT_FOUND`.

No C ABI structure, function signature, status numeric value, ABI major/minor, or CMake target changes are allowed.

## Cross-compiler evidence

The qualifying workflow must build and execute the same fundamental rejection corpus with GCC and Clang on Linux x86-64 and compare canonical result markers byte-for-byte.

This is a compiler-equivalence claim for the declared corpus only. It is not an ARM64 or universal cross-platform certification.

## Lifecycle

This commit creates the immutable PLANNED test inventory and control plane.

The seven semantic paths remain intentionally outside `frozen_files` because they must change during IMPLEMENTED.

No qualifying execution is authorized until an IMPLEMENTED source commit descends from the exact planning commit. The later result-binding closure may add `audit/validation/CS019/VALIDATION_RESULT.json` and update only lifecycle-mutable binding files such as the descriptor and manifest; it must not rewrite the frozen plan/test inventory.
