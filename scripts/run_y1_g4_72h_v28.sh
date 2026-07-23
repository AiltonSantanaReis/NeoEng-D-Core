#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 2 ]]; then echo "usage: $0 <build-dir> <output-dir> [hours]" >&2; exit 2; fi
build_dir=$1; out=$2; hours=${3:-72}
exe="$build_dir/neoeng_v28_year1_preclosure"
[[ -x "$exe" ]] || { echo "missing executable: $exe" >&2; exit 2; }
mkdir -p "$out"
start=$(date +%s); deadline=$((start + hours*3600)); run=0
: > "$out/runs.log"
while (( $(date +%s) < deadline )); do
  run=$((run+1)); run_dir="$out/run-$run"
  "$exe" history "$run_dir" 1000000 | tee -a "$out/runs.log"
  grep -q '^passed=true$' "$out/runs.log" || { echo "a history run failed" >&2; exit 1; }
done
end=$(date +%s)
printf 'requested_hours=%s\nstart_epoch=%s\nend_epoch=%s\nelapsed_seconds=%s\nruns=%s\npassed=true\n' "$hours" "$start" "$end" "$((end-start))" "$run" | tee "$out/summary.txt"
