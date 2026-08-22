# CS017 R22 — EV-00 direct physical wrapper / R20 verifier binding

## Objective

Correct only the physical wrapper binding defect exposed by the R21 Preflight. The product baseline remains immutable at `v1.14.1@e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

## Preserved history

R9-R21 remain failed/nonqualifying historical attempts and are not reusable as qualifying evidence. R21 is classified `FAILED_NONQUALIFYING_PHYSICAL_PREFLIGHT_LEGACY_VERIFIER_IDENTITY_INVARIANT_CONFLICT`.

## R22 correction

R22 preserves:
- canonical runner blob `2c8f2d41f708dfad27361b1e47774e536b87ade2`;
- canonical verifier blob `e22d0e8b73264a993b912abd77379a53853ae50b`;
- preserved R20 verifier snapshot at the same blob;
- immutable CS015 Windows 54-test oracle;
- dual-surface Build A / Build B contract.

R22 replaces the chained `R21 -> R20 -> R19 -> R17` physical wrapper path with a direct wrapper. The direct wrapper retains MSVC environment validation, real clang-cl/Ninja compile-link smoke, canonical CTest parser execution, dual-surface contract audit, verifier self-test, Python no-bytecode hygiene and clean-tree checks. The verifier identity invariant is updated prospectively to require canonical verifier bytes to equal the preserved **R20** verifier bytes, not obsolete R16 bytes.

## Non-effects

No `src/**`, `include/**`, `tests/**`, `CMakeLists.txt`, runtime, ABI, product-test or build-definition change is authorized. No release claim or qualification claim is authorized. EV-00 remains unaccepted until committed physical evidence, independent verification, frozen ChangeSet validation result and Trusted ChangeSet validation gate all pass.
