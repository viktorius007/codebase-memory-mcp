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

## Findings from a CLI-only evaluation against a Rust workspace (2026-09-16)

Every item below was reproduced on 2026-09-16 through `codebase-memory-mcp cli`
only (installed binary reports version `dev`). No CBM source was read. The
corpus was `/Users/viktor/Projects/project-management` at `8262ac8d8`: a
516-file Rust workspace, project `Users-viktor-Projects-project-management`,
abbreviated `$P` below. Ground truth came from `search_code`, `get_code_snippet`,
and `cargo tree`.

## Heuristic call resolution binds method calls to unrelated same-name symbols

`trace_path --function-name $P.crates.pm-port.src.writer_identity.encode_hex
--direction outbound --depth 1 --include-evidence true` returns three callees.
All three are wrong:

- `UnorderedReqStore.push` in `pm-usecase` at confidence 0.14. The source
  calls `String::push` on a local `String`.
- `LaneClaimSet.len` in `pm-domain` at 0.28. The source calls `str::len`.
- `writer_identity.String.from` at 0.06. The source calls `char::from`.

The worst case is `$P.crates.git-safety.src.db_path.clone`, a private test
helper `fn clone(tmp, origin, name, extra) -> PathBuf`. It has
`callers_total: 747`, drawn from `pm-usecase`, `pm-transport`, `pm-git-sync`,
and `xtask`. `cargo tree -p pm-usecase -e normal,dev` shows no dependency on
`pm-git-safety`, so none of those crates can call the helper. The calls are
`.clone()` method calls. A private free function with a different arity should
never be a candidate for a method call from a crate that cannot see it.

Without `--include-evidence`, these edges look the same as `lsp` edges at 0.95.
Nothing in the default output tells the reader that an edge is a guess.

## Rust trait impls are named after the implementing-for type

The impl `From<WriterIdentity> for String` produces the method node
`$P.crates.pm-port.src.writer_identity.String.from`. Three files contain such
impls, so three `String.from` nodes exist (`query_graph`: `MATCH (n) WHERE
n.name = 'from' AND n.qualified_name CONTAINS 'String'`). Every
`String::from(...)` call in the same crate then resolves to that impl, which
gives 30 callers at confidence 0.06, including `canonical_store.adr_id` and
`req_id`. A trait impl on a foreign type should be keyed by the trait and the
source type, not presented as a method of `String`.

## Cycle detection reports cycles built from heuristic edges

`get_architecture --aspects cycles` reports 8 cycles. Seven of them rely on
false edges. Only `cli_grammar_coverage_lint.find_match_in_stmt` ↔
`find_match_in_expr` is real mutual recursion. Two cycles rely on the bad edges
above:

- A 3-member cycle: `WriterIdentity.durable_key` → `encode_hex` →
  `String.from` → `durable_key`. The `encode_hex` → `String.from` edge is
  heuristic 0.06, and the real call is `char::from`.
- A 9-member cycle that runs through `clone_gate_lint.collect`,
  `git-safety db_path.clone`, and `UnorderedReqStore.push`. These symbols sit
  in crates that do not depend on each other.

Rechecked on 2026-09-17 at `dd7ec1b2e` with `trace_path --direction outbound
--depth 1 --include-evidence true` and `query_graph` over `CALLS` edge
properties. The remaining false cycles:

- `wildcard_default_lint.SiteVisitor.record` ↔ `SiteVisitor.visit_item`. The
  `record` → `visit_item` edge is heuristic 0.17. The source calls
  `matches.visit_item(scope)` on a local `WildcardMatches` visitor
  (`xtask/src/wildcard_default_lint.rs:170`), not on `SiteVisitor`.
- Three 2-member cycles in `pm-usecase`: `req.create_req` ↔
  `create_req_from_input`, `reason.update_reason` ↔ `update_reason_scoped`, and
  `external_ref.create_external_ref` ↔ `create_external_ref_scoped`. Each back
  edge is a method call on a generic store parameter
  (`store: &mut (impl ReasonDomainCapabilities + ?Sized)`), for example
  `store.create_external_ref(input)` at
  `crates/pm-usecase/src/usecase/external_ref.rs:47` and `.update_reason(...)`
  at `reason.rs:136`. CBM binds each call to the same-name free function in the
  same module, with strategy `heuristic` at confidence **0.9**. The correct
  edges from the same functions score `lsp` 0.95 or 0.9, so no threshold
  separates the false edge from the true ones.
- A 35-member cycle spanning `pm-git-sync` backup, `pm-cli` `lib`, and
  `pm-record` `EntitlementStore` methods. It needs `pm-record` and
  `pm-git-sync` to call into `pm-cli`, and neither crate depends on `pm-cli`.
  The edges are `suffix_match` at 0.21, all targeting
  `pm-cli.src.lib.RuntimeContextFactory.open`, for example from
  `EntitlementStore.publish`, `RepositoryOperationLock.acquire`, and
  `backup.clone_id`. The source calls are `fs::File::open`
  (`crates/pm-record/src/repository_entitlement.rs:465`,
  `crates/pm-git-sync/src/backup.rs:315`). The `suffix_match` strategy
  matches the bare name `open` to a method in a crate the caller cannot see.

Cycles, impact output (`detect_changes`), and `trace_path` counts all include
low-confidence edges with no threshold and no marker. A cycle report should
exclude edges below a confidence floor, or at least label cycles that contain
heuristic edges. A floor alone does not fix the `pm-usecase` cycles at 0.9.
Those need resolution to prefer a method on the receiver's trait bound over a
free function, and to never bind a method-call expression (`x.f()`) to a free
function.

## Calls through a trait object are not attributed to the trait method

`trace_path --function-name
$P.crates.pm-cli-grammar.src.context.RepositoryContinuity.reserve_for_mutation
--direction inbound` returns `callers_total: 0`. The trait is called through
`Rc<RefCell<dyn context::RepositoryContinuity>>` (`crates/pm-cli/src/lib.rs:1830`),
and codegraph reports a call site at `crates/pm-cli-grammar/src/context.rs:691`.
CBM credits that call to the concrete impl in `pm-cli` instead. So "who calls
this trait method" gets a false zero, and inbound impact analysis on a port
trait misses its callers.

## Trait impls through an aliased import get no IMPLEMENTS edge

`crates/pm-cli/src/repository_continuity.rs` imports
`RepositoryContinuity as RepositoryContinuityPort` and contains
`impl RepositoryContinuityPort for RepositoryContinuity`. This query returns 0
rows:

`MATCH (s)-[:IMPLEMENTS]->(t) WHERE
s.qualified_name='$P.crates.pm-cli.src.repository_continuity.RepositoryContinuity'
RETURN t.qualified_name`

No node named `RepositoryContinuityPort` exists either. A path-qualified impl
works: `impl reconcile_runtime::TreeHydrator for SqliteTreeHydrator` does get
its edge. Use-aliases are not resolved during impl linking.

## Semantic search ranks unrelated symbols first

`search_graph --semantic-query '["resolve database path","linked worktree","git
common dir"]' --limit 10` ranks `advice_rules.contains_uuid_like_token` first
(score 0.072), then two more uuid-token helpers, then shell and Python
experiment functions. None of the top 19 results relate to the query. The
relevant functions (`db_path::resolve_db_path`, `git_common_dir`) do not
appear, although `search_code --pattern git-common-dir` and `--query` BM25 both
find the right area. Top scores are below 0.08 and most are negative, so the
result set is noise. It is still returned as ranked results.

## Regex `search_code` misses matches that literal mode finds

With `--file-pattern repository_continuity.rs --mode files`, these patterns
find the file in literal mode but return 0 files with `--regex true`:
`Continuity for`, `y for`, `Continuity f`, `Continuity.for`,
`Continuity\sfor`, `impl.*Continuity.for`. In regex mode, `impl.*for`,
`impl.*Continuity`, and `impl.*Continuity ` (trailing space) do match. Across
the whole repo, regex `RepositoryContinuity for` returns 0 files, while literal
mode returns 2. Regex matching appears to drop matches when a space is
followed by more pattern text. A false-empty grep is dangerous because callers
treat an empty text search as proof of absence.

## Cypher `WITH` aggregation corrupts carried node properties

`query_graph --query "MATCH (f)<-[:CALLS]-(c) WITH f, count(c) AS n ORDER BY
n DESC LIMIT 3 RETURN f.name, f.qualified_name, f.file_path, n"` returns rows
where `f.qualified_name` is the literal string `f`, while `f.name` and
`f.file_path` are populated (e.g. `new f crates/pm-cli-grammar/src/commands/mutation.rs
"3030"`). With only `f.qualified_name, f.label, n` projected, every row's qn is
`f`. Grouped results cannot be identified.

Related: `RETURN f` for a whole node returns only its name (`encode_hex`), not
the node.

## Cypher parse errors are internal token numbers

- `MATCH (n RETURN n` → `expected token type 67, got 2 at pos 9`
- `MATCH p=(a)-[:CALLS*1..2]->(b) ...` → `expected token type 66, got 85 at pos 6`

The second one is an unsupported feature (named paths or variable-length
relationships), but the message looks like a syntax error. Errors should name
the expected token and say when a construct is unsupported. By contrast,
`type(r)` in `WHERE` gets a clear unsupported-function message.

## String literals and TOML tables become Routes, HTTP calls, and Classes

- All 3 `Route` nodes (`/tests/`, `/fakes/`, `/nonexistent/junit.rev`) and
  all 5 `HTTP_CALLS` edges come from path-substring checks in lint code and a
  test fixture path. This workspace has no HTTP surface.
- 134 of 142 `Class` nodes come from `.toml` files: `package`,
  `dependencies`, `features`, `dev-dependencies`, `lints` in each
  `Cargo.toml`. Label counts, architecture summaries, and `--label Class`
  searches are polluted by this.

## TESTS edges link unrelated symbols

There are only 23 `TESTS` edges, and a sample of 6 is mostly nonsense:
`spec_traceability.test_files` TESTS `rust-toolchain.components`, and
`test_db_isolation_lint.rs.__file__` TESTS `git-safety db_path.clone` and
`TreeDirectory.insert`. Meanwhile, the workspace has thousands of `#[test]`
functions, which `is_test` flags correctly. The TESTS edge set is too sparse
to use and too wrong to trust.

## Architecture packages and dependencies carry no dependency data

`get_architecture --aspects packages` reports `fan_in 0 fan_out 0` for every
package. It also lists `src` (2695 nodes) and `tests` (821) as packages
alongside real crates, which looks like directory-name grouping rather than
Cargo package grouping. `--aspects dependencies` returns only the edge-type
histogram, with no package or module dependency rows. For a Cargo workspace,
crate dependencies are readable directly from `Cargo.toml`, which CBM already
indexes.

## Array flags accept JSON inconsistently

- `--semantic-query '["a","b"]'` is parsed as an array.
- `check_index_coverage --paths '["a","b"]'` is treated as one literal path
  string, and returns `no_recorded_issue` / `missing` for that nonexistent
  path instead of an argument error.
- `get_architecture --aspects '["dependencies","cycles"]'` is rejected as an
  unknown aspect.

Repeating the flag (`--paths a --paths b`) works for both. Raw JSON arguments
also print a deprecation warning, which leaves no single form that works
everywhere. At minimum, a JSON-looking value for an array flag should be
parsed or refused, never silently used as a path.

## Ambiguous-name results ignore output budgets and exit 0

- `trace_path --function-name run` returns `{"status":"ambiguous",...}` as
  JSON even under the default tree format, and exits 0. The not-found case
  exits 1.
- `get_code_snippet --qualified-name run --max-output-tokens 200` prints
  11,870 bytes: all 64 suggestions, ignoring the budget.

Scripts cannot tell an ambiguous lookup from a successful one by exit code.

## Path formats differ between tools

`get_code_snippet` returns an absolute `file_path`
(`/Users/viktor/Projects/project-management/crates/pm-port/src/writer_identity.rs`).
`get_file_outline` rejects that value: `file_path must be a
repository-relative path without '..'`. `search_graph` and `trace_path` return
relative paths. Output from one tool should be valid input for the next.

## Project registry keeps deleted roots and duplicates

`list_projects` lists 43 projects. For 29 of them the `root_path` no longer
exists (removed worktrees, `/private/tmp/*`, `/var/folders/*` test repos).
Nothing in the output marks them as stale. Three projects
(`...writer-20283`, `issue-20283-verifier`, `issue-20283-verifier-full`) share
one root. The `project not found` error then prints all 43 names in its
`available_projects` field. The registry should mark or prune projects whose
root is gone, and the not-found hint should rank near matches instead of
dumping everything.

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
