# Production security boundary

The D-Core owns policy validation and canonical artifact framing. It does not
own deployment transport, private-key custody or external trust.

```text
authenticated session + confidential channel attestation
                         |
                         v
          bounded command/entity authorization
                         |
                         v
                 official D-Core API

verified support bundle ----> canonical protection frame
                                      |
                                      v
                            deployment AEAD provider

evidence-chain anchor ------> canonical anchor adapter
                                      |
                                      v
                         deployment WORM/notary domain
```

The dependency direction is always host/provider -> D-Core interface. Providers
receive bytes and metadata; they never receive canonical-state authority.

The product rejects absent channel binding, undeclared commands/entities,
inactive or mismatched keys, unsupported algorithms, modified protected
artifacts and invalid anchor receipts. Test providers are marked test-only and
cannot satisfy a production policy.

The included product deliberately removes the asymmetric State Signature claim.
Provider interfaces remain available without implying an algorithm
implementation, PKI, non-repudiation or independent assurance.
