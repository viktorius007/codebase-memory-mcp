# ISSUES — open defects and gaps

This is the canonical backlog of current issues. Remove resolved items; history
lives in git.

## Codex sandbox socket/lock denials produce misleading CLI errors

Reproduced on 2026-09-02 with Codex CLI `0.152.0` using actual `codex exec`
runs and direct `codex sandbox` controls:

- The pre-fix installed CBM build failed in `-s workspace-write` with
  `secure CLI coordination could not be created (cache-private)`. The
  cache-validation fix removes that first blocker and is now installed
  (build `065e0669a640…`).
- With the patched binary and a running daemon, an unallowed Unix socket
  connection fails with `EPERM`. CBM retries for 30 seconds and reports that
  the daemon is active or starting but could not accept the client, losing
  the permission-denied cause.
- Under the Codex `read-only` policy, coordination writes fail earlier with
  `CLI exact-build admission could not be verified; retry after active CBM
  operations exit`. Retrying or closing other clients does not grant writes.

A scoped configuration is verified: a Codex permissions profile, enabled network
proxy, and an allow entry for the exact daemon Unix socket permit a sandboxed
`codex exec` client to list projects with exit 0. Direct unlisted TCP remains denied.
A read-only profile also needs a write exception for the daemon coordination
directory. See `docs/CONFIGURATION.md` → "Codex exec on macOS" and the opt-in
`tests/test_cli_codex_sandbox.sh` integration test. Full-access mode is not
required; the client and daemon must use the same patched build and cache/runtime
settings.

Remaining defect: preserve permission-denied details from the socket and lock
operations, stop retrying a denied socket connection, and report the required
runtime/socket access instead of blaming daemon availability. The POSIX connect
path is `src/daemon/ipc.c` → `cbm_daemon_ipc_connect`; bootstrap classifies a
failed connection with a live lifetime holder as reserved in
`src/daemon/bootstrap.c` → `cbm_daemon_bootstrap_classify_failed_connect`.

## Permanent daemon hangs accepting connections but never completing the identity handshake

Observed twice on the same host, 2026-09-05 and again by 2026-09-07 on build
`065e0669a640…`. Symptom: every CLI invocation refuses with "an active
pre-coordination or unverified CBM daemon is running" and the CLI is unusable
until the daemon is killed by hand.

Diagnosis of the 2026-09-05 occurrence (launchd-started permanent daemon,
up 2d22h): last log line ~2 days stale, UI port dead, a single thread parked
in `poll_until` (`src/daemon/ipc.c:766`), the Unix socket still accepting
connections but never completing the identity handshake — so every client
classified the daemon as "unverified" and refused to start a replacement.
`kill -TERM <pid>` restored the CLI; per-call temporary daemons work fine.
The recurrence on the patched build rules out a one-off.

Two defects: (1) the daemon can reach a state where it accepts connections
but never services the handshake, and nothing internal detects or exits that
state — a handshake-servicing watchdog or a deadline on the accept-to-verify
window would make it self-terminate; (2) a client that finds an unverified
daemon holding the lifetime retries/refuses forever instead of reporting the
daemon's staleness (stale log mtime, dead UI port) and offering a takeover
path, so a hung permanent daemon bricks the CLI until a human intervenes.
Classification site: `src/daemon/bootstrap.c` →
`cbm_daemon_bootstrap_classify_failed_connect` (same path as the sandbox
issue above, different cause: here the connect succeeds and the handshake
stalls).

## Empty cross-file Java registry performs null-pointer arithmetic

`internal/cbm/lsp/java_lsp.c:3898`, in `cbm_java_build_cross_registry`, evaluates
`jvm + type_count` when `def_count == 0`, leaving `jvm == NULL` and
`type_count == 0`. The function explicitly supports an empty corpus, but adding
even zero to a null pointer is undefined behavior.

The current-source sanitizer run on 2026-09-02
(`scripts/test.sh --suites rust_lsp,pipeline,mcp,cli,daemon_ipc`) reported
`runtime error: applying zero offset to null pointer` at that expression.
Handle the empty registry without pointer arithmetic on NULL and cover that
valid input under the sanitizer.

## get_architecture aspect lists truncate silently

Authored pre-sync against `main` at `56749c16`; line numbers below predate the
826-commit sync and may have shifted.

`arch_entry_points` (`src/store/store.c:6424`) and `arch_routes`
(`src/store/store.c:6500`) both append a hard `LIMIT 20` with no `ORDER BY`, and
the result carries no total and no `truncated` flag. The rendered header
(`entry_points: 20`) states the returned count where a reader expects the
population, so a partial list is indistinguishable from a complete one and the
rows chosen are in unspecified scan order. On this repo `entry_points` returns
20 of 54 qualifying nodes and drops `main` in `src/main.c` — the aspect reports
a pure-C engine as a set of React components — while `routes` returns 20 of 44.
`arch_hotspots` (`src/store/store.c:6595`) caps at 10 but does order by
`fan_in DESC`, so it is correct-but-bounded rather than arbitrary. Fix: report
`total` and `truncated` per aspect, as `index_status` already does for its
coverage categories, and give the capped aspects a deterministic `ORDER BY`.

## Boolean node properties are unusable with the natural Cypher predicate

They are stored as integer `1` but surface to the Cypher layer as the string
`'true'`, so `MATCH (n) WHERE n.is_entry_point = 1` returns zero rows and no
warning, while `= 'true'` returns 72. A zero-row result is the reported shape of
a type mismatch, which makes this a silent false negative on exactly the
existence questions the engine is asked. Numeric properties are unaffected —
`n.complexity > 10` returns 817, matching
`CAST(json_extract(properties,'$.complexity') AS INTEGER) > 10` in SQL, and is
not a lexical compare (5,530 nodes hold complexity 2–9, which would sort above
`'10'` as text and do not appear). Fix: coerce booleans to a consistent type at
the Cypher boundary, or reject `= 1` against a boolean property instead of
returning an empty result.

## arch_entry_points and arch_hotspots exclude production paths via unanchored LIKE

`arch_entry_points` (`src/store/store.c:6422`) and `arch_hotspots`
(`src/store/store.c:6591`) exclude test material with `file_path NOT LIKE
'%test%'`, an unanchored substring match that also drops production paths
containing the token anywhere (`latest/`, `contest/`, `testbed/`). The companion
`is_test` property check on the same rows is the precise test; the `LIKE` is
redundant where `is_test` is set and wrong where it is not.
