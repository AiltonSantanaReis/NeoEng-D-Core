# NeoEng D-Core Host SDK C ABI v1

Status: active in NeoEng D-Core 1.8.0
Public header: `neoeng/dcore_host.h`
CMake target: `NeoEng::DCoreHostSdk`

## Purpose

The Host SDK is a one-way integration boundary over capabilities that already existed in the D-Core 1.6.0 runtime: canonical state transition, rollback, checkpoint restoration, state evidence, traces and the recovery host contract. It does not define a new simulation model and does not give the host authority to mutate internal state.

## Trust boundary

`neoeng_dcore_host_advance()` accepts trusted, in-process commands from a homologated host adapter. It is not a hostile-network ingress. Untrusted datagrams must continue through the authenticated packet/session gateway in `neoeng::core::OperationalRuntime`.

## ABI identity

- ABI major: 1
- ABI minor: 0
- Runtime version: 1.8.0
- All public integer fields use fixed-width C integer types.
- The ABI exposes opaque handles; it exposes no STL objects, C++ exceptions, references, templates, internal pointers or allocator ownership.
- ABI 1.x structure layouts are frozen. Extending or reordering a public structure requires a new ABI major. The `struct_size` field is descriptive and allows runtime inspection; it is not permission to enlarge a structure within ABI 1.x.
- A caller requesting a different ABI major is rejected with `NEOENG_DCORE_STATUS_ABI_MISMATCH`.

Frozen x86-64/ARM64 layout sizes:

| Structure | Bytes |
|---|---:|
| `neoeng_dcore_version_info` | 24 |
| `neoeng_dcore_host_config` | 48 |
| `neoeng_dcore_body` | 40 |
| `neoeng_dcore_input` | 24 |
| `neoeng_dcore_state_summary` | 96 |
| `neoeng_dcore_recovery_event` | 64 |
| `neoeng_dcore_recovery_ack_result` | 80 |
| `neoeng_dcore_trace_event` | 104 |

The implementation contains compile-time assertions for these layouts. Native ARM64 execution remains a deferred validation gate; the layout contract is defined but has not yet been physically confirmed there.

## Ownership and threading

- A host handle is created and owned by the creating thread.
- All handle operations, including destruction, must occur on that thread.
- Cross-thread calls fail closed with `NEOENG_DCORE_STATUS_WRONG_THREAD`.
- The API does not perform implicit synchronization and does not permit concurrent calls on one handle.
- Arrays passed to the API are borrowed only for the duration of the call.
- Arrays returned by copy functions are caller-owned.

## State authority

The host can:

- create an initial canonical state;
- submit trusted input commands;
- request deterministic advancement;
- correct retained historical input and resimulate;
- restore a retained checkpoint;
- copy an immutable state view;
- request hashes and evidence roots;
- report external faults and acknowledge the existing recovery contract;
- copy trace records.

The host cannot:

- obtain mutable pointers to `WorldState` or `Body` storage;
- change the fixed tick;
- change input ordering;
- replace canonical serialization;
- modify snapshots directly;
- bypass the recovery generation/checkpoint checks;
- inject renderer, Unreal, Unity, ROS, finance or other vertical dependencies into the core target.

## Error boundary

No C++ exception crosses the C ABI. Exceptions are translated to stable status codes:

- invalid arguments and ABI mismatch;
- wrong-thread access;
- invalid canonical state;
- missing retained frame;
- insufficient output buffer;
- recovery-required state;
- allocation failure;
- fixed-point overflow;
- unexpected internal failure.

Recovery acknowledgement rejection is a processed protocol result, not an ABI failure. The function returns `OK`, while `accepted` and `reject_reason` describe the recovery decision.

## Deterministic and diagnostic data

`neoeng_dcore_state_summary` returns:

- frame;
- body count;
- legacy stable hash;
- canonical SHA-256;
- Merkle root SHA-256.

Hash computation is on demand and does not replace the hot-path stable hash. Monotonic timestamps and trace data remain diagnostic and are excluded from canonical state transition.

## Installation

The installed CMake package exports:

- `NeoEng::DCore`
- `NeoEng::DCoreHostSdk`

Consumer projects use `find_package(NeoEngDCore 1.7 CONFIG REQUIRED)` and link the required target. A clean-prefix install/configure/build/run test is mandatory in the ChangeSet 007 suite.

## Deliberate exclusions in v1

- no shared-library distribution claim; the companion target is packaged as a static library in 1.8.0;
- no Unreal or Unity adapter implementation;
- no network socket or UDP/QUIC transport;
- no callbacks into arbitrary host code;
- no general domain schema system;
- no asynchronous API;
- no qualification or certification claim.
