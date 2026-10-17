#!/usr/bin/env bash
# fetch-corpora.sh [bench-dir]: materialise every corpus the gate scores. Rust corpora come from
# GitHub at the commits pinned in corpora.tsv, as plain trees without .git (an index of a git
# checkout reads its ignore rules and history, which changes the graph); C/C++ corpora come from
# scripts/graph-identity/fetch-corpora.sh. Needs an authenticated `gh` (pm is a private repo).
# Skips a corpus whose checkout already exists.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=corpora.sh
source "$HERE/corpora.sh"
BENCH=$(bench_dir "${1:-}")
fetch_one() { # <name>, or cpp for the C/C++ corpora
  if [ "$1" = cpp ]; then bash "$HERE/../../scripts/graph-identity/fetch-corpora.sh" "$BENCH/cpp"; return; fi
  local name="$1" dest="$BENCH/$1/$1" repo sha
  if [ -d "$dest" ]; then echo "SKIP: $name (exists)"; return; fi
  repo=$(corpus_field "$name" 2); sha=$(corpus_field "$name" 3)
  mkdir -p "$dest"
  gh api "repos/$repo/tarball/$sha" | tar -xz --strip-components=1 -C "$dest"
  echo "OK: $name $repo@${sha:0:10} $(find "$dest" -type f | wc -l | tr -d ' ') files"
}
# Downloads are network-bound, so all of them at once.
# shellcheck disable=SC2046 # corpus names are single words
CBM_BENCH_JOBS=99 parallel_each fetch_one $(corpus_names) cpp
