#!/usr/bin/env bash
# Reproduce objects ignoring shared test or production-header changes, which
# can execute a stale binary after a header-only semantic fix, and objects
# rebuilding for headers they do not include, which makes every header edit a
# full rebuild of its flavour.
#
# Builds one object per flavour (ASan test, TSan, production) in a scratch
# BUILD_DIR, then asks make (-q, nothing is compiled) whether a header the
# object includes, and a header it does not include, would rebuild it. The
# answer comes from the depfile the compile wrote, so this checks the real
# compiler output, not a list in the Makefile. The config stamps are held old
# (-o) because their phony check makes every -q answer "out of date".
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/cbm-repro-make-headers.XXXXXX")"
trap 'rm -rf "$WORKDIR"' EXIT
BUILD_DIR="$WORKDIR/build"

cd "$PROJECT_ROOT"

# object | header it includes | header it does not include
CASES=(
    "$BUILD_DIR/obj/test/tests/test_arena.o|tests/test_framework.h|src/pipeline/lsp_resolve.h"
    "$BUILD_DIR/obj/tsan/tests/test_arena.o|tests/test_framework.h|src/pipeline/lsp_resolve.h"
    "$BUILD_DIR/obj/prod/src/foundation/arena.o|src/foundation/arena.h|tests/test_framework.h"
)

OLD_STAMPS=(-o "$BUILD_DIR/.build-config" -o "$BUILD_DIR/.build-config-test"
            -o "$BUILD_DIR/.build-config-tsan")

failed=0
for case in "${CASES[@]}"; do
    IFS='|' read -r object included unrelated <<<"$case"
    make -s -f Makefile.cbm BUILD_DIR="$BUILD_DIR" "$object" >/dev/null

    status=0
    make -f Makefile.cbm -q "${OLD_STAMPS[@]}" BUILD_DIR="$BUILD_DIR" "$object" || status=$?
    if [[ $status -ne 0 ]]; then
        printf '[repro-make-headers] invariant=fresh_object_not_up_to_date object=%s status=%s\n' \
            "$object" "$status" >&2
        failed=1
        continue
    fi

    status=0
    make -f Makefile.cbm -q "${OLD_STAMPS[@]}" -W "$included" BUILD_DIR="$BUILD_DIR" "$object" \
        || status=$?
    if [[ $status -ne 1 ]]; then
        printf '[repro-make-headers] invariant=included_header_ignored object=%s header=%s status=%s\n' \
            "$object" "$included" "$status" >&2
        failed=1
    fi

    status=0
    make -f Makefile.cbm -q "${OLD_STAMPS[@]}" -W "$unrelated" BUILD_DIR="$BUILD_DIR" "$object" \
        || status=$?
    if [[ $status -ne 0 ]]; then
        printf '[repro-make-headers] invariant=unrelated_header_rebuilds object=%s header=%s status=%s\n' \
            "$object" "$unrelated" "$status" >&2
        failed=1
    fi
done
if [[ "$failed" -ne 0 ]]; then
    exit 1
fi

printf '[repro-make-headers] each flavour rebuilds an object for its own headers only\n'
