# NeoEng D-Core — Temporal Closure Contract v1

Status: normative for baseline 1.12.0
Schema: `neoeng.dcore.temporal-closure.v1`
ChangeSet: CS012

## 1. Scope

This contract closes the product obligations for retained reconstruction,
durable temporal evidence, semantic divergence, mandatory-path
instrumentation and irreversible host effects. It does not turn an external
effect into reversible canonical state and does not claim an external trust
anchor.

The only product-promised canonical state schema in this baseline is
`WorldState v1`:

- `state.frame`;
- `body.id`;
- `body.position.x`;
- `body.position.y`;
- `body.velocity.x`;
- `body.velocity.y`.

Every field above participates in semantic diff, canonical SHA-256 and Merkle
localization. Adding a promised canonical field or schema requires extending
all three mechanisms and their tests before it can be claimed.

## 2. Durable recorder

`DurableTimelineRecorder` is an append-only recorder for an authorized
time-travel export and its evidence-chain export. A record contains:

- monotonically increasing sequence and explicit branch identifier;
- first and last frame;
- SHA-256 of the previous record;
- SHA-256 and exact bytes of the timeline and evidence payloads;
- SHA-256 of the complete encoded record.

The recorder must recover and verify every existing record before accepting an
append. It writes a temporary record, flushes it, atomically renames it and
flushes the final file. Missing sequence, altered bytes, broken chain, invalid
input and size overflow are rejected fail-closed. A bounded in-memory
`TimeTravelDebugger` remains valid for live inspection; durability is supplied
only by the explicit recorder.

External anchoring of a verified recorder head is a deployment responsibility.
The product does not silently claim a public timestamp, HSM, transparency log
or third-party trust anchor.

## 3. Branch and rollback semantics

Temporal branches are explicit through their nonzero branch identifier and
evidence parent metadata. Rollback/truncation may discard canonical future
state and prepared, uncommitted external-effect intents.

Rollback cannot undo an already committed irreversible external effect. If a
rollback crosses such an effect, the ledger reports
`committed_effect_crossed_rollback`; the host must compensate it through its
declared handler or surface the unresolved boundary. The runtime must never
represent that condition as an ordinary successful undo.

## 4. External-effect protocol

Every external effect is first prepared with:

- a stable idempotency key;
- effect kind;
- canonical frame;
- payload SHA-256;
- explicit compensation support.

The same idempotency key with different intent metadata is rejected. The host
executor is invoked only when the intent frame is inside the explicit
confirmed horizon. Repeated prepare, commit and compensation calls are
idempotent. Retryable and permanent executor failures do not promote state to
committed or compensated.

The host owns the actual side effect and its durable idempotency store. The
D-Core owns the reference state machine, confirmation gate, conflict
detection, rollback reconciliation and trace contract.

## 5. Mandatory observability

When a caller enables tracing on a supported path, a correlated budget sample
is emitted automatically for:

1. authenticated input ingest;
2. state advance;
3. rollback restore;
4. ECS maintenance;
5. evidence checkpoint;
6. support-bundle export;
7. divergence localization;
8. durable recorder append;
9. external-effect commit.

Wall-clock measurements are diagnostic only. They never enter canonical
simulation decisions.

## 6. Non-claims

This contract does not claim:

- unbounded in-memory history;
- rollback of an irreversible committed host effect;
- exactly-once delivery without a conforming host idempotency store;
- a product-supplied external trust anchor;
- ARM64 equivalence without an ARM64 campaign;
- performance independent of machine, storage, thermal state or workload.

Evidence describes only the recorded source, binaries, host and
configuration. Machines with lower or higher capability may produce better or
worse timing and capacity observations; neither direction is a universal
rule.
