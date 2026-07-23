#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPU="${1:?usage: run_y1_g3_bare_metal_v28.sh <CPU> [output-root] [samples]}"
OUT="${2:-$ROOT/artifacts/v0.28-y1-g3-bare-metal}"
SAMPLES="${3:-200}"
[[ "$SAMPLES" =~ ^[0-9]+$ && "$SAMPLES" -ge 200 ]] || { echo "samples must be an integer >= 200" >&2; exit 2; }
[[ ! -e "$OUT" ]] || { echo "refusing to mix results: output already exists: $OUT" >&2; exit 2; }
mkdir -p "$OUT"
"$ROOT/scripts/preflight_bare_metal_v28.sh" "$OUT/preflight" "$CPU"

TELEMETRY_PID=""
cleanup_telemetry() {
  if [[ -n "$TELEMETRY_PID" ]]; then
    kill -TERM "$TELEMETRY_PID" 2>/dev/null || true
    wait "$TELEMETRY_PID" 2>/dev/null || true
    TELEMETRY_PID=""
  fi
}
trap cleanup_telemetry EXIT INT TERM

run_profile() {
  local tag="$1" cxx="$2" profile="$OUT/$tag"
  mkdir -p "$profile"
  python3 "$ROOT/scripts/collect_telemetry_v28.py" "$profile/telemetry.jsonl" "$CPU" --interval 0.25 &
  TELEMETRY_PID=$!
  set +e
  CXX="$cxx" "$ROOT/scripts/run_bare_metal_v28.sh" "$CPU" "$profile" "$SAMPLES" "$ROOT/build-v28-bare-$tag"
  local status=$?
  set -e
  cleanup_telemetry
  python3 "$ROOT/scripts/summarize_telemetry_v28.py" "$profile/telemetry.jsonl" --output "$profile/telemetry_summary.json"
  if [[ "$status" -eq 1 && -f "$profile/collection_status.json" ]]; then
    : # expected pending-thermal-review state
  elif [[ "$status" -ne 0 ]]; then
    echo "profile $tag failed or collection is incomplete (status $status)" >&2
    exit "$status"
  fi
}

run_profile gcc g++
run_profile clang clang++
cat > "$OUT/NEXT_STEP.txt" <<EOF
Collection completed without thermal approval.
Review both telemetry_summary.json files and the raw telemetry.jsonl files.
Then create one hash-bound review per profile with review_thermal_v28.py and run finalize_y1_g3_bare_metal_v28.sh.
EOF
printf 'Collection complete at %s\n' "$OUT"
printf 'Y1-G3 is NOT approved until thermal review and final evaluation are complete.\n'
