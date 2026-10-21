#!/usr/bin/env bash
# gate.sh <clone-dir> <scratch-dir> [bench-dir]
# Merge gate. Suites run alone (timing guards); lint and the scores then run in parallel.
# Build, eight suites (five Rust plus the three Python suites that caught ba3ad3b2), lint-ci,
# C/C++ graph identity, the score of every corpus in corpora.tsv, and the exact edge-set diff
# against the frozen base <bench>/rust-base (baseline.sh). Agents run it, never edit it.
# Output: <scratch>/gate/SUMMARY.txt (read this), logs and per-corpus dirs beside it.
# Exit 0 only when every step passed. The previous gate dir is emptied first.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=corpora.sh
source "$HERE/corpora.sh"
C="$1"; S="$2"; BENCH=$(bench_dir "${3:-}"); G="$S/gate"
BASE=$BENCH/rust-base
CORPORA=$(corpus_names)
mkdir -p "$G"; find "$G" -mindepth 1 -delete
ok=1
step(){ local n=$1 t0; shift; t0=$(date +%s); "$@" > "$G/$n.log" 2>&1; local rc=$?
  echo "$n: $([ $rc = 0 ] && echo PASS || echo FAIL) rc=$rc $(( $(date +%s)-t0 ))s log=$G/$n.log" >> "$G/SUMMARY.txt"
  [ $rc = 0 ] || ok=0; return $rc; }
cd "$C" || exit 2
# ccache on for build and suites: scripts/env.sh puts the masquerade dir on PATH; the
# cache is content-verified (CCACHE_COMPILERCHECK=content) so a hit is never stale.
# env.sh sets -e; this script continues past failing steps, so restore +e.
source scripts/env.sh >/dev/null 2>&1; set +e
echo "gate: base $(cat "$BASE/BINARY_SHA") at $BASE; $(git rev-parse --short=8 HEAD) ($(git rev-parse --abbrev-ref HEAD)) in $C at $(date '+%F %T'); uncommitted files: $(git status --porcelain | wc -l | tr -d ' ')" > "$G/SUMMARY.txt"
step build make -f Makefile.cbm cbm BUILD_DIR="$S/build" -j"$(getconf _NPROCESSORS_ONLN)" || { echo "GATE: FAIL (build)" >> "$G/SUMMARY.txt"; cat "$G/SUMMARY.txt"; exit 1; }
BIN="$S/build/codebase-memory-mcp"
step suites env CBM_RUNTIME_DIR="$S/rt" CBM_CACHE_DIR="$S/cache" \
  scripts/test.sh --suites "rust_lsp pipeline edge_structural lang_contract extraction call_reference_contract edge_imports incremental" BUILD_DIR="$S/build-tests"
grep -E -i '[0-9]+ (passed|failed)|FAIL' "$G/suites.log" | tail -4 | sed 's/^/  /' >> "$G/SUMMARY.txt"
for c in $CORPORA; do
  ( t0=$(date +%s)
    CBM_RUST_BENCH=$BENCH/$c CBM_RUST_CORPUS=$(corpus_dir "$BENCH" "$c") \
    scripts/rust-score.sh "$BIN" "$G/score-$c" > "$G/score-$c.log" 2>&1; echo "EXIT=$? $(( $(date +%s)-t0 ))s" >> "$G/score-$c.log" ) &
done
step lint make -f Makefile.cbm lint-ci
step cpp bash scripts/graph-identity/check.sh "$BIN" "$BENCH/cpp" "$G"; grep -E "^cpp-" "$G/cpp.log" | sed "s/^/  /" >> "$G/SUMMARY.txt"
wait
for c in $CORPORA; do
  ex=$(sed -n 's/^EXIT=//p' "$G/score-$c.log" | tail -1)
  if [ "${ex%% *}" != 0 ]; then echo "score-$c: FAIL exit=$ex log=$G/score-$c.log" >> "$G/SUMMARY.txt"; ok=0; continue; fi
  new=$(grep -E '^calls ALL' "$G/score-$c.log" | awk '{print $3"/"$5}'); nf=$(sed -n 's/^calls wrong-or-unconfirmed pairs: //p' "$G/score-$c.log")
  old=$(grep -E '^calls ALL' "$BASE/$c/score.log" | awk '{print $3"/"$5}'); of=$(sed -n 's/^calls wrong-or-unconfirmed pairs: //p' "$BASE/$c/score.log")
  python3 "$HERE/edgediff.py" "$BASE/$c/cache/score.db" "$G/score-$c/cache/score.db" "$G/edges-$c" > "$G/edges-$c.txt" 2>&1
  echo "score-$c: calls $new flagged $nf (base $old flagged $of) ${ex#* }; $(head -1 "$G/edges-$c.txt"); per-type $G/edges-$c.txt lists $G/edges-$c/{removed,added}.tsv" >> "$G/SUMMARY.txt"
  nc=${new%%/*}; oc=${old%%/*}
  { [ "$nc" -lt "$oc" ] || [ "$nf" -gt "$of" ]; } && echo "  WARNING score-$c: credited fell or flagged rose against base" >> "$G/SUMMARY.txt"
done
echo "GATE: $([ $ok = 1 ] && echo PASS || echo FAIL)   disk: $(du -sh "$S" | cut -f1)" >> "$G/SUMMARY.txt"
cat "$G/SUMMARY.txt"; [ $ok = 1 ]
