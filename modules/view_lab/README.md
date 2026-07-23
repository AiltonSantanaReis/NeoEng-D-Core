# NeoEng D-Core View Lab

`modules/view_lab` is an optional, read-only visual diagnostics companion for NeoEng D-Core. It does not own or mutate canonical state.

## Boundary

- Canonical authority remains `neoeng_dcore` (`NeoEng::DCore`).
- Dependency direction is one-way: `neoeng_dcore_view_lab -> neoeng_dcore`.
- The core target does not include or link the View Lab.
- The imported Year-2 visibility reference is isolated under `vendor/year2` and verified byte-for-byte against `v0342.zip`.
- GPU/EGL, SDF, voxel, sparse structures and device-specific Year-2 paths were not imported in ChangeSet 003.

## Output

`neoeng_dcore_view_lab_cli` exports:

- one deterministic BMP per retained time-travel frame;
- `visual-correlation.json` using `neoeng.dcore.visual-correlation.v1`;
- a self-contained `index.html` with a frame slider, entity state and trace events.

This viewer is static and browser-based. It is suitable for reproducing and inspecting recorded state, not for authoring or changing simulation state.

## Build

Official presets enable the module. For a manual CMake invocation:

```bash
cmake -S . -B build/view-lab -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DNEOENG_DCORE_BUILD_VIEW_LAB=ON
cmake --build build/view-lab
ctest --test-dir build/view-lab -L view-lab --output-on-failure
```

## Provenance

See `audit/YEAR2_EXTRACTION_LEDGER.json` and `evidence/year2/SELECTED_EVIDENCE_MANIFEST.json`.
