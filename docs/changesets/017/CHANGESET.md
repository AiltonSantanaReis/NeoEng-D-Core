# ChangeSet 017 — EV-00 baseline laboratory certification (R2)

State: in_progress

Stage: `EV-00`

ChangeSet: `CS017`

Control base commit: `855ff4563b96c4e5b2a7acac6e200fdf7f8d20d1`

Protected historical baseline:

- release tag: `v1.14.1`;
- source-under-test: `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

## 1. Normative authority consumed read-only

This ChangeSet consumes, without modifying for approval:

1. `docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`;
2. `audit/SOURCE_OF_TRUTH_INDEX.json`;
3. `docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_2_AMENDMENT.md`;
4. `docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_1_AMENDMENT.md`;
5. `docs/governance/DLAB_VALIDATION_STANDARD.md`;
6. `docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md`;
7. `audit/EVOLUTION_ROADMAP.json`;
8. `audit/EVOLUTION_AMENDMENTS.json`;
9. evolution requirements/invariants ledgers;
10. `audit/DLAB_EXECUTION_POLICY.json`;
11. historical revalidation matrix and scenario catalog.

Existing normative documents are inputs. A failure, missing environment, missing evidence or unexpected result is never converted into PASS by changing those documents or weakening a gate.

## 2. Predecessor attempt

The previous branch `agent/cs017-ev00-baseline-certification` and source
`bfafa432ad4dc7c402753293da080fc6d920c8ce` are retained only as historical
failure evidence. Run `31594048822` failed before any qualifying product campaign
because of the governance self-test defect recorded in `DEV-0002` and corrected
by accepted CS016B.

This R2 ChangeSet is reconstructed from the new official `main`; it does not
inherit the predecessor branch lifecycle or evidence as qualifying EV-00 data.

## 3. Closed objective

Reconstruct and certify the accepted historical baseline `v1.14.1` in D-Lab v2
before any technical improvement to NeoEng D-Core.

The campaign must create fresh, SHA-bound evidence for the exact historical
source-under-test and compare it against available historical CS001-CS015
records without rewriting them.

## 4. Required campaign surface

Where applicable to the declared environment and supported baseline surface,
CS017 must execute and preserve evidence for:

- exact historical checkout and clean source identity;
- environment/toolchain/dependency identity;
- fresh configure/build/install;
- supported CTest surface;
- determinism probe;
- Host SDK consumer boundary;
- replay and rollback;
- state evidence;
- support bundle;
- SHA-256 evidence manifest and independent verification;
- Historical Assurance Revalidation for exactly CS001-CS015;
- scenario classes normal, integration, degraded, adversarial, recovery, soak,
  combinatorial and regression;
- comparison with historical claims/evidence in the exact scope available.

## 5. Historical Assurance Revalidation

`audit/DLAB_HISTORICAL_REVALIDATION_MATRIX.json` is assessed entry-by-entry for
exactly CS001-CS015.

For every entry CS017 distinguishes:

- historical integrity;
- current reproducibility;
- risk classification;
- rerun requirement;
- rerun state;
- findings.

Unknown values are not guessed. `unclassified`, missing environment or missing
evidence remain non-conclusive until evidence exists.

Every reproducible `critical` or `high` item must be rerun before EV-00 can be
accepted. Contradictions create append-only findings and never alter the
historical evidence.

## 6. Scenario contract

The existing `audit/DLAB_SCENARIO_CATALOG.json` remains the normative inventory.
CS017 may update only execution/evidence fields and add a scenario only when a
confirmed defect or applicable EV-00 risk requires it.

Randomized qualifying scenarios record seed, generator and generator version.
Simulation/loopback/logical interruption remain limited to simulated/hybrid
scope and never qualify physical claims.

### Soak execution contract

For `SCN-SOAK-001`, CS017 sets a planned minimum wall-clock duration of
**30 minutes** on the executed Linux D-Lab environment, unless a historical
contract being revalidated requires a stricter duration.

This is a CS017 laboratory execution parameter, not a change to the Master Plan
or D-Lab Standard and not a general long-run claim. The planned duration will
not be reduced merely to obtain PASS. If the environment cannot complete it,
the scenario ends `BLOCKED` or `FAILED` according to cause.

## 7. D-Lab workspace contract

Every qualifying run is created as a new logical workspace:

```text
<run>/
├── run-identity.json
├── harness/
├── source/
├── build/
├── install/
└── evidence/
    ├── raw/
    ├── derived/
    ├── verification/
    ├── terminal-state.json
    └── evidence-manifest.json
```

Before the first qualifying command, `run-identity.json` records at minimum:
run ID, `EV-00`, `CS017`, repository, product SHA, baseline tag, harness SHA,
action authorization identity, control ref/SHA, environment, configuration,
toolchain, dependencies and creation timestamp.

Every attempt closes in exactly one terminal state: `PASSED`, `FAILED`,
`BLOCKED` or `ABORTED`.

## 8. Non-objectives

CS017 does not authorize:

- changes to `src/`, `include/` or the historical source-under-test;
- product bug fixes discovered during EV-00;
- ABI/Host SDK semantic changes;
- replay/rollback/snapshot/serialization/canonical semantic changes;
- public claim changes;
- release;
- EV-01 work;
- retroactive edits to CS001-CS015 evidence;
- weakening tests, timeouts, corpus, assertions, sanitizers or verifiers;
- qualification of an unexecuted environment.

A confirmed product defect is preserved as a finding and blocks/fails the
affected gate. Remediation requires a separately authorized future ChangeSet.

## 9. Active invariants

All `INV-EV-001..INV-EV-028` remain binding, including immutable historical
baseline, SHA-bound evidence, no evidence crossing SHAs, fail-closed action
authorization, append-only failures and lifecycle-independent governance
self-tests.

## 10. Authorized control-repository write surface

The machine-readable write scope is `docs/changesets/017/ACTION_SCOPE.json`.

D-Lab harness code may be added only under `scripts/dlab/**`; a dedicated
GitHub Actions harness may be added only at `.github/workflows/ev00-dlab.yml`.
Neither location is product runtime. The historical source-under-test remains
immutable and is checked out into a fresh validation workspace by the harness.

## 11. Entry gate

Entry requires:

- CS016 accepted;
- CS016A accepted;
- CS016B accepted;
- EV-00 current and `not_started` on official base;
- planned ChangeSet `CS017`;
- baseline `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`;
- D-Lab/evolution/product governance verifiers passing on official base;
- `prepare_stage_changeset` returning `AUTHORIZED` on official base;
- no functional improvement included.

The preparation commit creates CS017/ACTION_SCOPE and transitions EV-00 to
`in_progress`. No qualifying product command is authorized by that preparation
alone. `start_stage` and `stage_operation` are evaluated again on the exact new
branch state.

## 12. Exit gate

EV-00 may become `accepted` only when the effective Plan 1.2 and D-Lab Standard
are satisfied, including:

- D-Lab negative self-tests pass;
- source/build/install/evidence isolation is proven;
- fresh v1.14.1 reconstruction succeeds for each executed environment;
- required baseline surfaces have raw and derived evidence;
- CS001-CS015 matrix is fully assessed;
- mandatory critical/high reproducible contracts are rerun;
- historical findings remain append-only;
- mandatory scenario classes conclude in their applicable scope;
- randomized runs have reproducible seeds;
- every run has exactly one terminal state;
- manifests verify independently;
- EVREQ-001..EVREQ-004 and EVREQ-055..EVREQ-071 applicable to EV-00 are
  `verified` with evidence; EVREQ-072 remains verified;
- no test/gate was weakened;
- no unexecuted environment is promoted;
- exact accepted source commit and evidence manifest are recorded;
- candidate, PR and post-merge governance gates pass.

Until then EV-00 is not accepted and EV-01/CS018 cannot start.
