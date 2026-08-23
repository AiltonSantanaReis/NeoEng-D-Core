# CS019 R2 — EV-02 qualification amendment

State: **PLANNED / NOT YET EXECUTED / NOT ACCEPTED**

## Why R2 exists

CS019 R1 source
`9a131f35ed3d8d7d4989b23e6382f5eaf8c8f121`
was executed by qualifying run `32641132546`, attempt `1`.

The dedicated workflow concluded `success` and all 22 frozen required tests
passed. The same PR also triggered the permanent `Current product regression`
workflow, run `32641132506`, attempt `1`.

That permanent workflow failed before product execution because
`verify_build_ci_governance.py --classification` found two workflows matching
its CURRENT product-regression definition:

1. `.github/workflows/current-product-regression.yml`;
2. the pull_request-triggered CS019 qualifying workflow.

The product itself subsequently built successfully in the dedicated CS019 run,
all 28 smoke tests passed, and the GCC/Clang rejection corpus was identical.
The failure is therefore a Build/CI governance classification regression, not
a product-test failure.

## R1 disposition

R1 is preserved permanently as:

`POST_EXECUTION_PERMANENT_GOVERNANCE_REGRESSION`

with:

- dedicated workflow conclusion: `success`;
- required CS019 tests: `22/22 PASS`;
- validation state: `BLOCKED`;
- acceptance decision: `NOT_ACCEPTED`;
- rerun of attempt 1: `false`.

No R1 artifact, run or conclusion is rewritten as failure or acceptance.

## Amendment

This document is the prospective CS019 R2 amendment required by
`INV-EV-020`.

The correction does not weaken or modify
`scripts/verify_build_ci_governance.py`.

Instead, the dedicated CS019 qualifying workflow is changed prospectively
from `pull_request` to an exact-branch `push` trigger for:

`agent/cs019-ev02-fundamental-contract-hardening`

Under the already accepted Build/CI classifier this makes the dedicated
workflow `HISTORICAL_BRANCH`, leaving exactly
`.github/workflows/current-product-regression.yml` as the single CURRENT
product regression workflow.

The R2 qualifying workflow additionally executes the permanent Build/CI
governance self-test, workflow classification, action pinning, CMake-option,
permanent-regression-contract and historical-boundary checks as required R2
tests.

## Product preservation

R2 MUST preserve byte-for-byte the R1 product/contract implementation,
including:

- `docs/contracts/FUNDAMENTAL_TRANSITION_V1.md`;
- `include/neoeng/core/simulation.hpp`;
- `src/simulation.cpp`;
- `src/rollback.cpp`;
- `tests/test_main.cpp`;
- `tests/numeric_closure_tests.cpp`;
- `modules/host_sdk/tests/host_sdk_tests.cpp`.

No ABI, Q32.32 implementation, CMake definition, product behavior or release
authorization change is permitted.

## Trigger discipline

R2 qualification is a new prospective campaign.

The successful R1 run is not rerun.

The R2 workflow is push-triggered only after the R2 plan/source commit exists.

Lifecycle-mutable result binding paths are deliberately excluded from the R2
workflow path trigger so a later `VALIDATION_RESULT.json` binding does not
reexecute an already successful R2 qualifying campaign.

## Acceptance

R2 is not accepted merely because its dedicated workflow is green.

All 30 frozen R2 tests must PASS and exact GitHub evidence must subsequently
be bound through the ChangeSet validation policy.

Release authorization remains false.
