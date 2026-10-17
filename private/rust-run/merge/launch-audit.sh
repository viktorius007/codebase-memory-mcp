#!/usr/bin/env bash
# launch-audit.sh <name> <effort> <workdir> <brief-file>  — read-only audit agent, Sol 5.6, workspace-write in a scratch dir.
set -uo pipefail
NAME="$1"; EFF="$2"; WD="$3"; BRIEF="$4"
M="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$WD"; date +%s > "$M/out/$NAME.start"
codex exec --skip-git-repo-check -m gpt-5.6-sol -c model_reasoning_effort="$EFF" -s workspace-write \
  -C "$WD" --color never --json -o "$M/out/$NAME.report.md" "$(cat "$BRIEF")" < /dev/null > "$M/out/$NAME.jsonl" 2> "$M/out/$NAME.err"
echo "exit=$? end=$(date +%s)" > "$M/out/$NAME.exit"
