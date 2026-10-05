#!/usr/bin/env bash
# Worker start plus absence of daemon artifacts pins the socket-free route even
# on hosts where an accidental daemon fallback could successfully bind.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${CBM_TEST_BINARY:-$ROOT/build/w/codebase-memory-mcp}"
source "$ROOT/scripts/test-runtime.sh"
cbm_test_runtime_init
TASK_DIR="$CBM_TEST_RUNTIME_ROOT"
trap 'cbm_test_runtime_cleanup "$BIN"' EXIT
mkdir "$TASK_DIR/repo"
REPO="$TASK_DIR/repo"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) REPO="$(cygpath -m "$REPO")" ;;
esac
printf 'fn alpha() {}\nfn beta() { alpha(); }\n' > "$TASK_DIR/repo/lib.rs"
if ! "${CBM_TEST_INDEX_SCRIPT:-$ROOT/scripts/index-local.sh}" \
    "$BIN" "$REPO" fixture full > "$TASK_DIR/out" 2> "$TASK_DIR/err"; then
    cat "$TASK_DIR/err" >&2
    exit 1
fi
grep -q 'index.worker.start' "$TASK_DIR/err"
CALLS="$(sqlite3 "$CBM_TEST_CACHE_DIR_HOST/fixture.db" "
    select count(*) from edges e
    join nodes a on a.id=e.source_id join nodes b on b.id=e.target_id
    where e.type='CALLS' and a.name='beta' and b.name='alpha'")"
[[ "$CALLS" == 1 ]]
[[ -z "$(find "$CBM_TEST_RUNTIME_DIR_HOST" \( -name '*.anc' -o -name '*daemon*.log' -o -type s \) -print)" ]]
[[ ! -e "$CBM_TEST_CACHE_DIR_HOST/logs/cbm-daemon.log" ]]
echo 'PASS: local worker publishes beta -> alpha without daemon artifacts'
