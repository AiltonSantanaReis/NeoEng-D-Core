#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPU="${1:-0}"
OUT="${2:-$ROOT/artifacts/v0.26-bare-metal}"
BUILD="$ROOT/build-bare"
mkdir -p "$OUT"

if grep -qi hypervisor /proc/cpuinfo; then
  echo "WARNING: hypervisor flag detected; results are not bare-metal." >&2
fi

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DNEOENG_WARNINGS_AS_ERRORS=ON
cmake --build "$BUILD" --target neoeng_v25_benchmark neoeng_v26_benchmark neoeng_v26_fuzz neoeng_determinism_probe -j8

for run in $(seq 1 10); do
  taskset -c "$CPU" "$BUILD/neoeng_v25_benchmark" "$OUT/rollback-$run" | tee "$OUT/rollback-$run.txt"
  taskset -c "$CPU" "$BUILD/neoeng_v26_benchmark" "$OUT/v26-$run" | tee "$OUT/v26-$run.txt"
done
"$BUILD/neoeng_v26_fuzz" 5000 | tee "$OUT/fuzz.txt"
"$BUILD/neoeng_determinism_probe" | tee "$OUT/probe.txt"
lscpu > "$OUT/lscpu.txt"
uname -a > "$OUT/uname.txt"
