#!/usr/bin/env bash
# Exercise the shipped argument-resolution path: helper-only tests cannot catch
# a missing stdin gate in main.c. Keep each pipe open until the command exits.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${CBM_TEST_BINARY:-${ROOT}/build/c/codebase-memory-mcp}"
if [[ ! -x "$BINARY" && -x "$BINARY.exe" ]]; then
    BINARY="$BINARY.exe"
fi
[[ -x "$BINARY" ]] || { echo "missing binary: $BINARY" >&2; exit 2; }

# shellcheck source=../scripts/test-runtime.sh
source "$ROOT/scripts/test-runtime.sh"
cbm_test_runtime_init
trap 'cbm_test_runtime_cleanup "$BINARY"' EXIT

PYTHON_BINARY="$BINARY"
if command -v cygpath >/dev/null 2>&1; then
    PYTHON_BINARY="$(cygpath -m "$BINARY")"
fi

python3 - "$PYTHON_BINARY" <<'PY'
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

binary = str(Path(sys.argv.pop(1)).resolve())
fixture = str(Path(os.environ["CBM_CACHE_DIR"]).parent)


class CliStdinTests(unittest.TestCase):
    def invoke(self, tool, payload=b""):
        # Files prevent a full output pipe from masquerading as a stdin hang.
        with tempfile.TemporaryFile(dir=fixture) as stdout, \
                tempfile.TemporaryFile(dir=fixture) as stderr:
            process = subprocess.Popen(
                [binary, "cli", "--json", tool], cwd=fixture,
                stdin=subprocess.PIPE, stdout=stdout, stderr=stderr,
            )
            try:
                if payload is not None:
                    process.stdin.write(payload)
                    process.stdin.close()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    stderr.seek(0)
                    self.fail(
                        f"cli {tool} did not finish with stdin "
                        f"{'held open' if payload is None else 'closed'}: "
                        + stderr.read().decode("utf-8", "replace")
                    )
                stdout.seek(0)
                stderr.seek(0)
                output = stdout.read().decode("utf-8")
                diagnostics = stderr.read().decode("utf-8", "replace")
                try:
                    response = json.loads(output)
                except json.JSONDecodeError:
                    self.fail(f"cli {tool} returned invalid JSON: {output!r}; {diagnostics}")
                return process.returncode, response
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=5)
                if not process.stdin.closed:
                    process.stdin.close()

    def test_list_projects_finishes_before_stdin_closes(self):
        # EOF is a control for startup/cache failures, not the regression input.
        for payload in (b"", None):
            with self.subTest(stdin="EOF" if payload is not None else "open pipe"):
                rc, response = self.invoke("list_projects", payload)
                self.assertEqual(rc, 0, response)
                self.assertIs(response["isError"], False)
                self.assertEqual(response["structuredContent"]["projects"], [])

    def test_unknown_tool_is_rejected_before_stdin_closes(self):
        tool = "__stdin_unknown_tool__"
        rc, response = self.invoke(tool, None)
        self.assertEqual(rc, 1, response)
        self.assertIs(response["isError"], True)
        self.assertEqual(response["structuredContent"], {"error": f"unknown tool: {tool}"})

    def test_argument_tool_still_reads_piped_json(self):
        # Distinct validation errors prove the pipe's bytes reached the tool;
        # dropping stdin or substituting {} cannot satisfy either assertion.
        cases = (
            ({"limit": 0}, "limit must be an integer between 1 and 500; values are not clamped"),
            ({"verbose": "wrong"}, "verbose must be a boolean"),
        )
        for arguments, error in cases:
            with self.subTest(arguments=arguments):
                rc, response = self.invoke("index_status", json.dumps(arguments).encode("utf-8"))
                self.assertEqual(rc, 1, response)
                self.assertIs(response["isError"], True)
                self.assertEqual(response["structuredContent"], {"error": error})


unittest.main(verbosity=2)
PY
