#!/usr/bin/env bash
# rebuild.sh <base-sha> [bench-dir]: build the whole bench from nothing. Fetches every corpus,
# generates the Rust truth, builds the binary at <base-sha> and freezes the Rust and C/C++ base
# graphs from it. Ends with one line: REBUILD: PASS or REBUILD: FAIL <step>. The C/C++ freeze
# rewrites scripts/graph-identity/DIGESTS; the run fails when that differs from the committed
# file, which means this machine indexes the C/C++ corpora differently from the recorded base.
# It also fails when fingerprint.py's hashes of the corpora, truth and base edges differ from
# FINGERPRINTS, the values of the recorded bench; regenerate that file only on a deliberate rebase.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
# shellcheck source=corpora.sh
source "$HERE/corpora.sh"
SHA="${1:?usage: $0 <base-sha> [bench-dir]}"; BENCH=$(bench_dir "${2:-}")
fail() { echo "REBUILD: FAIL $1" >&2; exit 1; }
for tool in gh rustup cargo make python3 sqlite3 curl shasum tar git; do
  command -v "$tool" > /dev/null || fail "prerequisite: $tool not on PATH"
done
gh auth status > /dev/null || fail "prerequisite: gh is not logged in (gh auth login)"
rustup toolchain install 1.99.0 --profile minimal --component rust-analyzer > /dev/null ||
  fail "prerequisite: rustup toolchain install 1.99.0 --component rust-analyzer"
[ ! -e "$BENCH/rust-base" ] && [ ! -e "$BENCH/cpp/base" ] ||
  fail "existing base in $BENCH: trash rust-base and cpp/base first"
"$HERE/fetch-corpora.sh" "$BENCH" || fail fetch
"$HERE/gen-truth.sh" "$BENCH" || fail truth
SRC="$BENCH/.base-src"; BUILD="$BENCH/.base-build"
git -C "$ROOT" worktree add --detach "$SRC" "$SHA" > /dev/null || fail "worktree at $SHA"
trap 'git -C "$ROOT" worktree remove --force "$SRC"; rm -rf "$BUILD"' EXIT
make -C "$SRC" -f Makefile.cbm cbm BUILD_DIR="$BUILD" -j8 > "$BENCH/base-build.log" 2>&1 ||
  fail "build of $SHA, log $BENCH/base-build.log"
"$HERE/baseline.sh" "$BUILD/codebase-memory-mcp" "$SHA" "$BENCH" || fail baseline
bash "$ROOT/scripts/graph-identity/check.sh" "$BUILD/codebase-memory-mcp" "$BENCH/cpp" "$BENCH/.cpp-freeze" freeze ||
  fail "cpp freeze"
rm -rf "$BENCH/.cpp-freeze"
echo "$SHA" > "$BENCH/cpp/base/BINARY_SHA"
git -C "$ROOT" diff --exit-code --stat scripts/graph-identity/DIGESTS || fail "cpp digests differ from the committed DIGESTS"
IFS=$'\n' read -r -d '' -a names < <(corpus_names) || true # bash 3.2 has no mapfile
python3 "$HERE/fingerprint.py" "$BENCH" "${names[@]}" > "$BENCH/FINGERPRINTS"
diff "$HERE/FINGERPRINTS" "$BENCH/FINGERPRINTS" || fail "fingerprints differ from tools/rust-bench/FINGERPRINTS (lines above)"
echo "REBUILD: PASS base $SHA in $BENCH"
