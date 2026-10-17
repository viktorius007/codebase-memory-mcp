# shellcheck shell=bash
# Sourced by the scripts beside it. Bench layout: <bench>/<name>/<name> is the checkout,
# <bench>/<name>/{truth,refs}.tsv the rust-analyzer truth, <bench>/cpp the C/C++ corpora,
# <bench>/rust-base/<name> the frozen base score of BINARY_SHA.
bench_dir() { echo "${1:-$HOME/Developer/cbm-bench}"; } # [bench-dir argument]
CORPORA_TSV="$(dirname "${BASH_SOURCE[0]}")/corpora.tsv"
corpus_names() { grep -v '^#' "$CORPORA_TSV" | cut -f1; }
corpus_field() { # <name> <column 2..5>
  awk -F'\t' -v n="$1" -v c="$2" '$1 == n { print $c; found = 1 } END { exit !found }' "$CORPORA_TSV" ||
    { echo "corpus '$1' not in $CORPORA_TSV" >&2; return 1; }
}
corpus_dir() { # <bench> <name>: the directory scored and given to rust-analyzer
  local sub; sub=$(corpus_field "$2" 4) || return 1
  if [ "$sub" = . ]; then echo "$1/$2/$2"; else echo "$1/$2/$2/$sub"; fi
}
