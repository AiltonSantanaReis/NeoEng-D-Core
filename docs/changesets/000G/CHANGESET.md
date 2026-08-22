# CS000G — Specific validation workflow retirement

Status: **PLANNED / IMPLEMENTED CANDIDATE / NOT YET VALIDATED / NOT ACCEPTED**

## Objective

Retire automatic `pull_request` applicability of the accepted CS000F R2 validation workflow before EV-00/CS017 starts. The workflow remains preserved for explicit manual diagnostics, but it must not apply its frozen CS000F-only scope to future unrelated ChangeSets.

## Correction

- `.github/workflows/cs000f-r2-authorizer-transition-validation.yml` becomes `workflow_dispatch`-only.
- Its body from `permissions:` onward is preserved from accepted main `b8598b73245ee0b31cc40ccfa60a1baed5142194`.
- CS000G uses a dedicated validation workflow whose `pull_request.paths` set is exact and limited to CS000G candidate files. It deliberately excludes the active descriptor, validation result, and all EV-00/CS017 preparation/execution paths, so it does not become another stale universal gate.

## Historical preservation

CS000F R2 remains accepted with its source, qualifying run, result and merge ancestry unchanged. CS000G does not rewrite CS000F evidence or any prior failure.

## Non-effects

- EV-00 remains `not_started`.
- CS017 remains not started.
- No runtime, ABI, product-test, CMake, release, qualification, D-Lab authority, amendment, root, or acceptance-chain state is changed.
