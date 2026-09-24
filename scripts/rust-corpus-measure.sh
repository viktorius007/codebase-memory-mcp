#!/usr/bin/env bash
# Measure Rust CALLS accuracy of a built binary against the rust-analyzer SCIP
# index of a corpus copy. Builds nothing; indexes the corpus into private
# cache/runtime roots with CBM_RUST_RESOLVE_DUMP and scores the dump with
# scripts/scip-site-join.py. Prints the report path and the recall line.
#
#   scripts/rust-corpus-measure.sh <binary> <corpus-dir> <scip-index> <out-dir>
#
# out-dir receives: dump.tsv, cache/, index.log, report.txt, join/ (samples,
# rows). Re-running into the same out-dir overwrites it.
set -euo pipefail
BIN="$1"; CORPUS="$2"; SCIP="$3"; OUT="$4"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
[[ -x "$BIN" ]] || { echo "binary not executable: $BIN" >&2; exit 2; }
[[ -f "$CORPUS/Cargo.toml" ]] || { echo "corpus is not a Cargo workspace: $CORPUS" >&2; exit 2; }
[[ -f "$SCIP" ]] || { echo "missing SCIP index: $SCIP" >&2; exit 2; }
rm -rf "$OUT"; mkdir -p "$OUT/cache" "$OUT/join"
RT="$(mktemp -d /tmp/cbm-measure-rt.XXXXXX)"
trap 'rm -rf "$RT"' EXIT
NAME="corpus"
{ time CBM_CACHE_DIR="$OUT/cache" CBM_RUNTIME_DIR="$RT" CBM_RUST_RESOLVE_DUMP="$OUT/dump.tsv" \
    "$BIN" cli index_repository --repo-path "$CORPUS" --mode full --name "$NAME"; } \
    > "$OUT/index.log" 2>&1 || { echo "index failed; see $OUT/index.log" >&2; exit 1; }
DB="$OUT/cache/$NAME.db"
[[ -s "$DB" ]] || { echo "graph db missing or empty: $DB" >&2; exit 1; }
python3 "$ROOT/scripts/scip-site-join.py" --corpus "$CORPUS" --dump "$OUT/dump.tsv" \
    --db "$DB" --scip "$SCIP" --out "$OUT/join" > "$OUT/report.txt" 2>&1 || \
    { echo "join failed; see $OUT/report.txt" >&2; exit 1; }
CALLS="$(sqlite3 "$DB" "select count(*) from edges where type='CALLS'" 2>/dev/null || echo '?')"
echo "report: $OUT/report.txt"
echo "CALLS edges: $CALLS"
grep -E '^truth sites' "$OUT/report.txt"
grep -E '^lsp_[a-z_]+ +correct=' "$OUT/report.txt" | sed -E 's/ +/ /g' | awk '{print $1, $2, $3, $4}' | tr '\n' ';'; echo
