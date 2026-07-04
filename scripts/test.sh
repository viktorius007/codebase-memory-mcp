#!/bin/bash
# test.sh — Build + run all C tests with ASan + UBSan.
#
# Usage:
#   scripts/test.sh                          # Auto-detect everything
#   scripts/test.sh --incremental            # Preserve build/c for warm rebuilds
#   scripts/test.sh --ccache                 # Use sccache for compile steps
#   scripts/test.sh --fast-grammars          # Compile prod + selected slow test grammars with -O0
#   scripts/test.sh --quiet                  # Suppress command echo from make
#   scripts/test.sh --jobs 16                # Override make parallelism
#   scripts/test.sh --parallel-suites        # Run sanitizer suites in parallel
#   scripts/test.sh --serial-suites          # Force one sanitizer runner process
#   scripts/test.sh --arch x86_64            # Force x86_64 build
#   scripts/test.sh CC=gcc-14 CXX=g++-14    # Override compiler
#
# This script is the SINGLE source of truth for running tests.
# Used identically in local development and CI workflows.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# Parse --arch/--jobs flags before sourcing env.sh
for arg in "$@"; do
    case "$arg" in
        --arch|--jobs) :;; # next arg is the value, handled below
        --jobs=*) export CBM_JOBS="${arg#--jobs=}" ;;
        arm64|x86_64)
            # Check if previous arg was --arch
            if [[ "${prev_arg:-}" == "--arch" ]]; then
                export CBM_ARCH="$arg"
            fi
            ;;
        *)
            if [[ "${prev_arg:-}" == "--jobs" ]]; then
                export CBM_JOBS="$arg"
            fi
            ;;
    esac
    prev_arg="$arg"
done

# Also support --arch=value
for arg in "$@"; do
    case "$arg" in
        --arch=*) export CBM_ARCH="${arg#--arch=}" ;;
    esac
done

# shellcheck source=env.sh
source "$ROOT/scripts/env.sh"

# Forward CC/CXX and collect make-passthrough args
MAKE_ARGS=()
INCREMENTAL=false
USE_CCACHE=false
FAST_GRAMMARS=false
QUIET=false
PARALLEL_SUITES=true
if [[ "${CI:-}" == "true" ]]; then
    PARALLEL_SUITES=false
fi
case "${CBM_PARALLEL_SUITES:-}" in
    1|true|TRUE|yes|YES|on|ON) PARALLEL_SUITES=true ;;
    0|false|FALSE|no|NO|off|OFF) PARALLEL_SUITES=false ;;
    "") ;;
    *)
        echo "ERROR: CBM_PARALLEL_SUITES must be 0/1, true/false, yes/no, or on/off" >&2
        exit 1
        ;;
esac
MAKE_FLAGS=()
BUILD_OUT_DIR="build/c"
SUITE_JOBS="${CBM_SUITE_JOBS:-}"
SQLITE3_OBJ_PROD_OVERRIDDEN=false
ZSTD_OBJ_PROD_OVERRIDDEN=false
SUBPROCESS_BINARY=""
prev_arg=""
for arg in "$@"; do
    if [[ "${prev_arg:-}" == "--jobs" || "${prev_arg:-}" == "--arch" || "${prev_arg:-}" == "--suite-jobs" ]]; then
        if [[ "${prev_arg:-}" == "--suite-jobs" ]]; then
            SUITE_JOBS="$arg"
        fi
        prev_arg="$arg"
        continue
    fi
    case "$arg" in
        CC=*|CXX=*) export "${arg}"; MAKE_ARGS+=("$arg") ;;
        --incremental) INCREMENTAL=true ;;
        --ccache) USE_CCACHE=true ;;
        --fast-grammars) FAST_GRAMMARS=true ;;
        --quiet) QUIET=true ;;
        --parallel-suites) PARALLEL_SUITES=true ;;
        --serial-suites) PARALLEL_SUITES=false ;;
        --suite-jobs) prev_arg="$arg"; continue ;;
        --suite-jobs=*) SUITE_JOBS="${arg#--suite-jobs=}" ;;
        --jobs) prev_arg="$arg"; continue ;;
        --arch|--arch=*|--jobs=*) ;; # already handled
        arm64|x86_64) ;; # already handled
        BUILD_DIR=*) BUILD_OUT_DIR="${arg#BUILD_DIR=}"; MAKE_ARGS+=("$arg") ;;
        SQLITE3_OBJ_PROD=*) SQLITE3_OBJ_PROD_OVERRIDDEN=true; MAKE_ARGS+=("$arg") ;;
        ZSTD_OBJ_PROD=*) ZSTD_OBJ_PROD_OVERRIDDEN=true; MAKE_ARGS+=("$arg") ;;
        *=*) MAKE_ARGS+=("$arg") ;; # forward any VAR=VAL to make
    esac
    prev_arg="$arg"
done

print_env "test.sh"
echo "  incremental=$INCREMENTAL ccache=$USE_CCACHE fast_grammars=$FAST_GRAMMARS quiet=$QUIET parallel_suites=$PARALLEL_SUITES"

if [[ -n "$SUITE_JOBS" && ! "$SUITE_JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: --suite-jobs/CBM_SUITE_JOBS must be a positive integer" >&2
    exit 1
fi

run_serial_suites() {
    cd "$ROOT" && "$BUILD_OUT_DIR/test-runner"
}

run_parallel_suites() {
    local runner log_dir
    if [[ "$BUILD_OUT_DIR" = /* ]]; then
        runner="$BUILD_OUT_DIR/test-runner"
        log_dir="$BUILD_OUT_DIR/test-logs"
    else
        runner="$ROOT/$BUILD_OUT_DIR/test-runner"
        log_dir="$ROOT/$BUILD_OUT_DIR/test-logs"
    fi

    if [[ ! -x "$runner" ]]; then
        echo "ERROR: missing test runner: $runner" >&2
        exit 2
    fi

    local -a shard_names=()
    local -a shard_suites=()
    add_shard() {
        shard_names+=("$1")
        shard_suites+=("$2")
    }

    # Launch the longest shards first so the suite phase does not end with a
    # single long-running process after most cores have gone idle.
    add_shard incremental_mutation_adversarial_heavy "incremental_mutation_adversarial_heavy"
    add_shard foundation_mcp "mcp"
    add_shard infra_ui "store_arch traces configlink infrascan cli system_info worker_pool parallel mem ui httpd security yaml simhash"
    add_shard watcher_git "watcher_git"
    add_shard watcher_fs "watcher_fs"
    add_shard watcher_core "watcher_core"
    add_shard incremental_mutation_core "incremental_mutation_core"
    add_shard stack_overflow_nested_csharp "stack_overflow_nested_csharp"
    add_shard node_creation_probe "node_creation_probe"
    add_shard lsp_resolution_probe "lsp_resolution_probe"
    add_shard lang_contract_breadth "lang_contract_breadth"
    add_shard lang_contract_rest "lang_contract_rest"
    add_shard lsp_back "py_lsp kotlin_lsp rust_lsp py_lsp_bench py_lsp_stress py_lsp_scale ts_lsp java_lsp java_lsp_coverage"
    add_shard pipeline "pipeline"
    add_shard foundation_cypher "cypher"
    add_shard foundation_extraction "extraction extraction_inheritance extraction_imports"
    add_shard incremental_mutation_adversarial_light "incremental_mutation_adversarial_light"
    add_shard incremental_mutation_stress "incremental_mutation_stress"
    add_shard incremental_mutation_recovery "incremental_mutation_recovery"
    add_shard stack_overflow_nested_java "stack_overflow_nested_java"
    add_shard stack_overflow_call_walkers "stack_overflow_call_walkers"
    add_shard edge_types_probe "edge_types_probe"
    add_shard matrix_new_constructs "matrix_new_constructs"
    add_shard edge_imports "edge_imports"
    add_shard grammar_probe_d "grammar_probe_d"
    add_shard stack_overflow_nested_rust "stack_overflow_nested_rust"
    add_shard convergence_probe "convergence_probe"
    add_shard incremental_misc_tools "incremental_misc_tools"
    add_shard matrix_known_classes "matrix_known_classes"
    add_shard grammar_probe_a "grammar_probe_a"
    add_shard grammar_probe_e "grammar_probe_e"
    add_shard grammar_probe_f "grammar_probe_f"
    add_shard incremental_query_graph "incremental_query_graph"
    add_shard grammar_probe_b "grammar_probe_b"
    add_shard grammar_probe_g "grammar_probe_g"
    add_shard edge_structural "edge_structural"
    add_shard stack_overflow_lsp_front "stack_overflow_lsp_front"
    add_shard foundation_store "store_nodes store_edges store_search store_bulk store_pragmas store_checkpoint dump_verify_io"
    add_shard foundation_small "arena hash_table dyn_array str_intern log str_util platform dump_verify ac grammar_regression grammar_labels grammar_imports"
    add_shard discover_core "language userconfig gitignore git_context discover"
    add_shard incremental_search_graph "incremental_search_graph"
    add_shard incremental_code_trace "incremental_code_trace"
    add_shard grammar_probe_c "grammar_probe_c"
    add_shard lsp_front "scope type_rep go_lsp c_lsp php_lsp cs_lsp cs_lsp_bench"
    add_shard storage_artifact "lz4 zstd sqlite_writer artifact"
    add_shard integration "integration"
    add_shard stack_overflow_runtime "stack_overflow_runtime"
    add_shard stack_overflow_extractors "stack_overflow_extractors"
    add_shard pipeline_small "graph_buffer registry fqn route_canon path_alias"

    local shard_count="${#shard_names[@]}"
    local default_suite_jobs="$NPROC"
    local suite_jobs="${SUITE_JOBS:-$default_suite_jobs}"
    if (( suite_jobs > shard_count )); then
        suite_jobs="$shard_count"
    fi

    mkdir -p "$log_dir"
    echo "=== Step 3: sanitizer test suites (${shard_count} shards, jobs=${suite_jobs}) ==="
    echo "    logs: $log_dir"

    local -a pids=()
    local -a running_names=()
    local -a running_logs=()
    local -a running_statuses=()
    local -a all_logs=()
    local running=0
    local failed=0

    finish_shard_at() {
        local idx="$1"
        local pid="${pids[$idx]}"
        local name="${running_names[$idx]}"
        local log="${running_logs[$idx]}"
        local rc=0

        if wait "$pid"; then
            echo "PASS $name"
        else
            rc=$?
            echo "FAIL $name rc=$rc log=$log" >&2
            sed -n '1,220p' "$log" >&2 || true
            failed=1
        fi

        pids=("${pids[@]:0:$idx}" "${pids[@]:$((idx + 1))}")
        running_names=("${running_names[@]:0:$idx}" "${running_names[@]:$((idx + 1))}")
        running_logs=("${running_logs[@]:0:$idx}" "${running_logs[@]:$((idx + 1))}")
        running_statuses=("${running_statuses[@]:0:$idx}" "${running_statuses[@]:$((idx + 1))}")
        running=$((running - 1))
    }

    wait_for_any_shard() {
        local idx
        while true; do
            for idx in "${!running_statuses[@]}"; do
                if [[ -f "${running_statuses[$idx]}" ]]; then
                    finish_shard_at "$idx"
                    return
                fi
            done
            sleep 0.05
        done
    }

    local start_all end_all i
    start_all="$(date +%s)"
    for i in "${!shard_names[@]}"; do
        local name="${shard_names[$i]}"
        local suite_list="${shard_suites[$i]}"
        local log="$log_dir/$name.log"
        local status_file="$log.$$.status"
        all_logs+=("$log")

        (
            trap 'printf "%s\n" "$?" > "$status_file"' EXIT
            cd "$ROOT"
            shard_start="$(date +%s)"
            echo "=== shard $name start ==="
            echo "suites: $suite_list"
            IFS=" " read -r -a suite_args <<< "$suite_list"
            "$runner" "${suite_args[@]}"
            rc=$?
            shard_end="$(date +%s)"
            echo "=== shard $name end rc=$rc duration=$((shard_end - shard_start))s ==="
            exit "$rc"
        ) > "$log" 2>&1 &

        pids+=("$!")
        running_names+=("$name")
        running_logs+=("$log")
        running_statuses+=("$status_file")
        running=$((running + 1))

        if (( running >= suite_jobs )); then
            wait_for_any_shard
        fi
    done

    while (( running > 0 )); do
        wait_for_any_shard
    done

    end_all="$(date +%s)"
    echo "--- suite shard summaries ($((end_all - start_all))s) ---"
    for i in "${!shard_names[@]}"; do
        local name="${shard_names[$i]}"
        local log="${all_logs[$i]}"
        local summary
        summary="$(grep -E '^[[:space:]]+[0-9]+ passed|^=== shard .* end' "$log" | tail -n 2 | tr '\n' ' ' || true)"
        echo "  $name: ${summary:-see $log}"
    done

    if (( failed != 0 )); then
        return 1
    fi
}

# Verify compiler supports target arch
verify_compiler "$CC"

if $USE_CCACHE; then
    if ! command -v sccache >/dev/null 2>&1; then
        echo "ERROR: --ccache requested but sccache was not found in PATH" >&2
        exit 1
    fi
    MAKE_ARGS+=("CCACHE=sccache")
fi

if $FAST_GRAMMARS; then
    MAKE_ARGS+=("GRAMMAR_OPT=-O0")
    MAKE_ARGS+=("TEST_GRAMMAR_FAST_O0=1")
fi

# The production binary in this script is an auxiliary test artifact used by
# subprocess checks. Reuse vendored objects that are already non-sanitized in the
# test runner so cold builds do not compile SQLite/zstd twice. The normal
# `make cbm` production build still uses its dedicated prod objects.
if ! $SQLITE3_OBJ_PROD_OVERRIDDEN; then
    MAKE_ARGS+=('SQLITE3_OBJ_PROD=$(SQLITE3_OBJ_TEST)')
fi
if ! $ZSTD_OBJ_PROD_OVERRIDDEN; then
    MAKE_ARGS+=('ZSTD_OBJ_PROD=$(ZSTD_OBJ_TEST)')
fi

if $QUIET; then
    MAKE_FLAGS+=("-s")
fi

POST_CHECKS_STARTED=false
POST_CHECK_PIDS=()
POST_CHECK_NAMES=()
POST_CHECK_LOGS=()

run_parent_watchdog_check() {
    echo "=== Step 5: parent-death watchdog regression (#406/#407) ==="
    CBM_TEST_BINARY="$SUBPROCESS_BINARY" bash "$ROOT/tests/test_parent_watchdog.sh"
}

run_security_strings_check() {
    echo "=== Step 6: security-strings allow-list regression ==="
    bash "$ROOT/tests/test_security_strings_allowlist.sh"
}

start_default_post_checks() {
    local log_dir
    if [[ "$BUILD_OUT_DIR" = /* ]]; then
        log_dir="$BUILD_OUT_DIR/test-logs"
    else
        log_dir="$ROOT/$BUILD_OUT_DIR/test-logs"
    fi
    mkdir -p "$log_dir"

    local log
    log="$log_dir/parent_watchdog.log"
    (run_parent_watchdog_check) > "$log" 2>&1 &
    POST_CHECK_PIDS+=("$!")
    POST_CHECK_NAMES+=("parent_watchdog")
    POST_CHECK_LOGS+=("$log")

    log="$log_dir/security_strings_allowlist.log"
    (run_security_strings_check) > "$log" 2>&1 &
    POST_CHECK_PIDS+=("$!")
    POST_CHECK_NAMES+=("security_strings_allowlist")
    POST_CHECK_LOGS+=("$log")

    POST_CHECKS_STARTED=true
}

finish_default_post_checks() {
    local failed=0
    local i pid name log rc
    for i in "${!POST_CHECK_PIDS[@]}"; do
        pid="${POST_CHECK_PIDS[$i]}"
        name="${POST_CHECK_NAMES[$i]}"
        log="${POST_CHECK_LOGS[$i]}"
        if wait "$pid"; then
            cat "$log"
        else
            rc=$?
            cat "$log" >&2 || true
            echo "FAIL $name rc=$rc log=$log" >&2
            failed=1
        fi
    done
    return "$failed"
}

FASTAPI_FIXTURE_CACHE=""
FASTAPI_FIXTURE_CACHE_LOG=""
FASTAPI_FIXTURE_CACHE_PID=""
FASTAPI_DB_CACHE_DIR=""
FASTAPI_DB_CACHE_READY=false
TEST_FIXTURE_ROOT="${CBM_TEST_FIXTURE_DIR:-$ROOT/build/test-fixtures}"
if [[ "$TEST_FIXTURE_ROOT" != /* ]]; then
    TEST_FIXTURE_ROOT="$ROOT/$TEST_FIXTURE_ROOT"
fi

start_fastapi_fixture_cache() {
    if ! $PARALLEL_SUITES; then
        return 0
    fi

    FASTAPI_FIXTURE_CACHE="$TEST_FIXTURE_ROOT/fastapi-0.99.1"
    FASTAPI_FIXTURE_CACHE_LOG="$TEST_FIXTURE_ROOT/fastapi-0.99.1.log"
    FASTAPI_DB_CACHE_DIR="$TEST_FIXTURE_ROOT/fastapi-db-cache"
    mkdir -p "$FASTAPI_DB_CACHE_DIR"
    export CBM_INCREMENTAL_DB_CACHE_DIR="$FASTAPI_DB_CACHE_DIR"
    export CBM_INCREMENTAL_REINDEX_MODE="${CBM_INCREMENTAL_REINDEX_MODE:-fast}"
    if [[ -s "$FASTAPI_DB_CACHE_DIR/cbm_fastapi_incremental_fixture.db" ]]; then
        FASTAPI_DB_CACHE_READY=true
    else
        FASTAPI_DB_CACHE_READY=false
    fi

    if [[ -d "$FASTAPI_FIXTURE_CACHE/.git" ]]; then
        export CBM_FASTAPI_FIXTURE_CACHE="$FASTAPI_FIXTURE_CACHE"
        return 0
    fi

    mkdir -p "$(dirname "$FASTAPI_FIXTURE_CACHE")"
    (
        set -euo pipefail
        tmp="${FASTAPI_FIXTURE_CACHE}.tmp.$$"
        git -c advice.detachedHead=false clone --depth=1 --branch 0.99.1 --quiet --sparse \
            https://github.com/fastapi/fastapi.git "$tmp" 2>&1
        cd "$tmp"
        git sparse-checkout set --no-cone '/*' '!/docs' '!/tests' 2>&1
        cd "$ROOT"
        if [[ ! -e "$FASTAPI_FIXTURE_CACHE" ]]; then
            mv "$tmp" "$FASTAPI_FIXTURE_CACHE"
        fi
    ) > "$FASTAPI_FIXTURE_CACHE_LOG" 2>&1 &
    FASTAPI_FIXTURE_CACHE_PID="$!"
}

finish_fastapi_fixture_cache() {
    if [[ -z "$FASTAPI_FIXTURE_CACHE_PID" ]]; then
        return 0
    fi

    if ! wait "$FASTAPI_FIXTURE_CACHE_PID"; then
        echo "WARN: FastAPI fixture cache unavailable; incremental shards will clone remotely" >&2
        sed -n '1,80p' "$FASTAPI_FIXTURE_CACHE_LOG" >&2 || true
        return 0
    fi

    if [[ -d "$FASTAPI_FIXTURE_CACHE/.git" ]]; then
        export CBM_FASTAPI_FIXTURE_CACHE="$FASTAPI_FIXTURE_CACHE"
    else
        echo "WARN: FastAPI fixture cache missing after prepare; incremental shards will clone remotely" >&2
    fi
}

# Step 1: Clean unless this is a warm dev test run.
if ! $INCREMENTAL; then
    scripts/clean.sh --c-only
fi

start_fastapi_fixture_cache

if [[ "$BUILD_OUT_DIR" = /* ]]; then
    SUBPROCESS_BINARY="$BUILD_OUT_DIR/codebase-memory-mcp"
else
    SUBPROCESS_BINARY="$ROOT/$BUILD_OUT_DIR/codebase-memory-mcp"
fi

# Step 2: Build the sanitizer runner and production subprocess binary used by
# shell regression checks.
$ARCH_PREFIX make "${MAKE_FLAGS[@]+"${MAKE_FLAGS[@]}"}" -j"$NPROC" -f Makefile.cbm test-artifacts "${MAKE_ARGS[@]+"${MAKE_ARGS[@]}"}"

finish_fastapi_fixture_cache

if [ "${CBM_RUN_HANG_TEST:-0}" != "1" ]; then
    start_default_post_checks
fi

# Step 3: Run sanitizer tests.
suite_rc=0
if $PARALLEL_SUITES; then
    run_parallel_suites || suite_rc=$?
else
    run_serial_suites || suite_rc=$?
fi

post_rc=0
if $POST_CHECKS_STARTED; then
    finish_default_post_checks || post_rc=$?
fi

if (( suite_rc != 0 )); then
    exit "$suite_rc"
fi

# Step 4: C++ large-TU index-hang regression guard (#410). Runs the PROD binary
# in a subprocess with a wall-clock timeout — a hang must fail, not block the run.
# Opt-in via CBM_RUN_HANG_TEST=1 (it needs the prod binary, which the ASan unit
# run above does not build). Skipped by default so the fast unit run stays fast.
if [ "${CBM_RUN_HANG_TEST:-0}" = "1" ]; then
    echo "=== Step 4: C++ index-hang regression (#410) ==="
    CBM_TEST_BINARY="$SUBPROCESS_BINARY" bash "$ROOT/tests/test_cpp_index_hang.sh"
fi

# Step 5: Parent-death watchdog regression (#406/#407). Verifies the prod stdio
# binary built in Step 2 self-exits when its launching parent is killed.
if ! $POST_CHECKS_STARTED; then
    run_parent_watchdog_check
fi

# Step 6: security-strings URL allow-list regression. The MSYS2 CLANG64 toolchain
# bakes its package-tracker URL into the static Windows .exe; the binary string
# audit must allow-list it (Windows-only — Linux smoke never saw it).
if ! $POST_CHECKS_STARTED; then
    run_security_strings_check
fi

if (( post_rc != 0 )); then
    exit "$post_rc"
fi

echo "=== All tests passed ==="
