#!/usr/bin/env bash
# index.sh <binary> <corpus-dir> <out-dir>: full index of one corpus into a private cache and
# runtime dir, so the run never touches ~/.cbm or a live daemon. Prints one status line.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$1"; CORPUS="$2"; OUT="$3"
mkdir -p "$OUT/cache" "$OUT/rt"
t0=$(date +%s)
CBM_CACHE_DIR="$OUT/cache" CBM_RUNTIME_DIR="$OUT/rt" \
  "$ROOT/scripts/index-local.sh" "$BIN" "$CORPUS" graph full > "$OUT/index.out" 2> "$OUT/index.err"
rc=$?
echo "index $OUT rc=$rc secs=$(( $(date +%s) - t0 )) load: $(uptime | sed 's/.*load averages*: //')"
find "$OUT/cache" -name '*.db' -not -name '*-*' | head -3
