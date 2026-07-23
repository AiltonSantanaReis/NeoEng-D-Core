#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CPU="${1:-0}"
CXX_BIN="${CXX:-g++}"
case "$(basename "$CXX_BIN")" in
  *clang*) COMPILER_TAG="clang" ;;
  *g++*|*gcc*) COMPILER_TAG="gcc" ;;
  *) COMPILER_TAG="$(printf '%s' "$(basename "$CXX_BIN")" | sed 's/[^[:alnum:]_.-]/_/g')" ;;
esac
OUT="${2:-$ROOT/artifacts/v0.28-bare-metal-$COMPILER_TAG}"
TRIALS="${3:-200}"
BUILD="${4:-$ROOT/build-v28-bare-$COMPILER_TAG}"
mkdir -p "$OUT"

virtualized=0
if grep -qi hypervisor /proc/cpuinfo; then
  virtualized=1
elif command -v systemd-detect-virt >/dev/null 2>&1 && [[ "$(systemd-detect-virt 2>/dev/null || true)" != "none" ]]; then
  virtualized=1
fi

{
  echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "cpu=$CPU"
  echo "cxx=$CXX_BIN"
  echo "compiler_tag=$COMPILER_TAG"
  echo "trials_per_run=$TRIALS"
  echo "virtualized=$virtualized"
  echo "thermal_review_required=1"
  echo "note=Create thermal_review.json only after reviewing hash-bound telemetry for all ten runs."
} > "$OUT/protocol_state.txt"

if [[ "$virtualized" == "1" && "${ALLOW_VIRTUALIZED:-0}" != "1" ]]; then
  echo "ERROR: hypervisor flag detected. Bare-metal collection refused." >&2
  echo "For an explicitly non-authoritative script test, set ALLOW_VIRTUALIZED=1." >&2
  exit 3
fi
if [[ "$virtualized" == "1" ]]; then
  echo "WARNING: hypervisor flag detected. Results are non-authoritative." >&2
fi

capture_system_state() {
  local destination="$1"
  {
    echo "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "uptime=$(cat /proc/uptime 2>/dev/null || true)"
    echo "loadavg=$(cat /proc/loadavg 2>/dev/null || true)"
    echo "cpu=$CPU"
    echo "--- memory ---"
    free -b 2>&1 || true
    echo "--- top processes ---"
    ps -eo pid,psr,comm,stat,ni,pri,pcpu,pmem --sort=-pcpu 2>&1 | head -50 || true
    for field in bios_vendor bios_version board_vendor board_name product_name product_version; do
      if [[ -r "/sys/class/dmi/id/$field" ]]; then
        printf '%s=' "$field"
        cat "/sys/class/dmi/id/$field"
      fi
    done
    for control in /sys/devices/system/cpu/intel_pstate/no_turbo /sys/devices/system/cpu/amd_pstate/status; do
      if [[ -r "$control" ]]; then
        printf '%s=' "$control"
        cat "$control"
      fi
    done
    if [[ -r "/sys/devices/system/cpu/cpu$CPU/cpufreq/scaling_cur_freq" ]]; then
      echo "scaling_cur_freq_khz=$(cat "/sys/devices/system/cpu/cpu$CPU/cpufreq/scaling_cur_freq")"
    fi
    if [[ -r "/sys/devices/system/cpu/cpu$CPU/cpufreq/scaling_governor" ]]; then
      echo "scaling_governor=$(cat "/sys/devices/system/cpu/cpu$CPU/cpufreq/scaling_governor")"
    fi
    for zone in /sys/class/thermal/thermal_zone*; do
      [[ -d "$zone" ]] || continue
      printf '%s_type=' "$(basename "$zone")"
      cat "$zone/type" 2>/dev/null || true
      printf '%s_temp_millic=' "$(basename "$zone")"
      cat "$zone/temp" 2>/dev/null || true
    done
    if command -v sensors >/dev/null 2>&1; then
      echo "--- sensors ---"
      sensors 2>&1 || true
    fi
  } > "$destination"
}

lscpu > "$OUT/lscpu.txt"
uname -a > "$OUT/uname.txt"
cat /proc/cmdline > "$OUT/kernel_cmdline.txt"
"$CXX_BIN" --version > "$OUT/cxx-version.txt" 2>&1
if command -v cpupower >/dev/null 2>&1; then cpupower frequency-info > "$OUT/cpupower.txt" 2>&1 || true; fi
if command -v git >/dev/null 2>&1 && git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git -C "$ROOT" rev-parse HEAD > "$OUT/git-commit.txt"
fi
python3 - "$ROOT" "$OUT/source_sha256.txt" <<'PY'
from hashlib import sha256
from pathlib import Path
import sys
root = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2])
excluded_parts = {".git", "artifacts", "__pycache__"}
rows = []
for path in sorted(root.rglob("*")):
    if not path.is_file():
        continue
    rel = path.relative_to(root)
    if any(part in excluded_parts or part.startswith("build-") for part in rel.parts):
        continue
    digest = sha256(path.read_bytes()).hexdigest()
    rows.append(f"{digest}  {rel.as_posix()}")
out.write_text("\n".join(rows) + "\n", encoding="utf-8")
PY

capture_system_state "$OUT/system-before.txt"
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DNEOENG_WARNINGS_AS_ERRORS=ON \
  -DCMAKE_CXX_COMPILER="$CXX_BIN"
cmake --build "$BUILD" --target \
  neoeng_v28_bare_metal_rollback \
  neoeng_v28_raa_allocation_probe \
  neoeng_determinism_probe \
  -j"${JOBS:-8}"

"$BUILD/neoeng_v28_raa_allocation_probe" 1024 8 | tee "$OUT/raa-allocation-probe.json"
"$BUILD/neoeng_determinism_probe" 10000 | tee "$OUT/determinism-probe.txt"

for run in $(seq 1 10); do
  run_dir="$OUT/run-$run"
  mkdir -p "$run_dir"
  capture_system_state "$run_dir/system-before.txt"
  "$BUILD/neoeng_v28_bare_metal_rollback" "$run_dir" "$TRIALS" "$CPU" | tee "$run_dir/stdout.txt"
  capture_system_state "$run_dir/system-after.txt"
done
capture_system_state "$OUT/system-after.txt"

review_file="$OUT/thermal_review.json"
if [[ ! -f "$review_file" ]]; then
  cat > "$OUT/collection_status.json" <<EOF
{
  "schema": "neoeng.v0.28.y1-g3-collection-status.v1",
  "collection_complete": true,
  "thermal_review_pending": true,
  "gate_passed": false,
  "virtualized": $([[ "$virtualized" == "1" ]] && echo true || echo false)
}
EOF
  echo "Collection complete. Y1-G3 remains pending until a hash-bound thermal_review.json is created." >&2
  exit 1
fi

eval_args=("$OUT" --thermal-review-file "$review_file")
if [[ "$virtualized" == "1" ]]; then eval_args+=(--virtualized); fi
python3 "$ROOT/scripts/evaluate_bare_metal_v28.py" "${eval_args[@]}"
