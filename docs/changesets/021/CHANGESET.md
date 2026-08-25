# CS021 — EV-04 property-based and model-based testing

State: **IMPLEMENTED CANDIDATE / NOT YET EXECUTED / NOT ACCEPTED**

Base:

`9e5c00faa4db0868da48913b8ffa24e0f64972e2`

Branch:

`agent/cs021-ev04-property-model-testing`

## Objective

Activate EV-04 with a deterministic property/model campaign over the existing
D-Core contracts without changing runtime, public headers, ABI or canonical
formats.

The campaign tests properties of the existing optimized implementation and
compares transition results against a deliberately simple reference model.

## Governance transition

The source candidate changes:

- `current_stage`: `EV-03` -> `EV-04`;
- EV-03: remains `accepted`;
- EV-04: `not_started` -> `in_progress`;
- EVREQ-016..018: `planned` -> `in_progress`;
- EV-05: remains `not_started`;
- release authorization: remains `false`.

No EV-04 requirement receives evidence or `verified` status in the source
candidate.

## Accepted prerequisite

CS000M is already protectedly integrated and defines the exact EV-04 maximum
scope. CS021 operates only inside that maximum.

## Exact source scope

The source candidate is exactly 10 paths:

1. `.github/workflows/cs021-ev04-property-model-testing-validation.yml`
2. `CMakeLists.txt`
3. `MANIFEST.sha256`
4. `audit/CURRENT_CHANGESET_VALIDATION.json`
5. `audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json`
6. `audit/EVOLUTION_ROADMAP.json`
7. `audit/validation/CS021/VALIDATION_PLAN.json`
8. `docs/changesets/021/CHANGESET.md`
9. `scripts/verify_cs021_ev04_property_model_testing.py`
10. `tests/property_model_tests.cpp`

`MANIFEST.sha256` and `audit/CURRENT_CHANGESET_VALIDATION.json` are lifecycle
mutable and therefore excluded from the qualification trigger and frozen-file
set. The future `VALIDATION_RESULT.json` is absent from the source candidate.

The immutable qualification inventory contains 44 frozen paths and 26 required
tests.

## Deterministic campaign

The property/model executable uses the fixed seed:

`0xC5021E0400000001`

It executes 128 generated scenarios with no external property-testing
framework dependency.

### EVREQ-016 — canonical input order

Each generated scenario is executed under four deterministic input
permutations and must produce identical `WorldState`, canonical bytes and stable
hash.

A dedicated overflow-sensitive sentinel uses accelerations whose naive input
order can overflow an intermediate sum while the canonical `(entity, x, y)`
order remains valid. This makes the property sensitive to loss of the runtime
canonicalization rule rather than merely relying on commutativity of small
integer additions.

### EVREQ-017 — reference model

`reference_step` does not call `step` or `step_with_dirty`. It independently:

- groups commands by entity;
- insertion-orders commands by the normative canonical key;
- sums accelerations;
- applies the existing Q32.32 `Fixed` primitive and `kSimulationDelta`;
- advances velocity, position and frame.

The reference model intentionally reuses the already accepted numeric primitive;
EV-04 is testing transition/control-flow equivalence, not replacing the EV-02
numeric contract.

### EVREQ-018 — applicable properties

The campaign covers:

- repeated canonical serialization determinism;
- canonical-byte and stable-hash equivalence between model and optimized result;
- snapshot capture/restore over all six `SnapshotStrategy` values;
- rollback/corrected-input/resimulation over all six strategies;
- final-state strategy equivalence.

Canonical deserialize is **not applicable** in this stage because the protected
WorldState v1 public API currently exposes `canonical_serialize` but no canonical
WorldState deserializer. Adding a deserializer would require `include/**` and/or
`src/**`, which CS000M explicitly forbids for EV-04. CS021 therefore does not
invent a new API to satisfy a test item.

## Cross-compiler qualification

The same deterministic campaign is built and executed with GCC and Clang on the
qualification runner. The one-line campaign summary must be byte-identical
between compilers.

## EV-03 preservation

The accepted EV-03 golden corpus is immutable. CS021 qualification reruns
`neoeng_golden_corpus_tests` but does not regenerate or modify any file under
`tests/golden/ev03/v1` or `docs/contracts/GOLDEN_CORPUS_V1.md`.

## Explicit non-effects

CS021 changes no:

- `src/**` runtime implementation;
- `include/**` public or internal header;
- Host SDK ABI;
- snapshot implementation;
- rollback implementation;
- canonical serialization format;
- EV-03 golden bytes;
- governance root/policy/trusted workflows;
- EV-05 lifecycle state;
- release authorization;
- public product claim.

`CMakeLists.txt` changes only to register the permanent EV-04 test executable
and CTest label.

## Qualification trigger

The dedicated workflow is push-triggered only for:

`agent/cs021-ev04-property-model-testing`

Its trigger contains exactly eight source paths. Lifecycle binding paths are
excluded:

- `MANIFEST.sha256`;
- `audit/CURRENT_CHANGESET_VALIDATION.json`;
- `audit/validation/CS021/VALIDATION_RESULT.json`.

A successful qualifying run must not be rerun merely to bind acceptance.

## Acceptance boundary

A successful dedicated run is not acceptance.

Acceptance requires all 26 required tests PASS, exact source/run/workflow
binding, an immutable `VALIDATION_RESULT.json`, trusted-base PR verification,
protected integration and post-merge gates.

EV-04 remains `in_progress` after CS021 integration until a later explicit
ledger-closure ChangeSet verifies EVREQ-016..018 and marks the stage accepted.
Release remains separate.
