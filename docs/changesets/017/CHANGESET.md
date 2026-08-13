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

## Preserved R5 preparation failures

These failures occurred before the final qualifying `VALIDATION_PLAN` freeze and before any `-Mode Qualify` execution. They are preparation evidence only and cannot qualify EV-00.

1. Local preflight on the physical Windows control clone initially stopped with `Required tool not found in PATH: link.exe`. Root cause: the normal PowerShell session had not loaded the installed Visual Studio C++ developer environment. The same machine exposed `link.exe`, `rc.exe`, `clang-cl`, CMake, CTest and Ninja after `Launch-VsDevShell.ps1`; no tool requirement was removed.
2. The next local preflight stopped at `Cannot overwrite variable Host because it is read-only or constant.` Root cause: the harness assigned to `$host`, which collides case-insensitively with PowerShell's automatic read-only `$Host`. The harness was corrected by renaming the local value to `$hostAssessment`, and the non-qualifying CI preflight gained a regression that rejects direct `$Host/$host` assignment.
3. After the first provisional plan/descriptor commits, non-qualifying preflight run `31653982113` rejected `audit/CURRENT_CHANGESET_VALIDATION.json` as outside its preparation allowlist. Root cause: that allowlist predated the simplified ChangeSet validation paths. The allowlist was extended only for `audit/validation/CS017/*` and `audit/CURRENT_CHANGESET_VALIDATION.json`; product/runtime paths remain forbidden. The provisional plan is not a qualifying freeze because no `-Mode Qualify` execution occurred before this correction.

None of these failures is reclassified as PASS. The subsequent physical-PC preflight passed at harness head `84eea8f47925259f227c187d711139a522c21556`, proving a clean control tree, protected product commit presence, physical Windows host, and required tool availability. That preflight remains non-qualifying.

## Current state

Preparation complete through physical-PC preflight. No qualifying EV-00 local D-Lab command has been executed in R5 yet. Release remains unauthorized.
