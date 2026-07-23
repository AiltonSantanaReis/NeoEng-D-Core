#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$ROOT/build-v28-stage1}"
OUT="${2:-$ROOT/artifacts/v0.28-stage1}"
mkdir -p "$OUT"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DNEOENG_WARNINGS_AS_ERRORS=ON
cmake --build "$BUILD" -j"${JOBS:-8}"
ctest --test-dir "$BUILD" --output-on-failure
"$BUILD/neoeng_v27_fuzz" 5000 | tee "$OUT/v27-regression-fuzz.txt"
"$BUILD/neoeng_determinism_probe" 10000 | tee "$OUT/determinism-probe.txt"
"$BUILD/neoeng_v28_raa_allocation_probe" 1024 8 | tee "$OUT/raa-allocation-8.json"
"$BUILD/neoeng_v28_raa_allocation_probe" 1024 12 | tee "$OUT/raa-allocation-12.json"
"$BUILD/neoeng_v28_raa_allocation_probe" 1024 16 | tee "$OUT/raa-allocation-16.json"
# Smoke only. This execution does not constitute bare-metal evidence.
"$BUILD/neoeng_v28_bare_metal_rollback" "$OUT/rollback-vm-smoke" 5 | tee "$OUT/rollback-vm-smoke.txt"
