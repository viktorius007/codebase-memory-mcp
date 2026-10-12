#!/usr/bin/env bash
# Index in a build-bound worker process without starting a daemon.
# Uses the normal worker admission, coordination and pipeline code. Use private
# CBM_CACHE_DIR and CBM_RUNTIME_DIR directories for isolated measurements.
set -euo pipefail
BIN="${1:?usage: $0 <binary> <repo-path> <name> [full|moderate|fast]}"
REPO="${2:?repo-path required}"
NAME="${3:?name required}"
MODE="${4:-full}"
[[ -x "$BIN" ]] || { echo "binary not executable: $BIN" >&2; exit 2; }
case "$MODE" in full|moderate|fast) ;; *) echo "invalid mode: $MODE" >&2; exit 2 ;; esac
FINGERPRINT="$(python3 - "$BIN" <<'PY'
import hashlib, sys
with open(sys.argv[1], 'rb') as binary:
    digest = hashlib.sha256()
    for chunk in iter(lambda: binary.read(1024 * 1024), b''):
        digest.update(chunk)
    print(digest.hexdigest())
PY
)"
# A worker never reads the resource policy from config or environment: its
# supervisor resolves the policy and sends it in the request, and a request
# without one is refused. This script plays the supervisor, so it sends what
# the supervisor sends by default: both limits off.
ARGS="$(python3 - "$REPO" "$NAME" "$MODE" <<'PY'
import json, sys
policy = dict(index_max_files="off", index_max_source_mb="off")
print(json.dumps(dict(repo_path=sys.argv[1], name=sys.argv[2], mode=sys.argv[3],
                      _cbm_index_policy=policy)))
PY
)"
RESPONSE="$(mktemp "${TMPDIR:-/tmp}/cbm-local-response.XXXXXX")"
trap 'rm -f "$RESPONSE"' EXIT
"$BIN" cli --index-worker --index-worker-build "$FINGERPRINT" \
    index_repository "$ARGS" --response-out "$RESPONSE"
# The worker exits 0 on a refused request; the response says so.
python3 - "$RESPONSE" <<'PY'
import json, sys
with open(sys.argv[1], encoding="utf-8") as handle:
    text = handle.read()
try:
    response = json.loads(text)
except ValueError:
    response = None
if not isinstance(response, dict) or response.get("isError"):
    sys.exit("index worker refused or failed: " + (text.strip() or "no response"))
PY
