# Host SDK architectural boundary

## Dependency direction

```text
Host application / future adapter
             |
             v
NeoEng::DCoreHostSdk
             |
             v
NeoEng::DCore
```

The reverse dependency is prohibited. `neoeng_dcore` does not include the C ABI header and does not link the Host SDK.

## Why a companion module

The 1.6.0 baseline already contained the runtime mechanisms. The missing capability was a distributable and governed host boundary. Implementing it as `modules/host_sdk` prevents ABI concerns, host lifecycle policy and vertical adapters from contaminating canonical state code.

## Preserved invariants

ChangeSet 007 does not alter:

- `WorldState`, `Body` or `InputCommand` layout;
- fixed-point arithmetic or fixed tick;
- canonical ordering;
- simulation equations;
- rollback or snapshot algorithms;
- canonical serialization;
- stable hash, SHA-256 or Merkle formats;
- authenticated network/session protocols;
- recovery contract v1;
- View Lab read-only direction;
- hardware qualification rules.

## Trusted host input versus network input

The Host SDK is an in-process boundary. It validates count, entity identifier and acceleration range before converting C records to existing `InputCommand` values. It does not authenticate the host process. Remote or adversarial data must first pass the network/session security subsystem.

## Recovery

The SDK reuses `RecoveryController` and `RecoveryHostBridge`. It preserves:

- monotonic generation;
- acknowledgement type matching;
- stale-generation rejection;
- exact checkpoint-frame validation;
- retained-snapshot checks;
- terminal halt semantics.

No new recovery mode or action was introduced.

## Future adapters

Unreal, Unity, ROS 2, industrial, defense, aerospace and financial adapters must remain consumers of this boundary or another formally reviewed companion boundary. They may map lifecycle and data, but may not mutate the D-Core's canonical state directly.
