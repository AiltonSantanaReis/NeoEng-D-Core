#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if ! command -v aarch64-linux-gnu-g++ >/dev/null 2>&1; then
    echo "aarch64-linux-gnu-g++ is not installed; no ARM64 build was performed." >&2
    exit 2
fi
cmake -S "$ROOT" -B "$ROOT/build-aarch64" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchains/aarch64-linux-gnu.cmake"
cmake --build "$ROOT/build-aarch64" --target neoeng_tests neoeng_v26_fuzz neoeng_determinism_probe -j2
