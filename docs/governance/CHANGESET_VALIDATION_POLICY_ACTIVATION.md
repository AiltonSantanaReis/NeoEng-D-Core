# NeoEng D-Core — ChangeSet Validation Regime Activation

Status: **CANDIDATE — effective only if CS000E is VALIDATED/ACCEPTED by the trusted base `Trusted ChangeSet validation gate`.**

## Purpose

This document reconciles the completed governance transition without retroactively accepting CS016E.

The historical CS016E root-of-trust bootstrap was merged in PR #27, but CS016E never completed its own two-phase acceptance contract. Its `accepted_source_commit` and `evidence_manifest` therefore remain null and CS016E must not be described as accepted.

A later, explicitly authorized transition replaced that operational control plane with the simpler evidence-driven ChangeSet validation regime:

- PR #28 bootstrapped the new policy/workflow/verifier into `main`;
- PR #29 proved `Trusted ChangeSet validation gate` from the protected base;
- PR #30 retired automatic execution of the historical CS016 workflows while preserving their bodies and evidence;
- current `main` protection requires exactly `Trusted ChangeSet validation gate` bound to GitHub Actions app id `15368`.

## Prospective authority

If and only if CS000E is accepted, future ChangeSets are governed prospectively by:

- `audit/CHANGESET_VALIDATION_POLICY.json`;
- `docs/governance/CHANGESET_VALIDATION_POLICY.md`;
- `.github/workflows/changeset-validation.yml`;
- `scripts/verify_changeset_validation.py`;
- the branch-protection required context `Trusted ChangeSet validation gate`.

`audit/GOVERNANCE_TRANSITION_STATE.json` is the machine-readable activation record.

## Legacy classification

CS016E becomes **superseded, not accepted** for prospective ChangeSet authorization. This does not delete or weaken its historical artifacts. The following remain historical evidence:

- `audit/GOVERNANCE_ROOT_OF_TRUST.json`;
- `audit/GOVERNANCE_ACCEPTANCE_CHAIN.json`;
- Amendment 1.5 and DEV-0005;
- all CS016E runs, failures, partial bootstrap evidence and nonclaims.

The acceptance chain ending at CS016D remains truthful; CS000E must not append a fabricated CS016E acceptance entry.

## Stale-text reconciliation

Earlier texts that still say “transition candidate”, “not yet merged”, or equivalent are preserved as statements made during the transition. After CS000E acceptance, those phrases are historical context, not current operational authority. This activation document and `audit/GOVERNANCE_TRANSITION_STATE.json` control that interpretation prospectively.

## Non-effects

CS000E does not start EV-00 or CS017, modify D-Core runtime/ABI/product tests, authorize a release, create qualification, import D-Lab authority into the runtime, or rewrite any historical failure/evidence.
