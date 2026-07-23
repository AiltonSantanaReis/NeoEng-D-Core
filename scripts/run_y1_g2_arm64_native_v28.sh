#!/usr/bin/env bash
set -euo pipefail
if [[ $(uname -m) != aarch64 && $(uname -m) != arm64 ]]; then
  echo "Y1-G2 requires native ARM64; detected $(uname -m)" >&2
  exit 3
fi
out=${1:-artifacts/v0.28-y1-g2-arm64}
mkdir -p "$out"
for compiler in g++ clang++; do
  command -v "$compiler" >/dev/null || { echo "missing $compiler" >&2; exit 2; }
  build="$out/build-${compiler//+/p}"
  cmake -S . -B "$build" -DCMAKE_BUILD_TYPE=Release -DNEOENG_WARNINGS_AS_ERRORS=ON -DCMAKE_CXX_COMPILER="$compiler"
  cmake --build "$build" --target neoeng_v28_year1_preclosure neoeng_determinism_probe neoeng_v27_fuzz -j"$(nproc)"
  "$build/neoeng_determinism_probe" > "$out/determinism-${compiler//+/p}.txt"
  "$build/neoeng_v27_fuzz" 5000 > "$out/fuzz-${compiler//+/p}.txt"
  "$build/neoeng_v28_year1_preclosure" replay "$out/replay-${compiler//+/p}" 1000000 > "$out/replay-${compiler//+/p}.txt"
done
sha256sum "$out"/*.txt > "$out/EVIDENCE.sha256"
echo "ARM64 native evidence collected; compare against x86-64 canonical hashes before approving Y1-G2."
