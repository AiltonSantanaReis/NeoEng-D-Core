# ChangeSet 016D — D-Lab verifier lifecycle-scope correction

State: `in_progress`  
Program: `POST_1_14_1`  
Affected stage: EV-00 (blocked until this amendment is accepted)  
Protected product baseline: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`  
Control base: `7393b32d2be3fd2e65eab6a738a0066c13848f6c`

## Objective

Correct a governance-only defect that made the D-Lab verifier treat lifecycle-
mutable operational ledgers as permanently byte-frozen historical artifacts.

## Trigger

CS017 R3 control commit `5c5f328afc919527775742c702e3d1a47c1490c9`
failed workflow run `31613924661`, job `94171862183`, before `stage_operation`
with:

`protected v1.2 file changed during CS016C: audit/EVOLUTION_ROADMAP.json`

The Action Authorization self-test passed before that failure. No qualifying
product campaign was executed.

## Authorized delta

- append-only Amendment 1.4;
- DEV-0004;
- EVREQ-074;
- INV-EV-030;
- SCN-REGRESSION-004;
- execution-policy binding for regression 004;
- lifecycle-aware D-Lab governance verifier;
- Source of Truth Index registration for the new amendment/ledgers;
- SHA-bound CS016D evidence and manifests.

## Verification strategy

The new verifier must not replace prior proof. It must reexecute the complete
accepted v1.3 verifier and self-test at exact accepted snapshot
`7393b32d2be3fd2e65eab6a738a0066c13848f6c`.

Current-tree validation then distinguishes:

- immutable accepted normative/evidence artifacts, which must remain unchanged;
- operational ledgers, which are validated semantically according to current
  lifecycle and authorization rather than compared forever to historical bytes.

## Non-goals

No runtime/product source, ABI, CMake, product test, Host SDK, replay, rollback,
snapshot, serialization, hashing, claims or release behavior is modified.

CS016D does not authorize EV-00 execution.

## Acceptance conditions

CS016D can be accepted only after:

1. previous v1.3 D-Lab verifier + self-test replay successfully at `7393b32d...`;
2. current D-Lab self-test and verifier pass;
3. SCN-REGRESSION-004 passes;
4. EVREQ-074 is verified with SHA-bound evidence;
5. INV-EV-030 remains active;
6. authorizer regressions 001/002/003 remain passing;
7. evolution verifier/self-test pass;
8. product contract and assurance pass unchanged;
9. tracked-file manifest passes;
10. candidate source has successful CI;
11. accepted-state CI passes;
12. PR CI passes on the same final head;
13. post-merge `main` CI passes.

Failure remains blocker evidence; criteria are not altered to obtain PASS.
