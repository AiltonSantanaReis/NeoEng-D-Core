#!/usr/bin/env bash
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:?usage: finalize_y1_g3_bare_metal_v28.sh <output-root>}"
status=0
for tag in gcc clang; do
  profile="$OUT/$tag"
  review="$profile/thermal_review.json"
  if [[ ! -f "$review" ]]; then
    echo "missing $review" >&2
    status=2
    continue
  fi
  python3 "$ROOT/scripts/evaluate_bare_metal_v28.py" "$profile" --thermal-review-file "$review" || profile_status=$?
  profile_status=${profile_status:-0}
  if [[ "$profile_status" -gt "$status" ]]; then status="$profile_status"; fi
  unset profile_status
done
python3 "$ROOT/scripts/evaluate_bare_metal_matrix_v28.py" "$OUT" || matrix_status=$?
matrix_status=${matrix_status:-0}
if [[ "$matrix_status" -gt "$status" ]]; then status="$matrix_status"; fi
python3 - "$OUT" <<'PY'
from hashlib import sha256
from pathlib import Path
import sys, zipfile
root = Path(sys.argv[1]).resolve()
archive = root.with_name(root.name + "-results.zip")
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as zf:
    for path in sorted(root.rglob("*")):
        if path.is_file():
            zf.write(path, path.relative_to(root.parent))
digest = sha256(archive.read_bytes()).hexdigest()
archive.with_suffix(archive.suffix + ".sha256").write_text(f"{digest}  {archive.name}\n", encoding="utf-8")
print(archive)
print(digest)
PY
exit "$status"
