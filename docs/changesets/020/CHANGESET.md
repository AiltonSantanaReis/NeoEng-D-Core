# CS020 — EV-03 deterministic golden corpus

State: **IMPLEMENTED CANDIDATE / NOT YET EXECUTED / NOT ACCEPTED**

Base:

`98c1042249ced4c1775dddf9a871e29dc6070828`

Branch:

`agent/cs020-ev03-deterministic-golden-corpus`

## Objective

Materialize EV-03 as a versioned deterministic golden corpus over existing
D-Core canonical transition, serialization, snapshot, rollback/replay,
SHA-256, Merkle, diagnostic and state-evidence behavior.

CS020 creates immutable regression oracles. It does not change the runtime
behavior that produces those oracles.

## Governance transition

The source candidate changes:

- `current_stage`: `EV-02` -> `EV-03`;
- EV-03: `not_started` -> `in_progress`;
- EVREQ-013..015: `planned` -> `in_progress`;
- EV-04: remains `not_started`;
- EV-05: remains `not_started`;
- release authorization: remains `false`.

No requirement receives acceptance evidence in the source candidate.

## Frozen source contract

The source scope is exactly 19 repository paths.

Two source paths are lifecycle-mutable and therefore deliberately excluded
from the immutable post-execution frozen set:

- `MANIFEST.sha256`;
- `audit/CURRENT_CHANGESET_VALIDATION.json`.

The generic immutable frozen-file inventory is exactly 44 paths. Eleven
checkout-sensitive historical references are separately frozen by exact Git
blob identity under DEV-0011.

The validation inventory is exactly 28 required tests.

No required test may be removed or weakened after execution.

## Golden corpus

Versioned root:

`tests/golden/ev03/v1`

The corpus includes:

- initial canonical WorldState bytes;
- post-transition canonical WorldState bytes;
- post-rollback/replay canonical WorldState bytes;
- canonical StateEvidenceEnvelope bytes;
- semantic oracle JSON;
- golden manifest.

The corpus is generated before source freeze using
`neoeng_golden_corpus_tests --emit`.

Qualification never regenerates files on disk. It only compares in-memory
reproduction against the committed corpus.

## Pre-execution amendment DEV-0011

Before any qualifying execution, precommit validation identified eleven
historical CRLF Git blobs whose raw Git-object identity is stable but whose
worktree representation is checkout-sensitive under the current
`text=auto eol=lf` policy.

`docs/records/evolution/DEV-0011.md` records the required STOP, impact
analysis and amendment.

The amended freeze model is:

- 19 exact source paths;
- 17 immutable source paths;
- 2 lifecycle-mutable source paths;
- 44 generic cross-commit frozen files;
- 11 exact historical Git-blob bindings;
- 28 required tests unchanged.

`.gitattributes` is itself included in the generic frozen inventory.

The eleven historical product/reference files are not normalized or otherwise
modified.

The already emitted golden `.bin` files and `corpus.json` are not regenerated
or modified.
## Qualification trigger

The dedicated workflow is push-triggered only for the exact branch:

`agent/cs020-ev03-deterministic-golden-corpus`

The future lifecycle binding paths are excluded from its trigger:

- `MANIFEST.sha256`;
- `audit/CURRENT_CHANGESET_VALIDATION.json`;
- `audit/validation/CS020/VALIDATION_RESULT.json`.

A successful qualifying execution must not be rerun merely to bind its result.

## CS000K historical workflow

CS020 prospectively retires automatic pull-request applicability of the
historical CS000K closure workflow by changing its trigger to
`workflow_dispatch` while preserving its validation body.

## Explicit non-effects

CS020 changes no:

- core public header;
- core runtime implementation;
- Host SDK ABI;
- canonical format version;
- numeric contract;
- property/model-based campaign;
- semantic fuzz/corruption campaign;
- EV-04 or EV-05 lifecycle state;
- release authorization.

## Acceptance boundary

A successful dedicated run is not acceptance.

Acceptance requires all 28 frozen tests PASS, exact source/run/workflow
binding, an immutable `VALIDATION_RESULT.json`, and the trusted-base ChangeSet
gate.

Release remains separate.
