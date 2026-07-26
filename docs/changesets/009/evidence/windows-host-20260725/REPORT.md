# CS009 Windows host-local validation — 2026-07-25

## Result

The CS009-related binaries were executed directly on the recorded Windows 11 PC. All 15 direct positive and negative contract tests passed. The official ECS maintenance workload was executed twice, and both independent verifications passed.

This is environment-specific Windows host evidence. It is not a universal hardware requirement, a cross-machine performance promise or a native P1 qualification.

## Source and build identity

- tested source commit: `ec46d4d3c74303ba60a601066c4a7462091dc712`;
- branch: `changeset-009-r2`;
- binary build origin: `c2f80d4863f73d8f9c89687f24b462eb07e116bb`;
- runtime, application, test, Host SDK and qualification script differences between the build origin and tested source commit: none;
- the intervening commit changes only documentation, the hardware-profile template and the source manifest.

The earlier full Windows gate was run on the CS009 R2 working tree before it was committed. Its receipt did not record the resulting commit hash, so it is retained as supporting provenance rather than presented as a cryptographic commit binding. The direct campaign in this directory records the tested source commit explicitly.

## Recorded PC

| Component | Observed value |
|---|---|
| Operating system | Microsoft Windows 11 Pro, version `10.0.26200`, build `26200`, 64-bit |
| Motherboard | Gigabyte B550M AORUS ELITE |
| CPU | AMD Ryzen 7 5700X3D, 8 physical cores / 16 logical processors |
| GPU | NVIDIA GeForce RTX 3070 Ti, 8,192 MiB |
| NVIDIA driver | `610.47` (`32.0.16.1047` through WMI) |
| Memory | 32 GiB DDR4-3200 |
| Primary SSD | KINGSTON SFYRSK1000G, 1 TB, NVMe |
| Secondary disk | ST4000LM024-2AN17V, 4 TB, SATA HDD |
| Firmware | AMI / Gigabyte BIOS `FHb` |
| Power scheme | High performance |
| Hypervisor | Windows reports `HypervisorPresent=true` |
| Controlled thermal campaign | Not performed |

No serial number, user name, device ID, network identifier or personal path is included in `hardware-inventory.json`.

User-profile paths in the retained configure log and Python executable records were replaced with `<USERPROFILE>` and `<PYTHON_EXECUTABLE>` before publication. No result, exit code, tool version or test output was changed by this privacy redaction.

## Execution classification

These binaries ran locally on the user's Windows PC. However, `HypervisorPresent=true` prevents this evidence from being labeled `native_physical` under `neoeng.dcore.hardware-qualification.v2`. The accurate classification is:

`windows_host_local_hypervisor_present`

This distinction does not invalidate functional, determinism, fail-closed or host-local benchmark evidence. It blocks only a native hardware-profile qualification claim.

## Direct binary tests

| Test | Observed exit | Expected exit | Result |
|---|---:|---:|---|
| Core tests | 0 | 0 | passed |
| Hardware qualification policy tests | 0 | 0 | passed |
| Host SDK tests | 0 | 0 | passed |
| Host SDK C header test | 0 | 0 | passed |
| Host SDK reference host | 0 | 0 | passed |
| Determinism probe | 0 | 0 | passed |
| RAA allocation probe | 0 | 0 | passed |
| ECS official workload, run 1 | 0 | 0 | passed |
| ECS independent verifier, run 1 | 0 | 0 | passed |
| ECS official workload, run 2 | 0 | 0 | passed |
| ECS independent verifier, run 2 | 0 | 0 | passed |
| ECS zero-body rejection | 1 | 1 | passed |
| ECS active-count overflow rejection | 1 | 1 | passed |
| Hardware probe missing-arguments rejection | 2 | 2 | passed |
| Hardware probe unknown-option rejection | 2 | 2 | passed |

## Official CS009 ECS workload

Both runs used the registered `Y1-O2-SPARSE-COMPONENT-MAINTENANCE-V1` parameters:

- 10,000 bodies;
- 100 active bodies;
- 1,000 measured samples;
- 128 warmup samples.

| Observation | Run 1 | Run 2 |
|---|---:|---:|
| ECS p99 | 8,200 ns | 8,500 ns |
| Final deterministic hash | `0x89FF0A43B7F5C573` | `0x89FF0A43B7F5C573` |
| ECS scope complete | true | true |
| Independent verifier | passed | passed |
| General allocation zero | false | false |

The identical final hash and two accepted evidence sets establish semantic repeatability for these runs. Timing differences are expected and are not deterministic-state inputs.

The observed p99 values are below the P1 reference budget of 100,000 ns, but this does not qualify P1: general allocation was nonzero, a controlled thermal/clock campaign was not performed and Windows reported a hypervisor.

## Earlier full Windows gate

The retained supporting gate evidence records:

- clean Windows Clang Release build: passed;
- first CTest run: 64/64 passed;
- second CTest run: 64/64 passed;
- replay smoke: passed;
- history smoke: passed;
- network smoke: passed;
- hardware profile qualified: false.

## Interpretation across machines

The numbers above apply only to this recorded PC, software stack and observed conditions. They are not minimum hardware requirements for NeoEng D-Core.

A machine with lower or higher nominal specifications may produce better or worse measurements. CPU architecture, cache behavior, memory, storage, drivers, firmware, power management, thermals, virtualization and background activity can outweigh simple component rankings. Any claim about another machine requires its own recorded execution.
