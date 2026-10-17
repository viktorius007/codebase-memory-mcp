#!/usr/bin/env bash
# launch.sh <name> <effort> <clone-dir> <sandbox> <brief-file>...  — one Sol 6.1 agent at the fast (priority) service tier.
# Prompt = concatenation of brief files with @NAME@/@CLONE@/@SCRATCH@ substituted. Output under merge/out/<name>.*
set -uo pipefail
NAME="$1"; EFF="$2"; CLONE="$3"; SB="$4"; shift 4
M="$(cd "$(dirname "$0")" && pwd)"
SCR=/tmp/$NAME; mkdir -p "$SCR"
SUB="s#@NAME@#$NAME#g; s#@CLONE@#$CLONE#g; s#@SCRATCH@#$SCR#g; s#@MERGE@#$M#g"
PROMPT="$(for f in "$@"; do sed "$SUB" "$f"; echo; echo '---'; echo; done)"
git -C "$CLONE" rev-parse --short=8 HEAD > "$M/out/$NAME.base"
date +%s > "$M/out/$NAME.start"
codex exec --skip-git-repo-check -m gpt-6.1-sol -c service_tier=priority -c model_reasoning_effort="$EFF" -s "$SB" \
  -C "$CLONE" --color never --json -o "$M/out/$NAME.report.md" "$PROMPT" < /dev/null > "$M/out/$NAME.jsonl" 2> "$M/out/$NAME.err"
echo "exit=$? end=$(date +%s)" > "$M/out/$NAME.exit"
