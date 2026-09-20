#!/usr/bin/env bash
# Semantic index epoch tripwire. Merge 562bfecd silently moved the epoch
# backwards (4 -> 3), so stale fork-quality indexes kept serving and the Rust
# resolver loss stayed invisible for ten days (ISSUES.md, root-cause §2.3).
# The constant and tests/semantic-epoch.expected must move together: a
# deliberate bump edits both in one commit; any drift — either direction —
# fails this step.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HEADER="$ROOT/src/store/store.h"
EXPECTED_FILE="$ROOT/tests/semantic-epoch.expected"

fail() {
    printf 'FAIL semantic-epoch: %s\n' "$1" >&2
    exit 1
}

[[ -f "$HEADER" ]] || fail "missing $HEADER"
[[ -f "$EXPECTED_FILE" ]] || fail "missing $EXPECTED_FILE"

actual="$(sed -nE 's/.*CBM_RUST_SEMANTIC_GAPS_COVERAGE_VERSION = ([0-9]+).*/\1/p' "$HEADER")"
[[ -n "$actual" ]] || fail "CBM_RUST_SEMANTIC_GAPS_COVERAGE_VERSION not found in src/store/store.h"
[[ "$(printf '%s\n' "$actual" | wc -l | tr -d ' ')" == "1" ]] || \
    fail "CBM_RUST_SEMANTIC_GAPS_COVERAGE_VERSION defined more than once in src/store/store.h"

expected="$(tr -d '[:space:]' <"$EXPECTED_FILE")"
[[ "$expected" =~ ^[0-9]+$ ]] || fail "tests/semantic-epoch.expected does not hold a single integer"

if [[ "$actual" != "$expected" ]]; then
    fail "epoch is $actual but tests/semantic-epoch.expected says $expected; a bump edits both in one commit, and an epoch may never decrease"
fi

printf 'PASS semantic-epoch: %s\n' "$actual"
