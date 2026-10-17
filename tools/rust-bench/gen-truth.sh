#!/usr/bin/env bash
# gen-truth.sh [bench-dir] [name...]: regenerate <bench>/<name>/{truth,refs}.tsv for the named
# corpora (default all) from a rust-analyzer SCIP index of the fetched checkout. The truth is a
# function of the corpus commit, the rust-analyzer version and the target platform, so all are
# pinned: a different one produces a different truth, and every frozen score is relative to this one.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=corpora.sh
source "$HERE/corpora.sh"
# The release commit, in full: builds print it abbreviated to different lengths (macOS 8, Linux 7).
RA_RELEASE=1.99.0; RA_COMMIT=b940084d7eb6a299eb4bfeb8e34901bc051e7ac4
RA="${CBM_RUST_ANALYZER:-$(rustup which --toolchain 1.99.0 rust-analyzer)}"
# rust-analyzer resolves #[cfg(target_os = ...)] code for one platform, by default the host's, so
# a Linux host left that code out and wrote a different truth from the recorded one, made on
# Apple Silicon macOS. Every host now analyses for that platform.
TRUTH_TARGET=aarch64-apple-darwin
BENCH=$(bench_dir "${1:-}"); shift || true
NAMES="${*:-$(corpus_names)}"
given=$("$RA" --version)
[[ "$given" =~ ^rust-analyzer\ $RA_RELEASE\ \(([0-9a-f]{7,})\  && "$RA_COMMIT" == "${BASH_REMATCH[1]}"* ]] ||
  { echo "rust-analyzer at $RA is '$given', expected release $RA_RELEASE at commit $RA_COMMIT" >&2; exit 2; }
TARGET="${CARGO_TARGET_DIR:-$BENCH/.tsdiff-target}"
(cd "$HERE/../rust-audit/tsdiff" && CARGO_TARGET_DIR="$TARGET" cargo build --release --quiet)
T="$TARGET/release"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
printf '{"cargo": {"target": "%s"}}\n' "$TRUTH_TARGET" > "$TMP/ra-config.json"
die() { echo "$1: $2 failed, log $3; its last 40 lines:" >&2; tail -40 "$3" >&2; exit 1; } # <name> <step> <log>
# rust-analyzer loads the corpus through cargo and rustc with rustup auto-install off, and reads
# the standard library from the toolchain's rust-src, so the truth depends on that toolchain.
# Installed one at a time: concurrent rustup installs share its download directory.
for name in $NAMES; do
  root="$BENCH/$name"; corpus=$(corpus_dir "$BENCH" "$name")
  [ -d "$corpus" ] || { echo "no checkout at $corpus: run fetch-corpora.sh first" >&2; exit 2; }
  toolchain=$(corpus_field "$name" 5)
  { rustup toolchain install "$toolchain" --profile minimal --component rust-src &&
    rustup target add "$TRUTH_TARGET" --toolchain "$toolchain"; } > "$root/toolchain.log" 2>&1 ||
    die "$name" "rustup toolchain install $toolchain" "$root/toolchain.log"
done
truth_one() { # <name>
  local name="$1" root="$BENCH/$1" corpus toolchain; SECONDS=0
  corpus=$(corpus_dir "$BENCH" "$name"); toolchain=$(corpus_field "$name" 5)
  # Build-script output stays out of the scored corpus.
  (cd "$corpus" && RUSTUP_TOOLCHAIN="$toolchain" CARGO_TARGET_DIR="$TMP/$name.target" "$RA" scip . --config-path "$TMP/ra-config.json" --output "$TMP/$name.scip") > "$root/scip.log" 2>&1 ||
    die "$name" "rust-analyzer scip" "$root/scip.log"
  (cd "$root" && "$T/truth" "$TMP/$name.scip" "$corpus" truth.tsv > truth.log 2>&1) ||
    die "$name" "truth" "$root/truth.log"
  (cd "$root" && "$T/refs" "$TMP/$name.scip" "$corpus" refs.tsv > refs.log 2>&1) ||
    die "$name" "refs" "$root/refs.log"
  echo "OK: $name truth $(wc -l < "$root/truth.tsv" | tr -d ' ') refs $(wc -l < "$root/refs.tsv" | tr -d ' ') rows, ${SECONDS}s"
}
# One at a time unless CBM_TRUTH_JOBS says otherwise: rust-analyzer peaks at 19 GB on codex, and
# two side by side on a 32 GB machine thrashed for two hours without finishing.
# shellcheck disable=SC2086 # NAMES is a word list
CBM_BENCH_JOBS="${CBM_TRUTH_JOBS:-1}" parallel_each truth_one $NAMES
