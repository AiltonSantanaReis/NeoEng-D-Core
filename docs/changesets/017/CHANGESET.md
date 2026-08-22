# ChangeSet 017 R13 — EV-00 baseline laboratory certification

State: `IMPLEMENTED CANDIDATE / PLAN FREEZE PENDING / PHYSICAL QUALIFICATION NOT STARTED / EV-00 NOT ACCEPTED`

Base: `3ebb989c5aaca65501ddbc5e552e1f751079e310`  
Protected product baseline: `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## Objective

Execute EV-00 against immutable v1.14.1 after preserving R9–R12 failures. R13 keeps the R11/R12 normative historical-oracle correction but repairs the R12 serialization mismatch prospectively.

## R12 triggering failure

R12 plan `ed747232d17253d141aeb12d3563a5288182c97f`, static run `32566544173`, failed before any physical execution at `Verify R12 normative historical oracle and isolated runner delta` because the canonical R12 runner omitted the terminal LF present in the preserved R11 byte stream.

Machine-readable record:
`docs/changesets/017/R12_STATIC_VALIDATION_FAILURE.json`

R12 remains failed and is never rerun or reclassified.

## R13 correction

Preserved R11 runner:
`scripts/dlab/ev00/run_ev00_dlab_windows_r11.ps1`  
blob `00cc326fa1c8cd40402c525c877a1af21969ba42`.

Preserved R12 runner:
`scripts/dlab/ev00/run_ev00_dlab_windows_r12.ps1`  
blob `2e9c32a6e342839ebcb7d6d1f71fff26b6f50f3b`.

Canonical R13 runner:
`scripts/dlab/ev00/run_ev00_dlab_windows.ps1`  
blob `47e6cda9567513c9357b20c31e3e5b1b10b2e4c4`.

R13 requires byte-exact equality to the R11 runner after only the authorized historical-oracle substitutions. It also proves that R12→R13 is exactly one terminal LF byte.

The normative historical reference remains:
`docs/changesets/015/evidence/github-actions-run-30375982639/raw/windows-clang-cl-ctest.txt`  
blob `ef550aa5223794d8466360dd8364c07e85f91bc0`, 54/54 supported-surface tests.

Exact test-count equality, exact sorted test-name equality and zero failures remain mandatory.

## Non-effects

No runtime, ABI, product tests, build definitions or public claims are changed. Release remains unauthorized. EV-00 remains unaccepted pending the frozen campaign and independent validation.
