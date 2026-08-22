# ChangeSet 000H — Evolution authorizer self-test lifecycle correction

State: `IMPLEMENTED CANDIDATE / VALIDATION PENDING / EV-00 NOT STARTED`

Base: `ed3661ee3aad366d639d1a3de5934e53c507c135`  
Triggering non-qualifying CS017 R8 source: `5697828d499051f9fc397c99dd6f43cc9a021318`  
Triggering run: `32546849264` / attempt `1`

## Objective

Correct only the lifecycle dependence of the canonical `scripts/authorize_evolution_action.py` self-test that was revealed by the CS017 R8 preparation preflight.

The authorization logic introduced by accepted CS000F remains unchanged. CS000H changes the self-test so its synthetic supersession assertions use explicit `not_started` and `in_progress` EV-00 fixtures instead of inheriting the live roadmap lifecycle.

## Triggering failure preserved

CS017 R8 preparation run `32546849264` failed at:

`Canonical evolution authorizer self-test`

Observed message:

`authoritative CS016E supersession did not resolve EV-00 preflight`

The preceding preparation-state/ACTION_SCOPE check passed. The later `start_stage`, harness-publication authorization and combined scope steps were skipped.

This was a non-qualifying preparation failure. No D-Core product campaign, physical Windows qualification, stage operation, harness publication, release or qualification occurred.

The machine-readable record is:

`audit/validation/CS000H/R8_PREPARATION_FAILURE.json`

R8 is not rerun or reclassified.

## Prospective correction

The pre-CS000H canonical wrapper is preserved byte-for-byte as:

`scripts/authorize_evolution_action_cs000f.py`

Expected preserved Git blob:

`2b2969d0a173fbd71bb1ec12f739998f02278715`

The corrected canonical wrapper has expected Git blob:

`0316e2659e3284cbf2a45f816c0f2e958bd3674e`

The correction:

- adds an explicit EV-00 roadmap fixture helper for self-test use;
- tests valid CS016E supersession with EV-00 explicitly `not_started`;
- tests lifecycle rejection with EV-00 explicitly `in_progress`;
- requires the in-progress rejection to come from the lifecycle rule, not from a reintroduced CS016E amendment blocker;
- preserves tampered-transition, unbound-supersession and generic-superseded negative tests.

`effective_amendments()` and `authorize_state()` are unchanged. Runtime authorization decisions are compared between the preserved CS000F wrapper and the corrected canonical wrapper during validation.

## Frozen campaign

Plan:

`audit/validation/CS000H/VALIDATION_PLAN.json`

Plan SHA-256:

`274fa5aaca2540e49e7b9d57fd09b42d5f88664be7679de4f087a04938fffa46`

Required tests:

1. CS000H verifier negative self-test;
2. prior wrapper byte preservation and semantic isolation;
3. preserved R8 failure plus live GitHub run/job/step binding;
4. lifecycle-independent authorizer behavior;
5. bounded future workflow applicability;
6. administrative-only ChangeSet scope;
7. no EV-00/product effects;
8. generic ChangeSet plan validation.

## Applicability boundary

The CS000H workflow is `pull_request` path-scoped only to the CS000H implementation files. It intentionally excludes:

- `audit/CURRENT_CHANGESET_VALIDATION.json`;
- `audit/validation/CS000H/VALIDATION_RESULT.json`;
- `docs/changesets/017/**`;
- `audit/EVOLUTION_ROADMAP.json`;
- `.github/workflows/ev00-dlab.yml`.

Therefore it may rerun on the CS000H closure because GitHub evaluates paths against the whole PR diff, but it is not a universal gate for future CS017/EV-00 work.

## Non-effects

CS000H does not:

- start EV-00 or CS017;
- change runtime, ABI, build definitions or product tests;
- change D-Core product behavior;
- change the persistent CS016E classification (`superseded`, unaccepted);
- authorize a release;
- create qualification;
- rewrite the failed CS017 R8 history.

Acceptance of CS000H, if achieved, authorizes only this administrative correction.
