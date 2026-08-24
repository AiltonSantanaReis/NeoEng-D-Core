# CS000M — EV-04 maximum-scope authorization

State: **IMPLEMENTED CANDIDATE / NOT YET EXECUTED / NOT ACCEPTED**

Base:

`f3629342f9db5fca75393a217f2d559493e13001`

Branch:

`agent/cs000m-ev04-scope-authorization`

## Objective

Reconcile the active `audit/STAGE_SCOPE_MAXIMA.json` ledger with the
prospective ChangeSet-validation regime before CS021 begins.

The protected base has EV-03 accepted and EV-04 planned as CS021, but the
higher-precedence stage-scope ledger still lists EV-04 in `undefined_stages`.
Its own rule forbids preparation, start or operation of an undefined stage.

CS000M resolves only that conflict.

## Exact normative effect

CS000M performs exactly these stage-scope changes:

1. adds one `EV-04` entry with:
   - `planned_changeset = CS021`;
   - `status = defined`;
   - an explicit preparation maximum;
   - an explicit operational maximum;
   - explicit mandatory forbidden surfaces;
2. removes only `EV-04` from `undefined_stages`.

No other stage-scope row or undefined-stage classification is reinterpreted.

## EV-04 lifecycle non-effect

CS000M does **not** start EV-04.

The following remain unchanged:

- `audit/EVOLUTION_ROADMAP.json`;
- EV-04 status `not_started`;
- `current_stage = EV-03`;
- EVREQ-016..018 status `planned`;
- all EVREQ-016..018 evidence arrays;
- `release_authorized = false`.

CS021 remains unimplemented and unqualified.

## EV-04 maximum

CS021 may prospectively prepare/operate only within:

- `.github/workflows/cs021-ev04-property-model-testing-validation.yml`;
- `CMakeLists.txt`;
- `MANIFEST.sha256`;
- `audit/CURRENT_CHANGESET_VALIDATION.json`;
- `audit/EVOLUTION_REQUIREMENTS_TRACEABILITY.json`;
- `audit/EVOLUTION_ROADMAP.json`;
- `audit/validation/CS021/**`;
- `docs/changesets/021/**`;
- `scripts/verify_cs021_ev04_property_model_testing.py`;
- `tests/property_model_tests.cpp`.

This maximum deliberately permits property/model validation infrastructure
without authorizing runtime implementation changes.

## Mandatory forbidden EV-04 surfaces

EV-04 does not authorize modification of:

- `src/**`;
- `include/**`;
- `modules/**`;
- `apps/**`;
- `cmake/**`;
- governance source-of-truth documents;
- the stage-scope ledger itself;
- current ChangeSet policy/transition ledgers;
- permanent trusted ChangeSet or product-regression workflows;
- generic governance/manifest verifiers;
- EV-03 golden corpus bytes;
- `GOLDEN_CORPUS_V1.md`;
- CS000L validation/evidence history.

Any real need to cross one of those boundaries requires a new prospective
governance decision before the implementation.

## Source freeze geometry

The CS000M source candidate is exactly eight paths.

Six qualification-trigger paths are frozen:

1. `.github/workflows/cs000m-ev04-scope-authorization-validation.yml`;
2. `audit/STAGE_SCOPE_MAXIMA.json`;
3. `audit/validation/CS000M/VALIDATION_PLAN.json`;
4. `docs/changesets/000M/CHANGESET.md`;
5. `docs/records/evolution/DEV-0013.md`;
6. `scripts/verify_cs000m_ev04_scope_authorization.py`.

Two lifecycle-mutable source paths do not trigger qualification:

- `MANIFEST.sha256`;
- `audit/CURRENT_CHANGESET_VALIDATION.json`.

`audit/validation/CS000M/VALIDATION_RESULT.json` does not exist in the source
candidate. It is a future binding path only.

## Validation inventory

The source plan freezes twelve required tests.

No required test may be removed, weakened, skipped or converted to optional
after the qualifying execution begins.

## Explicit non-effects

CS000M changes no:

- product runtime;
- public header;
- ABI;
- canonical format;
- CMake build definition;
- product test;
- EV-03 golden artifact;
- EV-03 acceptance;
- EVREQ-013..015 verification;
- EV-04 lifecycle state;
- EVREQ-016..018 lifecycle state;
- release authorization.

## Acceptance boundary

A successful dedicated workflow run is qualification evidence only.

Acceptance requires exact source/run/attempt/workflow binding, all twelve
required tests PASS, immutable frozen-source verification and the trusted-base
ChangeSet gate.

Release remains separate.
