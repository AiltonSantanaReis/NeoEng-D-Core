#!/usr/bin/env bash
set -euo pipefail

output_root="${1:-artifacts/v0.28-stage4-local}"
contacts="${2:-1000}"
repetitions="${3:-24}"
fuzz_seeds="${4:-32}"
fuzz_contacts="${5:-128}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$output_root"

{
  uname -a
  cat /etc/os-release 2>/dev/null || true
  command -v systemd-detect-virt >/dev/null && systemd-detect-virt || true
  lscpu
  g++ --version | head -1
  clang++ --version | head -1
  cmake --version | head -1
  python3 --version
} > "$output_root/environment.txt"

for compiler in gcc clang; do
  if [[ "$compiler" == gcc ]]; then cxx=g++; else cxx=clang++; fi
  build="$output_root/build-$compiler"
  result="$output_root/$compiler"
  cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release \
    -DNEOENG_WARNINGS_AS_ERRORS=ON -DCMAKE_CXX_COMPILER="$cxx"
  cmake --build "$build" --target \
    neoeng_v28_raa_selective_benchmark neoeng_v28_raa_selective_fuzz -j"$(nproc)"
  "$build/neoeng_v28_raa_selective_benchmark" "$result/benchmark" "$contacts" "$repetitions"
  "$build/neoeng_v28_raa_selective_fuzz" "$result/fuzz" "$fuzz_seeds" "$fuzz_contacts"
done

python3 "$root/scripts/compare_v28_stage4.py" \
  "$output_root/gcc/benchmark/selective_raa_results.csv" \
  "$output_root/clang/benchmark/selective_raa_results.csv" \
  "$output_root/gcc/fuzz/selective_raa_fuzz.csv" \
  "$output_root/clang/fuzz/selective_raa_fuzz.csv" \
  "$output_root/comparison"

echo "Stage 4 VM comparison complete. Timings are not bare-metal evidence: $output_root"
