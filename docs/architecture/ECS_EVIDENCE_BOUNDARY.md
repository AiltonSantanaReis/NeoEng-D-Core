# ECS Evidence Boundary

The Y1-O2 evidence path is a product-module boundary, not a mutation of canonical simulation semantics.

```text
ComponentWorld benchmark execution
        |
        +-- index-maintenance stream
        +-- C++ allocation stream
        +-- epoch-arena stream
        +-- copy-on-write stream
        v
independent semantic verifier
        v
qualification campaign decision
```

Rules:

1. The benchmark observes existing ECS/component and arena mechanisms; it does not become canonical state authority.
2. Wall-clock durations and allocation counters are qualification evidence and never enter `S[t+1] = f(S[t], I[t])`.
3. Raw streams from one execution are cross-linked by sample number and semantics.
4. External candidate files are not accepted for qualification.
5. Evidence completeness and gate success are separate facts.
6. P1 remains fail-closed unless native execution and every profile gate pass.
