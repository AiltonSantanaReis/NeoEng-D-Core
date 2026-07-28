# ChangeSet 014 — release assurance and complete SDK surface

Target baseline: `1.14.0`

Campaign: `TEST-CS014-001`

## Scope

- reconcile every public claim with an installed SDK or official tool;
- produce one deterministic cumulative release archive;
- add continuous coverage-guided fuzzing for hostile byte ingress;
- execute ASan/UBSan over the supported release surface;
- execute blocking static analysis over supported implementation sources;
- generate SPDX SBOM, provenance, public claims and SHA-256 manifests;
- require externally signed release attestation and fail-closed update policy;
- define distribution-controlled commercial entitlement outside canonical
  state;
- publish a CI matrix reproducible from the repository.

## Preserved limits

- no ARM64 or P0-P4 result is inferred;
- no certification, independent audit or vulnerability-free claim is created;
- no private signing key is included;
- no local unsigned candidate is publishable;
- no runtime licensing decision enters canonical state;
- CS015 final acceptance remains separate.

## Static-analysis disposition

The first blocking run found unsafe-to-reason-about null output flows in the C
Host SDK copy APIs. The control flow was made explicit and empty-state behavior
was added to the Host SDK regression tests.

LLVM 22 also reports `clang-analyzer-security.ArrayBound` inside the
third-party Boost multiprecision unchecked `cpp_int` implementation while
analyzing `src/exact_oblique_tree_oracle.cpp`. That one check is suppressed
only for that translation unit and recorded in the policy and result summary.
All other analyzer checks remain blocking there, and the complete rule set
remains blocking for every other supported implementation unit.

## Exit criteria

- Windows clang-cl, Linux GCC and Linux Clang release gates pass;
- sanitizer, coverage-guided fuzzing and static-analysis jobs pass;
- consolidated archive and all generated assurance artifacts verify
  independently;
- the published archive receives an external signed attestation;
- six CS014 requirements and eight limitations are reconciled without promoting
  CS015.
