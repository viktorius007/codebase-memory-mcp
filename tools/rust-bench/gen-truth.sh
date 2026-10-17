#!/usr/bin/env bash
# gen-truth.sh [bench-dir] [name...]: regenerate <bench>/<name>/{truth,refs}.tsv for the named
# corpora (default all) from a rust-analyzer SCIP index of the fetched checkout. The truth is a
# function of the corpus commit and the rust-analyzer version, so the version is pinned: a
# different one produces a different truth, and every frozen score is relative to this one.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=corpora.sh
source "$HERE/corpora.sh"
# The release commit, in full: builds print it abbreviated to different lengths (macOS 8, Linux 7).
RA_RELEASE=1.99.0; RA_COMMIT=b940084d7eb6a299eb4bfeb8e34901bc051e7ac4
RA="${CBM_RUST_ANALYZER:-$(rustup which --toolchain 1.99.0 rust-analyzer)}"
BENCH=$(bench_dir "${1:-}"); shift || true
NAMES="${*:-$(corpus_names)}"
given=$("$RA" --version)
[[ "$given" =~ ^rust-analyzer\ $RA_RELEASE\ \(([0-9a-f]{7,})\  && "$RA_COMMIT" == "${BASH_REMATCH[1]}"* ]] ||
  { echo "rust-analyzer at $RA is '$given', expected release $RA_RELEASE at commit $RA_COMMIT" >&2; exit 2; }
TARGET="${CARGO_TARGET_DIR:-$BENCH/.tsdiff-target}"
(cd "$HERE/../rust-audit/tsdiff" && CARGO_TARGET_DIR="$TARGET" cargo build --release --quiet)
T="$TARGET/release"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
for name in $NAMES; do
  root="$BENCH/$name"; corpus=$(corpus_dir "$BENCH" "$name")
  [ -d "$corpus" ] || { echo "no checkout at $corpus: run fetch-corpora.sh first" >&2; exit 2; }
  SECONDS=0
  # rust-analyzer runs build scripts through cargo; their output stays out of the scored corpus.
  (cd "$corpus" && CARGO_TARGET_DIR="$TMP/target" "$RA" scip . --output "$TMP/$name.scip") > "$root/scip.log" 2>&1 ||
    { echo "$name: rust-analyzer scip failed, log $root/scip.log" >&2; exit 1; }
  (cd "$root" && "$T/truth" "$TMP/$name.scip" "$corpus" truth.tsv > truth.log 2>&1) ||
    { echo "$name: truth failed, log $root/truth.log" >&2; exit 1; }
  (cd "$root" && "$T/refs" "$TMP/$name.scip" "$corpus" refs.tsv > refs.log 2>&1) ||
    { echo "$name: refs failed, log $root/refs.log" >&2; exit 1; }
  echo "OK: $name truth $(wc -l < "$root/truth.tsv" | tr -d ' ') refs $(wc -l < "$root/refs.tsv" | tr -d ' ') rows, ${SECONDS}s"
done
