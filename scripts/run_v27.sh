#!/usr/bin/env bash
set -euo pipefail
build_dir="${1:-build-v27}"
out_dir="${2:-artifacts/v0.27-benchmark}"
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release -DNEOENG_WARNINGS_AS_ERRORS=ON
cmake --build "$build_dir" --target neoeng_tests neoeng_v27_fuzz neoeng_v27_benchmark neoeng_determinism_probe -j"${JOBS:-8}"
"$build_dir/neoeng_tests"
"$build_dir/neoeng_v27_fuzz" 5000
"$build_dir/neoeng_determinism_probe" 10000
"$build_dir/neoeng_v27_benchmark" "$out_dir"
