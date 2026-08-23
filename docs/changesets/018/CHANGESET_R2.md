# ChangeSet 018 R2 — prospective qualification recovery

## Identity

- ChangeSet: `CS018`
- Revision: `R2`
- Protected base: `e9e095e61d4de2995db704c51e9308850e1c929d`
- Preserved R1 source/plan commit: `4adccdb77607fc6d6f886369a842d2efa48a7028`
- R1 disposition: `BLOCKED` before execution; not accepted.

## R1 platform constraint

The R1 qualifying workflow was introduced on the feature branch and used only
`workflow_dispatch`.

GitHub Actions requires a workflow triggered by `workflow_dispatch` to exist on
the repository default branch. Because `.github/workflows/cs018-validation.yml`
does not yet exist on `main`, the frozen R1 execution contract cannot create a
qualifying run.

No R1 dispatch was attempted. No R1 workflow run exists. R1 is preserved as
nonaccepted evidence in
`audit/validation/CS018/ATTEMPT_001_NONACCEPTANCE.json`.

## R2 trigger

R2 keeps the same qualifying workflow path but changes its prospective trigger
to `push` restricted to the exact CS018 branch and R2 source-definition paths.

GitHub evaluates `push` workflows from the pushed ref even when the workflow has
not yet been merged to the default branch.

The result-closure paths are deliberately absent from the trigger path filter,
so binding `VALIDATION_RESULT_R2.json`, repointing the current descriptor and
updating `MANIFEST.sha256` will not create a second qualifying run.

## Preserved controls

R2 preserves without semantic change:

- the permanent current PR/main product regression workflow;
- the accepted EV-00 binding;
- the EV-01 in-progress ledger transition;
- the original R1 validation plan, discovery report and ChangeSet record;
- CS010-CS015 historical workflow bytes;
- all product source and build-definition paths.

`release_authorized` remains `false`.

## Qualification rule

The R2 plan/source commit must be committed before publication.

Publishing that exact commit to
`agent/cs018-ev01-build-ci-governance-hardening` is the qualifying trigger. A valid result must bind the resulting
GitHub run to the exact R2 source SHA, run attempt and workflow path and must
show every required test as `PASS`.
