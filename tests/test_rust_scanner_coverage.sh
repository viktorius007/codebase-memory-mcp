#!/usr/bin/env bash
# Fast contract tests for scripts/rust-scanner-coverage.sh.  Tool/build/suite
# seams make every unhappy path executable without rebuilding the repository.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/cbm-rust-coverage-contract.XXXXXX")"

cleanup() {
    if command -v trash >/dev/null 2>&1; then
        trash "$WORKDIR"
    fi
}
trap cleanup EXIT

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

TOOLS="$WORKDIR/tools"
SOURCE_ROOT="$WORKDIR/source"
mkdir -p "$TOOLS" "$SOURCE_ROOT/internal/cbm/lsp" "$SOURCE_ROOT/src" \
    "$SOURCE_ROOT/tests" "$SOURCE_ROOT/vendored"
printf '%s\n' 'fixture:' >"$SOURCE_ROOT/Makefile.cbm"
printf '%s\n' 'int build_input_fixture;' >"$SOURCE_ROOT/tests/build_input.c"
for source_name in rust_lsp.c rust_cargo.c rust_rustdoc.c; do
    printf 'int %s_fixture(void) { return 1; }\n' "${source_name%.c}" \
        >"$SOURCE_ROOT/internal/cbm/lsp/$source_name"
done

for compiler in clang clangxx; do
    compiler_path="$TOOLS/$compiler"
    {
        printf '%s\n' '#!/usr/bin/env bash'
        printf '%s\n' "printf '%s\\n' 'Apple clang version 17.0.0'"
    } >"$compiler_path"
    chmod +x "$compiler_path"
done

cat >"$TOOLS/llvm-profdata" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == "--version" ]]; then
    printf '%s\n' 'Apple LLVM version 17.0.0'
    exit 0
fi
output=""
previous=""
for argument in "$@"; do
    if [[ "$previous" == "-o" ]]; then
        output="$argument"
        break
    fi
    previous="$argument"
done
[[ -n "$output" ]] || exit 2
printf 'merged-profile\n' >"$output"
EOF
chmod +x "$TOOLS/llvm-profdata"

cat >"$TOOLS/llvm-cov" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${1:-}" == "--version" ]]; then
    printf '%s\n' 'Apple LLVM version 17.0.0'
    exit 0
fi
if [[ "${1:-}" == "report" ]]; then
    printf '%s\n' 'Filename Regions Missed Cover Functions Missed Executed Lines Missed Cover Branches Missed Cover'
    printf '%s\n' 'rust_lsp.c fixture report'
    printf '%s\n' 'rust_cargo.c fixture report'
    printf '%s\n' 'rust_rustdoc.c fixture report'
    exit 0
fi
[[ "${1:-}" == "export" ]] || exit 2
rate=95
if [[ "${FAKE_LOW_COVERAGE:-0}" == "1" ]]; then
    rate=10
fi
extra=""
if [[ "${FAKE_EXTRA_SOURCE:-0}" == "1" ]]; then
    extra=',{"filename":"/unexpected.c","summary":{"lines":{"count":10,"covered":10},"branches":{"count":10,"covered":10}}}'
fi
printf '{"data":[{"files":['
separator=""
for source_name in rust_lsp.c rust_cargo.c rust_rustdoc.c; do
    printf '%s{"filename":"%s/internal/cbm/lsp/%s","summary":{"lines":{"count":100,"covered":%s},"branches":{"count":100,"covered":%s}}}' \
        "$separator" "$FAKE_SOURCE_ROOT" "$source_name" "$rate" "$rate"
    separator=','
done
printf '%s]}],"type":"llvm.coverage.json.export","version":"2.0.1"}\n' "$extra"
EOF
chmod +x "$TOOLS/llvm-cov"

cat >"$TOOLS/llvm-cov-18" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' 'Apple LLVM version 18.0.0'
EOF
chmod +x "$TOOLS/llvm-cov-18"

cat >"$TOOLS/make" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
build_dir=""
for argument in "$@"; do
    if [[ "$argument" == BUILD_DIR=* ]]; then
        build_dir="${argument#BUILD_DIR=}"
    fi
done
[[ -n "$build_dir" ]] || exit 2
emit_flags() {
    if [[ "${FAKE_MISSING_INSTRUMENTATION_CLASS:-}" != "$1" ]]; then
        printf '%s' ' -fprofile-instr-generate -fcoverage-mapping -fno-omit-frame-pointer'
    fi
}
printf 'clang'; emit_flags c; printf ' -c -o %s/lsp_all.o input.c\n' "$build_dir"
printf 'clang++'; emit_flags cxx; printf ' -c -o %s/preprocessor.o input.cpp\n' "$build_dir"
printf 'clang'; emit_flags grammar; printf ' -c -o %s/grammar_rust.o parser.c\n' "$build_dir"
printf 'clang'; emit_flags link; printf ' -o %s test.c\n' "$CBM_COVERAGE_RUNNER"
if [[ "${FAKE_BUILD_NOISE:-0}" == "1" ]]; then
    awk 'BEGIN { for (i = 0; i < 200000; i++) printf "x" }'
    exit 1
fi
exit 0
EOF
chmod +x "$TOOLS/make"

cat >"$TOOLS/runner" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
if [[ "${FAKE_SUITE_FAIL:-0}" == "1" ]]; then
    printf '%s\n' 'fixture suite failure with private detail'
    exit 1
fi
if [[ "${FAKE_MUTATE_SOURCE:-0}" == "1" ]]; then
    printf '%s\n' '/* concurrent mutation */' >>"$FAKE_SOURCE_ROOT/internal/cbm/lsp/rust_lsp.c"
fi
if [[ "${FAKE_MUTATE_BUILD_INPUT:-0}" == "1" ]]; then
    printf '%s\n' '/* concurrent mutation */' >>"$FAKE_SOURCE_ROOT/tests/build_input.c"
fi
if [[ "${FAKE_NO_PROFILE:-0}" != "1" ]]; then
    profile="${LLVM_PROFILE_FILE//%p/1234}"
    profile="${profile//%m/fixture}"
    printf 'raw-profile\n' >"$profile"
fi
printf '%s\n' 'Tests: 100 passed, 0 failed'
EOF
chmod +x "$TOOLS/runner"

BASE_ENV=(
    CBM_COVERAGE_ALLOW_NON_DARWIN=1
    CBM_COVERAGE_REQUIRE_APPLE_LLVM=1
    CBM_COVERAGE_CC="$TOOLS/clang"
    CBM_COVERAGE_CXX="$TOOLS/clangxx"
    CBM_COVERAGE_PROFDATA="$TOOLS/llvm-profdata"
    CBM_COVERAGE_COV="$TOOLS/llvm-cov"
    CBM_COVERAGE_MAKE="$TOOLS/make"
    CBM_COVERAGE_RUNNER="$TOOLS/runner"
    CBM_COVERAGE_SOURCE_ROOT="$SOURCE_ROOT"
    CBM_COVERAGE_FINGERPRINT_ROOT="$SOURCE_ROOT"
    FAKE_SOURCE_ROOT="$SOURCE_ROOT"
)

run_case() {
    local name="$1"
    local expected_status="$2"
    local expected_pattern="$3"
    shift 3
    local build_dir="$WORKDIR/build-$name"
    local output="$WORKDIR/$name.out"
    local run_status=0
    env "${BASE_ENV[@]}" CBM_COVERAGE_BUILD_DIR="$build_dir" "$@" \
        bash "$ROOT/scripts/rust-scanner-coverage.sh" >"$output" 2>&1 || run_status=$?
    if [[ "$run_status" -ne "$expected_status" ]]; then
        fail "$name exited $run_status, expected $expected_status"
    fi
    if ! head -n 1 "$output" | grep -Eq "$expected_pattern"; then
        fail "$name did not emit the expected verdict first"
    fi
    byte_count="$(wc -c <"$output" | tr -d ' ')"
    if [[ "$byte_count" -gt 2048 ]]; then
        fail "$name emitted $byte_count bytes (limit 2048)"
    fi
}

run_case pass 0 '^PASS coverage-rust:'
if [[ "$(awk 'END { print NR }' "$WORKDIR/build-pass/coverage/metrics.tsv")" -ne 4 ]]; then
    fail "pass fixture did not report exactly three scanner sources"
fi
run_case reused-build 2 '^FAIL coverage-rust: coverage artifact directory already exists' \
    CBM_COVERAGE_BUILD_DIR="$WORKDIR/build-pass"

run_case missing-tool 2 '^FAIL coverage-rust: required tool llvm-cov' \
    CBM_COVERAGE_COV="$WORKDIR/does-not-exist"
run_case version-mismatch 2 '^FAIL coverage-rust: coverage mapping version mismatch:' \
    CBM_COVERAGE_COV="$TOOLS/llvm-cov-18"
run_case suite-failure 1 '^FAIL coverage-rust: Rust LSP suite failed' FAKE_SUITE_FAIL=1
run_case missing-profile 1 '^FAIL coverage-rust: Rust LSP suite produced no raw coverage profiles' \
    FAKE_NO_PROFILE=1
run_case threshold-regression 1 '^FAIL coverage-rust: .*lines 10.00% < 90.00%' \
    FAKE_LOW_COVERAGE=1 \
    CBM_COVERAGE_RUST_LSP_LINE_FLOOR=90 \
    CBM_COVERAGE_RUST_LSP_BRANCH_FLOOR=90 \
    CBM_COVERAGE_RUST_CARGO_LINE_FLOOR=90 \
    CBM_COVERAGE_RUST_CARGO_BRANCH_FLOOR=90 \
    CBM_COVERAGE_RUST_RUSTDOC_LINE_FLOOR=90 \
    CBM_COVERAGE_RUST_RUSTDOC_BRANCH_FLOOR=90
run_case nan-floor 1 '^FAIL coverage-rust: invalid coverage floor' \
    CBM_COVERAGE_RUST_LSP_LINE_FLOOR=nan
run_case negative-floor 1 '^FAIL coverage-rust: invalid coverage floor' \
    CBM_COVERAGE_RUST_CARGO_BRANCH_FLOOR=-1
run_case excessive-floor 1 '^FAIL coverage-rust: invalid coverage floor' \
    CBM_COVERAGE_RUST_RUSTDOC_LINE_FLOOR=101
for command_class in c cxx grammar link; do
    run_case "missing-instrumentation-$command_class" 1 \
        '^FAIL coverage-rust: build transcript lacks instrumentation on' \
        FAKE_MISSING_INSTRUMENTATION_CLASS="$command_class"
done
run_case unexpected-source 1 '^FAIL coverage-rust: coverage source-set mismatch:' \
    FAKE_EXTRA_SOURCE=1
run_case source-changed 1 '^FAIL coverage-rust: Rust scanner sources changed during' \
    FAKE_MUTATE_SOURCE=1
run_case build-input-changed 1 '^FAIL coverage-rust: coverage build inputs changed during' \
    FAKE_MUTATE_BUILD_INPUT=1
run_case bounded-build-failure 1 '^FAIL coverage-rust: instrumented test-runner build failed' \
    FAKE_BUILD_NOISE=1

MISSING_SOURCE_ROOT="$WORKDIR/missing-source"
mkdir -p "$MISSING_SOURCE_ROOT/internal/cbm/lsp"
for source_name in rust_lsp.c rust_cargo.c; do
    printf 'int fixture(void) { return 1; }\n' \
        >"$MISSING_SOURCE_ROOT/internal/cbm/lsp/$source_name"
done
run_case missing-source 2 '^FAIL coverage-rust: required coverage source is missing:' \
    CBM_COVERAGE_SOURCE_ROOT="$MISSING_SOURCE_ROOT" \
    FAKE_SOURCE_ROOT="$MISSING_SOURCE_ROOT"

# Static contract: coverage flags reach C, C++, grammar objects and the test
# link, while the production flag definitions remain independent.
for variable in CFLAGS_TEST CXXFLAGS_TEST GRAMMAR_CFLAGS_TEST LDFLAGS_TEST; do
    if ! awk -v name="$variable" '
        $0 ~ "^" name "[[:space:]]*=" { active=1 }
        active { text=text " " $0 }
        active && $0 !~ /\\$/ { exit(index(text, "$(COVERAGE_FLAGS)") ? 0 : 1) }
        END { if (!active) exit 1 }
    ' "$ROOT/Makefile.cbm"; then
        fail "$variable does not include COVERAGE_FLAGS"
    fi
done
if awk '
    /^CFLAGS_PROD[[:space:]]*=/ { active=1 }
    active { text=text " " $0 }
    active && $0 !~ /\\$/ { exit(index(text, "COVERAGE_FLAGS") ? 0 : 1) }
    END { if (!active) exit 1 }
' "$ROOT/Makefile.cbm"; then
    fail "production C flags unexpectedly include coverage instrumentation"
fi

printf '%s\n' 'PASS: Rust scanner coverage harness rejects invalid tools, runs, profiles, sources, floors, and unbounded output'
