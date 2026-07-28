# NeoEng D-Core Release Assurance Contract v1

Status: normative candidate for baseline 1.14.0

ChangeSet: CS014

## Supported release surface

The supported horizontal product consists of the installed C++ SDK, the stable
C Host SDK, the distributed-reference companion, official release tools,
governance ledgers and independent verifiers. Historical research executables
remain reproducibility material but are not marketed release APIs.

Every publicly usable claim must be mapped by
`audit/PRODUCT_CAPABILITY_SURFACE.json` to at least one supported surface.
Planned, unsupported, removed and prohibited claims are excluded from generated
commercial material.

## Blocking release gates

A release candidate is rejected unless all applicable gates pass:

- warnings-as-errors builds and tests on Linux GCC, Linux Clang and Windows
  clang-cl;
- ASan and UBSan over the complete supported release test surface;
- LLVM libFuzzer campaigns for every untrusted binary ingress;
- structured adversarial fuzzing for state evidence and support bundles;
- blocking clang-tidy analysis over the supported C++ implementation;
- clean-prefix SDK installation and consumer execution;
- a consolidated cumulative archive, internal SHA-256 manifest, SPDX SBOM,
  source/build provenance and independently verified evidence.

ARM64 and native hardware profiles remain separate qualification gates and are
never inferred from x86_64 release assurance.

Static-analysis suppressions must name one rule, one translation unit and a
technical reason. CS014 records one LLVM 22 `ArrayBound` false positive emitted
inside Boost multiprecision while analyzing the exact oblique-tree oracle. It
does not disable the remaining analyzer rules for that unit or any rule for
other implementation units.

## Consolidated distribution

The official 1.14.0 source distribution is one deterministic cumulative ZIP.
It contains the complete tracked baseline and generated release metadata. A
base archive plus ChangeSet patches is not an accepted release form.

The archive is reproducible from a clean commit. Every packaged file except the
manifest itself is covered by `SHA256SUMS.txt`. The outer archive has a sibling
SHA-256 record and is checked by an independent verifier.

## SBOM and provenance

`SBOM.spdx.json` uses SPDX 2.3 and records packaged files with SHA-256 checksums,
the NeoEng D-Core package and declared third-party dependencies. Deterministic
in-archive provenance binds the release version, Git commit/tree and
clean-worktree state. Variable builder identity remains outside the ZIP and is
bound by the external workflow attestation, so identical source inputs produce
the same archive bytes.

The generated documents are evidence for the recorded build only. They do not
constitute external certification or a vulnerability-free claim.

## Signature and secure update

No private release-signing key is stored in the repository. Publication
requires two keyless Sigstore bundles, issued through Fulcio and recorded in
the public Rekor transparency log: one binds release provenance and one binds
the SPDX SBOM to the archive digest. Cosign verifies the exact GitHub Actions
workflow identity, OIDC issuer, repository, commit and ref before the
verification receipts are emitted. A locally generated unsigned candidate may
be tested but must not be presented as an authenticated release.

The public transparency record exposes the repository name, workflow, ref and
commit identity. It does not publish the private source contents. GitHub
Artifact Attestations are not the CS014 closure provider because that service
is unavailable to a user-owned private repository without GitHub Enterprise
Cloud.

Consumers must verify the external attestation, outer SHA-256 and internal
manifest before installation. Downgrade requires explicit operator approval,
and downloaded content is never executed automatically by the D-Core.

## Commercial delivery and entitlement

Entitlement is controlled by distribution access and the applicable commercial
contract. The horizontal runtime contains no licensing decision in canonical
state and no included runtime entitlement server. Separately licensed vertical
adapters do not change canonical authority.

No redistribution permission is inferred from the source package or this
technical contract.
