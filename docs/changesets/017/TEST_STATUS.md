# CS017 — Test Status

State: in_progress

Program: `POST_1_14_1`  
Stage: `EV-00`  
Control base: `7393b32d2be3fd2e65eab6a738a0066c13848f6c`  
Source under test: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## Current state

CS017 R3 is being prepared from the post-CS016C `main` as required by Amendment
1.3.

Preparation authorization was executed by GitHub Actions run `31613040252`, job
`94168915002`, on exact source `7393b32d2be3fd2e65eab6a738a0066c13848f6c`:

`prepare_stage_changeset(CS017, EV-00) => AUTHORIZED`

Required amendments at that decision:

- CS016A: accepted;
- CS016B: accepted;
- CS016C: accepted.

## Qualifying evidence state

No EV-00 qualifying D-Lab campaign has been executed by this R3 attempt yet.

The following are therefore **not yet claimed** for R3:

- baseline build reproduced;
- supported CTest surface reproduced;
- determinism probe reproduced;
- Host SDK baseline reproduced;
- replay/rollback baseline reproduced;
- state evidence baseline reproduced;
- support bundle baseline reproduced;
- CS001-CS015 historical revalidation completed;
- evidence manifest completed;
- EV-00 accepted.

The prior R1/R2 attempts are non-qualifying for this R3 campaign.

## Next gates

1. governance validation of this start state;
2. tracked-file manifest reconciliation and stable validation;
3. `start_stage(CS017)` authorization;
4. `stage_operation` authorization for exact D-Lab harness/workflow paths;
5. only then, creation/execution of the qualifying harness.

`release_authorized` remains `false`.
