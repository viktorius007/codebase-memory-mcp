#!/usr/bin/env bash
# baseline.sh <binary> <commit> [bench-dir]: freeze the base score the gate diffs against, by
# scoring every Rust corpus with <binary> (built from <commit>) into <bench>/rust-base.
# Refuses an existing rust-base: trash the old one first.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=corpora.sh
source "$HERE/corpora.sh"
BIN="$1"; SHA="$2"; BENCH=$(bench_dir "${3:-}"); BASE="$BENCH/rust-base"
[ ! -e "$BASE" ] || { echo "$BASE exists: trash it before freezing a new base" >&2; exit 2; }
mkdir -p "$BASE"
score_one() { # <name>
  local name="$1"
  mkdir -p "$BASE/$name"
  CBM_RUST_BENCH="$BENCH/$name" CBM_RUST_CORPUS=$(corpus_dir "$BENCH" "$name") \
    "$HERE/../../scripts/rust-score.sh" "$BIN" "$BASE/$name" > "$BASE/$name/score.log" 2>&1 ||
    { tail -40 "$BASE/$name/score.log" >&2; echo "$name: score failed, log $BASE/$name/score.log (last 40 lines above)" >&2; exit 1; }
  echo "OK: $name $(grep -E '^calls ALL' "$BASE/$name/score.log")"
}
# shellcheck disable=SC2046 # corpus names are single words
parallel_each score_one $(corpus_names)
echo "$SHA" > "$BASE/BINARY_SHA"
