# CS014 — test status

State: concluded

Baseline: `1.14.0`

Campaign: `TEST-CS014-001`

## Preliminary native Windows validation

The candidate was configured and built on the user's physical Windows x86_64
host with Visual Studio Build Tools 18.7.4, clang-cl 22.1.0, CMake
4.3.1-msvc1 and Ninja 1.13.2.

- supported release build: passed, 126 build steps;
- `ctest -L dcore`: passed, 51/51;
- clean-prefix installed Host SDK consumer: passed as part of CTest;
- blocking clang-tidy: passed, 64/64 implementation units;
- CS014 verifier and negative self-tests: passed, 5/5.

These are preliminary local results from a worktree under construction. They
describe only the recorded host and configuration and are not immutable
closure evidence. No ARM64, certification, external audit or performance rule
for other hardware is inferred.

## Immutable closure evidence

`TEST-CS014-001` passed on committed clean source
`82580064cd38be07af9f5264599165cbf48c218b` in GitHub Actions run
`30367653644`:

- Linux GCC, Linux Clang and Windows clang-cl release gates: passed;
- supported release CTest surface: passed, including 51/51 on Windows;
- ASan and UBSan over the supported surface: passed;
- two LLVM libFuzzer campaigns of 120 seconds each: passed;
- blocking clang-tidy over 64 implementation units: passed;
- cumulative source ZIP generated twice byte-identically and independently
  verified;
- keyless Sigstore provenance and SPDX SBOM attestations: issued by Fulcio,
  recorded in public Rekor and verified by Cosign 3.0.6;
- downloaded campaign package: 23 manifest entries independently verified on
  the user's Windows host with zero errors;
- both downloaded Sigstore bundles: independently verified on the user's
  Windows host against the archive digest, workflow identity, issuer,
  repository, commit and ref.

Evidence:
`docs/changesets/014/evidence/github-actions-run-30367653644`.

The campaign closes CS014 only. It does not prove ARM64 behavior, certify the
product, constitute an external security audit, generalize performance to
other hardware or complete CS015 commercial acceptance.
