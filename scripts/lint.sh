#!/bin/bash
# lint.sh — Run all linters (clang-tidy + cppcheck + clang-format).
#
# Usage:
#   scripts/lint.sh                                    # All 3 linters
#   scripts/lint.sh CLANG_FORMAT=clang-format-20       # Override formatter
#   scripts/lint.sh --ci                               # CI mode (skip clang-tidy)
#   scripts/lint.sh --jobs 16                          # Override make parallelism
#   scripts/lint.sh --quiet                            # Suppress command echo from make
#
# This script is the SINGLE source of truth for linting.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Parse --jobs before sourcing env.sh.
prev_arg=""
for arg in "$@"; do
    case "$arg" in
        --jobs) ;;
        --jobs=*) export CBM_JOBS="${arg#--jobs=}" ;;
        *)
            if [[ "${prev_arg:-}" == "--jobs" ]]; then
                export CBM_JOBS="$arg"
            fi
            ;;
    esac
    prev_arg="$arg"
done

# shellcheck source=env.sh
source "$ROOT/scripts/env.sh"

# Check script flags.
CI_ONLY=false
QUIET=false
MAKE_ARGS=()
MAKE_FLAGS=()
prev_arg=""
for arg in "$@"; do
    if [[ "${prev_arg:-}" == "--jobs" ]]; then
        prev_arg="$arg"
        continue
    fi
    case "$arg" in
        --ci) CI_ONLY=true ;;
        --quiet) QUIET=true ;;
        --jobs) prev_arg="$arg"; continue ;;
        --jobs=*) ;;
        *) MAKE_ARGS+=("$arg") ;;
    esac
    prev_arg="$arg"
done

print_env "lint.sh"
echo "  ci=$CI_ONLY quiet=$QUIET"

if $QUIET; then
    MAKE_FLAGS+=("-s")
fi

# No-skips policy: every test must pass or fail. The only tolerable skip is a
# genuinely platform-specific test (SKIP_PLATFORM / #ifdef). Runs in both modes.
echo "=== no-skips policy (tests pass or fail) ==="
bash "$ROOT/scripts/check-no-test-skips.sh"

if $CI_ONLY; then
    echo "=== CI mode: cppcheck + clang-format ==="
    $ARCH_PREFIX make "${MAKE_FLAGS[@]+"${MAKE_FLAGS[@]}"}" -j"$NPROC" -f Makefile.cbm lint-ci "${MAKE_ARGS[@]+"${MAKE_ARGS[@]}"}"
    echo "=== CI linters passed ==="
else
    echo "=== Full lint: clang-tidy + cppcheck + clang-format ==="
    $ARCH_PREFIX make "${MAKE_FLAGS[@]+"${MAKE_FLAGS[@]}"}" -j"$NPROC" -f Makefile.cbm lint "${MAKE_ARGS[@]+"${MAKE_ARGS[@]}"}"
fi

echo "=== All linters passed ==="
