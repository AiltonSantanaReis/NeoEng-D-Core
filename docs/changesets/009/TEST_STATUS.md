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

## Windows host-local validation — 2026-07-25

The CS009 binaries were subsequently executed directly on a recorded Windows 11 PC:

- AMD Ryzen 7 5700X3D, 8 physical cores / 16 logical processors;
- NVIDIA GeForce RTX 3070 Ti, 8 GiB, driver 610.47;
- 32 GiB DDR4-3200;
- 1 TB Kingston NVMe SSD;
- Windows 11 Pro build 26200;
- high-performance power scheme.

All 15 direct positive and negative binary tests passed. The official ECS workload was executed twice with 10,000 bodies, 100 active bodies, 1,000 measured samples and 128 warmup samples. Both independent verifications passed, with p99 values of 8,200 ns and 8,500 ns and the identical deterministic hash `0x89FF0A43B7F5C573`.

Both runs observed general allocation, so zero allocation and P1 qualification are not claimed. Windows also reported `HypervisorPresent=true`; the evidence is therefore classified as `windows_host_local_hypervisor_present`, not `native_physical`. Full inventory, raw streams, logs, binary hashes and checksums are retained under `docs/changesets/009/evidence/windows-host-20260725/`.

These measurements describe only the recorded PC and conditions. Hardware with lower or higher nominal specifications may perform better or worse and requires its own campaign.

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

- Windows host-local execution was performed, but native-physical qualification is not claimed because a hypervisor was reported;
- ARM64;
- native P0–P4 campaigns;
- LeakSanitizer;
- external security or cryptographic audit;
- regulatory or production certification.

## Manifest portability revision

- Linux `python scripts/generate_manifest.py --check`: passed.
- Component-wise POSIX and Windows path models: byte-identical for all 1,281 entries.
- Whole-string POSIX sorting was explicitly shown to diverge at the `v0.26-stability` / `v0.26-stability-final` boundary and is not used.
- Physical Windows execution of the generator: not performed in this Linux environment and remains required before acceptance.
