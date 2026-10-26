#!/usr/bin/env bash
# check.sh <binary> <corpora-dir> <out-dir> [freeze]
# Graph identity for the C/C++ corpora: index each corpus under <corpora-dir> with <binary> and
# compare the complete node and edge multisets (graphcmp.py) against the frozen base graph
# <corpora-dir>/base/<corpus>/graph.db. "freeze" writes that base from this binary and records
# the digests in DIGESTS beside this script. SEMANTICALLY_RELATED edges are reported on their own
# "semantic:" line and do not decide the verdict. Exit 0 only when every corpus is IDENTICAL or frozen.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN="$1"; CPP="$2"; G="$3"; MODE="${4:-check}"
CORPORA="react-native-0.88 bitcoin-v27"
mkdir -p "$G"; ok=1
for c in $CORPORA; do
  ( bash "$HERE/index.sh" "$BIN" "$CPP/$c" "$G/cpp-$c" > "$G/cpp-$c.log" 2>&1 ) &
done
wait
for c in $CORPORA; do
  db=$(find "$G/cpp-$c/cache" -name 'graph.db' | head -1)
  if [ -z "$db" ]; then echo "cpp-$c: FAIL no graph.db ($(head -1 "$G/cpp-$c.log"))"; ok=0; continue; fi
  if [ "$MODE" = freeze ]; then
    mkdir -p "$CPP/base/$c" && cp "$db" "$CPP/base/$c/graph.db"
    d=$(python3 -I "$HERE/graphcmp.py" "$db" "$db" | sed -n 's/^edges:.*digest before=\([0-9a-f]*\).*/\1/p')
    n=$(python3 -I "$HERE/graphcmp.py" "$db" "$db" | sed -n 's/^nodes:.*digest before=\([0-9a-f]*\).*/\1/p')
    echo "cpp-$c: FROZEN nodes=$n edges=$d -> $CPP/base/$c/graph.db"
    continue
  fi
  base="$CPP/base/$c/graph.db"
  if [ ! -f "$base" ]; then echo "cpp-$c: FAIL no frozen base at $base (run with freeze on a trusted binary)"; ok=0; continue; fi
  python3 -I "$HERE/graphcmp.py" "$base" "$db" > "$G/cpp-$c.cmp" 2>&1
  verdict=$(tail -1 "$G/cpp-$c.cmp")
  echo "cpp-$c: $([ "$verdict" = IDENTICAL ] && echo PASS || echo FAIL) $verdict $(grep -E '^edges:' "$G/cpp-$c.cmp" | head -1) $(grep -E '^semantic:' "$G/cpp-$c.cmp" | head -1 | cut -d' ' -f1-5) diff=$G/cpp-$c.cmp"
  [ "$verdict" = IDENTICAL ] || ok=0
done
[ $ok = 1 ]
