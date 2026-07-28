# CS013 — test status

State: approved

Baseline: `1.13.0`

Campaign: `TEST-CS013-001`

Approved source:
`28c990174742b1da1885750bc72c29a4614997a0`

| Environment | Result |
|---|---|
| Windows 11 x86_64, clang-cl | passed |
| Linux x86_64, GCC | passed |
| Linux x86_64, Clang | passed |
| GCC/Clang semantic comparison | byte-identical |

The Windows campaign records the physical host inventory. Results describe
only the recorded source, binaries, environments and configurations. ARM64,
production-provider cryptographic strength, external-anchor trust and results
for another machine are not inferred.
