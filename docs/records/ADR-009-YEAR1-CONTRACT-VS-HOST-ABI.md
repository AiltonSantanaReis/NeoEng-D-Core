# ADR-009 - Year-1 internal contract versus public Host C ABI

Status: accepted
Product version at decision: 1.9.0; preserved by 1.10.0

## Context

`year1_contract.hpp` historically called its version an ABI candidate pending Year-1 gates. ChangeSet 007 introduced a separately versioned public Host C ABI 1.0. Treating both as the same ABI creates a governance ambiguity.

## Decision

1. The public binary boundary is the C ABI declared by `neoeng/dcore_host.h`.
2. Host ABI 1.0 remains frozen under its compatibility rules; runtime product version changes do not change the ABI version.
3. `year1_contract.hpp` describes internal canonical replay/schema constants and the source-level C++ contract. It is not a cross-toolchain binary ABI promise.
4. Replay schema version 1 is frozen and changes require an explicit new schema plus migration/compatibility handling.
5. C++ object layout across compilers, standard libraries or runtime modes is not guaranteed.
6. The historical `kYear1AbiCandidateVersion` name is retained as a compatibility alias and must not be interpreted as the Host ABI.

## Consequences

The ambiguity is closed without changing canonical transition, serialization bytes, replay magic, tick rate, state layout or Host ABI 1.0.
