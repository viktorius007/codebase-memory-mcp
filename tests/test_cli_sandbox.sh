#!/usr/bin/env bash
# The actual macOS sandbox must deny cache mutations: mode bits alone cannot
# reproduce a read-only cache owned by the CLI's effective user.
set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
    echo "SKIP: CLI cache sandbox tests require macOS sandbox-exec"
    exit 0
fi
[[ -x /usr/bin/sandbox-exec ]] || {
    echo "missing macOS sandbox-exec" >&2
    exit 2
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${CBM_TEST_BINARY:-${ROOT}/build/c/codebase-memory-mcp}"
[[ -x "$BINARY" ]] || { echo "missing binary: $BINARY" >&2; exit 2; }

# shellcheck source=../scripts/test-runtime.sh
source "$ROOT/scripts/test-runtime.sh"
cbm_test_runtime_init
trap 'cbm_test_runtime_cleanup "$BINARY"' EXIT

python3 - "$BINARY" <<'PY'
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import time
import unittest

binary = str(Path(sys.argv.pop(1)).resolve())
fixture = Path(os.environ["CBM_CACHE_DIR"]).parent


class CliSandboxTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp(dir=fixture, prefix="sandbox-")
        self.cache = Path(self.directory) / "cache"
        self.cache.mkdir(mode=0o700)
        self.environment = dict(os.environ, CBM_CACHE_DIR=str(self.cache), LC_ALL="C")
        self.addCleanup(self.cleanup)
        # Runtime coordination remains writable and separate from the cache.
        self.profile = (
            '(version 1)(allow default)(deny file-write* '
            f'(subpath {json.dumps(str(self.cache))}))'
        )

    def run_command(self, arguments):
        # A daemon child retaining a pipe must not prevent its caller exiting.
        with tempfile.TemporaryFile(dir=fixture) as stdout, \
                tempfile.TemporaryFile(dir=fixture) as stderr:
            result = subprocess.run(
                arguments, env=self.environment, cwd=self.directory,
                stdin=subprocess.DEVNULL, stdout=stdout, stderr=stderr, timeout=15,
            )
            stdout.seek(0)
            stderr.seek(0)
            result.stdout = stdout.read().decode("utf-8", "replace")
            result.stderr = stderr.read().decode("utf-8", "replace")
            return result

    def cleanup(self):
        stopped = self.run_command([binary, "daemon", "stop"])
        self.assertEqual(stopped.returncode, 0, stopped.stdout + stopped.stderr)
        for _ in range(50):
            status = self.run_command([binary, "daemon", "status"])
            if status.returncode == 1 and "daemon: not running" in status.stdout:
                shutil.rmtree(self.directory)
                return
            time.sleep(0.1)
        self.fail(f"fixture daemon did not stop: {status.stdout}; {status.stderr}")

    def run_sandbox(self, arguments):
        return self.run_command(["/usr/bin/sandbox-exec", "-p", self.profile, *arguments])

    def assert_chmod_denied(self, original_mode):
        requested_mode = "0755" if original_mode == 0o700 else "0700"
        control = self.run_sandbox(["/bin/chmod", requested_mode, str(self.cache)])
        self.assertNotEqual(control.returncode, 0, "sandbox allowed cache chmod")
        self.assertIn("Operation not permitted", control.stderr)
        self.assertEqual(stat.S_IMODE(self.cache.stat().st_mode), original_mode)

    def invoke(self):
        return self.run_sandbox(
            [binary, "cli", "--json", "list_projects", '{"format":"json"}']
        )

    def test_private_cache_lists_projects_through_running_daemon_when_writes_denied(self):
        # Cold daemon startup needs writable logs; an existing daemon owns those
        # writes while its sandboxed CLI client only validates the cache.
        started = self.run_command([binary, "daemon", "start"])
        self.assertEqual(started.returncode, 0, started.stdout + started.stderr)
        self.assert_chmod_denied(0o700)
        result = self.invoke()
        self.assertEqual(result.returncode, 0, result.stderr)
        response = json.loads(result.stdout)
        self.assertIs(response["isError"], False)
        self.assertEqual(response["structuredContent"]["projects"], [])
        self.assertEqual(stat.S_IMODE(self.cache.stat().st_mode), 0o700)

    def test_denied_cache_mode_repair_names_path_and_failed_check(self):
        self.cache.chmod(0o755)
        self.assert_chmod_denied(0o755)
        result = self.invoke()
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn("cache-private", result.stderr)
        self.assertIn(str(self.cache), result.stderr)
        self.assertIn("chmod 0700 failed", result.stderr)
        self.assertEqual(stat.S_IMODE(self.cache.stat().st_mode), 0o755)


unittest.main(verbosity=2)
PY
