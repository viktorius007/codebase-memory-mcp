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
for name in $(corpus_names); do
  dest="$BENCH/$name/$name"
  if [ -d "$dest" ]; then echo "SKIP: $name (exists)"; continue; fi
  repo=$(corpus_field "$name" 2); sha=$(corpus_field "$name" 3)
  mkdir -p "$dest"
  gh api "repos/$repo/tarball/$sha" | tar -xz --strip-components=1 -C "$dest"
  echo "OK: $name $repo@${sha:0:10} $(find "$dest" -type f | wc -l | tr -d ' ') files"
done
bash "$HERE/../../scripts/graph-identity/fetch-corpora.sh" "$BENCH/cpp"
