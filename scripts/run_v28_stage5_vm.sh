#!/usr/bin/env bash
set -euo pipefail

output_root="${1:-artifacts/v0.28-stage5-local}"
seeds="${2:-32}"
groups="${3:-4}"
frames="${4:-6}"
terms="${5:-8,12,16}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$output_root"

{
  uname -a
  cat /etc/os-release 2>/dev/null || true
  if command -v systemd-detect-virt >/dev/null; then systemd-detect-virt || true; fi
  lscpu
  g++ --version | head -1
  clang++ --version | head -1
  cmake --version | head -1
  python3 --version
} > "$output_root/environment.txt"

for compiler in gcc clang; do
  if [[ "$compiler" == gcc ]]; then cxx=g++; else cxx=clang++; fi
  build="$output_root/build-$compiler"
  cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DNEOENG_WARNINGS_AS_ERRORS=ON -DCMAKE_CXX_COMPILER="$cxx"
  cmake --build "$build" --target neoeng_v28_active_island_shadow -j"$(nproc)"
done

python3 "$root/scripts/run_v28_stage5_matrix.py" \
  --gcc-exe "$output_root/build-gcc/neoeng_v28_active_island_shadow" \
  --clang-exe "$output_root/build-clang/neoeng_v28_active_island_shadow" \
  --output "$output_root/comparison" \
  --seeds "$seeds" --groups "$groups" --frames "$frames" --terms "$terms"

echo "Stage 5 comparison complete. Timing is non-authoritative unless collected under the native bare-metal protocol: $output_root"
