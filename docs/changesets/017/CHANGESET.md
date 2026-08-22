# CS017 R20 — EV-00 supported/research verifier set separation

## Objective

Certify the immutable `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c` baseline in the external D-Lab while correcting only the independent-verifier defect exposed by R19.

R19 produced a locally PASSED physical campaign (`ev00-20260822T171439Z-03ca937d`) with exact 54/54 supported-surface equivalence and exact 3/3 isolated research validation, but the frozen verifier rejected the correct supported inventory because it treated `neoeng_temporal_closure_tests` as research-only. R19 is preserved and non-reusable.

## R20 correction

R20 splits the verifier semantics:

- `RESEARCH_SURFACE_EXPECTED` = replay + history + temporal-closure, required exactly for isolated Build B;
- `RESEARCH_ONLY_FORBIDDEN_IN_SUPPORTED` = replay + history only, forbidden from Build A;
- `neoeng_temporal_closure_tests` remains mandatory in the normative 54-test supported inventory and in the explicit three-test research validation surface.

The runtime runner, product source and physical supported/research build definitions are unchanged. The R20 wrapper is a revision adapter over the preserved R19 wrapper.

## Static fail-closed requirements

Before physical execution, CI must prove:

1. R9-R19 history is preserved and non-reusable.
2. The canonical R20 verifier self-test passes without changing the Git tree.
3. The normative CS015 54-test oracle contains temporal closure and excludes replay/history.
4. The immutable baseline CMake registration places temporal closure in the supported `dcore` surface and replay/history under `NEOENG_DCORE_BUILD_RESEARCH_TOOLS`.
5. The actual `check_ctest` accepts a fixture equal to the normative 54-test oracle and rejects an injected replay/history leak.
6. The preserved exact three-test research contract remains mandatory.
7. Wrapper/runner/verifier provenance bindings are exact.
8. The complete static job leaves the control tree clean.

## Non-effects

No `src/**`, `include/**`, `tests/**`, `CMakeLists.txt`, product runtime, ABI, product build definition, release claim or public acceptance state is modified. Candidate EV-00 may remain `in_progress`; accepted `main` remains authoritative until all frozen checks and the Trusted ChangeSet validation gate pass.
