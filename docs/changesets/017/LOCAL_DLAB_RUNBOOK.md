# CS017 / EV-00 — Local Windows D-Lab Runbook

Status: **PREPARATION ONLY — QUALIFY MUST NOT RUN BEFORE PLAN FREEZE**

## Purpose

EV-00 certifies the immutable `v1.14.1` baseline on the maintainer's physical Windows PC. GitHub-hosted runners may test the harness and independently verify committed evidence, but they do not qualify the physical D-Lab environment.

Protected product under test:

- release: `v1.14.1`;
- commit: `e3fff973554a2e56b8bd7afdc1132f75f3ec337c`.

Current CS017 control line starts from `main@d092ac56290d76dddf51982549a98234f038f3ee`.

## Required local environment

The local preflight requires:

- Windows physical host;
- PowerShell 7+ (`pwsh`);
- Git;
- Python;
- CMake and CTest;
- Ninja;
- `clang-cl`;
- MSVC linker environment (`link.exe`);
- Windows SDK resource compiler (`rc.exe`);
- repository history containing the protected `v1.14.1` commit.

For `clang-cl`, `link.exe` and `rc.exe` to be available together, use an x64 Visual Studio / Build Tools developer environment and run `pwsh` from it.

## Preflight — non-qualifying

Preflight does not build or certify the product. It only checks the host/tool prerequisites and the clean control repository.

```powershell
pwsh -NoProfile -File .\scripts\ev00\run_ev00_dlab_windows.ps1 -Mode Preflight
```

A failed preflight means **BLOCKED**, not approved. Fix the missing prerequisite; do not weaken the harness.

## Qualification — explicit hold point

Do **not** run this command until `audit/validation/CS017/VALIDATION_PLAN.json` has been frozen in a dedicated plan commit and that exact plan/harness SHA has been confirmed.

```powershell
pwsh -NoProfile -File .\scripts\ev00\run_ev00_dlab_windows.ps1 -Mode Qualify
```

The qualification run creates a new unique workspace under:

```text
%USERPROFILE%\NeoEng-DLab\EV-00\runs\<run-id>\
```

with separated:

```text
source/
build/
install/
deps/
evidence/
```

The product source is a detached worktree at the exact historical SHA. Existing product build directories are not reused.

## Evidence behavior

Every relevant command records:

- executable and arguments;
- working directory;
- UTC start/end;
- exit code;
- stdout;
- stderr;
- PASS/FAIL classification.

At terminal state the harness writes a SHA-256 manifest over the `evidence/` package. The package is copied, without changing its bytes, to:

```text
docs/changesets/017/evidence/local-windows/<run-id>/
```

A failed qualification is still evidence and must be preserved. Before another attempt, preserve/commit the failed package so the control tree can return to a clean state.

## What is not acceptance

None of these alone accept EV-00:

- CI green;
- preflight PASS;
- successful compilation without all mandatory tests;
- historical R1-R4 evidence;
- `NOT_TESTED`, `PARTIAL`, `BLOCKED` or missing evidence;
- an unverified local JSON summary.

Acceptance requires the frozen CS017 plan, a real local terminal `PASSED` package, independent evidence verification, the full historical-assurance result, an official GitHub validation run bound to an exact SHA/run/attempt/workflow, and the protected `Trusted ChangeSet validation gate`.
