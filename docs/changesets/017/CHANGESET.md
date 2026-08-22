# ChangeSet 017 — R17 EV-00 baseline certification campaign

## Purpose

R17 is a prospective successor to failed/nonqualifying R9-R16. It preserves the immutable `v1.14.1` D-Core source and the R16 dual-surface runtime verification contract while correcting the R16 physical-Preflight wrapper function-scope defect.

## Preserved boundary

- D-Core source under test remains `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.
- R16 runner bytes remain unchanged.
- R16 independent verifier bytes remain unchanged.
- R16 frozen plan `8ab016056e2e4191a7e24cb2d6192b4c5836e6d2` remains historical and is not repaired or reused.
- R16 physical Preflight failed before runner delegation; Qualify was not executed and no EV-00 run ID was created.

## R17 correction

The R16 wrapper evaluated the extracted `Parse-CtestInventory` function inside the local scope of `Import-CtestParser`; the function ceased to exist when the importer returned. R17 changes the wrapper import so the extracted function is defined explicitly in wrapper script scope.

Static validation must execute this import path in PowerShell and invoke the imported parser against the normative CS015 Windows clang-cl CTest reference before any physical Preflight is authorized.

## Qualification contract

The runtime campaign is unchanged from R16:

1. supported surface: research tools OFF, exact normative CS015 Windows 54-test inventory, zero failures;
2. isolated research surface: research tools ON, exact replay/history/temporal-closure inventory of three tests, zero failures;
3. immutable v1.14.1 product source;
4. independent evidence verification and separate CS001-CS015 Historical Assurance closure;
5. no acceptance or release implication from a local physical pass alone.

## Non-effects

No product/runtime/ABI/public-header/test/build-definition change is authorized. No product claim is changed. Release remains unauthorized. EV-01 remains not started.
