# CS015 evidence

Reserved for `TEST-CS015-001`.

The closure package must contain source, build and configuration identity;
raw Windows clang-cl and Linux GCC/Clang regression results; governance and
claims reconciliation; CS014 evidence re-verification; result summary;
limitations; SHA-256 manifest; and independent verification.

The closure candidate from source
`0d909e75380140078e84d1b5af07ed1e709082b5` is recorded under
`github-actions-run-30375982639`. Its 14 manifested artifacts passed
independent verification. That immutable candidate is the normative evidence
cited by the later acceptance decision that closes `DCORE-ACCEPT-001`.

## Corrective revalidation

Run `31246260738` revalidated the same horizontal baseline from commit
`2348f147452e8183a62f54db34dd3cf46388f28d` on branch
`changeset-016-p0-assurance`. The run passed Windows clang-cl, Linux GCC and
Linux Clang with 54/54 supported-surface tests in each environment. It then
assembled the package, created the external provenance attestation with
public Sigstore, verified that attestation with Cosign, and passed the strict
fail-closed evidence verifier.

The resulting artifact `cs015-final-acceptance-evidence` contains 19 files,
with 15 manifest entries verified, and has digest
`sha256:285d3904f3f46f3913e2c8aa8f87b8aa5beaade53e71256ce0d4d52cc9b365c5`.
This is an append-only corrective revalidation of the baseline above, not a
new release or an expansion of its hardware, performance or production
claims.
