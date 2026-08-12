# ChangeSet 016C — Action Authorization dot-path canonicalization fix

State: `accepted`  
Program: `POST_1_14_1`  
Affected stage: EV-00 (blocked until this amendment is accepted and merged)  
Protected product baseline: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`  
Control base: `855ff4563b96c4e5b2a7acac6e200fdf7f8d20d1`  
Accepted source commit: `2dfa5c0fae6a2639277bf2b6f2917428fbde4383`  
Qualifying run: `31611909872`

## Objective

Correct a governance-only defect in repository-path canonicalization used by the
Action Authorization Gate, without changing runtime/product behavior or relaxing
any existing rule.

## Trigger

`DEV-0003` records that `.github/workflows/ev00-dlab.yml`, explicitly allowlisted
for the intended CS017 stage operation, lost its leading dot because the
previous authorizer used `lstrip("./")`.

The defect was detected before the EV-00 D-Lab harness was published or a
qualifying product campaign was executed.

## Accepted delta

- append-only Amendment 1.3;
- DEV-0003;
- EVREQ-073;
- INV-EV-029;
- SCN-REGRESSION-003;
- execution-policy binding for that regression;
- conservative path canonicalization fix in the Action Authorization Gate;
- D-Lab verifier evolution that reexecutes the accepted 1.2 verifier on the
  accepted `855ff456...` snapshot and validates only the append-only 1.3 delta;
- Source of Truth Index registration for the new amendment/ledgers;
- SHA-bound CS016C evidence and manifest.

Exact path control is defined by `ACTION_SCOPE.json`.

## Explicit non-goals

CS016C does not authorize:

- changes to `src/`, `include/`, modules, apps, product CMake or product tests;
- changes to the Source of Truth document;
- edits to Plan 1.0, Amendment 1.1, Amendment 1.2 or D-Lab Standard;
- EV-00 baseline execution;
- claim expansion;
- release authorization;
- reinterpretation of historical evidence.

## Required regression

`SCN-REGRESSION-003` proves both sides of the authorization boundary:

1. legitimate `.github/...` paths retain identity and can match an explicit
   allowlist;
2. forbidden, absolute, traversal and ambiguous paths remain rejected.

SCN-REGRESSION-001 and SCN-REGRESSION-002 also remain passing.

## Acceptance evidence

Qualifying source:
`2dfa5c0fae6a2639277bf2b6f2917428fbde4383`

Qualifying workflow run:
`31611909872`

Evidence manifest:
`docs/changesets/016C/evidence/EVIDENCE_MANIFEST_ACCEPTED.json`

The qualifying source passed Action Authorization self-test, D-Lab self-test,
D-Lab verifier, required-amendments gate, evolution self-test/verifier, product
contract, product assurance, tracked-file manifest and evidence upload.

This accepted ChangeSet is still not permission to execute EV-00 from the old
CS017 branch. Merge, PR CI and post-merge `main` validation remain mandatory
before CS017 is rebuilt from the new `main` and the operational gates are
requested again.
