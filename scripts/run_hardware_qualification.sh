#!/usr/bin/env bash
set -euo pipefail
if [[ $# -lt 3 ]]; then
  echo "usage: $0 REQUEST_JSON BUILD_DIRECTORY OUTPUT_DIRECTORY" >&2
  exit 2
fi
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
python3 "$root/scripts/qualification/run_qualification_campaign.py" \
  --request "$1" --build-dir "$2" --output-dir "$3"
