#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$ROOT/artifacts/v0.28-bare-metal-preflight}"
CPU="${2:-}"
mkdir -p "$OUT"

fail=0
log="$OUT/preflight.txt"
: > "$log"
record() { printf '%s\n' "$*" | tee -a "$log"; }
require_cmd() {
  if command -v "$1" >/dev/null 2>&1; then record "PASS command $1=$(command -v "$1")";
  else record "FAIL missing command: $1"; fail=1; fi
}

record "NeoEng v0.28 Y1-G3 bare-metal preflight"
record "date_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
record "root=$ROOT"
record "kernel=$(uname -srmo)"
record "architecture=$(uname -m)"
[[ "$(uname -s)" == "Linux" ]] || { record "FAIL native Linux is required by this authoritative runner"; fail=1; }
[[ "$(uname -m)" == "x86_64" ]] || { record "FAIL this Y1-G3 profile requires x86_64"; fail=1; }

virtualized=0
if grep -qi hypervisor /proc/cpuinfo 2>/dev/null; then virtualized=1; fi
if command -v systemd-detect-virt >/dev/null 2>&1; then
  detected="$(systemd-detect-virt 2>/dev/null || true)"
  [[ -z "$detected" || "$detected" == "none" ]] || virtualized=1
  record "systemd_detect_virt=${detected:-unavailable}"
fi
record "virtualized=$virtualized"
[[ "$virtualized" == 0 ]] || { record "FAIL virtualized environment cannot approve Y1-G3"; fail=1; }

for cmd in bash cmake python3 g++ clang++ sha256sum lscpu uname ps awk sed grep; do require_cmd "$cmd"; done

if [[ -n "$CPU" ]]; then
  [[ "$CPU" =~ ^[0-9]+$ ]] || { record "FAIL CPU must be a non-negative integer"; fail=1; }
  if [[ -r "/sys/devices/system/cpu/cpu$CPU/online" ]] && [[ "$(cat "/sys/devices/system/cpu/cpu$CPU/online")" != 1 ]]; then
    record "FAIL cpu$CPU is offline"; fail=1
  fi
  if [[ ! -d "/sys/devices/system/cpu/cpu$CPU" ]]; then record "FAIL cpu$CPU does not exist"; fail=1; fi
  record "requested_cpu=$CPU"
  if [[ -r "/sys/devices/system/cpu/cpu$CPU/topology/thread_siblings_list" ]]; then
    record "thread_siblings=$(cat "/sys/devices/system/cpu/cpu$CPU/topology/thread_siblings_list")"
  fi
  if [[ -r "/sys/devices/system/cpu/cpu$CPU/cpufreq/scaling_governor" ]]; then
    record "governor=$(cat "/sys/devices/system/cpu/cpu$CPU/cpufreq/scaling_governor")"
  else
    record "FAIL cpufreq governor is unavailable for cpu$CPU"; fail=1
  fi
else
  record "FAIL explicit CPU index is required; inspect lscpu -e and choose one logical CPU consistently"; fail=1
fi

thermal_sources=$(find /sys/class/hwmon /sys/class/thermal -type f \( -name 'temp*_input' -o -name temp \) -readable 2>/dev/null | wc -l || true)
record "thermal_sources=$thermal_sources"
[[ "$thermal_sources" -gt 0 ]] || { record "FAIL no readable thermal source; load the platform sensor driver before the gate"; fail=1; }

lscpu -e=CPU,CORE,SOCKET,NODE,ONLINE,MAXMHZ,MINMHZ > "$OUT/lscpu-e.txt" 2>&1 || true
lscpu > "$OUT/lscpu.txt" 2>&1 || true
cat /proc/cmdline > "$OUT/kernel_cmdline.txt" 2>/dev/null || true
cat /proc/cpuinfo > "$OUT/cpuinfo.txt" 2>/dev/null || true
cat /proc/meminfo > "$OUT/meminfo.txt" 2>/dev/null || true
g++ --version > "$OUT/gcc-version.txt" 2>&1 || true
clang++ --version > "$OUT/clang-version.txt" 2>&1 || true
cmake --version > "$OUT/cmake-version.txt" 2>&1 || true
python3 --version > "$OUT/python-version.txt" 2>&1 || true

if [[ "$fail" != 0 ]]; then
  record "PREFLIGHT=FAILED"
  exit 3
fi
record "PREFLIGHT=PASSED"
