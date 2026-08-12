# CS016D — Test Status

State: accepted

Control base: `7393b32d2be3fd2e65eab6a738a0066c13848f6c`  
Triggering CS017 source: `5c5f328afc919527775742c702e3d1a47c1490c9`  
Triggering workflow run: `31613924661`  
Accepted source commit: `3831d1080059811fe3f7e906607ef8c60917dae0`  
Qualifying workflow run: `31615538151`  
Qualifying job: `94177285913`

## Preserved failure

The triggering CS017 R3 run failed before EV-00 stage operation or product
execution:

`D-LAB GOVERNANCE SELF-TEST: REJECT`

`protected v1.2 file changed during CS016C: audit/EVOLUTION_ROADMAP.json`

That failure remains preserved by `DEV-0004` and is not reclassified as product
evidence.

## Qualifying correction result

The accepted source completed the `Evolution governance` push workflow with
conclusion `success`.

Passed gates:

- D-Lab action authorization self-test;
- D-Lab governance self-test;
- D-Lab governance verifier;
- Required evolution amendments gate;
- evolution verifier self-test;
- evolution plan verifier;
- product contract verifier;
- product assurance verifier;
- tracked-file manifest verifier;
- governance evidence upload.

The verifier also reexecuted the accepted v1.3 verifier and self-test on exact
snapshot `7393b32d2be3fd2e65eab6a738a0066c13848f6c` and preserved accepted
normative/evidence artifacts while validating operational ledgers by lifecycle.

`EVREQ-074` is verified.
`SCN-REGRESSION-004` is passed.
`INV-EV-030` remains active.

Evidence manifest:
`docs/changesets/016D/evidence/EVIDENCE_MANIFEST_ACCEPTED.json`

## Scope boundary

No EV-00 qualifying product campaign is claimed.
No runtime/ABI/canonical-data claim is expanded.
`release_authorized` remains `false`.

CS017 may only be rebuilt after CS016D passes accepted-state CI, PR CI, merge and
post-merge `main` CI.
