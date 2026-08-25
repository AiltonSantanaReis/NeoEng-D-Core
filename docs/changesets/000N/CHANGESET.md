# CS000N — EV-04 closure-scope authorization

State: **IMPLEMENTED CANDIDATE / NOT YET EXECUTED / NOT ACCEPTED**

Base:

`d75fb80e7aa304576060339c31ff87fdb9dae206`

Branch:

`agent/cs000n-ev04-closure-scope-authorization`

## Objective

Extend the already-defined EV-04 stage-scope maximum only enough to permit a
future administrative ledger-closure ChangeSet after accepted CS021 protected
integration.

CS000N does not close EV-04 and does not start EV-05.

## Accepted technical predecessor

The protected base already contains accepted CS021 integration:

- CS021 qualified source:
  `0c4b66b4474c128583f2ce1920aef848bebc3db8`;
- source tree:
  `c8c6cb2c38c5b09480629992da24a85528877e4c`;
- qualifying run:
  `32794089676`, attempt `1`;
- accepted binding:
  `8a44bc57bea3f5053746a3648f7e0b26bf8f4bb3`;
- binding tree:
  `52c8b90c69806008322a4fc5043aff60c7125040`;
- pull request:
  `#59`;
- candidate diagnostic:
  `32795606536`;
- trusted-base gate:
  `32795606519`;
- PR product regression:
  `32795606513`;
- protected merge:
  `d75fb80e7aa304576060339c31ff87fdb9dae206`;
- post-merge ChangeSet validation:
  `32796180635`;
- post-merge product regression:
  `32796180646`.

CS021 remains the technical ChangeSet planned for EV-04.

## Preserved protected-base MANIFEST anomaly

The first local CS000N source-preparation script, SHA-256
`5c5969ddc6ce9799829344f310125ebf4750a766d32d9f1a73562f224d718d80`,
aborted before source materialization, staging, commit or push because the
protected base `MANIFEST.sha256` did not reproduce from the exact protected
merge worktree.

Read-only adjudication of protected merge
`d75fb80e7aa304576060339c31ff87fdb9dae206` established:

- tracked files expected by the manifest: `1103`;
- manifest rows: `1103`;
- missing rows: `0`;
- extra rows: `0`;
- raw worktree/index byte mismatches: `0`;
- manifest hash mismatches: exactly `2`.

The exact stale rows are:

1. `audit/CURRENT_CHANGESET_VALIDATION.json`
   - protected manifest: `b1098370c6f36a6b8d9a3ede6c9e0426a616bdf4734dfb81d9c8f34dd48e157b`;
   - protected blob bytes: `9d25d9170e1e3251a66525ea1c0141847318c52c3d9c51a0c124311c373f4214`;
2. `audit/validation/CS021/VALIDATION_RESULT.json`
   - protected manifest: `ebfdf94cf26fe71e32d44388dfbc3bc93aafcb51c98b69096558107e51baa24e`;
   - protected blob bytes: `28a96b7dd7a871508764a2db41591596443d0c856d506442df64a5ac74188921`.

CS000N requires the verifier to reconstruct CRLF from each protected LF blob
and prove that the reconstructed SHA-256 equals the stale manifest row. This
locates the defect at the lifecycle-binding manifest-generation boundary: the
manifest recorded pre-index CRLF bytes while Git committed normalized LF blobs.

The historical CS021 descriptor/result bytes are not rewritten. CS000N only
regenerates the current repository manifest prospectively after its own exact
source paths are staged. The anomaly and the failed R1 preflight remain
explicitly preserved here.

## Exact normative effect

CS000N preserves the current EV-04 scope row and appends exactly five new
patterns to both `preparation_allowed_patterns` and `allowed_patterns`:

1. `.github/workflows/cs000o-ev04-ledger-closure-validation.yml`;
2. `audit/validation/CS000O/**`;
3. `docs/changesets/000O/**`;
4. `docs/records/evolution/DEV-0015.md`;
5. `scripts/verify_cs000o_ev04_ledger_closure.py`.

These paths are reserved for the future administrative closure ChangeSet
`CS000O`.

CS000N does not remove, reorder or reinterpret any existing CS021-authorized
pattern, does not alter `planned_changeset = CS021`, does not alter
`status = defined`, does not alter `mandatory_forbidden_patterns`, and does not
change `undefined_stages`.

## Lifecycle non-effect

CS000N performs no EV-04 closure.

The following must remain unchanged from the protected base:

- `current_stage = EV-04`;
- `EV-04.status = in_progress`;
- `EV-04.accepted_commit = null`;
- `EV-04.evidence_manifest = null`;
- `EV-04.decision_record = null`;
- `EVREQ-016..018 = in_progress`;
- EVREQ-016..018 evidence arrays remain empty;
- `EV-05 = not_started`;
- `EVREQ-019..021 = planned`;
- `release_authorized = false`.

## Future CS000O boundary

CS000O remains dormant until CS000N is:

1. qualified on its exact source SHA;
2. bound to exact successful run evidence;
3. `VALIDATED / ACCEPTED`;
4. accepted by the trusted-base PR gate;
5. protected-integrated; and
6. post-merge verified.

Only after that protected integration may CS000O be prepared.

The future closure may use the existing EV-04 lifecycle paths already present in
the maximum (`MANIFEST.sha256`, current ChangeSet descriptor, evolution roadmap
and requirements ledger) together with the five additional administrative
patterns authorized by CS000N.

## Source freeze geometry

The CS000N source candidate is exactly eight paths.

Six qualification-trigger paths are frozen:

1. `.github/workflows/cs000n-ev04-closure-scope-authorization-validation.yml`;
2. `audit/STAGE_SCOPE_MAXIMA.json`;
3. `audit/validation/CS000N/VALIDATION_PLAN.json`;
4. `docs/changesets/000N/CHANGESET.md`;
5. `docs/records/evolution/DEV-0014.md`;
6. `scripts/verify_cs000n_ev04_closure_scope_authorization.py`.

Two lifecycle-mutable source paths do not trigger qualification:

- `MANIFEST.sha256`;
- `audit/CURRENT_CHANGESET_VALIDATION.json`.

`audit/validation/CS000N/VALIDATION_RESULT.json` does not exist in the source candidate. It is a future
result-binding path only.

The future accepted binding delta is therefore exactly three lifecycle paths:

- `MANIFEST.sha256`;
- `audit/CURRENT_CHANGESET_VALIDATION.json`;
- `audit/validation/CS000N/VALIDATION_RESULT.json`.

The final PR net scope is therefore exactly nine paths.

## Validation inventory

The source plan freezes thirteen required tests, including an explicit required test for the preserved CS021 manifest anomaly and prospective manifest repair.

No required test may be removed, weakened, skipped or converted to optional
after qualifying execution begins.

## Explicit non-effects

CS000N changes no:

- product runtime;
- public header;
- ABI;
- canonical format;
- CMake build definition;
- product test;
- property/model implementation;
- accepted CS021 validation result;
- accepted CS021 technical source or binding;
- EV-03 golden artifact;
- EV-04 lifecycle state;
- EVREQ-016..018 lifecycle state or evidence;
- EV-05 lifecycle state;
- public product claim;
- release authorization;
- historical evidence.

## Acceptance boundary

A successful dedicated workflow run is qualification evidence only.

Acceptance requires exact source/run/attempt/workflow binding, all thirteen
required tests PASS, immutable frozen-source verification and the trusted-base
ChangeSet gate.

Release remains separate.
