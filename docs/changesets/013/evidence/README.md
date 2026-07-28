# CS013 evidence

Campaign `TEST-CS013-001` was approved on source
`28c990174742b1da1885750bc72c29a4614997a0`.

| Evidence | Environment | Result |
|---|---|---|
| `windows-x86_64-clang-20260728` | physical Windows 11 x86_64, clang-cl | passed |
| `github-actions-run-30354055225/cs013-linux-gcc` | Linux x86_64, GCC | passed |
| `github-actions-run-30354055225/cs013-linux-clang` | Linux x86_64, Clang | passed |
| `github-actions-run-30354055225/cs013-cross-compiler-comparison` | GCC/Clang comparison | byte-identical |

Every environment must record source/build identity, configuration, raw tests,
raw deterministic probe, result summary, limitations, SHA-256 manifest and
independent verification. Test providers demonstrate the integration and
fail-closed policy only; they do not prove the security of a production
cryptographic provider.

The Windows inventory identifies the machine that produced the local result.
Machines with lower or higher capability may produce better or worse
observations; no monotonic rule, universal hardware requirement or result for
another machine is inferred. ARM64 was not executed.
