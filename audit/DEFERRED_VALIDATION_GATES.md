# Deferred Validation Gates

The normative machine-readable ledger is `audit/DEFERRED_VALIDATION_GATES.json`.

## Current candidate baseline: 1.14.1

This ledger is current for the accepted horizontal baseline `1.14.0`. The
deferred gates are non-blocking for CS015 acceptance, but they remain blocking
for the qualification or deployment claim that each gate names. The current
machine-readable ledger records native-validation gates, one external
cryptographic-assurance gate and future infrastructure gates. None of these
statuses is a product-code failure, and none authorizes a claim beyond the
published claims ledger.

A gate marked `native_validation_pending` means the implementation and campaign tooling are ready for the declared target, but execution was intentionally deferred. It is not a current functional failure. It blocks qualification of the associated P0-P4 profile until verified native evidence exists.

The current ledger records the ECS scope as implemented in `1.9.0`; native P1
execution, timing and zero-allocation qualification remain separate gates. The
historical ChangeSet 006 wording below is preserved as historical context and
must not be read as the current implementation status.

`external_assurance_pending` and `future_infrastructure` gates depend on deployment, key custody, storage or independent review decisions outside the current virtualized engineering campaign.

## Historical ChangeSet 006 context

- the P0-P4 campaign runner, independent verifier and x86_64/ARM64 semantic comparator are implemented;
- virtualized and containerized executions are permanently ineligible for `passed` under schema `neoeng.dcore.hardware-qualification.v2`;
- Windows, ARM64, NVIDIA, AMD and P4 physical campaigns were not executed;
- no profile is qualified;
- timing data from the current host is diagnostic engineering evidence only.
