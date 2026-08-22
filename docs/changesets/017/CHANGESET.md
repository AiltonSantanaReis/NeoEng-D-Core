# CS017 R18 — EV-00 no-side-effect wrapper campaign

## Purpose

R18 is a prospective correction after R17 physical Preflight detected that wrapper-side checks dirtied the control tree before the preserved runner's clean-tree invariant. R17 remains closed, unmerged, nonqualifying and non-reusable.

R18 starts fresh from accepted `main@3ebb989c5aaca65501ddbc5e552e1f751079e310` and keeps the protected source under test immutable at `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

## Narrow correction

R18 preserves the exact R17 wrapper as `scripts/dlab/ev00/invoke_ev00_r17.ps1` and introduces only a thin `invoke_ev00_r18.ps1` adapter. The adapter:

1. requires the exact frozen R18 plan commit, a named branch and an initially clean tree;
2. verifies the preserved R17 wrapper blob is `09540ceafbeb1336489911897e9b2f7f156f5114`;
3. sets `PYTHONDONTWRITEBYTECODE=1` before delegating to R17, preventing Python import bytecode caches from mutating the control tree;
4. checks `git status --porcelain --untracked-files=all` immediately before and after delegation and rejects any side effect;
5. otherwise returns the preserved wrapper/runner result unchanged.

The R17 dual-surface runner and R16/R17 independent verifier remain byte-identical. Build A remains research-OFF with exact normative CS015 Windows 54-test inventory. Build B remains isolated research-ON with exactly replay/history/temporal-closure tests.

## Historical Assurance

The corrected CS001-CS015 provenance plan from R16/R17 is preserved unchanged. It freezes provenance only and does not pre-assign risk, reproducibility, rerun necessity or final disposition.

## Non-effects

No product source, ABI, public header, product test, CMake/build definition, release claim or release authorization is changed. EV-00 remains unaccepted until committed physical evidence, independent verification, Historical Assurance, all frozen required tests and the actual Trusted ChangeSet validation gate pass.
