# ChangeSet 017 — EV-00 baseline laboratory certification

State: in_progress

Stage: `EV-00`

Planned ChangeSet identity: `CS017`

Control base commit: `adf763617a3788d6c711ec72cfa8a0ae38c1c1bc`

Protected historical baseline:

- release: `v1.14.1`;
- source-under-test commit: `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

## 1. Authority

CS017 consumes, without modifying for approval, the current normative stack:

1. `docs/governance/NEOENG_DCORE_SOURCE_OF_TRUTH.md`;
2. `audit/SOURCE_OF_TRUTH_INDEX.json`;
3. `docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN_V1_1_AMENDMENT.md`;
4. `docs/governance/DLAB_VALIDATION_STANDARD.md`;
5. `docs/governance/POST_1_14_1_EVOLUTION_MASTER_PLAN.md`;
6. `audit/EVOLUTION_ROADMAP.json`;
7. evolution requirements and invariants ledgers;
8. `audit/DLAB_EXECUTION_POLICY.json`;
9. historical revalidation matrix and scenario catalog.

Normative documents and rules are inputs to this ChangeSet. They are not changed to convert a failure, missing environment, missing evidence or unexpected result into approval.

## 2. Closed objective

Reconstruct and certify the accepted historical baseline `v1.14.1` in D-Lab v2 before any technical improvement to the NeoEng D-Core.

The campaign must establish a fresh, reproducible laboratory record for the exact historical source-under-test and compare current results with available historical evidence without rewriting that history.

## 3. Required EV-00 campaign surface

The qualifying campaign must cover, when applicable to the declared environment and supported baseline surface:

- checkout of the exact historical SHA;
- environment identity;
- clean/fresh build;
- supported CTest surface;
- determinism probe;
- Host SDK consumer boundary;
- replay and rollback;
- state evidence;
- support bundle;
- hashes and evidence manifest;
- comparison with available historical evidence;
- Historical Assurance Revalidation for CS001-CS015;
- applicable D-Lab scenario classes: normal, integration, degraded, adversarial, recovery, soak, combinatorial and regression;
- independent evidence verification.

## 4. Historical Assurance Revalidation

`audit/DLAB_HISTORICAL_REVALIDATION_MATRIX.json` must be evaluated entry by entry for exactly CS001-CS015.

For each entry CS017 must distinguish:

- historical integrity;
- present reproducibility;
- risk classification;
- whether rerun is required;
- rerun state;
- findings.

No missing historical commit, risk, environment or conclusion may be guessed. `unclassified` remains non-conclusive until evidence supports classification.

Any reproducible item classified `critical` or `high` must be rerun before EV-00 can be accepted.

A contradiction creates an append-only finding; it never edits the original historical evidence.

## 5. Scenario execution

The existing `audit/DLAB_SCENARIO_CATALOG.json` is consumed as normative scenario inventory. CS017 may update only execution status/evidence fields and add scenario records required by confirmed defects or by an explicitly applicable EV-00 risk.

Randomized qualifying scenarios must record seed, generator and generator version.

Simulation, loopback and logical interruption remain limited to the declared simulated/hybrid scope and never qualify a physical claim.

### Soak baseline rule for CS017

`SCN-SOAK-001` uses a planned minimum wall-clock duration of **30 minutes** for the EV-00 baseline campaign, unless an existing historical contract being revalidated requires a stricter duration. This duration is a laboratory regression observation only and does not create or expand any long-run claim.

If the environment cannot complete the planned duration, the scenario is `BLOCKED` or `FAILED` according to cause; the duration is not reduced merely to obtain PASS.

## 6. D-Lab workspace contract

Each qualifying run is created in a new workspace with logically separate:

```text
harness/
source/
build/
install/
evidence/
  raw/
  derived/
  verification/
```

Before the first qualifying command, `run-identity.json` records at minimum:

- run ID;
- stage `EV-00`;
- ChangeSet `CS017`;
- product repository;
- product SHA `e3fff973...`;
- baseline tag `v1.14.1`;
- harness SHA;
- action authorization identity;
- control branch/ref;
- environment/toolchain/configuration/dependencies;
- creation time.

Every run closes with exactly one terminal state: `PASSED`, `FAILED`, `BLOCKED` or `ABORTED`.

## 7. Non-objectives

CS017 does not authorize:

- modification of `src/` or `include/`;
- modification of the historical source-under-test;
- functional fixes discovered during EV-00;
- changes to ABI or Host SDK semantics;
- changes to replay, rollback, snapshots, serialization or canonical semantics;
- changes to public claims;
- release;
- EV-01 work;
- retroactive editing of CS001-CS015 evidence;
- weakening of tests, timeouts, corpus, assertions, sanitizers or verifiers to obtain approval;
- qualification of an environment that was not executed.

A confirmed product defect is recorded and blocks/failed the affected gate. Its remediation belongs to a separately authorized future ChangeSet.

## 8. Invariants preserved

All active `INV-EV-001..INV-EV-027` remain binding. In particular:

- baseline history is immutable;
- source-under-test is immutable during a qualifying run;
- evidence is SHA-bound;
- failures are preserved;
- evidence does not cross SHAs;
- one EV stage is active at a time;
- no operational action occurs without fail-closed authorization.

## 9. Authorized implementation surface

The exact repository allowlist is machine-readable in `docs/changesets/017/ACTION_SCOPE.json`.

The control repository may add D-Lab harness code only under `scripts/dlab/**` and may update CS017 evidence/operational ledgers within the allowlist. Product source and normative governance documents remain outside the ChangeSet write scope.

## 10. Entry gate

Entry requires all of the following:

- CS016 accepted;
- CS016A accepted;
- EV-00 current and initially `not_started`;
- planned ChangeSet is CS017;
- protected baseline remains `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`;
- `prepare_stage_changeset` decision is `AUTHORIZED`;
- CS017 ChangeSet and ACTION_SCOPE exist;
- no functional improvement is included.

The preparation commit transitions EV-00 to `in_progress`. Stage operations remain forbidden until a separate `start_stage`/`stage_operation` authorization is evaluated on the resulting exact branch state.

## 11. Exit gate

EV-00 may become `accepted` only when every applicable condition from the effective Plan 1.1 and D-Lab Standard is demonstrated, including:

- D-Lab negative self-tests pass;
- fresh baseline reconstruction succeeds in the executed environment(s);
- required baseline campaign surfaces have evidence;
- CS001-CS015 historical matrix is fully assessed;
- critical/high reproducible historical contracts are rerun;
- findings are append-only;
- mandatory scenario classes are concluded for their applicable scope;
- randomized runs have reproducible seeds;
- every attempt has a terminal state;
- manifests verify independently;
- EVREQ-001..EVREQ-004 and EVREQ-055..EVREQ-071 applicable to EV-00 are `verified` with evidence;
- no test or gate was weakened;
- no unexecuted environment is promoted;
- exact accepted source commit and evidence manifest are recorded;
- candidate, PR and post-merge governance gates pass.

Until those conditions are met, `EV-00` is not accepted and `EV-01/CS018` cannot start.
