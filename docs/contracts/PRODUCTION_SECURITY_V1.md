# NeoEng D-Core Production Security Contract v1

Status: normative candidate for baseline 1.13.0

Schema: `neoeng.dcore.production-security.v1`

ChangeSet: CS013

## 1. Supported deployment model

The in-process HMAC packet format authenticates and integrity-protects a
datagram, but it does not provide confidentiality. A production remote
deployment must place the session behind an authenticated confidential
transport and provide a nonzero channel binding. Supported attestations are
TLS 1.3, QUIC v1, IPsec or a deployment-reviewed authenticated-encryption
transport.

The D-Core validates the attestation and fails closed when confidentiality,
peer authentication or channel binding is absent. It does not implement the
outer transport, validate a certificate chain or claim that an attestation is
independent evidence.

Forward secrecy is described by the selected transport and remains a deployment
property. The product does not infer it from HMAC or from an arbitrary provider.

## 2. Command and entity authorization

`CommandAuthorizationPolicy` authorizes a tuple containing:

- authenticated session role and origin;
- key identifier and epoch;
- operation;
- entity range;
- validity interval;
- confidential transport context.

Rules are bounded, uniquely identified and deny by default. A batch is rejected
at the first unauthorized command and identifies its index. Role-only
authentication is insufficient for a production operation.

The supported operations are input submission, snapshot/trace reads, evidence
and support-bundle export, key management and runtime recovery. Hosts must route
every exposed production operation through the policy or provide an equivalent
adapter proven by their deployment evidence.

## 3. Key lifecycle and custody

Session root keys retain the existing active/retired/revoked lifecycle and
bounded in-memory ring. Persistence, entropy, HSM/TPM custody and trusted time
remain host/deployment responsibilities.

External provider keys are represented only by non-secret descriptors. A key
used for protection must:

- have a nonempty provider identifier and nonzero epoch;
- be provider-backed and declare private material non-exportable;
- have the required purpose;
- be active and inside its validity interval;
- match the provider selected for the operation.

Retired, revoked, expired, future, mismatched or exportable private-key
descriptors are rejected fail-closed.

## 4. State Signature disposition

The product does not include a production asymmetric State Signature
provider. `EvidenceSigner` and `EvidenceSignatureVerifier` remain provider
interfaces, while `HmacSha256TestOnly` remains prohibited as an asymmetric,
non-repudiation or public-identity claim.

`CLAIM-SIGN-001` is removed from the product offer rather than promoted without
a concrete reviewed provider. Deployments may integrate Ed25519, ECDSA P-256,
RSA-PSS or a private provider, but their algorithm implementation, PKI,
certificate policy, custody and audit are outside the included product.

## 5. Protected support bundles

The core serializes the already verified support bundle into a canonical
length-delimited payload and binds the following metadata as additional
authenticated data:

- schema version;
- provider algorithm and key identifier;
- key epoch;
- nonce;
- plaintext SHA-256;
- original manifest SHA-256.

Protection and opening are performed only through
`ArtifactEncryptionProvider`. AES-256-GCM, ChaCha20-Poly1305 and reviewed
external AEAD identifiers are accepted for production policy. The deterministic
provider exists only in tests and is rejected unless the caller explicitly
enables test mode. Test-only providers cannot satisfy production policy.

Opening authenticates before decoding, checks the plaintext digest, restores
the original artifact and reruns `verify_support_bundle`. Modified ciphertext,
tag, metadata, key, nonce, manifest or decoded content is rejected.

## 6. External anchor adapter

`EvidenceAnchorAdapter` receives canonical bytes for a validated evidence-chain
anchor and returns a provider receipt. The D-Core binds and verifies the
receipt, but does not supply WORM storage, public timestamping, a transparency
log, notary, HSM or independent custody.

An adapter test proves the integration contract. It does not convert a local
test receipt into an external trust claim.

## 7. Evidence and non-claims

The CS013 campaign must exercise positive, negative, adversarial, recovery,
cross-compiler and evidence-integrity paths. Evidence describes only the source,
binary, environment and configuration recorded.

This contract does not claim:

- included production asymmetric signing;
- confidentiality from the HMAC packet format;
- secure key persistence or certified hardware custody;
- forward secrecy without a conforming transport;
- external trust from the test anchor adapter;
- cryptographic certification or independent security audit;
- ARM64 equivalence without an ARM64 campaign;
- performance independent of hardware or deployment.

Machines with lower or higher capability may produce better or worse
observations; neither direction is a universal rule.
