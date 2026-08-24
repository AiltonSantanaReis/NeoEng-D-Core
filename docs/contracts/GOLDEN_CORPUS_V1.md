# Golden Corpus V1

Status: normative CS020 / EV-03 candidate contract.

## 1. Authority and base

This corpus is the deterministic golden-oracle candidate for EV-03.

It is bound to integrated base:

`98c1042249ced4c1775dddf9a871e29dc6070828`

The corpus consumes existing product behavior. It does not redefine or
reimplement canonical serialization, transition semantics, snapshot semantics,
rollback/replay, SHA-256, Merkle hashing, or state-evidence encoding.

The existing producer contracts remain normative:

- `docs/contracts/FUNDAMENTAL_TRANSITION_V1.md`;
- `docs/contracts/STATE_EVIDENCE_V1.md`;
- `docs/contracts/TEMPORAL_CLOSURE_V1.md`.

## 2. Versioned artifacts

Schema:

`neoeng.dcore.golden-corpus.v1`

Manifest schema:

`neoeng.dcore.golden-corpus-manifest.v1`

Corpus version:

`1`

Versioned root:

`tests/golden/ev03/v1`

Required golden artifacts:

- `world_initial.bin`;
- `world_after_transition.bin`;
- `world_after_rollback_replay.bin`;
- `evidence_envelope.bin`;
- `corpus.json`;
- `manifest.json`.

The four `.bin` files are raw canonical bytes produced by existing D-Core
APIs. `corpus.json` is a semantic oracle emitted from the same scenario.
`manifest.json` binds raw-byte SHA-256 identities for the artifacts and
contracts, binds the exact base Git blob identities of the producer surfaces,
and records the eleven DEV-0011 checkout-sensitive historical references by
exact Git blob identity.

The manifest MUST NOT hash itself.

### 2.1 DEV-0011 cross-platform historical blob binding

Eleven historical source/reference paths in the integrated base contain CRLF
bytes while the current repository checkout policy is `text=auto eol=lf`.

Those paths are not rewritten or normalized by CS020.

They are excluded only from the generic worktree-byte frozen-file mechanism
and are instead bound by exact Git blob identity:

`git-blob-sha1-cross-platform`

The exact set is:

- `include/neoeng/core/fixed.hpp`;
- `include/neoeng/core/hash.hpp`;
- `include/neoeng/core/rollback.hpp`;
- `include/neoeng/core/simulation.hpp`;
- `include/neoeng/core/snapshot_store.hpp`;
- `include/neoeng/core/types.hpp`;
- `src/hash.cpp`;
- `src/rollback.cpp`;
- `src/simulation.cpp`;
- `src/snapshot_store.cpp`;
- `tests/test_main.cpp`.

The CS020 verifier must require both:

1. the exact base blob listed in `legacy_blob_bindings`;
2. the candidate `HEAD` blob for the same path to equal that base blob.

`.gitattributes` is separately part of the generic frozen inventory and must
remain unchanged.

This amendment is recorded by `DEV-0011` and does not reduce the 28-test
inventory or alter any emitted golden oracle.

## 3. Scenario

The corpus uses one small deterministic WorldState with two sorted entities,
fixed Q32.32 positions/velocities and an intentionally non-canonical input
order.

The golden producer MUST exercise:

1. initial canonical serialization;
2. successful deterministic transition;
3. stable hash;
4. canonical state SHA-256;
5. state Merkle root;
6. capture and restore through every currently supported SnapshotStrategy;
7. rollback correction and deterministic replay;
8. maximum-frame rejection class and exact diagnostic;
9. unknown-entity rejection class and exact diagnostic;
10. canonical StateEvidenceEnvelope bytes and envelope SHA-256;
11. accepted state-evidence verification;
12. deterministic state-evidence rejection reason.

## 4. Snapshot strategy coverage

Corpus version 1 covers exactly the currently public strategies:

- `full_copy`;
- `delta_checkpoint`;
- `paged_cow`;
- `persistent_chunk_tree`;
- `component_soa`;
- `hybrid_adaptive`.

The corpus asserts restored canonical state equality. It does not freeze
internal storage layout, allocation count, selected hybrid encoding, timing,
or implementation-private representation.

## 5. Diagnostics

The golden negative transition contract preserves:

- `std::overflow_error` / `World frame maximum reached`;
- `std::out_of_range` / `Input references unknown EntityId`.

The deterministic evidence-negative case is constructed from an otherwise
valid record whose canonical digest is deliberately changed and whose envelope
hash is recomputed. The expected verification reason is:

`canonical_digest_mismatch`

## 6. Golden generation discipline

`neoeng_golden_corpus_tests --emit <root>` is a materialization-only operation.

It may be used only before the CS020 source candidate is committed and
executed.

The qualifying workflow MUST NOT call `--emit`.

During qualification, the test regenerates all oracle values in memory using
the frozen producer APIs and requires byte-for-byte equality with the committed
golden files.

A failing or diverging oracle MUST NOT rewrite the committed corpus during
validation.

## 7. Cross-compiler boundary

The CS020 qualifying campaign compares GCC and Clang on Linux x86-64.

Both compilers must consume the same committed corpus and emit an identical
canonical PASS marker.

This does not claim ARM64, MSVC, or universal platform equivalence.

## 8. Explicit non-effects

CS020 does not change:

- any `include/neoeng/core/` product API;
- any `src/` product implementation;
- Host SDK ABI;
- canonical world format version;
- state-evidence schema version;
- Merkle format version;
- Q32.32 policy;
- EV-04 lifecycle state;
- EV-05 lifecycle state;
- release authorization.

Property-based/model-based testing belongs to EV-04.

Semantic fuzzing/corruption campaigns belong to EV-05.

CI success is validation evidence only and is not acceptance or release
authorization.
