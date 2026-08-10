# CS015 — test status

State: accepted for candidate `1.14.1`; historical baseline `1.14.0` preserved

Baseline: published release `v1.14.1`; historical baseline: `1.14.0`

Campaign: `TEST-CS015-001`

## Final result

- source commit: `0d909e75380140078e84d1b5af07ed1e709082b5`;
- GitHub Actions run: `30375982639`;
- Windows clang-cl supported-surface regression: passed;
- Linux GCC supported-surface regression: passed;
- Linux Clang supported-surface regression: passed;
- 54/54 supported-surface tests passed in each recorded environment;
- internal mandatory requirements open: 0;
- internal mandatory limitations open: 0;
- unsupported/prohibited claims rendered in public material: 0;
- CS014 immutable release-assurance evidence: passed independent verification;
- CS015 evidence manifest entries verified: 14;
- horizontal baseline acceptance: granted within published claims and limits.

The campaign is recorded at
`docs/changesets/015/evidence/github-actions-run-30375982639` and passed the
independent CS015 evidence verifier without missing, additional or modified
manifested artifacts.

No native ARM64/P0–P4 result, certification, independent external audit,
mission-critical readiness or performance rule for another machine is
inferred.

## Corrective provenance revalidation

The acceptance package was revalidated on GitHub Actions in run
`31246260738`, using source commit
`2348f147452e8183a62f54db34dd3cf46388f28d` on branch
`changeset-016-p0-assurance`. This is a corrective assurance run; it does
not create a new product baseline or replace the historical run above.

- Windows clang-cl, Linux GCC and Linux Clang each passed 54/54 supported-
  surface tests;
- the final-acceptance package was independently assembled and verified with
  the strict fail-closed verifier;
- the external provenance attestation was created and verified with public
  Sigstore/Cosign (`Verified OK`);
- 15 manifest entries were verified and 19 final evidence files were
  uploaded in artifact `cs015-final-acceptance-evidence`;
- artifact digest: `sha256:285d3904f3f46f3913e2c8aa8f87b8aa5beaade53e71256ce0d4d52cc9b365c5`.

The corrective run closes the provenance-authentication gap found during
assurance review. It preserves all prior limitations: no native ARM64/P0–P4
qualification, certification, external audit, mission-critical readiness or
performance rule for another machine is inferred.

## Published baseline 1.14.1 — final acceptance

- source commit: `880a1820b570ec1c9ebb2892206068fbcf7bd1ef`;
- GitHub Actions run: `31384684512`;
- Linux GCC, Linux Clang and Windows clang-cl regressions: passed;
- fail-closed evidence assembly: passed;
- public Sigstore provenance attestation and verification: passed;
- independent evidence verification: passed;
- immutable campaign evidence: `docs/changesets/015/evidence/github-actions-run-31384684512`;
- 19 evidence files and 15 SHA256 manifest entries;
- current ledgers: `DCORE-ACCEPT-001` complete, `TEST-CS015-001` executed, assurance evidenced;
- current final-acceptance report: `accepted`, zero open mandatory requirements and zero open mandatory limitations.

Supplemental local corroboration on the user's Windows x86_64 host also passed
`ctest --test-dir build/release-1.14.1-windows-clang --output-on-failure`:
89/89 tests passed, including the governance, CS010–CS015, recovery, security,
fuzz-smoke, Host SDK and distributed-reference tests. The raw output and
configuration are preserved at
`docs/changesets/015/evidence/windows-x86_64-clang-20260810`.
This supplemental build was pre-existing and is not cryptographically bound to
a source commit; it is not substituted for the immutable cross-platform CS015
package and does not establish claims for other hardware.

The package's own `final-acceptance-validation.json` records `closure_candidate`,
as required before registering the immutable evidence. The current ledgers then
recalculate the accepted state; the package was not modified after publication.
This candidate is not attributed to the immutable `v1.14.0` release. The published
release `v1.14.1` points to merge `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`;
its main-branch package and attestations were verified in CS014 run
`31387419484`, with archive SHA-256
`107c8c4f90642151648de523e6b461500c6dc90915f759f930490c03e29f53bf`.

No native ARM64/P0–P4 result, certification, independent external audit,
mission-critical readiness or performance rule for another machine is inferred.
