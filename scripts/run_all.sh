#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DNEOENG_WARNINGS_AS_ERRORS=ON
cmake --build "$BUILD" --target neoeng_tests neoeng_v25_fuzz neoeng_v26_fuzz neoeng_v25_benchmark neoeng_v26_benchmark neoeng_determinism_probe -j8
ctest --test-dir "$BUILD" -R '^(neoeng_tests|neoeng_v25_fuzz|neoeng_v26_fuzz)$' --output-on-failure
"$ROOT/scripts/verify_cross_compiler.sh"
"$BUILD/neoeng_v26_fuzz" 5000
"$BUILD/neoeng_v25_benchmark" "$ROOT/artifacts/v0.26-regression"
"$BUILD/neoeng_v26_benchmark" "$ROOT/artifacts/v0.26-benchmark"
