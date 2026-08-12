# ChangeSet 016C — Action Authorization dot-path canonicalization fix

State: `in_progress`  
Program: `POST_1_14_1`  
Affected stage: EV-00 (blocked until this amendment is accepted)  
Protected product baseline: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`  
Control base: `855ff4563b96c4e5b2a7acac6e200fdf7f8d20d1`

## Objective

Correct a governance-only defect in repository-path canonicalization used by the
Action Authorization Gate, without changing runtime/product behavior or relaxing
any existing rule.

## Trigger

`DEV-0003` records that `.github/workflows/ev00-dlab.yml`, explicitly allowlisted
for the intended CS017 stage operation, loses its leading dot because the
current authorizer uses `lstrip("./")`.

The defect was detected before the EV-00 D-Lab harness was published or a
qualifying product campaign was executed.

## Authorized delta

- append-only Amendment 1.3;
- DEV-0003;
- EVREQ-073;
- INV-EV-029;
- SCN-REGRESSION-003;
- execution-policy binding for that regression;
- conservative path canonicalization fix in the Action Authorization Gate;
- D-Lab verifier evolution that reexecutes the accepted 1.2 verifier on the
  accepted `855ff456...` snapshot and validates only the current append-only delta;
- Source of Truth Index registration for the new amendment/ledgers;
- CS016C evidence and manifests.

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

`SCN-REGRESSION-003` must prove both sides of the authorization boundary:

1. legitimate `.github/...` paths retain identity and can match an explicit
   allowlist;
2. forbidden, absolute, traversal and ambiguous paths remain rejected.

SCN-REGRESSION-001 and SCN-REGRESSION-002 must continue to pass.

## Acceptance conditions

CS016C may be accepted only when:

1. Action Authorization self-test passes with regressions 001/002/003;
2. D-Lab governance self-test passes;
3. D-Lab governance verifier passes and reexecutes the accepted v1.2 gate;
4. evolution-plan verifier and self-test pass;
5. product contract and assurance gates pass unchanged;
6. tracked-file manifest matches;
7. a candidate source SHA has a successful GitHub Actions run;
8. qualifying evidence is SHA-bound and independently manifest-verified;
9. EVREQ-073 is `verified` only after qualifying evidence exists;
10. SCN-REGRESSION-003 is `passed` only after qualifying evidence exists;
11. accepted-state CI passes on the promoted state;
12. pull-request CI passes on the same final head;
13. post-merge `main` CI passes before CS017 is rebuilt.

A failure at any condition remains failure/blocker evidence; the condition is not
edited to obtain approval.
