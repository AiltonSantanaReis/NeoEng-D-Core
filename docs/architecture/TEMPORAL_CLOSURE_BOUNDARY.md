# Temporal closure boundary

The canonical authority remains inside `NeoEng::DCore`.

```text
canonical state / evidence chain
              |
              v
  durable recorder (append-only)
              |
              v
 deployment-selected storage / anchor

prepared external intent
              |
      confirmed horizon
              |
              v
 host idempotent executor
```

The recorder persists product-generated temporal and evidence exports but does
not confer trust by itself. A deployment may anchor the verified head digest
externally.

External effects never become canonical mutations. The ledger permits commit
only after confirmation and records whether compensation exists. Rollback
discards future prepared intents and explicitly reports committed effects that
cross the restored frame.

`WorldState v1` is the sole promised canonical schema for baseline 1.12.0.
Diff, canonical SHA-256 and Merkle coverage are co-declared in
`canonical_world_v1_fields()`.
