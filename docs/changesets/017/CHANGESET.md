# CS017 R16 — EV-00 verifier-fixture correction over preserved dual-surface contract

## Status

Prospective R16 campaign. R9-R15 remain preserved failed/nonqualifying history and are never reused as qualifying output.

R16 does not modify D-Core product/runtime/test/build-definition sources. The immutable source under test remains `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

## R15 finding carried forward

R15 was blocked by static validation before any physical execution. Its frozen verifier self-test fixture generated synthetic command records without `started_at_utc` and `finished_at_utc`; the preserved R11 verifier correctly rejected those incomplete records. R15 is therefore nonqualifying and not reusable.

Machine record: `R15_STATIC_VALIDATION_FAILURE.json`.

## R16 correction

R16 preserves the R15 dual-surface runtime contract unchanged:

1. supported surface: research-OFF, exact normative CS015 Windows clang-cl 54-test `dcore` inventory, zero failures;
2. isolated replay/history surface: research-ON sibling build, explicit target allowlist, anchored exact three-test CTest inventory, zero failures.

The canonical R16 runner is byte-identical to the R15 runner. The R15 verifier is preserved byte-for-byte as `verify_ev00_dlab_evidence_r15.py`; the R16 verifier is an adapter that delegates runtime checks to that snapshot and corrects only the synthetic research-contract self-test fixture by emitting schema-complete command records with start/finish timestamps.

R16 Preflight additionally executes the independent verifier `--self-test` before delegating to the unchanged runner.

## Historical assurance

`HISTORICAL_ASSURANCE_PROVENANCE_PLAN.json` freezes only verifiable provenance inputs for CS001-CS015. It does not pre-assign risk, reproducibility, rerun necessity, or PASS/FAIL disposition. Historical failures and limitations remain immutable.

## Acceptance boundary

A local terminal `PASSED` is not acceptance. CS017/EV-00 remain unaccepted until committed physical evidence, independent verification, Historical Assurance, all frozen validation-plan tests, and the actual Trusted ChangeSet validation gate succeed.

No release is authorized and EV-01 remains not started.
