#!/usr/bin/env bash
set -euo pipefail

# Regression guard for the ts_runtime unity build.  ts_runtime.c includes
# vendored/ts_runtime/src/lib.c, which in turn includes the rest of the runtime.
# A change below that wrapper must invalidate every build variant.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

BUILD_DIR="$WORKDIR/build"
mkdir -p "$BUILD_DIR"

DEPENDENCIES=(
    internal/cbm/vendored/ts_runtime/src/stack.c
    internal/cbm/vendored/ts_runtime/src/stack.h
    internal/cbm/vendored/ts_runtime/src/unicode/utf8.h
    internal/cbm/vendored/ts_runtime/include/tree_sitter/api.h
)
TARGETS=(
    "$BUILD_DIR/ts_runtime.o"
    "$BUILD_DIR/tsan_ts_runtime.o"
    "$BUILD_DIR/prod_ts_runtime.o"
)

# The config stamps' phony check makes every -q answer "out of date"; holding
# them old (-o) lets -q answer for the dependency alone. The baseline probe
# below proves it does.
OLD_STAMPS=(-o "$BUILD_DIR/.build-config" -o "$BUILD_DIR/.build-config-test"
            -o "$BUILD_DIR/.build-config-tsan")

cd "$ROOT"

for target in "${TARGETS[@]}"; do
    # An object without a depfile is rebuilt once by design; an empty one
    # records no extra headers and leaves the explicit list under test.
    touch "$target" "${target%.o}.d"
    status=0
    make -f Makefile.cbm -q "${OLD_STAMPS[@]}" BUILD_DIR="$BUILD_DIR" "$target" || status=$?
    if [[ $status -ne 0 ]]; then
        echo "FAIL: $target is not up to date before any change (exit $status); the probe cannot discriminate"
        exit 1
    fi
done

for dependency in "${DEPENDENCIES[@]}"; do
    if [[ ! -f "$dependency" ]]; then
        echo "FAIL: expected tree-sitter runtime dependency is missing: $dependency"
        exit 1
    fi

    for target in "${TARGETS[@]}"; do
        # Make the object newer than its currently-declared prerequisites.  -W
        # then asks make whether changing the unity-included file would rebuild
        # it, without compiling or modifying the source tree.
        touch "$target" "${target%.o}.d"

        status=0
        make -f Makefile.cbm -q "${OLD_STAMPS[@]}" -W "$dependency" \
            BUILD_DIR="$BUILD_DIR" "$target" || status=$?

        if [[ $status -eq 0 ]]; then
            echo "FAIL: $target would stay stale after changing $dependency"
            exit 1
        fi
        if [[ $status -ne 1 ]]; then
            echo "FAIL: make dependency probe failed for $target and $dependency (exit $status)"
            exit 1
        fi
    done
done

echo "PASS: all tree-sitter runtime unity objects track included sources and headers"
