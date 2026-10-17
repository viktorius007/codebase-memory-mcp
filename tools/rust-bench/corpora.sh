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
# parallel_each <fn> <name...>: run `<fn> <name>` for every name, CBM_BENCH_JOBS (default 2) at a
# time, in the order given: put the longest first. Prints the output of the jobs that passed in
# name order, then that of the jobs that failed on stderr, so a failing run still ends with the
# failing job's own lines; returns 1 if any job failed. Polls instead of `wait -n`: bash 3.2.
parallel_each() {
  local fn="$1" out name i=0 failed=() pids=(); shift
  out=$(mktemp -d)
  for name in "$@"; do
    while [ "$(jobs -pr | wc -l)" -ge "${CBM_BENCH_JOBS:-2}" ]; do sleep 1; done
    "$fn" "$name" > "$out/$name" 2>&1 & pids+=($!)
  done
  for name in "$@"; do
    if wait "${pids[$i]}"; then cat "$out/$name"; else failed+=("$name"); fi
    i=$((i + 1))
  done
  for name in ${failed[@]+"${failed[@]}"}; do cat "$out/$name" >&2; done
  rm -rf "$out"
  [ ${#failed[@]} -eq 0 ]
}
