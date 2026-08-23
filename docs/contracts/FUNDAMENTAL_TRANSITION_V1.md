# Fundamental Transition Contract V1

Status: normative CS019 / EV-02 contract.

## 1. Scope

This contract defines the fail-closed boundary behavior exercised by CS019 for
the fundamental `WorldState` transition path, `RollbackEngine`, Q32.32
primitive arithmetic, and the existing Host SDK C ABI 1.0 translation layer.

The cross-compiler evidence scope for CS019 is GCC and Clang on Linux x86-64
using the qualifying workflow corpus. This document does not claim ARM64,
MSVC, or universal platform equivalence.

## 2. Maximum frame

A fundamental transition whose input `WorldState.frame` is
`UINT64_MAX` is not representable because no next `uint64_t` frame exists.

The C++ transition API MUST reject before constructing or committing a next
canonical state.

Required C++ classification:

- exception class: `std::overflow_error`;
- exact diagnostic: `World frame maximum reached`.

`RollbackEngine::advance` MUST preserve its canonical state, snapshot
retention state, and retained input-history cardinality when this rejection
occurs.

The Host SDK MUST reuse the existing status:

`NEOENG_DCORE_STATUS_NUMERIC_OVERFLOW`

and MUST preserve the canonical host state.

No saturating frame, wrap to zero, or partial successor state is permitted.

## 3. Input targeting an absent entity

For the C++ fundamental transition API, every `InputCommand.entity` MUST
identify an entity present in the canonical `WorldState.bodies` collection.

Multiple inputs for the same existing entity remain valid and remain subject
to existing canonical input ordering.

An absent entity MUST reject the complete transition.

Required C++ classification:

- exception class: `std::out_of_range`;
- exact diagnostic: `Input references unknown EntityId`.

`RollbackEngine::advance` MUST not append or replace retained input history
before this transition validation succeeds.

The Host SDK MUST reuse:

`NEOENG_DCORE_STATUS_NOT_FOUND`

for a nonzero input entity that passes C import validation but is absent from
the current world.

The existing Host SDK rule for `entity_id == 0` remains unchanged and is
classified as `NEOENG_DCORE_STATUS_INVALID_ARGUMENT` before the fundamental
entity-existence rule is reached.

No input may be silently discarded merely because its EntityId has no
matching body.

## 4. Q32.32 arithmetic

`docs/contracts/NUMERIC_CLOSURE_V1.md` remains the normative arithmetic
contract. CS019 does not modify the Q32.32 implementation.

The direct CS019 regression corpus MUST demonstrate:

- arithmetic overflow rejects with `std::overflow_error`;
- division by zero rejects with `std::domain_error`;
- construction/narrowing outside the representable Q32.32 raw range rejects
  with `std::overflow_error`;
- multiplication and division of signed nonintegral results truncate toward
  zero.

No saturation, wraparound, implementation-defined narrowing result, or
partial value is permitted.

## 5. Stable rejection corpus

The qualifying GCC and Clang jobs MUST emit the same canonical markers:

`cs019_core_contract=PASS frame_max=overflow_rejected unknown_entity=not_found rollback_precommit=preserved`

`cs019_q32_contract=PASS rounding=toward_zero overflow=overflow_error division_by_zero=domain_error narrowing=overflow_error`

`cs019_host_contract=PASS frame_max=numeric_overflow unknown_entity=not_found abi=1.0`

The three-line corpus is compared byte-for-byte between the two compiler
builds.

## 6. Explicit non-effects

CS019 does not change:

- Host SDK ABI major/minor;
- any Host SDK C structure layout;
- any Host SDK status numeric value;
- Q32.32 storage layout or arithmetic implementation;
- CMake target or test registration;
- canonical serialization format;
- release authorization;
- EV-03 lifecycle state.

A successful qualifying CI execution is validation evidence only and is not
by itself acceptance or release authorization.
