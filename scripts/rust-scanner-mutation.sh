#!/usr/bin/env bash
# Seeded behavioral mutation gate for historically fixed Rust scanner defects.
# Agent output is a bounded verdict; complete build and suite logs stay under
# the collision-safe artifact directory.

set -euo pipefail

ROOT="${CBM_MUTATION_ROOT:-$(cd "$(dirname "$0")/.." && pwd)}"
MANIFEST="${CBM_MUTATION_MANIFEST:-$ROOT/tests/mutation/rust-scanner.tsv}"
STEP="initialization"
ARTIFACT_DIR=""

fail_bounded() {
    printf 'FAIL mutation-rust: %.900s\n' "$1" >&2
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

if [[ ! -f "$MANIFEST" ]]; then
    fail_bounded "seed manifest is missing: $MANIFEST"
    exit 2
fi

GIT_BIN="${CBM_MUTATION_GIT:-$(command -v git || true)}"
MAKE_BIN="${CBM_MUTATION_MAKE:-$(command -v make || true)}"
TAR_BIN="${CBM_MUTATION_TAR:-$(command -v tar || true)}"
PYTHON_BIN="${CBM_MUTATION_PYTHON:-$(command -v python3 || true)}"
for tool_spec in "git:$GIT_BIN" "make:$MAKE_BIN" "tar:$TAR_BIN" "python3:$PYTHON_BIN"; do
    tool_name="${tool_spec%%:*}"
    tool_path="${tool_spec#*:}"
    if [[ -z "$tool_path" || ! -x "$tool_path" ]]; then
        fail_bounded "required tool $tool_name is missing or not executable: ${tool_path:-<unset>}"
        exit 2
    fi
done

checkout_state() {
    if [[ -n "${CBM_MUTATION_CHECKOUT_PROBE:-}" ]]; then
        "$CBM_MUTATION_CHECKOUT_PROBE" "$ROOT"
    else
        printf '%s\n' "$($GIT_BIN -C "$ROOT" rev-parse --verify HEAD)"
        "$GIT_BIN" -C "$ROOT" status --porcelain=v1 --untracked-files=all
    fi
}
STEP="creating isolated artifact directory"
if [[ -n "${CBM_MUTATION_BUILD_DIR:-}" ]]; then
    BUILD_ROOT="$CBM_MUTATION_BUILD_DIR"
    if [[ -e "$BUILD_ROOT" ]]; then
        fail_bounded "mutation artifact directory already exists; choose a fresh path"
        exit 2
    fi
    mkdir -p "$BUILD_ROOT"
else
    mkdir -p "$ROOT/build"
    BUILD_ROOT="$(mktemp -d "$ROOT/build/mutation-rust.XXXXXX")"
fi
BUILD_ROOT="$(cd "$(dirname "$BUILD_ROOT")" && pwd)/$(basename "$BUILD_ROOT")"
ARTIFACT_DIR="$BUILD_ROOT/artifacts"
mkdir -p "$ARTIFACT_DIR"

STEP="verifying clean committed checkout"
if ! HEAD_COMMIT="$($GIT_BIN -C "$ROOT" rev-parse --verify HEAD \
        2>"$ARTIFACT_DIR/checkout-state.log")"; then
    fail_bounded "could not resolve committed HEAD"
    exit 2
fi
if ! INITIAL_STATE="$(checkout_state 2>>"$ARTIFACT_DIR/checkout-state.log")"; then
    fail_bounded "could not inspect checkout state"
    exit 2
fi
if ! INITIAL_STATUS="$($GIT_BIN -C "$ROOT" status --porcelain=v1 \
        --untracked-files=all 2>>"$ARTIFACT_DIR/checkout-state.log")"; then
    fail_bounded "could not inspect checkout cleanliness"
    exit 2
fi
if [[ "${CBM_MUTATION_SKIP_CLEAN_CHECK:-0}" != "1" && -n "$INITIAL_STATUS" ]]; then
    fail_bounded "checkout is not clean; commit or relocate every tracked and untracked change"
    exit 2
fi

STEP="validating seed manifest"
SEED_TABLE="$ARTIFACT_DIR/seeds.tsv"
if ! "$PYTHON_BIN" - "$MANIFEST" "$ROOT" "$SEED_TABLE" \
        >"$ARTIFACT_DIR/manifest-validation.log" 2>&1 <<'PY'
import os
import re
import sys

manifest, root, output = sys.argv[1:]
rows = []
seen = set()
with open(manifest, encoding="utf-8") as handle:
    for line_number, raw in enumerate(handle, 1):
        line = raw.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        fields = line.split("\t")
        if len(fields) != 4:
            raise SystemExit(f"manifest line {line_number} must contain four tab-separated fields")
        seed_id, patch, suite, failure_regex = fields
        if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", seed_id) or seed_id in seen:
            raise SystemExit(f"invalid or duplicate seed id at line {line_number}: {seed_id}")
        if not re.fullmatch(r"[A-Za-z0-9_]+", suite):
            raise SystemExit(f"invalid suite at line {line_number}: {suite}")
        if not failure_regex:
            raise SystemExit(f"empty failure regex at line {line_number}")
        try:
            re.compile(failure_regex)
        except re.error as error:
            raise SystemExit(f"invalid failure regex at line {line_number}: {error}") from error
        patch_path = patch if os.path.isabs(patch) else os.path.join(root, patch)
        if not os.path.isfile(patch_path):
            raise SystemExit(f"missing patch at line {line_number}: {patch_path}")
        seen.add(seed_id)
        rows.append((seed_id, os.path.realpath(patch_path), suite, failure_regex))
if not rows:
    raise SystemExit("seed manifest contains no seeds")
with open(output, "w", encoding="utf-8") as handle:
    for row in rows:
        handle.write("\t".join(row) + "\n")
PY
then
    manifest_reason="$(head -c 900 "$ARTIFACT_DIR/manifest-validation.log")"
    fail_bounded "${manifest_reason:-seed manifest validation failed}"
    exit 2
fi

snapshot_checkout() {
    local destination="$1"
    if [[ -n "${CBM_MUTATION_SNAPSHOTTER:-}" ]]; then
        "$CBM_MUTATION_SNAPSHOTTER" "$destination"
    else
        mkdir -p "$destination"
        "$GIT_BIN" -C "$ROOT" archive --format=tar "$HEAD_COMMIT" | "$TAR_BIN" -xf - -C "$destination"
    fi
}

build_runner() {
    local source_root="$1"
    local runner="$2"
    local log="$3"
    if [[ -n "${CBM_MUTATION_BUILDER:-}" ]]; then
        "$CBM_MUTATION_BUILDER" "$source_root" "$runner" >"$log" 2>&1
    else
        (cd "$source_root" && "$MAKE_BIN" -j"${CBM_MUTATION_JOBS:-16}" -f Makefile.cbm \
            "$runner" BUILD_DIR="$(dirname "$runner")" SANITIZE=) >"$log" 2>&1
    fi
}

fingerprint_source_tree() {
    local source_root="$1"
    local output="$2"
    "$PYTHON_BIN" - "$source_root" >"$output" <<'PY'
import hashlib
import os
import sys

root = sys.argv[1]
for directory, names, files in os.walk(root):
    names[:] = sorted(name for name in names if name != "build")
    for name in sorted(files):
        path = os.path.join(directory, name)
        with open(path, "rb") as handle:
            digest = hashlib.sha256(handle.read()).hexdigest()
        print(digest, os.path.relpath(path, root))
PY
}

failure_regex_matches() {
    local pattern="$1"
    local log="$2"
    "$PYTHON_BIN" - "$pattern" "$log" <<'PY'
import re
import sys

pattern, log = sys.argv[1:]
with open(log, encoding="utf-8", errors="replace") as handle:
    raise SystemExit(0 if re.search(pattern, handle.read()) else 1)
PY
}

apply_patch_file() {
    local source_root="$1"
    local patch_path="$2"
    local mode="$3"
    if [[ -n "${CBM_MUTATION_PATCHER:-}" ]]; then
        "$CBM_MUTATION_PATCHER" "$source_root" "$patch_path" "$mode"
    elif [[ "$mode" == "check" ]]; then
        GIT_CEILING_DIRECTORIES="$(dirname "$source_root")" \
            "$GIT_BIN" -C "$source_root" apply --check "$patch_path"
    else
        GIT_CEILING_DIRECTORIES="$(dirname "$source_root")" \
            "$GIT_BIN" -C "$source_root" apply "$patch_path"
    fi
}

STEP="building committed baseline"
BASELINE="$BUILD_ROOT/baseline"
if ! snapshot_checkout "$BASELINE" >"$ARTIFACT_DIR/snapshot.log" 2>&1; then
    fail_bounded "could not materialize committed baseline $HEAD_COMMIT"
    exit 2
fi
if ! fingerprint_source_tree "$BASELINE" "$ARTIFACT_DIR/baseline-before.sha256" \
        2>"$ARTIFACT_DIR/baseline-fingerprint.log"; then
    fail_bounded "could not fingerprint pristine committed baseline"
    exit 2
fi
BASELINE_RUNNER="$BASELINE/build/mutation/test-runner"
if ! build_runner "$BASELINE" "$BASELINE_RUNNER" "$ARTIFACT_DIR/baseline-build.log"; then
    fail_bounded "committed baseline did not compile"
    exit 2
fi
if [[ ! -x "$BASELINE_RUNNER" ]]; then
    fail_bounded "baseline build succeeded without an executable test runner"
    exit 2
fi
if ! "$BASELINE_RUNNER" --list-suites >"$ARTIFACT_DIR/baseline-suites.txt" 2>&1; then
    fail_bounded "baseline runner could not list suites"
    exit 2
fi

# A green baseline is an independent oracle: a red suite before mutation cannot
# establish that a seed caused its named regression.
cut -f3 "$SEED_TABLE" | sort -u >"$ARTIFACT_DIR/required-suites.txt"
while IFS= read -r suite; do
    if ! grep -Fxq "$suite" "$ARTIFACT_DIR/baseline-suites.txt"; then
        continue
    fi
    if ! "$BASELINE_RUNNER" "$suite" >"$ARTIFACT_DIR/baseline-$suite.log" 2>&1; then
        fail_bounded "committed baseline suite is not green: $suite"
        exit 2
    fi
done <"$ARTIFACT_DIR/required-suites.txt"
if ! fingerprint_source_tree "$BASELINE" "$ARTIFACT_DIR/baseline-after.sha256" \
        2>>"$ARTIFACT_DIR/baseline-fingerprint.log"; then
    fail_bounded "could not re-fingerprint committed baseline"
    exit 2
fi
if ! cmp -s "$ARTIFACT_DIR/baseline-before.sha256" \
        "$ARTIFACT_DIR/baseline-after.sha256"; then
    fail_bounded "baseline build or suite changed committed source inputs"
    exit 2
fi

killed=0
survived=0
invalid=0
harness_errors=0
total=0
printf 'seed\toutcome\tdetail\n' >"$ARTIFACT_DIR/results.tsv"

while IFS=$'\t' read -r seed_id patch_path suite failure_regex; do
    total=$((total + 1))
    STEP="running seed $seed_id"
    seed_root="$BUILD_ROOT/seeds/$seed_id"
    seed_log_dir="$ARTIFACT_DIR/$seed_id"
    mkdir -p "$seed_root" "$seed_log_dir"
    if ! cp -cR "$BASELINE/." "$seed_root" 2>"$seed_log_dir/copy.log"; then
        cp -R "$BASELINE/." "$seed_root" 2>>"$seed_log_dir/copy.log"
    fi

    if ! grep -Fxq "$suite" "$ARTIFACT_DIR/baseline-suites.txt"; then
        harness_errors=$((harness_errors + 1))
        printf '%s\tHARNESS_ERROR\tunknown suite %s\n' "$seed_id" "$suite" \
            >>"$ARTIFACT_DIR/results.tsv"
        continue
    fi

    before_hash="$seed_log_dir/before.sha256"
    after_hash="$seed_log_dir/after.sha256"
    if ! fingerprint_source_tree "$seed_root" "$before_hash" \
            2>"$seed_log_dir/fingerprint.log"; then
        harness_errors=$((harness_errors + 1))
        printf '%s\tHARNESS_ERROR\tcould not fingerprint pristine seed\n' "$seed_id" \
            >>"$ARTIFACT_DIR/results.tsv"
        continue
    fi

    if ! apply_patch_file "$seed_root" "$patch_path" check \
            >"$seed_log_dir/patch-check.log" 2>&1; then
        harness_errors=$((harness_errors + 1))
        printf '%s\tHARNESS_ERROR\tpatch does not apply exactly once\n' "$seed_id" \
            >>"$ARTIFACT_DIR/results.tsv"
        continue
    fi
    if ! apply_patch_file "$seed_root" "$patch_path" apply \
            >"$seed_log_dir/patch.log" 2>&1; then
        harness_errors=$((harness_errors + 1))
        printf '%s\tHARNESS_ERROR\tpatch application failed\n' "$seed_id" \
            >>"$ARTIFACT_DIR/results.tsv"
        continue
    fi
    if ! fingerprint_source_tree "$seed_root" "$after_hash" \
            2>>"$seed_log_dir/fingerprint.log"; then
        harness_errors=$((harness_errors + 1))
        printf '%s\tHARNESS_ERROR\tcould not fingerprint mutated seed\n' "$seed_id" \
            >>"$ARTIFACT_DIR/results.tsv"
        continue
    fi
    if cmp -s "$before_hash" "$after_hash"; then
        harness_errors=$((harness_errors + 1))
        printf '%s\tHARNESS_ERROR\tpatch was a no-op\n' "$seed_id" >>"$ARTIFACT_DIR/results.tsv"
        continue
    fi

    seed_runner="$seed_root/build/mutation/test-runner"
    if ! build_runner "$seed_root" "$seed_runner" "$seed_log_dir/build.log"; then
        invalid=$((invalid + 1))
        printf '%s\tINVALID\tmutant did not compile\n' "$seed_id" >>"$ARTIFACT_DIR/results.tsv"
        continue
    fi
    if [[ ! -x "$seed_runner" ]]; then
        invalid=$((invalid + 1))
        printf '%s\tINVALID\tbuild produced no runner\n' "$seed_id" >>"$ARTIFACT_DIR/results.tsv"
        continue
    fi
    if ! "$seed_runner" --list-suites >"$seed_log_dir/suites.txt" 2>&1 ||
            ! grep -Fxq "$suite" "$seed_log_dir/suites.txt"; then
        harness_errors=$((harness_errors + 1))
        printf '%s\tHARNESS_ERROR\tunknown suite %s\n' "$seed_id" "$suite" \
            >>"$ARTIFACT_DIR/results.tsv"
        continue
    fi

    suite_status=0
    "$seed_runner" "$suite" >"$seed_log_dir/suite.log" 2>&1 || suite_status=$?
    if [[ "$suite_status" -eq 0 ]]; then
        survived=$((survived + 1))
        printf '%s\tSURVIVED\tnamed behavior remained green\n' "$seed_id" \
            >>"$ARTIFACT_DIR/results.tsv"
    elif failure_regex_matches "$failure_regex" "$seed_log_dir/suite.log" \
            >"$seed_log_dir/regex.log" 2>&1; then
        killed=$((killed + 1))
        printf '%s\tKILLED\tnamed behavioral assertion failed\n' "$seed_id" \
            >>"$ARTIFACT_DIR/results.tsv"
    else
        harness_errors=$((harness_errors + 1))
        printf '%s\tHARNESS_ERROR\tsuite failed outside named assertion\n' "$seed_id" \
            >>"$ARTIFACT_DIR/results.tsv"
    fi
done <"$SEED_TABLE"

STEP="verifying source checkout remained unchanged"
if ! FINAL_STATE="$(checkout_state 2>>"$ARTIFACT_DIR/checkout-state.log")"; then
    harness_errors=$((harness_errors + 1))
    printf '%s\tHARNESS_ERROR\tcould not inspect final checkout state\n' '__checkout__' \
        >>"$ARTIFACT_DIR/results.tsv"
    FINAL_STATE="$INITIAL_STATE"
fi
if [[ "$FINAL_STATE" != "$INITIAL_STATE" ]]; then
    harness_errors=$((harness_errors + 1))
    printf '%s\tHARNESS_ERROR\tsource checkout changed\n' '__checkout__' \
        >>"$ARTIFACT_DIR/results.tsv"
fi

trap - ERR
if [[ "$killed" -eq "$total" && "$survived" -eq 0 && "$invalid" -eq 0 &&
      "$harness_errors" -eq 0 ]]; then
    printf 'PASS mutation-rust: killed=%d survived=0 invalid=0 harness_errors=0 total=%d\n' \
        "$killed" "$total"
    printf 'Artifacts: %.900s\n' "$ARTIFACT_DIR"
    exit 0
fi
printf 'FAIL mutation-rust: killed=%d survived=%d invalid=%d harness_errors=%d total=%d\n' \
    "$killed" "$survived" "$invalid" "$harness_errors" "$total" >&2
printf 'Artifacts: %.900s\n' "$ARTIFACT_DIR" >&2
exit 1
