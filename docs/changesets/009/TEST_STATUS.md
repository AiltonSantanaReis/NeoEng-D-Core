# ChangeSet 009 — Test Status

## Result

All tests executed for this ChangeSet passed. This result verifies the CS009 implementation and evidence pipeline; it does not qualify any physical hardware profile.

## Compiler matrices

| Matrix | Result | Real test time |
|---|---:|---:|
| GCC 14.2.0 Release, full toolset, research tools, View Lab and Host SDK | 64/64 passed in three runs | 10.36 s / 9.13 s / 9.25 s |
| Clang 17.0.0 Release, full toolset, research tools, View Lab and Host SDK | 64/64 passed in three runs | 9.06 s / 7.59 s / 7.51 s |
| Clang 17.0.0 ASan + UBSan, exact selected critical scope | 17/17 passed | 2.68 s |

LeakSanitizer was disabled with `ASAN_OPTIONS=detect_leaks=0`. The sanitizer result must not be described as full-tree leak validation.

## Execution anomaly retained

An initial local ASan/UBSan build attempt collided with another process using the same build directory and failed while creating a dependency file. The complete failure log is retained in the outer validation-extras directory. A distinct clean build directory was then configured and built successfully in 80/80 steps; the exact 17-test scope passed 17/17.

## ECS scope verification

- Independent verifier smoke: passed.
- Independent verifier self-test: passed.
- Tamper classes rejected: 6.
- Streams verified: general allocation, arena, copy-on-write, index maintenance and legacy compatibility.
- Deterministic workload final hash: `0x3562ED2079D86CF7`.
- Independent campaign verification: passed for both P0 and P1 virtualized campaigns.

## Qualification campaigns

### P0 virtualized

- evidence disposition: `incomplete`;
- status: `unqualified`;
- ECS scope complete: true;
- ECS p99: 5,200 ns;
- rollback p99: 1,202,243 ns;
- ECS/rollback samples: 32;
- failure mask: 262274;
- failure reasons: `EnvironmentMismatch`, `NativeExecutionRequired`, `ClockPolicyMissing`;
- source manifest generated from the exact non-build tree at campaign execution time;
- independent verifier: passed;
- no native qualification claimed.

### P1 virtualized

- evidence disposition: `incomplete`;
- status: `unqualified`;
- ECS scope complete: true;
- `EcsScopeIncomplete`: false;
- ECS p99: 6,763 ns;
- rollback p99: 1,205,388 ns;
- ECS/rollback samples: 32;
- contractual minimum: 1,000 samples;
- allocation events: 672;
- allocation bytes: 901,376;
- arena overflow zero: true;
- copy-on-write semantics valid: true;
- index semantics valid: true;
- failure mask: 1409154;
- profile compatibility: false;
- compatibility reasons: NVIDIA GPU required, at least 16 GiB GPU memory, at least 16 physical CPU cores, and NVMe PCIe 4.0/5.0 storage;
- failure reasons: `EnvironmentMismatch`, `NativeExecutionRequired`, `ProfileCompatibilityFailed`, `InsufficientSamples`, `ClockPolicyMissing`, `AllocationGateFailed`;
- source manifest generated from the exact non-build tree at campaign execution time;
- independent verifier: passed;
- no native, zero-allocation or performance qualification claimed.

The 32-sample campaigns exercise the complete decision and evidence pipeline. Their timing values are diagnostic only and do not satisfy native P1 qualification.

## Cross-compiler deterministic outputs

| Output | GCC versus Clang | SHA-256 |
|---|---|---|
| Determinism probe | byte-identical | `d787bb9afc9f55a3c7f79803f5cad46bdb181484060214f250109531926a4ade` |
| State-evidence probe | byte-identical | `fe2e91e747400295a40e47884c2a8cc7acf13cdbb759dd5e5f46748f4d89263e` |
| Host SDK reference | byte-identical | `8ad2bd6c8922dde353fb24f386455f82c8f4c6f0afa95c8cd866527f6fa99538` |

The corresponding diff files are empty.

## Governance, integrity and isolation

- product-contract verifier: passed;
- product-contract fail-closed self-test: 7/7 mutation classes rejected;
- product-assurance verifier: passed;
- product-assurance fail-closed self-test: 9/9 mutation classes rejected;
- Host SDK boundary verifier: passed;
- isolation verifier: passed;
- ECS evidence self-test: 6/6 tamper classes rejected;
- canonical implementation units changed: 0/58;
- authorized canonical header changes: 1/64, governance-only;
- base header SHA-256: `642aa5c557ba6072c186343f4b6eda70e4e1d818cce0431f12c32d614824a866`;
- result header SHA-256: `44bbd48ff8765799ad19a00104222689faad94115fd90ceb84a5ddf063a59dab`.

## Product status after CS009

- requirements in ledger: 36;
- claims in ledger: 20;
- known limitations: 41;
- open mandatory internal requirements: 18;
- open mandatory internal limitations: 23;
- commercial ready: false.

## Not executed or not claimed

- Windows physical regression for 1.9.0;
- ARM64;
- native P0–P4 campaigns;
- PowerShell execution;
- LeakSanitizer;
- external security or cryptographic audit;
- regulatory or production certification.

## Manifest portability revision

- Linux `python scripts/generate_manifest.py --check`: passed.
- Component-wise POSIX and Windows path models: byte-identical for all 1,281 entries.
- Whole-string POSIX sorting was explicitly shown to diverge at the `v0.26-stability` / `v0.26-stability-final` boundary and is not used.
- Physical Windows execution of the generator: not performed in this Linux environment and remains required before acceptance.
