#!/usr/bin/env bash
# Score a built binary's Rust graph against the rust-analyzer truth of the
# codex-rs corpus. Indexes the corpus into a private cache, runs the
# tools/rust-audit scripts, prints a summary and leaves full output in out-dir.
#
#   scripts/rust-score.sh <binary> [out-dir]
#   scripts/rust-score.sh --rescore <out-dir>   re-run the audit scripts on the
#       graph tables an earlier run left in out-dir (no indexing)
#
# Bench root (CBM_RUST_BENCH, default ~/.cache/cbm-rust-bench) holds:
#   codex/codex-rs  corpus at bc98fc69f8 (read-only: never edit or commit there)
#   truth.tsv refs.tsv  from tools/rust-audit/tsdiff (truth, refs binaries)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH="${CBM_RUST_BENCH:-$HOME/.cache/cbm-rust-bench}"
CORPUS="$BENCH/codex/codex-rs"
[[ -f "$CORPUS/Cargo.toml" && -s "$BENCH/truth.tsv" && -s "$BENCH/refs.tsv" ]] ||
    { echo "bench incomplete: $BENCH" >&2; exit 2; }
[[ "$(find "$CORPUS" -name '*.rs' -type f | wc -l | tr -d ' ')" == 4698 ]] ||
    { echo "corpus is not the 4,698-file tree: $CORPUS" >&2; exit 2; }
if [[ "${1:-}" == --rescore ]]; then
    OUT="${2:?usage: $0 --rescore <out-dir>}"; INDEX_S=-
    for t in g_nodes g_calls g_IMPORTS g_DEFINES_METHOD g_USAGE edge_types; do
        [[ -s "$OUT/$t.tsv" ]] || { echo "no $t.tsv in $OUT: run a full score first" >&2; exit 2; }
    done
    ln -sf "$BENCH/truth.tsv" "$BENCH/refs.tsv" "$OUT/"
else
    BIN="$1"; OUT="${2:-$(mktemp -d /tmp/cbm-score.XXXXXX)}"
    [[ -x "$BIN" ]] || { echo "binary not executable: $BIN" >&2; exit 2; }
    mkdir -p "$OUT/cache" "$OUT/rt"
    ln -sf "$BENCH/truth.tsv" "$BENCH/refs.tsv" "$OUT/"
    SECONDS=0
    CBM_CACHE_DIR="$OUT/cache" CBM_RUNTIME_DIR="$OUT/rt" \
        "$BIN" cli index_repository --repo-path "$CORPUS" --mode full --name score \
        > "$OUT/index.out" 2> "$OUT/index.err" || { echo "index failed: $OUT/index.err" >&2; exit 1; }
    INDEX_S=$SECONDS
    DB="file:$OUT/cache/score.db?mode=ro"
    q() { sqlite3 -separator $'\t' "$DB" "$1"; }
    q "select id,label,name,file_path,start_line,end_line from nodes where file_path like '%.rs'" > "$OUT/g_nodes.tsv"
    q "select source_id,target_id,coalesce(json_extract(properties,'\$.strategy'),'') from edges where type='CALLS'" > "$OUT/g_calls.tsv"
    for t in IMPORTS DEFINES_METHOD IMPLEMENTS USAGE; do
        q "select source_id,target_id from edges where type='$t'" > "$OUT/g_$t.tsv"
    done
    q "select type,count(*) from edges group by 1 order by 1" > "$OUT/edge_types.tsv"
fi
export AUDIT_DIR="$OUT" AUDIT_CORPUS="$CORPUS"
for s in join precision misses families; do
    python3 "$ROOT/tools/rust-audit/$s.py" > "$OUT/$s.txt" 2>&1 ||
        { echo "$s.py failed: $OUT/$s.txt" >&2; exit 1; }
done
python3 - "$OUT" "$INDEX_S" <<'EOF'
import re, sys
out, secs = sys.argv[1], sys.argv[2]
c = {}
for l in open(out + '/join.txt'):
    m = re.match(r'\s*(\d+)\s+(path|method|macro_arg): in corpus, fn caller, (PUBLISHED|missing)', l)
    if m: c[(m.group(2), m.group(3))] = int(m.group(1))
tp = tt = 0
print(f'out: {out}   index: {secs}s')
for k in ('path', 'method', 'macro_arg'):
    p, t = c[(k, 'PUBLISHED')], c[(k, 'PUBLISHED')] + c[(k, 'missing')]
    tp += p; tt += t
    print(f'calls {k:<10}{p:>7} / {t:<7} {100*p/t:5.1f}%')
print(f'calls ALL       {tp:>7} / {tt:<7} {100*tp/tt:5.1f}%')
wrong = sum(int(m.group(1)) for l in open(out + '/precision.txt')
            if (m := re.match(r'\s*(\d+)\s+[\d.]+%\s+(WRONG TARGET|NOT IN TRUTH)', l)))
print(f'calls wrong-or-unconfirmed pairs: {wrong}')
blind = sum(int(m.group(1)) for l in open(out + '/precision.txt')
            if (m := re.match(r'\s*(\d+)\s+[\d.]+%\s+unverifiable: (oracle left|no oracle record)', l)))
print(f'calls unverifiable pairs (oracle has no record at the call site): {blind}')
keep = re.compile(r'^\s+(macro|module|term:|type|fn:)|owner link correct|^impl links published|^IMPORTS|published Rust File|^\s+(Enum|Field|Function|Interface|Method|Struct|Type|Variable)\s+\d')
for l in open(out + '/families.txt'):
    if keep.search(l): print(l.rstrip()[:150])
EOF
