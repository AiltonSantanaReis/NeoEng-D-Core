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

Until that local campaign is actually executed and independently verified, the following are not approval states: CI green, harness preflight success, `NOT_TESTED`, missing evidence, partial output or a historical run from another revision.

## Required EV-00 surface

The validation plan requires, at minimum:

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

Historical branches R1-R4 remain preserved and non-qualifying for this restart. No build, run or evidence from those attempts may be reused as qualification for a later revision.

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

The required-test inventory and harness/verifier files must be frozen in `audit/validation/CS017/VALIDATION_PLAN.json` before the first qualifying local command of each revision. The qualifying GitHub result must later bind the closure candidate to `source_sha + run_id + run_attempt + workflow_path`, while the local evidence package independently binds the physical D-Lab run to the historical product SHA and exact harness SHA.

## Preserved R5 preparation failures

These failures occurred before the final R5 qualifying `VALIDATION_PLAN` freeze and before any R5 `-Mode Qualify` execution. They are preparation evidence only and cannot qualify EV-00.

1. Local preflight on the physical Windows control clone initially stopped with `Required tool not found in PATH: link.exe`. Root cause: the normal PowerShell session had not loaded the installed Visual Studio C++ developer environment. The same machine exposed `link.exe`, `rc.exe`, `clang-cl`, CMake, CTest and Ninja after `Launch-VsDevShell.ps1`; no tool requirement was removed.
2. The next local preflight stopped at `Cannot overwrite variable Host because it is read-only or constant.` Root cause: the harness assigned to `$host`, which collides case-insensitively with PowerShell's automatic read-only `$Host`. The harness was corrected by renaming the local value to `$hostAssessment`, and the non-qualifying CI preflight gained a regression that rejects direct `$Host/$host` assignment.
3. After the first provisional plan/descriptor commits, non-qualifying preflight run `31653982113` rejected `audit/CURRENT_CHANGESET_VALIDATION.json` as outside its preparation allowlist. Root cause: that allowlist predated the simplified ChangeSet validation paths. The allowlist was extended only for `audit/validation/CS017/*` and `audit/CURRENT_CHANGESET_VALIDATION.json`; product/runtime paths remain forbidden. The provisional plan is not a qualifying freeze because no `-Mode Qualify` execution occurred before this correction.

None of these failures is reclassified as PASS.

## R5 qualifying result — FAILED

The first qualifying R5 campaign executed on the maintainer's physical Windows PC as run `ev00-20260813T002607Z-80c3ae08` using frozen harness commit `d478a1b7647eb9d86fdd54e2c8c21ddb0df743ab`.

The run proved:

- exact baseline source `v1.14.1` / `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`;
- clean detached historical source worktree;
- fresh workspace with no preexisting build;
- exact pinned vcpkg commit `0878b5224d4a4968940ee296a2e7fae2d3b62983` fetched and checked out successfully.

It then failed at `vcpkg-bootstrap` before the batch file executed. The harness had constructed the `cmd.exe` argument using escaped quotes that arrived as literal characters, so Windows rejected the command string. This is a harness invocation defect, not a vcpkg, baseline, compiler, build, CTest or product failure.

All later build/test surfaces are therefore `NOT_TESTED` for R5. R5 remains `FAILED`, CS017 is not `VALIDATED`, EV-00 is not `ACCEPTED`, and release remains unauthorized.

The formal record is `docs/changesets/017/R5_FAILURE_RECORD.json`, bound to local evidence manifest SHA-256 `fc5b39c052ebd5513053050d377d3c98384359f87831570a3cc44ea270fbf09c`.

## R6 restart rule

R5 is immutable failure history. R6 starts from the R5 frozen repository state, changes only the defective batch invocation and adds a regression for that exact quoting defect before creating a new validation-plan freeze. R6 must use a new local workspace and may not reuse R5 build or test outputs.

## Current state

R5 is preserved as `FAILED`. R6 preparation is in progress. No R6 qualifying D-Lab command has been executed. Release remains unauthorized.
