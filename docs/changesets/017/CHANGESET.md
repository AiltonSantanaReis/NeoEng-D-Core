# ChangeSet 017 — EV-00 baseline laboratory certification

State: `PLANNED`  
Program: `POST_1_14_1`  
Stage: `EV-00`  
Control base: `d092ac56290d76dddf51982549a98234f038f3ee`  
Source under test: `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`

## Objective

Reproduce, characterize and record the immutable accepted `v1.14.1` baseline in the authorized local D-Lab before any post-baseline behavior change.

CS017 is a certification ChangeSet. It does not improve or modify NeoEng D-Core product behavior.

## Execution boundary

The qualifying D-Lab campaign MUST execute on the maintainer's physical Windows PC. GitHub Actions may verify harness integrity, evidence structure, hashes, exact run bindings and the committed evidence package, but GitHub-hosted runners do not substitute for the local D-Lab campaign.

Until that local campaign is actually executed, the following are not approval states: CI green, harness preflight success, `NOT_TESTED`, missing evidence, partial output or a historical run from R1-R4.

## Required EV-00 surface

The frozen validation plan will require, at minimum:

- exact checkout of historical product SHA `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`;
- local Windows environment identity;
- fresh isolated workspace and fresh build;
- supported `CTest` surface;
- determinism probe with repeat comparison;
- Host SDK boundary;
- replay/rollback surface;
- state-evidence probe;
- support-bundle probe;
- SHA-256 evidence manifest and independent verification;
- comparison against available accepted historical evidence;
- historical assurance review for CS001-CS015;
- explicit terminal state and preservation of every failed/blocked attempt.

## Prior attempts

Historical branches R1-R4 remain preserved and non-qualifying for this restart. No build, run or evidence from those attempts may be reused as CS017-R5 qualification.

## Non-goals

CS017 does not authorize:

- changes under product/runtime source or public ABI;
- semantic changes to replay, rollback, snapshots, serialization or hashing;
- EV-01 hardening work;
- release authorization;
- claims for hardware, operating systems, architectures or toolchains not executed;
- weakening/removal of tests in response to a failure.

## Lifecycle under the simplified validation regime

`PLANNED -> IMPLEMENTED -> VALIDATED -> ACCEPTED`

`CI green` is never equivalent to `VALIDATED` or `ACCEPTED`.

The required-test inventory and harness/verifier files must be frozen in `audit/validation/CS017/VALIDATION_PLAN.json` before the first qualifying local command. The qualifying GitHub result must later bind the closure candidate to `source_sha + run_id + run_attempt + workflow_path`, while the local evidence package independently binds the physical D-Lab run to the historical product SHA and exact harness SHA.

## Current state

Preparation only. No qualifying EV-00 local D-Lab command has been executed in R5 yet. Release remains unauthorized.