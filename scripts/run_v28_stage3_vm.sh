#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_ROOT="${1:-$ROOT_DIR/artifacts/v0.28-stage3-local}"
TIMING_REPETITIONS="${2:-16}"
MONTE_CARLO_SAMPLES="${3:-2048}"

if [[ ! "$TIMING_REPETITIONS" =~ ^[1-9][0-9]*$ ]] || [[ ! "$MONTE_CARLO_SAMPLES" =~ ^[1-9][0-9]*$ ]]; then
  echo "usage: $0 [output-root] [timing-repetitions] [monte-carlo-samples]" >&2
  exit 2
fi

mkdir -p "$OUT_ROOT"
BUILD_JOBS="${NEOENG_BUILD_JOBS:-8}"
if [[ ! "$BUILD_JOBS" =~ ^[1-9][0-9]*$ ]]; then
  echo "NEOENG_BUILD_JOBS must be a positive integer" >&2
  exit 2
fi

virt="unknown"
if command -v systemd-detect-virt >/dev/null 2>&1; then
  virt="$(systemd-detect-virt 2>/dev/null || true)"
  [[ -n "$virt" ]] || virt="none"
fi
printf '%s\n' "$virt" > "$OUT_ROOT/virtualization.txt"
uname -a > "$OUT_ROOT/uname.txt"

run_profile() {
  local name="$1" compiler="$2"
  local build="$OUT_ROOT/build-$name"
  local results="$OUT_ROOT/$name"
  mkdir -p "$results"
  cmake -S "$ROOT_DIR" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DNEOENG_WARNINGS_AS_ERRORS=ON \
    -DCMAKE_CXX_COMPILER="$compiler"
  cmake --build "$build" --target neoeng_v28_raa_decomposition_benchmark -j"$BUILD_JOBS"
  ctest --test-dir "$build" -R neoeng_v28_raa_decomposition --output-on-failure
  "$build/neoeng_v28_raa_decomposition_benchmark" \
    "$results" "$TIMING_REPETITIONS" "$MONTE_CARLO_SAMPLES"
}

run_profile gcc "${CXX_GCC:-g++}"
run_profile clang "${CXX_CLANG:-clang++}"

python3 - "$OUT_ROOT" "$TIMING_REPETITIONS" "$MONTE_CARLO_SAMPLES" <<'PY'
from pathlib import Path
import csv, json, sys
root=Path(sys.argv[1])
files={name: root/name/'raa_active_subset_decomposition.csv' for name in ('gcc','clang')}
rows={}
for name,p in files.items():
    with p.open(newline='') as f:
        rows[name]=list(csv.DictReader(f))
if len(rows['gcc']) != 18 or len(rows['clang']) != 18:
    raise SystemExit('expected exactly 18 rows per compiler')
key=lambda r:(r['active_bodies'],r['maximum_terms'])
non_timing=[c for c in rows['gcc'][0] if c not in {'p50_us','p95_us','p99_us','maximum_us','ns_per_body_step','ns_per_contact_step'}]
for a,b in zip(sorted(rows['gcc'],key=key), sorted(rows['clang'],key=key)):
    if key(a)!=key(b): raise SystemExit('row-key mismatch')
    for c in non_timing:
        if a[c]!=b[c]: raise SystemExit(f'non-timing mismatch {key(a)} {c}: {a[c]} != {b[c]}')
summary={
  'classification':'VM/comparative only; never approves Y1-G3',
  'virtualization':(root/'virtualization.txt').read_text().strip(),
  'rows_per_compiler':18,
  'non_timing_fields_equal':True,
  'timing_repetitions':int(sys.argv[2]),
  'monte_carlo_samples_per_row':int(sys.argv[3]),
}
(root/'comparison_summary.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
PY

echo "Stage 3 VM diagnostic completed. Results: $OUT_ROOT"
echo "This execution cannot approve Y1-G3."
