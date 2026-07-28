# ChangeSet 013 — production security and cryptographic-evidence boundary

Target baseline: `1.13.0`

Campaign: `TEST-CS013-001`

## Normative decision

CS013 closes `DCORE-CRYPTO-002`, `DCORE-SEC-001` and `DCORE-BUNDLE-002`
without claiming cryptographic primitives that the product does not include.
The production asymmetric State Signature claim is removed. Confidentiality,
private-key operations and independent anchors use explicit external providers
with fail-closed validation.

## Delivered scope

- authenticated confidential-transport attestation and channel binding;
- bounded command-, entity-, origin-, role-, key- and time-granular policy;
- external provider key-purpose and lifecycle validation;
- canonical protected support-bundle framing and verified round-trip;
- external evidence-anchor adapter and receipt verification;
- explicit rejection of test-only providers by production policy;
- tests, deterministic probe, boundary verifier and independent campaign
  evidence verifier.

## Preserved boundaries

- canonical state and transition semantics are unchanged;
- Host SDK ABI 1.0 is unchanged;
- the raw HMAC packet format is not described as confidential;
- no private key, PKI, certificate chain, CSPRNG, trusted time, HSM/TPM,
  WORM store or external notary is included;
- no asymmetric State Signature, forward secrecy, non-repudiation, external
  audit, ARM64 or universal hardware claim is inferred.

## Exit criteria

- Windows x86_64 clang-cl tests and campaign pass;
- Linux x86_64 GCC/Clang campaigns pass from a clean common source;
- semantic probe output is byte-identical across GCC and Clang;
- boundary, evidence, product-contract and assurance verifiers pass their
  fail-closed self-tests;
- requirements, claims, limitations, provenance and package manifest are
  reconciled before approval.
