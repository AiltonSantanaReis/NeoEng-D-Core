#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: $0 <build-dir> <output-dir> [compiler-label]" >&2
  exit 2
fi
build_dir=$1
out=$2
label=${3:-unknown}
exe="$build_dir/neoeng_v28_year1_preclosure"
[[ -x "$exe" ]] || { echo "missing executable: $exe" >&2; exit 2; }
mkdir -p "$out"

"$exe" replay "$out/replay" 1000000 | tee "$out/replay-result.txt"
"$exe" history "$out/history-1m" 1000000 | tee "$out/history-result.txt"

mkdir -p "$out/network"
: > "$out/network/results.txt"
for seed in $(seq 1 32); do
  seed_hex=$(printf '0x280600000000%04X' "$seed")
  "$exe" network "$out/network/seed-$seed" 5000 "$seed_hex" | tee -a "$out/network/results.txt"
done

python3 - "$out" "$label" <<'PY'
import json, pathlib, re, sys
root=pathlib.Path(sys.argv[1]); label=sys.argv[2]
def parse(path):
    d={}
    for line in pathlib.Path(path).read_text().splitlines():
        if '=' in line:
            k,v=line.split('=',1); d[k]=v
    return d
replay=parse(root/'replay-result.txt')
history=parse(root/'history-result.txt')
blocks=[]; current={}
for line in (root/'network/results.txt').read_text().splitlines():
    if '=' in line:
        k,v=line.split('=',1); current[k]=v
        if k=='passed': blocks.append(current); current={}
summary={
  'compiler_label': label,
  'replay': replay,
  'history': history,
  'network_runs': len(blocks),
  'network_passed': sum(b.get('passed')=='true' for b in blocks),
  'network_unique_hashes': sorted({b.get('server_hash') for b in blocks}),
  'all_passed': replay.get('passed')=='true' and history.get('passed')=='true' and len(blocks)==32 and all(b.get('passed')=='true' for b in blocks),
}
(root/'year1-remaining-gates-summary.json').write_text(json.dumps(summary,indent=2)+'\n')
if not summary['all_passed']: raise SystemExit(1)
PY
