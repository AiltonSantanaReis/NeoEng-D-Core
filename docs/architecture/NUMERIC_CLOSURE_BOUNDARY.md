# Numeric closure boundary

The authoritative state transition continues to use `Fixed` Q32.32 primitives.
CS011 adds policy and evidence; it does not replace the canonical state type,
serialization, hash, tick or rollback semantics.

```text
research / shadow laboratories
uncertainty, interval, RAA
          │ evidence only
          ▼
numeric closure policy ───────► claims and campaign
          │
          ├── Q32.32 primitive: exact wide intermediate, fail-closed narrowing
          ├── exact rational small-tree certificate
          ├── finite-grid-only certificate
          ├── residual-only certificate
          └── connected fallback: operational, non-certified

authoritative runtime
          │
          └── never consumes Y1-O4 or RAA output as authoritative state
```

## Dependency rule

`numeric_contract` may classify evidence from the existing numerical surfaces.
The uncertainty and RAA laboratories must not be invoked by canonical
transition, rollback or production contact-solver sources. Their presence in
the library preserves the historical research API; it does not make them
authoritative.

## Failure rule

- fixed primitive overflow and division by zero reject before a result exists;
- invalid solver input and capacity failure reject;
- a failed certificate can only become the documented operational fallback or
  a rejection;
- no fallback can promote its own certification level.

## Non-goals

CS011 does not provide:

- a global proof for arbitrary composed numerical workloads;
- a production Lyapunov predictor;
- authoritative RAA state;
- continuous certification from a finite grid;
- general oblique-tree global optimality;
- ARM64 equivalence or a hardware-independent timing claim.
