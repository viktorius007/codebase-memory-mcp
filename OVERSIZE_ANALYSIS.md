# Oversize result reporting — failure path and fix

## The true failure path (verified, not assumed)

The lead in my brief was **partly right and materially incomplete**.

Correct: `src/main.c:1352` did print `error: daemon-backed CLI execution failed`
for every non-result, discarding the reason.

Incomplete: that line is where the reason was *lost*, not where it was
*destroyed*. The reason never reached `main.c` at all. The actual sequence:

1. `handle_query_graph` (`src/mcp/mcp.c`) succeeds and returns a complete
   ~10.58 MB MCP result string. **No error exists at this point.**
2. `application_tool_request` (`src/daemon/application.c`) took `strlen(response)`
   and only rejected `> UINT32_MAX` — 4 GB. A 10.58 MB payload passed that
   check and was handed to the runtime as an `OK` response.
3. `runtime_worker_send_application_response` (`src/daemon/runtime.c:997`)
   enforced the *real* bound, `CBM_DAEMON_RUNTIME_APPLICATION_PAYLOAD_MAX`
   (10485744 = 10 MB frame − 16 B header). The oversize response failed this
   guard and the function returned `false` **carrying no reason** — a bare bool.
4. `runtime_application_worker` then treated the failed send as a dead peer and
   called `cbm_daemon_ipc_connection_interrupt`, i.e. the oversize condition was
   escalated into a *connection fault*.
5. `main.c` observed no result and printed the generic line.

So the payload-size check in `application.c` was dead code for this case
(`UINT32_MAX` can never be reached before the 10 MB frame guard fires), and the
condition WAS detectable at step 2 — a point where a specific reason could
still be reported. That is where I fixed it.

### Measured, not recalled

- Real limit: `CBM_DAEMON_RUNTIME_APPLICATION_PAYLOAD_MAX` = **10485744 bytes**.
  The error message prints this constant, never a hardcoded number.
- The failing query produces **10582789 bytes** (measured by the fixed binary).
- `LIMIT 30500` produces 10355608 bytes → under the cap → succeeds. Consistent
  with the brief's measured 30500/31000 boundary, and confirms the constraint is
  total payload bytes, not row count.

## A second, worse collapse found in the same path

`application_mcp_request` had the identical `> UINT32_MAX` dead check on the
**MCP/stdio transport**. There the consequence is more severe than a bad
message: the dropped frame becomes `HANDLER_ERROR`, and
`src/daemon/frontend.c:466` responds to a failed request by calling
`_Exit(EXIT_FAILURE)` — terminating the whole MCP session. One oversize query
from an agent's MCP client looked exactly like the server crashing. Fixed with
the same substitution, framed as a JSON-RPC response.

## Other failures that were collapsed into the same message

Everything reaching `main_local_cli_daemon_execute` shared one line:

| Real condition | Before | After |
|---|---|---|
| session context could not be set | `daemon-backed CLI execution failed` | `the CLI session context could not be established with the daemon` |
| oversize result | same | `result too large to return: …` (from the daemon) |
| daemon shutting down (`UNAVAILABLE`) | same | `the daemon is shutting down and accepted no new work` |
| request in flight (`BUSY`) | same | `the daemon session already has a request in flight` |
| malformed/not permitted (`REJECTED`) | same | `the daemon rejected the request as malformed or not permitted` |
| genuine transport loss | same | `the daemon connection failed or the daemon exited mid-request` |
| cancelled | same | `the request was cancelled` |
| OK but empty response | same | `the daemon returned an empty result for '<tool>'` |

Genuine transport death remains distinct from every reportable outcome; the fix
does not make any other failure lie.

## Remedy chosen, and why not the alternatives

**Chosen:** substitute a bounded MCP error envelope for the unsendable payload,
at the point the handler result is measured.

Rationale: the handler *succeeded*. The only thing missing is a way to say so.
An error envelope is a normal, sendable result on the existing success path, so
it needs no new wire status, no protocol change, and no version straddling —
and it reaches the caller through the machinery that already works.

Rejected alternatives:
- **Raise the frame cap** — explicitly out of scope, and correctly so: the cap
  is a legitimate resource bound. Reporting was the defect.
- **A new wire status (`PAYLOAD_TOO_LARGE`)** — needs a protocol change across
  both transports and a client that understands it; the envelope carries strictly
  more information (actual bytes, limit, remedy) with no wire change.
- **Truncate and return partial rows** — silently mutates the caller's result.
  A partial answer an agent believes is complete is worse than a clear refusal.

## What changed, by file

- `src/daemon/application.c` — new `application_oversize_result()` builds the
  envelope (cause + real limit + remedy). Both `application_tool_request` and
  `application_mcp_request` now check against the real frame bound instead of
  the unreachable `UINT32_MAX` and substitute the envelope. Fixed an incidental
  ordering issue in `application_tool_request`: `tool` was freed before its last
  use was needed for the message, so the free moved after.
- `src/daemon/runtime.c` / `runtime.h` — new
  `cbm_daemon_runtime_application_status_str()`, one sentence per status.
- `src/main.c` — the collapsing branch replaced by per-status reporting; the
  status variable is `tool_status` to avoid shadowing the enclosing
  `cbm_daemon_bootstrap_status_t status`.
- `tests/test_daemon_application.c` — the red-first test.

## Note on the build

`src/main.c` is **not** among the test-runner's sources, so no suite compiles
it. My first `main.c` edit shadowed the bootstrap `status` and produced four
`-Werror` errors that every suite run passed straight through; only the
production `make -f Makefile.cbm cbm` link surfaced them. Anything touching
`main.c` needs that production build to be trusted — the suites alone cannot
prove it compiles.
