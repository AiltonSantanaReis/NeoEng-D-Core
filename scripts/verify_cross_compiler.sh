#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_and_probe() {
    local compiler="$1" build_dir="$2"
    rm -rf "$build_dir"
    cmake -S "$ROOT" -B "$build_dir" -DCMAKE_CXX_COMPILER="$compiler" \
        -DCMAKE_BUILD_TYPE=Release -DNEOENG_WARNINGS_AS_ERRORS=ON >/dev/null
    cmake --build "$build_dir" --target neoeng_determinism_probe neoeng_v26_fuzz -j8 >/dev/null
    "$build_dir/neoeng_determinism_probe"
    "$build_dir/neoeng_v26_fuzz" 2000
}
GCC_OUTPUT="$(build_and_probe g++ "$ROOT/build-cross-gcc")"
CLANG_OUTPUT="$(build_and_probe clang++ "$ROOT/build-cross-clang")"
printf '%s\n' "$GCC_OUTPUT" | sed 's/^/gcc:   /'
printf '%s\n' "$CLANG_OUTPUT" | sed 's/^/clang: /'
[[ "$GCC_OUTPUT" == "$CLANG_OUTPUT" ]] || { echo "Cross-compiler determinism check failed" >&2; exit 1; }
echo "Cross-compiler determinism check passed"
