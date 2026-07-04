#!/bin/bash
# repro.sh — Build + run the cumulative BUG-REPRODUCTION suite (test-repro).
#
# Unlike test.sh (the gating suite, must be GREEN), this suite is RED by design:
# every case reproduces an open bug. So we distinguish two outcomes:
#   - BUILD/LINK failure  → real breakage → exit non-zero (fail the CI job).
#   - Test redness        → EXPECTED → report the count, exit 0 (green board).
#
# Usage:
#   scripts/repro.sh                          # Auto-detect everything
#   scripts/repro.sh --ccache                 # Use sccache for compile steps
#   scripts/repro.sh --quiet                  # Suppress command echo from make
#   scripts/repro.sh --jobs 16                # Override make parallelism
#   scripts/repro.sh [CC=clang] [CXX=clang++] [--arch arm64|x86_64]
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# --arch/--jobs before sourcing env.sh (mirrors test.sh)
prev_arg=""
for arg in "$@"; do
    case "$arg" in
        --arch|--jobs) ;;
        --jobs=*) export CBM_JOBS="${arg#--jobs=}" ;;
        arm64|x86_64) [[ "$prev_arg" == "--arch" ]] && export CBM_ARCH="$arg" ;;
        --arch=*) export CBM_ARCH="${arg#--arch=}" ;;
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

MAKE_ARGS=()
MAKE_FLAGS=()
USE_CCACHE=false
QUIET=false
prev_arg=""
for arg in "$@"; do
    if [[ "${prev_arg:-}" == "--jobs" || "${prev_arg:-}" == "--arch" ]]; then
        prev_arg="$arg"
        continue
    fi
    case "$arg" in
        CC=*|CXX=*) export "${arg?}"; MAKE_ARGS+=("$arg") ;;
        --ccache) USE_CCACHE=true ;;
        --quiet) QUIET=true ;;
        --jobs) prev_arg="$arg"; continue ;;
        --arch|--arch=*|--jobs=*|arm64|x86_64) ;;
        *=*) MAKE_ARGS+=("$arg") ;;
    esac
    prev_arg="$arg"
done

print_env "repro.sh"
echo "  ccache=$USE_CCACHE quiet=$QUIET"
verify_compiler "$CC"

if $USE_CCACHE; then
    if ! command -v sccache >/dev/null 2>&1; then
        echo "ERROR: --ccache requested but sccache was not found in PATH" >&2
        exit 1
    fi
    MAKE_ARGS+=("CCACHE=sccache")
fi

if $QUIET; then
    MAKE_FLAGS+=("-s")
fi

OUT="$ROOT/repro-out.txt"
# A RED reproduction fails its assertion and returns EARLY — before any cleanup —
# so LeakSanitizer would flag benign harness leaks on every red store-level test
# and abort. The board's signal is the FAIL rows, not leak-cleanliness (the leak
# BUG #581 gets a dedicated RSS-growth test, not LSan). Disable leak detection
# only; ASan's real checks (use-after-free, overflow) stay ON.
export ASAN_OPTIONS="detect_leaks=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}"

# test-repro both builds and runs the runner; tolerate its non-zero (red) exit.
set +e
$ARCH_PREFIX make "${MAKE_FLAGS[@]+"${MAKE_FLAGS[@]}"}" -j"$NPROC" -f Makefile.cbm test-repro "${MAKE_ARGS[@]+"${MAKE_ARGS[@]}"}" 2>&1 | tee "$OUT"
set -e

# The runner prints a "<N> passed[, <M> failed]" summary line only if it actually
# ran. No summary line ⇒ the build/link failed ⇒ real breakage.
if ! grep -qE '[0-9]+ passed' "$OUT"; then
    echo "::error::bug-repro runner did not execute — build or link failure"
    exit 1
fi

reproduced=$(grep -oE '[0-9]+ failed' "$OUT" | head -1 | grep -oE '[0-9]+' || echo 0)
green=$(grep -oE '[0-9]+ passed' "$OUT" | head -1 | grep -oE '[0-9]+' || echo 0)

{
    echo "## Bug-reproduction board — ${OS:-$(uname -s)} ${ARCH:-}"
    echo ""
    echo "- **${reproduced}** open bug(s) still reproduced (RED — expected)"
    echo "- **${green}** case(s) PASSING — candidate-fixed → verify + close the issue + promote the guard to the gating suite"
} >> "${GITHUB_STEP_SUMMARY:-/dev/stderr}"

echo "=== bug-repro board: ${reproduced} reproduced (RED), ${green} passing (candidate-fixed) ==="
# Green board: the suite ran. Redness is the data, not a job failure.
exit 0
