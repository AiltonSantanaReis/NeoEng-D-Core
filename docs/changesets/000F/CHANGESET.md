# CS000F — Evolution authorizer transition correction

Status: **PLANNED / IMPLEMENTED CANDIDATE / NOT YET VALIDATED / NOT ACCEPTED**

## Objective

Resolve the post-CS000E control-plane mismatch in which the canonical evolution action authorizer still treats every required amendment whose status is not `accepted` as a blocker, even though CS000E authoritatively classifies CS016E as `superseded` and unaccepted for prospective ChangeSet authorization.

## Correction

The pre-CS000F implementation of `scripts/authorize_evolution_action.py` is preserved byte-for-byte at `scripts/authorize_evolution_action_legacy.py`.

The canonical path becomes a fail-closed wrapper. It may operationally resolve CS016E for EV-00 only when all of the following are simultaneously true:

- the transition record is `neoeng.dcore.governance-transition-state.v1` for CS000E;
- the prospective regime is `CHANGESET_VALIDATION`;
- the transition record says CS016E is `SUPERSEDED_UNACCEPTED`, `accepted=false`, with no accepted source commit or evidence manifest;
- the persistent CS016E amendment row remains `superseded`, is bound to `superseded_by=CS000E`, references `audit/GOVERNANCE_TRANSITION_STATE.json`, and retains null acceptance fields.

No other `superseded` amendment is treated as resolved.

## Historical preservation

This ChangeSet does not change `audit/EVOLUTION_AMENDMENTS.json`, `audit/GOVERNANCE_TRANSITION_STATE.json`, `audit/GOVERNANCE_ACCEPTANCE_CHAIN.json`, or `audit/GOVERNANCE_ROOT_OF_TRUST.json`. CS016E remains historically unaccepted.

## Non-effects

- EV-00 remains `not_started`.
- CS017 is not started by CS000F.
- No runtime, ABI, product-test, CMake, release, qualification, or D-Lab authority change is authorized.
- No historical run, failure, or acceptance record is rewritten.
