#!/usr/bin/env bash
# Native Apple LLVM source-coverage gate for the Rust scanner.
#
# Agent-facing output is deliberately bounded to a verdict and artifact path.
# Full compiler, suite, merge, export, and report output stays in BUILD_DIR.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STEP="initialization"
ARTIFACT_DIR=""

bounded_failure() {
    local reason="$1"
    local artifact_hint="${ARTIFACT_DIR:-not created}"
    printf 'FAIL coverage-rust: %.900s\n' "$reason" >&2
    printf 'Artifacts: %.900s\n' "$artifact_hint" >&2
}

unexpected_failure() {
    local exit_code="$1"
    trap - ERR
    bounded_failure "unexpected failure during $STEP (exit $exit_code)"
    exit "$exit_code"
}
trap 'unexpected_failure $?' ERR

if [[ "$(uname -s)" != "Darwin" && "${CBM_COVERAGE_ALLOW_NON_DARWIN:-0}" != "1" ]]; then
    bounded_failure "Apple LLVM coverage requires macOS"
    exit 2
fi

STEP="creating isolated build directory"
if [[ -n "${CBM_COVERAGE_BUILD_DIR:-}" ]]; then
    BUILD_DIR="$CBM_COVERAGE_BUILD_DIR"
    mkdir -p "$BUILD_DIR"
else
    mkdir -p "$ROOT/build"
    BUILD_DIR="$(mktemp -d "$ROOT/build/coverage-rust.XXXXXX")"
fi
ARTIFACT_DIR="$BUILD_DIR/coverage"
if [[ -e "$ARTIFACT_DIR" ]]; then
    bounded_failure "coverage artifact directory already exists; choose a fresh build directory"
    exit 2
fi
PROFILE_DIR="$ARTIFACT_DIR/profiles"
mkdir -p "$PROFILE_DIR"

BUILD_LOG="$ARTIFACT_DIR/build.log"
TEST_LOG="$ARTIFACT_DIR/test.log"
MERGE_LOG="$ARTIFACT_DIR/merge.log"
EXPORT_LOG="$ARTIFACT_DIR/export.log"
REPORT_LOG="$ARTIFACT_DIR/report.log"
SUMMARY_JSON="$ARTIFACT_DIR/summary.json"
METRICS_FILE="$ARTIFACT_DIR/metrics.tsv"
MANIFEST_FILE="$ARTIFACT_DIR/instrument-manifest.txt"
PROFILE_DATA="$ARTIFACT_DIR/rust-scanner.profdata"

resolve_xcrun_tool() {
    local name="$1"
    local override="$2"
    if [[ -n "$override" ]]; then
        printf '%s\n' "$override"
        return
    fi
    if ! command -v xcrun >/dev/null 2>&1; then
        bounded_failure "required tool xcrun is unavailable while resolving $name"
        exit 2
    fi
    local resolved
    if ! resolved="$(xcrun --find "$name" 2>>"$BUILD_LOG")"; then
        bounded_failure "xcrun could not resolve required tool $name"
        exit 2
    fi
    printf '%s\n' "$resolved"
}

STEP="resolving Apple LLVM tools"
CC_BIN="$(resolve_xcrun_tool clang "${CBM_COVERAGE_CC:-}")"
CXX_BIN="$(resolve_xcrun_tool clang++ "${CBM_COVERAGE_CXX:-}")"
PROFDATA_BIN="$(resolve_xcrun_tool llvm-profdata "${CBM_COVERAGE_PROFDATA:-}")"
COV_BIN="$(resolve_xcrun_tool llvm-cov "${CBM_COVERAGE_COV:-}")"
MAKE_BIN="${CBM_COVERAGE_MAKE:-$(command -v make || true)}"
PYTHON_BIN="${CBM_COVERAGE_PYTHON:-$(command -v python3 || true)}"
SHASUM_BIN="${CBM_COVERAGE_SHASUM:-$(command -v shasum || true)}"

SDK_PATH="${CBM_COVERAGE_SDKROOT:-}"
if [[ -z "$SDK_PATH" && "$(uname -s)" == "Darwin" ]]; then
    if ! SDK_PATH="$(xcrun --show-sdk-path 2>>"$BUILD_LOG")"; then
        bounded_failure "xcrun could not resolve the macOS SDK"
        exit 2
    fi
fi
if [[ -n "$SDK_PATH" && ! -d "$SDK_PATH" ]]; then
    bounded_failure "resolved macOS SDK does not exist: $SDK_PATH"
    exit 2
fi

for tool_spec in \
    "clang:$CC_BIN" \
    "clang++:$CXX_BIN" \
    "llvm-profdata:$PROFDATA_BIN" \
    "llvm-cov:$COV_BIN" \
    "make:$MAKE_BIN" \
    "python3:$PYTHON_BIN" \
    "shasum:$SHASUM_BIN"; do
    tool_name="${tool_spec%%:*}"
    tool_path="${tool_spec#*:}"
    if [[ -z "$tool_path" || ! -x "$tool_path" ]]; then
        bounded_failure "required tool $tool_name is missing or not executable: ${tool_path:-<unset>}"
        exit 2
    fi
done

version_major() {
    local tool="$1"
    local label="$2"
    local version_file="$ARTIFACT_DIR/version-$label.txt"
    if ! "$tool" --version >"$version_file" 2>&1; then
        bounded_failure "$label --version failed"
        exit 2
    fi
    local major
    major="$(sed -nE 's/.*version[[:space:]]+([0-9]+).*/\1/p' "$version_file" | head -n 1)"
    if [[ -z "$major" ]]; then
        bounded_failure "could not parse $label major version"
        exit 2
    fi
    printf '%s\n' "$major"
}

STEP="verifying coverage mapping tool versions"
CC_MAJOR="$(version_major "$CC_BIN" clang)"
CXX_MAJOR="$(version_major "$CXX_BIN" clangxx)"
PROFDATA_MAJOR="$(version_major "$PROFDATA_BIN" llvm-profdata)"
COV_MAJOR="$(version_major "$COV_BIN" llvm-cov)"
for mapping_major in "$CXX_MAJOR" "$PROFDATA_MAJOR" "$COV_MAJOR"; do
    if [[ "$mapping_major" != "$CC_MAJOR" ]]; then
        bounded_failure "coverage mapping version mismatch: clang=$CC_MAJOR clang++=$CXX_MAJOR llvm-profdata=$PROFDATA_MAJOR llvm-cov=$COV_MAJOR"
        exit 2
    fi
done
if [[ "${CBM_COVERAGE_REQUIRE_APPLE_LLVM:-1}" == "1" ]]; then
    for version_file in "$ARTIFACT_DIR"/version-*.txt; do
        if ! grep -Eq 'Apple (clang|LLVM) version' "$version_file"; then
            bounded_failure "resolved tool is not Apple LLVM: $(basename "$version_file")"
            exit 2
        fi
    done
fi

SOURCE_ROOT="${CBM_COVERAGE_SOURCE_ROOT:-$ROOT}"
SOURCES=(
    "$SOURCE_ROOT/internal/cbm/lsp/rust_lsp.c"
    "$SOURCE_ROOT/internal/cbm/lsp/rust_cargo.c"
    "$SOURCE_ROOT/internal/cbm/lsp/rust_rustdoc.c"
)
STEP="verifying requested Rust scanner source set"
for source_file in "${SOURCES[@]}"; do
    if [[ ! -f "$source_file" ]]; then
        bounded_failure "required coverage source is missing: $source_file"
        exit 2
    fi
done
SOURCE_HASH_BEFORE="$ARTIFACT_DIR/source-hashes-before.sha256"
SOURCE_HASH_AFTER="$ARTIFACT_DIR/source-hashes-after.sha256"
if ! "$SHASUM_BIN" -a 256 "${SOURCES[@]}" >"$SOURCE_HASH_BEFORE" \
        2>"$ARTIFACT_DIR/source-hash.log"; then
    bounded_failure "could not fingerprint requested Rust scanner sources"
    exit 2
fi

# Hash every source, header, fixture, vendored input, and the Makefile that can
# affect this test-runner. Isolating outputs is insufficient when another agent
# can edit shared inputs during a build.
INPUT_HASH_BEFORE="$ARTIFACT_DIR/build-inputs-before.sha256"
INPUT_HASH_AFTER="$ARTIFACT_DIR/build-inputs-after.sha256"
FINGERPRINT_ROOT="${CBM_COVERAGE_FINGERPRINT_ROOT:-$ROOT}"
fingerprint_build_inputs() {
    local output="$1"
    "$PYTHON_BIN" - "$FINGERPRINT_ROOT" "$output" <<'PY'
import hashlib
import os
import stat
import sys

root, output = sys.argv[1:]
inputs = ["Makefile.cbm", "internal/cbm", "src", "tests", "vendored"]
paths = []
for item in inputs:
    absolute = os.path.join(root, item)
    if os.path.isfile(absolute) or os.path.islink(absolute):
        paths.append(absolute)
        continue
    for directory, names, files in os.walk(absolute, followlinks=False):
        names.sort()
        files.sort()
        paths.extend(os.path.join(directory, name) for name in files)

with open(output, "w", encoding="utf-8") as handle:
    for path in sorted(paths):
        mode = os.lstat(path).st_mode
        if stat.S_ISLNK(mode):
            digest = hashlib.sha256(os.readlink(path).encode()).hexdigest()
        elif stat.S_ISREG(mode):
            hasher = hashlib.sha256()
            with open(path, "rb") as source:
                for chunk in iter(lambda: source.read(1024 * 1024), b""):
                    hasher.update(chunk)
            digest = hasher.hexdigest()
        else:
            continue
        handle.write(f"{digest}  {os.path.relpath(path, root)}\n")
PY
}
if ! fingerprint_build_inputs "$INPUT_HASH_BEFORE" \
        2>"$ARTIFACT_DIR/build-input-hash.log"; then
    bounded_failure "could not fingerprint complete coverage build inputs"
    exit 2
fi

RUNNER="${CBM_COVERAGE_RUNNER:-$BUILD_DIR/test-runner}"
NPROC="${CBM_COVERAGE_JOBS:-16}"
{
    printf 'compiler=%s\n' "$CC_BIN"
    printf 'compiler_major=%s\n' "$CC_MAJOR"
    printf 'cxx=%s\n' "$CXX_BIN"
    printf 'llvm_profdata=%s\n' "$PROFDATA_BIN"
    printf 'llvm_cov=%s\n' "$COV_BIN"
    printf 'sdk=%s\n' "${SDK_PATH:-not-required-by-fixture}"
    printf 'coverage_flags=%s\n' '-fprofile-instr-generate -fcoverage-mapping -fno-omit-frame-pointer'
    printf 'sanitizers=disabled (coverage lane only)\n'
    printf 'suite=rust_lsp\n'
    printf 'source=%s\n' "${SOURCES[@]}"
} >"$MANIFEST_FILE"

STEP="building instrumented Rust scanner suite"
if ! (cd "$ROOT" && SDKROOT="$SDK_PATH" "$MAKE_BIN" -j"$NPROC" -f Makefile.cbm "$RUNNER" \
        BUILD_DIR="$BUILD_DIR" COVERAGE=1 SANITIZE= CC="$CC_BIN" CXX="$CXX_BIN") \
        >"$BUILD_LOG" 2>&1; then
    bounded_failure "instrumented test-runner build failed"
    exit 1
fi
if [[ ! -x "$RUNNER" ]]; then
    bounded_failure "build succeeded without producing executable test runner: $RUNNER"
    exit 1
fi
if ! "$PYTHON_BIN" - "$BUILD_LOG" "$BUILD_DIR" "$RUNNER" \
        >"$ARTIFACT_DIR/instrument-validation.log" 2>&1 <<'PY'
import sys

log_path, build_dir, runner = sys.argv[1:]
with open(log_path, encoding="utf-8", errors="replace") as handle:
    commands = handle.read().splitlines()
flags = ("-fprofile-instr-generate", "-fcoverage-mapping",
         "-fno-omit-frame-pointer")
classes = (
    ("C", f"-o {build_dir}/lsp_all.o"),
    ("C++", f"-o {build_dir}/preprocessor.o"),
    ("grammar", f"-o {build_dir}/grammar_rust.o"),
    ("test link", f"-o {runner}"),
)
for label, marker in classes:
    matching = [line for line in commands if marker in line]
    if len(matching) != 1:
        raise SystemExit(f"build transcript has {len(matching)} {label} commands; expected 1")
    missing = [flag for flag in flags if flag not in matching[0]]
    if missing:
        raise SystemExit(f"build transcript lacks instrumentation on {label} command: {missing}")
PY
then
    instrument_reason="$(head -c 900 "$ARTIFACT_DIR/instrument-validation.log")"
    bounded_failure "${instrument_reason:-could not verify per-command coverage instrumentation}"
    exit 1
fi
if grep -Fq -- '-DCBM_SANITIZED_BUILD=1' "$BUILD_LOG"; then
    bounded_failure "coverage build unexpectedly enabled sanitizer feature macros"
    exit 1
fi

STEP="running Rust LSP suite"
PROFILE_PATTERN="$PROFILE_DIR/rust-%p-%m.profraw"
if ! (cd "$ROOT" && LLVM_PROFILE_FILE="$PROFILE_PATTERN" "$RUNNER" rust_lsp) \
        >"$TEST_LOG" 2>&1; then
    bounded_failure "Rust LSP suite failed"
    exit 1
fi

shopt -s nullglob
PROFILES=("$PROFILE_DIR"/*.profraw)
shopt -u nullglob
if [[ ${#PROFILES[@]} -eq 0 ]]; then
    bounded_failure "Rust LSP suite produced no raw coverage profiles"
    exit 1
fi
if ! "$SHASUM_BIN" -a 256 "${SOURCES[@]}" >"$SOURCE_HASH_AFTER" \
        2>>"$ARTIFACT_DIR/source-hash.log"; then
    bounded_failure "could not re-fingerprint Rust scanner sources after the suite"
    exit 1
fi
if ! cmp -s "$SOURCE_HASH_BEFORE" "$SOURCE_HASH_AFTER"; then
    bounded_failure "Rust scanner sources changed during the coverage build or suite"
    exit 1
fi
if ! fingerprint_build_inputs "$INPUT_HASH_AFTER" \
        2>>"$ARTIFACT_DIR/build-input-hash.log"; then
    bounded_failure "could not re-fingerprint complete coverage build inputs"
    exit 1
fi
if ! cmp -s "$INPUT_HASH_BEFORE" "$INPUT_HASH_AFTER"; then
    bounded_failure "coverage build inputs changed during the build or suite"
    exit 1
fi

STEP="merging raw profiles"
if ! "$PROFDATA_BIN" merge -sparse "${PROFILES[@]}" -o "$PROFILE_DATA" \
        >"$MERGE_LOG" 2>&1; then
    bounded_failure "llvm-profdata could not merge Rust scanner profiles"
    exit 1
fi
if [[ ! -s "$PROFILE_DATA" ]]; then
    bounded_failure "llvm-profdata succeeded without producing merged profile data"
    exit 1
fi

STEP="exporting exact Rust scanner coverage"
if ! "$COV_BIN" export "$RUNNER" -instr-profile="$PROFILE_DATA" \
        -summary-only -show-branch-summary --sources "${SOURCES[@]}" \
        >"$SUMMARY_JSON" 2>"$EXPORT_LOG"; then
    bounded_failure "llvm-cov export failed"
    exit 1
fi
if ! "$COV_BIN" report "$RUNNER" -instr-profile="$PROFILE_DATA" \
        -show-branch-summary -show-region-summary --sources "${SOURCES[@]}" \
        >"$REPORT_LOG" 2>&1; then
    bounded_failure "llvm-cov report failed"
    exit 1
fi
if ! "$SHASUM_BIN" -a 256 "${SOURCES[@]}" >"$SOURCE_HASH_AFTER" \
        2>>"$ARTIFACT_DIR/source-hash.log"; then
    bounded_failure "could not re-fingerprint Rust scanner sources after reporting"
    exit 1
fi
if ! cmp -s "$SOURCE_HASH_BEFORE" "$SOURCE_HASH_AFTER"; then
    bounded_failure "Rust scanner sources changed during coverage reporting"
    exit 1
fi

# These floors are rounded down to one decimal place from two byte-identical
# native runs at scanner commit e6ca3209.  The margin avoids
# display-rounding noise while still rejecting a meaningful regression.
# Per-file floors prevent the larger LSP scanner from hiding a test loss in
# either Cargo or rustdoc adapters.
LSP_LINE_FLOOR="${CBM_COVERAGE_RUST_LSP_LINE_FLOOR:-77.0}"
LSP_BRANCH_FLOOR="${CBM_COVERAGE_RUST_LSP_BRANCH_FLOOR:-59.3}"
CARGO_LINE_FLOOR="${CBM_COVERAGE_RUST_CARGO_LINE_FLOOR:-78.5}"
CARGO_BRANCH_FLOOR="${CBM_COVERAGE_RUST_CARGO_BRANCH_FLOOR:-63.0}"
RUSTDOC_LINE_FLOOR="${CBM_COVERAGE_RUST_RUSTDOC_LINE_FLOOR:-64.0}"
RUSTDOC_BRANCH_FLOOR="${CBM_COVERAGE_RUST_RUSTDOC_BRANCH_FLOOR:-37.6}"

STEP="validating exact source set and coverage floors"
if ! "$PYTHON_BIN" - "$SUMMARY_JSON" "$METRICS_FILE" \
        "${SOURCES[0]}" "$LSP_LINE_FLOOR" "$LSP_BRANCH_FLOOR" \
        "${SOURCES[1]}" "$CARGO_LINE_FLOOR" "$CARGO_BRANCH_FLOOR" \
        "${SOURCES[2]}" "$RUSTDOC_LINE_FLOOR" "$RUSTDOC_BRANCH_FLOOR" \
        >"$ARTIFACT_DIR/verdict.txt" 2>"$ARTIFACT_DIR/validation.log" <<'PY'
import json
import math
import os
import sys

summary_path, metrics_path = sys.argv[1:3]
spec = []
for offset in range(3, len(sys.argv), 3):
    path = os.path.realpath(sys.argv[offset])
    try:
        line_floor = float(sys.argv[offset + 1])
        branch_floor = float(sys.argv[offset + 2])
    except ValueError as error:
        raise SystemExit(f"invalid coverage floor for {path}: {error}") from error
    if (not math.isfinite(line_floor) or not 0.0 <= line_floor <= 100.0 or
            not math.isfinite(branch_floor) or not 0.0 <= branch_floor <= 100.0):
        raise SystemExit(
            f"invalid coverage floor for {path}: line={line_floor}, branch={branch_floor}; "
            "expected finite values from 0 through 100")
    spec.append((path, line_floor, branch_floor))

with open(summary_path, encoding="utf-8") as handle:
    payload = json.load(handle)
if len(payload.get("data", [])) != 1:
    raise SystemExit("coverage export must contain exactly one data object")

files = payload["data"][0].get("files", [])
actual = {os.path.realpath(item["filename"]): item for item in files}
expected = {path for path, _, _ in spec}
if len(actual) != len(files):
    raise SystemExit("coverage export contains duplicate source entries")
if set(actual) != expected:
    missing = sorted(expected - set(actual))
    extra = sorted(set(actual) - expected)
    raise SystemExit(f"coverage source-set mismatch: missing={missing}, extra={extra}")

failures = []
rows = []
summaries = []
for path, line_floor, branch_floor in spec:
    summary = actual[path].get("summary", {})
    lines = summary.get("lines")
    branches = summary.get("branches")
    if not isinstance(lines, dict) or not isinstance(branches, dict):
        raise SystemExit(f"coverage summary lacks line/branch data for {path}")
    line_count = int(lines.get("count", -1))
    line_covered = int(lines.get("covered", -1))
    branch_count = int(branches.get("count", -1))
    branch_covered = int(branches.get("covered", -1))
    if (line_count <= 0 or branch_count <= 0 or line_covered < 0 or
            branch_covered < 0 or line_covered > line_count or
            branch_covered > branch_count):
        raise SystemExit(f"coverage summary has invalid counts for {path}")
    line_pct = 100.0 if line_count == 0 else 100.0 * line_covered / line_count
    branch_pct = 100.0 if branch_count == 0 else 100.0 * branch_covered / branch_count
    name = os.path.basename(path)
    rows.append((name, line_covered, line_count, line_pct,
                 branch_covered, branch_count, branch_pct,
                 line_floor, branch_floor))
    summaries.append(f"{name} L={line_pct:.2f}% B={branch_pct:.2f}%")
    if line_pct + 1e-9 < line_floor:
        failures.append(f"{name} lines {line_pct:.2f}% < {line_floor:.2f}%")
    if branch_pct + 1e-9 < branch_floor:
        failures.append(f"{name} branches {branch_pct:.2f}% < {branch_floor:.2f}%")

with open(metrics_path, "w", encoding="utf-8") as handle:
    handle.write("file\tlines_covered\tlines_total\tline_percent\t"
                 "branches_covered\tbranches_total\tbranch_percent\t"
                 "line_floor\tbranch_floor\n")
    for row in rows:
        handle.write("\t".join(str(value) for value in row) + "\n")

if failures:
    print("FAIL coverage-rust: " + "; ".join(failures))
    raise SystemExit(1)
print("PASS coverage-rust: " + "; ".join(summaries) + "; sources=3")
PY
then
    verdict="$(head -c 1200 "$ARTIFACT_DIR/verdict.txt")"
    if [[ -n "$verdict" ]]; then
        printf '%s\n' "$verdict" >&2
        printf 'Artifacts: %.900s\n' "$ARTIFACT_DIR" >&2
    else
        validation_reason="$(head -c 900 "$ARTIFACT_DIR/validation.log")"
        bounded_failure "${validation_reason:-coverage validation failed; see validation.log}"
    fi
    exit 1
fi

trap - ERR
head -c 1400 "$ARTIFACT_DIR/verdict.txt"
printf '\nArtifacts: %.900s\n' "$ARTIFACT_DIR"
