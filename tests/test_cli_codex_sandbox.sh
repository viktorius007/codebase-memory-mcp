#!/usr/bin/env bash
# Opt-in integration with the installed Codex sandbox, without an LLM session.
set -euo pipefail

if [[ "${CBM_TEST_CODEX_SANDBOX:-0}" != 1 ]]; then
    echo "SKIP: set CBM_TEST_CODEX_SANDBOX=1 to test the installed Codex sandbox"
    exit 0
fi
[[ "$(uname -s)" == Darwin ]] || {
    echo "Codex sandbox integration requires macOS" >&2
    exit 2
}
command -v codex >/dev/null 2>&1 || {
    echo "Codex sandbox integration requires codex on PATH" >&2
    exit 2
}

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINARY="${CBM_TEST_BINARY:-${ROOT}/build/c/codebase-memory-mcp}"
[[ -x "$BINARY" ]] || { echo "missing binary: $BINARY" >&2; exit 2; }

# shellcheck source=../scripts/test-runtime.sh
source "$ROOT/scripts/test-runtime.sh"
cbm_test_runtime_init
trap 'cbm_test_runtime_cleanup "$BINARY"' EXIT
codex --version

python3 - "$BINARY" <<'PY'
import errno
import json
import os
from pathlib import Path
import socket
import stat
import subprocess
import sys
import tempfile
import time
import unittest

binary = str(Path(sys.argv.pop(1)).resolve())
fixture = Path(os.environ["CBM_CACHE_DIR"]).parent
runtime = Path(os.environ["CBM_RUNTIME_DIR"])


def run(arguments):
    # A daemon retaining an output pipe must not delay its caller's completion.
    with tempfile.TemporaryFile(dir=fixture) as stdout, \
            tempfile.TemporaryFile(dir=fixture) as stderr:
        result = subprocess.run(
            arguments, cwd=fixture, stdin=subprocess.DEVNULL,
            stdout=stdout, stderr=stderr, timeout=20,
        )
        stdout.seek(0)
        stderr.seek(0)
        result.stdout = stdout.read().decode("utf-8", "replace")
        result.stderr = stderr.read().decode("utf-8", "replace")
        return result


class CodexSandboxTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        started = run([binary, "daemon", "start"])
        if started.returncode != 0:
            raise AssertionError(started.stdout + started.stderr)
        cls.addClassCleanup(cls.stop_daemon)
        sockets = [path for path in runtime.rglob("*.sock")
                   if stat.S_ISSOCK(path.stat().st_mode)]
        if len(sockets) != 1:
            raise AssertionError(f"expected one fixture daemon socket, found {sockets}")
        cls.socket_path = str(sockets[0])
        # sandbox has no --ignore-user-config; a unique profile avoids merging
        # any saved permissions profile. Behavioral controls pin actual policy.
        cls.permission_key = "permissions.cbm-test-" + fixture.name.replace(".", "-")
        cls.workspace = [
            "codex", "sandbox", "-C", str(fixture), "-P", ":workspace",
        ]
        cls.allowlisted = [
            "codex", "sandbox", "-C", str(fixture),
            "-P", cls.permission_key.removeprefix("permissions."),
            "-c", cls.permission_key + '.extends=":workspace"',
            "-c", cls.permission_key + ".network.enabled=true",
            "-c", cls.permission_key + ".network.unix_sockets={"
                  + json.dumps(cls.socket_path) + '="allow"}',
            "-c", "features.network_proxy.enabled=true",
        ]

    @classmethod
    def stop_daemon(cls):
        stopped = run([binary, "daemon", "stop"])
        if stopped.returncode != 0:
            raise AssertionError(stopped.stdout + stopped.stderr)
        for _ in range(50):
            status = run([binary, "daemon", "status"])
            if status.returncode == 1 and "daemon: not running" in status.stdout:
                return
            time.sleep(0.1)
        raise AssertionError(f"fixture daemon did not stop: {status.stdout}; {status.stderr}")

    def connect(self, policy, family, address):
        probe = (
            "import socket,sys; "
            f"s=socket.socket(socket.{family},socket.SOCK_STREAM); "
            "s.settimeout(2); "
            f"print(s.connect_ex({address!r})); s.close()"
        )
        prefix = [*policy, "--"] if policy else []
        result = run([*prefix, sys.executable, "-c", probe])
        self.assertEqual(result.returncode, 0, result.stderr)
        return int(result.stdout.strip())

    def test_workspace_policy_denies_existing_daemon_unix_socket(self):
        self.assertEqual(self.connect([], "AF_UNIX", self.socket_path), 0)
        self.assertEqual(self.connect(self.workspace, "AF_UNIX", self.socket_path), errno.EPERM)

    def test_exact_socket_allowlist_allows_cli_projects(self):
        self.assertEqual(self.connect(self.allowlisted, "AF_UNIX", self.socket_path), 0)
        self.assert_projects_listed(self.allowlisted)

    def assert_projects_listed(self, policy):
        result = run([
            *policy, "--", binary, "cli", "--json", "list_projects",
            '{"format":"json"}',
        ])
        self.assertEqual(result.returncode, 0, result.stderr)
        response = json.loads(result.stdout)
        self.assertIs(response["isError"], False)
        self.assertEqual(response["structuredContent"]["projects"], [])

    def test_read_only_policy_allows_cli_with_only_runtime_writable(self):
        policy = [item.replace('extends=":workspace"', 'extends=":read-only"')
                  for item in self.allowlisted]
        policy += ["-c", self.permission_key + ".filesystem={"
                   + json.dumps(str(Path(self.socket_path).parent)) + '="write"}']
        blocked_file = Path(os.environ["CBM_CACHE_DIR"]) / "read-only-control"
        probe = (
            "import pathlib,sys\n"
            "try:\n"
            "    pathlib.Path(sys.argv[1]).write_text('control')\n"
            "except OSError as error:\n"
            "    print(error.errno)\n"
            "else:\n"
            "    print(0)\n"
        )
        control = run([*policy, "--", sys.executable, "-c", probe, str(blocked_file)])
        self.assertEqual(control.returncode, 0, control.stderr)
        self.assertEqual(int(control.stdout.strip()), errno.EPERM)
        self.assertFalse(blocked_file.exists())
        self.assert_projects_listed(policy)

    def test_socket_allowlist_still_denies_unlisted_tcp(self):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(2)
            address = listener.getsockname()
            self.assertEqual(self.connect([], "AF_INET", address), 0)
            self.assertEqual(self.connect(self.allowlisted, "AF_INET", address), errno.EPERM)


unittest.main(verbosity=2)
PY
