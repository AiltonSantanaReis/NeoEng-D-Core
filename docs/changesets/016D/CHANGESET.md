# ChangeSet 016D — D-Lab verifier lifecycle-scope correction

State: `accepted`  
Program: `POST_1_14_1`  
Affected stage: EV-00  
Protected product baseline: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`  
Control base: `7393b32d2be3fd2e65eab6a738a0066c13848f6c`  
Accepted source commit: `3831d1080059811fe3f7e906607ef8c60917dae0`  
Qualifying run: `31615538151`

## Objective

Correct the governance-only defect that made the D-Lab verifier treat
lifecycle-mutable operational ledgers as permanently byte-frozen historical
artifacts.

## Trigger

CS017 R3 source `5c5f328afc919527775742c702e3d1a47c1490c9`
failed workflow run `31613924661`, job `94171862183`, before `stage_operation`
with:

`protected v1.2 file changed during CS016C: audit/EVOLUTION_ROADMAP.json`

No qualifying product campaign was executed.

## Accepted delta

- append-only Amendment 1.4;
- DEV-0004;
- EVREQ-074 verified;
- INV-EV-030 active;
- SCN-REGRESSION-004 passed;
- execution-policy binding for regression 004;
- lifecycle-aware D-Lab governance verifier;
- Source of Truth Index registration for the new amendment/ledgers;
- SHA-bound CS016D evidence and manifest.

## Verification strategy and result

The accepted source reexecutes the complete accepted v1.3 verifier and self-test
at exact snapshot `7393b32d2be3fd2e65eab6a738a0066c13848f6c`, preserving that historical
proof instead of weakening it.

Current-tree verification distinguishes:

- immutable accepted normative/evidence artifacts, preserved byte-for-byte;
- operational ledgers, validated semantically according to current lifecycle and
  authorization rather than frozen forever to historical bytes.

Qualifying source `3831d1080059811fe3f7e906607ef8c60917dae0`
completed workflow run `31615538151` with all governance, evolution, product
contract/assurance and manifest gates passing.

Evidence manifest:
`docs/changesets/016D/evidence/EVIDENCE_MANIFEST_ACCEPTED.json`

## Non-goals and boundary

No runtime/product source, ABI, CMake, product test, Host SDK, replay, rollback,
snapshot, serialization, hashing, claims or release behavior is modified.

CS016D does not itself authorize EV-00 execution. CS017 must be rebuilt from the
new `main` only after PR CI, merge and post-merge `main` validation succeed.
