# CS014 — test status

State: implementation validation

Baseline: `1.14.0`

Campaign: `TEST-CS014-001`

## Preliminary native Windows validation

The candidate was configured and built on the user's physical Windows x86_64
host with Visual Studio Build Tools 18.7.4, clang-cl 22.1.0, CMake
4.3.1-msvc1 and Ninja 1.13.2.

- supported release build: passed, 126 build steps;
- `ctest -L dcore`: passed, 51/51;
- clean-prefix installed Host SDK consumer: passed as part of CTest;
- blocking clang-tidy: passed, 64/64 implementation units;
- CS014 verifier and negative self-tests: passed, 5/5.

These are preliminary local results from a worktree under construction. They
describe only the recorded host and configuration and are not immutable
closure evidence. No ARM64, certification, external audit or performance rule
for other hardware is inferred.

## Pending immutable evidence

`TEST-CS014-001` remains planned until the committed clean source passes Linux
GCC, Linux Clang, Windows clang-cl, ASan/UBSan, two 120-second libFuzzer
campaigns, blocking static analysis, deterministic consolidated packaging,
independent verification and both externally signed attestations.
