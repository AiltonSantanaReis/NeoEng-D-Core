# CS017 — Test Status

State: in_progress

Program: `POST_1_14_1`  
Stage: `EV-00`  
Control base: `de55e0882c6400a0409b5cf881c6ee796a975cdf`  
Source under test: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## Current state

CS017 R4 is being prepared from the post-CS016D `main` as required by Amendment 1.4.

Preparation authorization was executed by GitHub Actions run `31617502522`, job `94183881160`, on exact source `de55e0882c6400a0409b5cf881c6ee796a975cdf`:

`prepare_stage_changeset(CS017, EV-00) => AUTHORIZED`

Required amendments at that decision:

- CS016A: accepted;
- CS016B: accepted;
- CS016C: accepted;
- CS016D: accepted.

## Qualifying evidence state

No EV-00 qualifying D-Lab campaign has been executed by R4 yet. Therefore R4 does not yet claim baseline build, CTest, determinism, Host SDK, replay/rollback, state evidence, support bundle, historical CS001-CS015 revalidation, evidence-manifest completion or EV-00 acceptance.

R1/R2/R3 are non-qualifying for R4.

## Next gates

1. governance validation of this start state;
2. tracked-file manifest reconciliation and stable validation;
3. exact proof that re-preparation is `REJECT` and `start_stage(CS017)` is `AUTHORIZED`;
4. explicit `stage_operation` authorization for the exact D-Lab harness/workflow paths;
5. only then, harness publication/execution.

`release_authorized` remains `false`.
