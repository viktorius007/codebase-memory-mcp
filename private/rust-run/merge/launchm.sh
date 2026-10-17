#!/usr/bin/env bash
# launchm.sh <model> <service_tier|standard> <name> <effort> <clone-dir> <sandbox> <brief-file>...  — one codex agent, any model/tier.
set -uo pipefail
MODEL="$1"; TIER="$2"; NAME="$3"; EFF="$4"; CLONE="$5"; SB="$6"; shift 6
M="$(cd "$(dirname "$0")" && pwd)"
SCR=/tmp/$NAME; mkdir -p "$SCR"
SUB="s#@NAME@#$NAME#g; s#@CLONE@#$CLONE#g; s#@SCRATCH@#$SCR#g; s#@MERGE@#$M#g"
PROMPT="$(for f in "$@"; do sed "$SUB" "$f"; echo; echo '---'; echo; done)"
TIERARG=(); [ "$TIER" != standard ] && TIERARG=(-c "service_tier=$TIER")
git -C "$CLONE" rev-parse --short=8 HEAD > "$M/out/$NAME.base" 2>/dev/null || echo none > "$M/out/$NAME.base"
date +%s > "$M/out/$NAME.start"
codex exec --skip-git-repo-check -m "$MODEL" "${TIERARG[@]}" -c model_reasoning_effort="$EFF" -s "$SB" \
  -C "$CLONE" --color never --json -o "$M/out/$NAME.report.md" "$PROMPT" < /dev/null > "$M/out/$NAME.jsonl" 2> "$M/out/$NAME.err"
echo "exit=$? end=$(date +%s)" > "$M/out/$NAME.exit"
