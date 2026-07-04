#!/bin/bash
# build.sh — Build production binary (standard or with UI).
#
# Usage:
#   scripts/build.sh                              # Standard binary
#   scripts/build.sh --incremental                # Preserve build/c for warm rebuilds
#   scripts/build.sh --ccache                     # Use sccache for compile steps
#   scripts/build.sh --fast-grammars              # Compile generated grammars with -O0
#   scripts/build.sh --quiet                      # Suppress command echo from make
#   scripts/build.sh --jobs 16                    # Override make parallelism
#   scripts/build.sh --with-ui                    # Binary with embedded UI
#   scripts/build.sh --version v0.8.0             # With version stamp
#   scripts/build.sh --arch x86_64                # Force x86_64 build
#   scripts/build.sh CC=gcc-14 CXX=g++-14        # Override compiler
#
# This script is the SINGLE source of truth for building release binaries.
# Used identically in local development and CI workflows.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Pre-parse --arch/--jobs flags before sourcing env.sh
for arg in "$@"; do
    case "$arg" in
        --arch=*) export CBM_ARCH="${arg#--arch=}" ;;
        --jobs=*) export CBM_JOBS="${arg#--jobs=}" ;;
    esac
done
prev_arg=""
for arg in "$@"; do
    if [[ "${prev_arg:-}" == "--arch" ]]; then
        export CBM_ARCH="$arg"
    fi
    if [[ "${prev_arg:-}" == "--jobs" ]]; then
        export CBM_JOBS="$arg"
    fi
    prev_arg="$arg"
done

# shellcheck source=env.sh
source "$ROOT/scripts/env.sh"

# Parse remaining arguments
WITH_UI=false
INCREMENTAL=false
USE_CCACHE=false
FAST_GRAMMARS=false
QUIET=false
VERSION=""
EXTRA_MAKE_ARGS=()
MAKE_FLAGS=()
BUILD_OUT_DIR="build/c"

prev_arg=""
for arg in "$@"; do
    # Skip --arch/--jobs and their values (already handled)
    if [[ "${prev_arg:-}" == "--arch" || "${prev_arg:-}" == "--jobs" ]]; then
        prev_arg="$arg"
        continue
    fi
    case "$arg" in
        --with-ui)
            WITH_UI=true
            ;;
        --incremental)
            INCREMENTAL=true
            ;;
        --ccache)
            USE_CCACHE=true
            ;;
        --fast-grammars)
            FAST_GRAMMARS=true
            ;;
        --quiet)
            QUIET=true
            ;;
        --version)
            prev_arg="$arg"
            continue
            ;;
        --jobs)
            prev_arg="$arg"
            continue
            ;;
        --arch|--arch=*|--jobs=*)
            ;; # already handled
        CC=*|CXX=*)
            export "${arg}"
            EXTRA_MAKE_ARGS+=("$arg")
            ;;
        BUILD_DIR=*)
            BUILD_OUT_DIR="${arg#BUILD_DIR=}"
            EXTRA_MAKE_ARGS+=("$arg")
            ;;
        *)
            # Check if this is the value for --version
            if [[ "${prev_arg:-}" == "--version" ]]; then
                VERSION="$arg"
            else
                EXTRA_MAKE_ARGS+=("$arg")
            fi
            ;;
    esac
    prev_arg="$arg"
done

# Version flag
CFLAGS_EXTRA=""
if [[ -n "$VERSION" ]]; then
    CLEAN_VERSION="${VERSION#v}"
    CFLAGS_EXTRA="-DCBM_VERSION=\"\\\"$CLEAN_VERSION\\\"\""
fi

print_env "build.sh"
echo "  ui=$WITH_UI version=${VERSION:-dev} incremental=$INCREMENTAL ccache=$USE_CCACHE fast_grammars=$FAST_GRAMMARS quiet=$QUIET"

# Verify compiler supports target arch
verify_compiler "$CC"

if $USE_CCACHE; then
    if ! command -v sccache >/dev/null 2>&1; then
        echo "ERROR: --ccache requested but sccache was not found in PATH" >&2
        exit 1
    fi
    EXTRA_MAKE_ARGS+=("CCACHE=sccache")
fi

if $FAST_GRAMMARS; then
    EXTRA_MAKE_ARGS+=("GRAMMAR_OPT=-O0")
fi

if $QUIET; then
    MAKE_FLAGS+=("-s")
fi

# Step 1: Clean C build artifacts only unless this is a warm dev rebuild.
# node_modules is never removed here — npm ci handles frontend dependencies.
if ! $INCREMENTAL; then
    if [[ "$BUILD_OUT_DIR" != "build/c" ]]; then
        echo "ERROR: refusing to clean custom BUILD_DIR='$BUILD_OUT_DIR'" >&2
        echo "       Pass --incremental for custom build directories." >&2
        exit 1
    fi
    case "$BUILD_OUT_DIR" in
        build/c)
            rm -rf "$ROOT/build/c"
            ;;
        *)
            echo "ERROR: refusing to clean unsafe BUILD_DIR='$BUILD_OUT_DIR'" >&2
            exit 1
            ;;
    esac
fi

# Step 2: Build (with arch prefix on macOS)
if $WITH_UI; then
    $ARCH_PREFIX make "${MAKE_FLAGS[@]+"${MAKE_FLAGS[@]}"}" -j"$NPROC" -f Makefile.cbm cbm-with-ui \
        CFLAGS_EXTRA="$CFLAGS_EXTRA" "${EXTRA_MAKE_ARGS[@]+"${EXTRA_MAKE_ARGS[@]}"}"
else
    $ARCH_PREFIX make "${MAKE_FLAGS[@]+"${MAKE_FLAGS[@]}"}" -j"$NPROC" -f Makefile.cbm cbm \
        CFLAGS_EXTRA="$CFLAGS_EXTRA" "${EXTRA_MAKE_ARGS[@]+"${EXTRA_MAKE_ARGS[@]}"}"
fi

echo "=== Build complete: $BUILD_OUT_DIR/codebase-memory-mcp ==="
