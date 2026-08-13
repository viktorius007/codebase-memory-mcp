#!/usr/bin/env bash
# Fast classifier contract for the seeded Rust scanner mutation harness.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORKDIR="$(mktemp -d "${TMPDIR:-/tmp}/cbm-rust-mutation-contract.XXXXXX")"
BUILD_DIRS=()
LAST_BUILD_DIR=""

cleanup() {
    if command -v trash >/dev/null 2>&1; then
        for build_dir in "${BUILD_DIRS[@]:-}"; do
            if [[ -e "$build_dir" ]]; then
                trash "$build_dir"
            fi
        done
        trash "$WORKDIR"
    fi
}
trap cleanup EXIT

fail() {
    printf 'FAIL: %s\n' "$1" >&2
    exit 1
}

FIXTURE="$WORKDIR/fixture"
mkdir -p "$FIXTURE"
printf '%s\n' 'GOOD' >"$FIXTURE/state.txt"

make_patch() {
    local name="$1"
    local replacement="$2"
    cat >"$WORKDIR/$name.patch" <<EOF
diff --git a/state.txt b/state.txt
--- a/state.txt
+++ b/state.txt
@@ -1 +1 @@
-GOOD
+$replacement
EOF
}
make_patch killed KILLED
make_patch survived SURVIVED
make_patch invalid INVALID
make_patch unknown UNKNOWN
make_patch wrong-red WRONG_RED
cat >"$WORKDIR/no-op.patch" <<'EOF'
diff --git a/build/ignored.txt b/build/ignored.txt
new file mode 100644
--- /dev/null
+++ b/build/ignored.txt
@@ -0,0 +1 @@
+excluded from source fingerprint
EOF

cat >"$WORKDIR/snapshotter" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
destination="$1"
mkdir -p "$destination"
cp -R "$FAKE_MUTATION_FIXTURE/." "$destination"
EOF
chmod +x "$WORKDIR/snapshotter"

cat >"$WORKDIR/builder" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
source_root="$1"
runner="$2"
state="$(tr -d '\n' <"$source_root/state.txt")"
if [[ "${FAKE_MUTATION_MUTATE_BASELINE_SOURCE:-0}" == "1" && "$state" == "GOOD" ]]; then
    printf '%s\n' contaminated >>"$source_root/state.txt"
fi
if [[ "$state" == "INVALID" ]]; then
    if [[ "${FAKE_MUTATION_BUILD_NOISE:-0}" == "1" ]]; then
        awk 'BEGIN { for (i = 0; i < 200000; i++) printf "x" }'
    fi
    exit 1
fi
mkdir -p "$(dirname "$runner")"
cat >"$runner" <<RUNNER
#!/usr/bin/env bash
set -euo pipefail
if [[ "\${1:-}" == "--list-suites" ]]; then
    printf '%s\\n' fixture_suite
    exit 0
fi
if [[ "\${1:-}" != "fixture_suite" ]]; then
    exit 2
fi
if [[ "$state" == "KILLED" ]]; then
    printf '%s\\n' 'fixture_named_assertion FAIL expected behavior'
    exit 1
fi
if [[ "$state" == "WRONG_RED" ]]; then
    printf '%s\\n' 'unrelated assertion failed'
    exit 1
fi
printf '%s\\n' 'Tests: 1 passed, 0 failed'
RUNNER
chmod +x "$runner"
EOF
chmod +x "$WORKDIR/builder"

cat >"$WORKDIR/manifest.tsv" <<EOF
killed	$WORKDIR/killed.patch	fixture_suite	fixture_named_assertion
survived	$WORKDIR/survived.patch	fixture_suite	fixture_named_assertion
invalid	$WORKDIR/invalid.patch	fixture_suite	fixture_named_assertion
no-op	$WORKDIR/no-op.patch	fixture_suite	fixture_named_assertion
unknown	$WORKDIR/unknown.patch	missing_suite	fixture_named_assertion
wrong-red	$WORKDIR/wrong-red.patch	fixture_suite	fixture_named_assertion
EOF

BASE_ENV=(
    CBM_MUTATION_SKIP_CLEAN_CHECK=1
    CBM_MUTATION_SNAPSHOTTER="$WORKDIR/snapshotter"
    CBM_MUTATION_BUILDER="$WORKDIR/builder"
    CBM_MUTATION_MANIFEST="$WORKDIR/manifest.tsv"
    FAKE_MUTATION_FIXTURE="$FIXTURE"
)

run_case() {
    local name="$1"
    local expected_status="$2"
    local expected_first="$3"
    shift 3
    local output="$WORKDIR/$name.out"
    local exit_status=0
    local build_dir="$ROOT/build/mutation-contract-$name.$$.${RANDOM}"
    BUILD_DIRS+=("$build_dir")
    LAST_BUILD_DIR="$build_dir"
    env "${BASE_ENV[@]}" CBM_MUTATION_BUILD_DIR="$build_dir" "$@" \
        bash "$ROOT/scripts/rust-scanner-mutation.sh" >"$output" 2>&1 || exit_status=$?
    if [[ "$exit_status" -ne "$expected_status" ]]; then
        fail "$name exited $exit_status, expected $expected_status"
    fi
    if ! head -n 1 "$output" | grep -Eq "$expected_first"; then
        fail "$name did not print its bounded verdict first"
    fi
    local bytes
    bytes="$(wc -c <"$output" | tr -d ' ')"
    if [[ "$bytes" -gt 2048 ]]; then
        fail "$name emitted $bytes bytes (limit 2048)"
    fi
}

run_case classifiers 1 \
    '^FAIL mutation-rust: killed=1 survived=1 invalid=1 harness_errors=3 total=6$' \
    FAKE_MUTATION_BUILD_NOISE=1

for expected_result in \
    $'killed\tKILLED\tnamed behavioral assertion failed' \
    $'survived\tSURVIVED\tnamed behavior remained green' \
    $'invalid\tINVALID\tmutant did not compile' \
    $'no-op\tHARNESS_ERROR\tpatch was a no-op' \
    $'unknown\tHARNESS_ERROR\tunknown suite missing_suite' \
    $'wrong-red\tHARNESS_ERROR\tsuite failed outside named assertion'; do
    if ! grep -Fxq "$expected_result" "$LAST_BUILD_DIR/artifacts/results.tsv"; then
        fail "classifier result row missing or swapped: $expected_result"
    fi
done
if [[ "$(wc -c <"$LAST_BUILD_DIR/artifacts/invalid/build.log" | tr -d ' ')" -lt 200000 ]]; then
    fail "invalid mutant's complete noisy build log was not retained"
fi

cat >"$WORKDIR/killed-only.tsv" <<EOF
killed	$WORKDIR/killed.patch	fixture_suite	fixture_named_assertion
EOF
run_case all-killed 0 \
    '^PASS mutation-rust: killed=1 survived=0 invalid=0 harness_errors=0 total=1$' \
    CBM_MUTATION_MANIFEST="$WORKDIR/killed-only.tsv"

cat >"$WORKDIR/python-regex.tsv" <<EOF
python-regex	$WORKDIR/killed.patch	fixture_suite	\\Afixture_named_assertion
EOF
run_case python-regex-engine 0 \
    '^PASS mutation-rust: killed=1 survived=0 invalid=0 harness_errors=0 total=1$' \
    CBM_MUTATION_MANIFEST="$WORKDIR/python-regex.tsv"

printf 'empty-regex\t%s\tfixture_suite\t\n' "$WORKDIR/killed.patch" >"$WORKDIR/empty-regex.tsv"
run_case empty-regex 2 '^FAIL mutation-rust: empty failure regex at line 1$' \
    CBM_MUTATION_MANIFEST="$WORKDIR/empty-regex.tsv"

run_case baseline-source-drift 2 \
    '^FAIL mutation-rust: baseline build or suite changed committed source inputs$' \
    CBM_MUTATION_MANIFEST="$WORKDIR/killed-only.tsv" \
    FAKE_MUTATION_MUTATE_BASELINE_SOURCE=1

cat >"$WORKDIR/checkout-probe" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
count_file="$FAKE_MUTATION_PROBE_COUNT"
count=0
if [[ -f "$count_file" ]]; then
    count="$(cat "$count_file")"
fi
count=$((count + 1))
printf '%s\n' "$count" >"$count_file"
printf 'checkout-state-%s\n' "$count"
EOF
chmod +x "$WORKDIR/checkout-probe"
run_case checkout-changed 1 \
    '^FAIL mutation-rust: killed=1 survived=0 invalid=0 harness_errors=1 total=1$' \
    CBM_MUTATION_MANIFEST="$WORKDIR/killed-only.tsv" \
    CBM_MUTATION_CHECKOUT_PROBE="$WORKDIR/checkout-probe" \
    FAKE_MUTATION_PROBE_COUNT="$WORKDIR/probe-count"

run_case missing-tool 2 '^FAIL mutation-rust: required tool make is missing' \
    CBM_MUTATION_MAKE="$WORKDIR/missing-make"

cat >"$WORKDIR/noisy-checkout-probe" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
awk 'BEGIN { for (i = 0; i < 200000; i++) printf "q" }' >&2
exit 7
EOF
chmod +x "$WORKDIR/noisy-checkout-probe"
run_case noisy-early-helper 2 \
    '^FAIL mutation-rust: could not inspect checkout state$' \
    CBM_MUTATION_MANIFEST="$WORKDIR/killed-only.tsv" \
    CBM_MUTATION_CHECKOUT_PROBE="$WORKDIR/noisy-checkout-probe"
if [[ "$(wc -c <"$LAST_BUILD_DIR/artifacts/checkout-state.log" | tr -d ' ')" -lt 200000 ]]; then
    fail "noisy early helper's complete stderr was not retained"
fi

GIT_FIXTURE="$WORKDIR/git-fixture"
mkdir -p "$GIT_FIXTURE"
git -C "$GIT_FIXTURE" init -q
git -C "$GIT_FIXTURE" config user.name 'Mutation Contract'
git -C "$GIT_FIXTURE" config user.email 'mutation-contract@example.invalid'
printf '%s\n' GOOD >"$GIT_FIXTURE/state.txt"
git -C "$GIT_FIXTURE" add state.txt
git -C "$GIT_FIXTURE" commit -qm 'fixture baseline'
printf '%s\n' DIRTY >"$GIT_FIXTURE/state.txt"

run_git_case() {
    local name="$1"
    local expected_status="$2"
    local expected_first="$3"
    shift 3
    local output="$WORKDIR/$name.out"
    local exit_status=0
    local build_dir="$ROOT/build/mutation-contract-$name.$$.${RANDOM}"
    BUILD_DIRS+=("$build_dir")
    env CBM_MUTATION_ROOT="$GIT_FIXTURE" \
        CBM_MUTATION_BUILDER="$WORKDIR/builder" \
        CBM_MUTATION_MANIFEST="$WORKDIR/killed-only.tsv" \
        CBM_MUTATION_BUILD_DIR="$build_dir" "$@" \
        bash "$ROOT/scripts/rust-scanner-mutation.sh" >"$output" 2>&1 || exit_status=$?
    if [[ "$exit_status" -ne "$expected_status" ]]; then
        fail "$name exited $exit_status, expected $expected_status"
    fi
    if ! head -n 1 "$output" | grep -Eq "$expected_first"; then
        fail "$name did not print its bounded verdict first"
    fi
    if [[ "$(wc -c <"$output" | tr -d ' ')" -gt 2048 ]]; then
        fail "$name exceeded bounded output"
    fi
}

run_git_case dirty-checkout 2 '^FAIL mutation-rust: checkout is not clean'
run_git_case committed-archive 0 \
    '^PASS mutation-rust: killed=1 survived=0 invalid=0 harness_errors=0 total=1$' \
    CBM_MUTATION_SKIP_CLEAN_CHECK=1
if [[ "$(tr -d '\n' <"$GIT_FIXTURE/state.txt")" != "DIRTY" ]]; then
    fail "default committed archive path changed the source checkout"
fi

printf '%s\n' \
    'PASS: Rust scanner mutation harness distinguishes every outcome, rejects dirty/noisy/empty/drifting inputs, retains logs, and snapshots committed HEAD'
