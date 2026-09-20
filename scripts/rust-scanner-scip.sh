#!/usr/bin/env bash
# SCIP acceptance lane for Rust CALLS accuracy. Builds the production binary
# from the current tree, indexes a copy of the corpus with it under isolated
# cache/runtime roots, derives an independent rust-analyzer SCIP oracle for the
# same corpus, and joins the two via scripts/scip-harness-join.py against the
# expectation file. Agent output is a bounded verdict; complete build, index,
# oracle, and join logs stay under the collision-safe build directory.
#
# Corpus and expectation default to the committed fixture; override with
# CBM_SCIP_CORPUS and CBM_SCIP_EXPECTED to run against a larger pinned corpus.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STEP="initialization"
ARTIFACT_DIR=""

fail_bounded() {
    printf 'FAIL scip-rust: %.900s\n' "$1" >&2
    printf 'Artifacts: %.900s\n' "${ARTIFACT_DIR:-not created}" >&2
}

# shellcheck disable=SC2329
unexpected_failure() {
    local exit_code="$1"
    trap - ERR
    fail_bounded "unexpected failure during $STEP (exit $exit_code)"
    exit "$exit_code"
}
trap 'unexpected_failure $?' ERR

RUST_ANALYZER_BIN="${CBM_SCIP_RUST_ANALYZER:-$(command -v rust-analyzer || true)}"
CARGO_BIN="${CBM_SCIP_CARGO:-$(command -v cargo || true)}"
PYTHON_BIN="${CBM_SCIP_PYTHON:-$(command -v python3 || true)}"
MAKE_BIN="${CBM_SCIP_MAKE:-$(command -v make || true)}"
for tool_spec in "rust-analyzer:$RUST_ANALYZER_BIN" "cargo:$CARGO_BIN" \
        "python3:$PYTHON_BIN" "make:$MAKE_BIN"; do
    tool_name="${tool_spec%%:*}"
    tool_path="${tool_spec#*:}"
    if [[ -z "$tool_path" || ! -x "$tool_path" ]]; then
        fail_bounded "required tool $tool_name is missing or not executable: ${tool_path:-<unset>}"
        exit 2
    fi
done

CORPUS_SRC="${CBM_SCIP_CORPUS:-$ROOT/tests/fixtures/scip_harness/corpus}"
EXPECTED_FILE="${CBM_SCIP_EXPECTED:-$ROOT/tests/fixtures/scip_harness/expected.txt}"
if [[ ! -f "$CORPUS_SRC/Cargo.toml" ]]; then
    fail_bounded "corpus is not a Cargo workspace (no Cargo.toml): $CORPUS_SRC"
    exit 2
fi
if [[ ! -f "$EXPECTED_FILE" ]]; then
    fail_bounded "expectation file is missing: $EXPECTED_FILE"
    exit 2
fi

STEP="creating isolated build directory"
if [[ -n "${CBM_SCIP_BUILD_DIR:-}" ]]; then
    BUILD_ROOT="$CBM_SCIP_BUILD_DIR"
    if [[ -e "$BUILD_ROOT" ]]; then
        fail_bounded "scip artifact directory already exists; choose a fresh path"
        exit 2
    fi
    mkdir -p "$BUILD_ROOT"
else
    mkdir -p "$ROOT/build"
    BUILD_ROOT="$(mktemp -d "$ROOT/build/scip-rust.XXXXXX")"
fi
BUILD_ROOT="$(cd "$BUILD_ROOT" && pwd)"
ARTIFACT_DIR="$BUILD_ROOT/artifacts"
mkdir -p "$ARTIFACT_DIR"

STEP="recording oracle tool versions"
if ! "$RUST_ANALYZER_BIN" --version >"$ARTIFACT_DIR/rust-analyzer-version.txt" 2>&1; then
    fail_bounded "rust-analyzer --version failed: $(head -c 300 "$ARTIFACT_DIR/rust-analyzer-version.txt")"
    exit 2
fi
if ! "$CARGO_BIN" --version >"$ARTIFACT_DIR/cargo-version.txt" 2>&1; then
    fail_bounded "cargo --version failed: $(head -c 300 "$ARTIFACT_DIR/cargo-version.txt")"
    exit 2
fi

STEP="copying corpus into the build directory"
if ! cp -R "$CORPUS_SRC" "$BUILD_ROOT/corpus" 2>"$ARTIFACT_DIR/corpus-copy.log"; then
    fail_bounded "could not copy corpus from $CORPUS_SRC"
    exit 2
fi

STEP="building production binary from the current tree"
PROD_BIN="$BUILD_ROOT/prod/codebase-memory-mcp"
if ! (cd "$ROOT" && "$MAKE_BIN" -j"${CBM_SCIP_JOBS:-16}" -f Makefile.cbm cbm \
        BUILD_DIR="$BUILD_ROOT/prod") >"$ARTIFACT_DIR/build.log" 2>&1; then
    fail_bounded "production binary build failed"
    exit 1
fi
if [[ ! -x "$PROD_BIN" ]]; then
    fail_bounded "build succeeded without an executable production binary: $PROD_BIN"
    exit 1
fi

STEP="indexing corpus with the production binary"
# The runtime root holds the CLI coordination socket. The socket's absolute
# path — runtime root plus the rendezvous key and socket name the binary
# appends (~55 bytes, measured) — must fit a Unix sun_path (104 bytes on
# macOS), so the root must stay short: BUILD_ROOT in a worktree and even
# macOS's TMPDIR (~50 bytes) are both refused with "(endpoint)". /tmp keeps
# the whole path under the limit; the chosen root is recorded in the
# artifacts for inspection.
export CBM_CACHE_DIR="$BUILD_ROOT/cache"
CBM_RUNTIME_DIR="$(mktemp -d /tmp/cbm-scip-rt.XXXXXX)"
export CBM_RUNTIME_DIR
mkdir -p "$CBM_CACHE_DIR"
chmod 700 "$CBM_CACHE_DIR" "$CBM_RUNTIME_DIR"
printf '%s\n' "$CBM_RUNTIME_DIR" >"$ARTIFACT_DIR/runtime-dir.txt"
if ! "$PROD_BIN" cli index_repository --repo-path "$BUILD_ROOT/corpus" \
        --mode full --name scip-lane >"$ARTIFACT_DIR/index.log" 2>&1; then
    fail_bounded "index_repository failed on the corpus: $(tail -c 300 "$ARTIFACT_DIR/index.log")"
    exit 1
fi

STEP="deriving the SCIP oracle"
if ! "$RUST_ANALYZER_BIN" scip "$BUILD_ROOT/corpus" \
        --output "$ARTIFACT_DIR/index.scip" >"$ARTIFACT_DIR/scip.log" 2>&1; then
    fail_bounded "rust-analyzer scip failed: $(tail -c 300 "$ARTIFACT_DIR/scip.log")"
    exit 1
fi

STEP="joining graph edges against the SCIP oracle"
JOIN_OUT="$ARTIFACT_DIR/join-verdict.txt"
join_status=0
"$PYTHON_BIN" "$ROOT/scripts/scip-harness-join.py" \
    --cbm "$PROD_BIN" --project scip-lane --corpus "$BUILD_ROOT/corpus" \
    --scip "$ARTIFACT_DIR/index.scip" --cargo "$CARGO_BIN" \
    --expected "$EXPECTED_FILE" --detail-dir "$ARTIFACT_DIR" \
    >"$JOIN_OUT" 2>"$ARTIFACT_DIR/join.log" || join_status=$?

trap - ERR
counts="$(head -n 1 "$JOIN_OUT")"
if [[ "$join_status" -eq 0 && -n "$counts" ]]; then
    printf 'PASS scip-rust: %s\n' "$counts"
    printf 'Artifacts: %.900s\n' "$ARTIFACT_DIR"
    exit 0
fi
if [[ "$join_status" -eq 1 && -n "$counts" ]]; then
    printf 'FAIL scip-rust: %s\n' "$counts" >&2
    tail -n +2 "$JOIN_OUT" | head -c 1200 >&2
    printf 'Artifacts: %.900s\n' "$ARTIFACT_DIR" >&2
    exit 1
fi
fail_bounded "oracle join failed: $(head -c 600 "$ARTIFACT_DIR/join.log")"
exit 2
