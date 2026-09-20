# ISSUES — open defects and gaps

This is the canonical backlog of current issues. Remove resolved items; history
lives in git.

## Upstream-research convention (checked 2026-09-19)

The resource notes below distinguish **exact** reports from **related** ones.
“Open PR” means precisely that: it is neither merged nor released. “No exact
upstream report found” means a search of all 1,036 issues then visible in
`DeusData/codebase-memory-mcp`, including titles and bodies; it is not evidence
that the defect is unique. Local-history notes are separate from upstream
status, because this fork contains commits that upstream may not yet have.

## Locally fixed in this fork (do not reinvestigate before checking the commit)

These fixes are already ancestors of the checked-out `main` at the research
date. They are local repository history, not a claim that upstream has merged
or released them. A future agent should first run `git merge-base --is-ancestor
<commit> HEAD` and inspect the diff if it needs to confirm the current branch
still contains the fix.

- **Codex read-only sandbox admission:** local commit [`2e2dfc8c`](https://github.com/viktorius007/codebase-memory-mcp/commit/2e2dfc8c4f109dd98d7942ffca6db0e6b27752bc)
  (`fix(cli): support read-only sandboxed clients`) fixes the initial
  cache-validation blocker and adds the Codex sandbox configuration/integration
  coverage. It does **not** fix the remaining socket `EPERM` diagnostic/retry
  defect described below.
- **Empty Java/Kotlin cross-registry:** local commit [`459be8bf`](https://github.com/viktorius007/codebase-memory-mcp/commit/459be8bf570b94bd5fd373f0a71787134593f822)
  (`fix(lsp): build empty cross-registries for zero-def corpora`) avoids
  indexing a NULL `jvm` array for an empty definition set. It addresses the
  null-pointer-arithmetic finding below.
- **Architecture entry-point truncation disclosure:** local commit [`8eebc00e`](https://github.com/viktorius007/codebase-memory-mcp/commit/8eebc00ee46231b52f7fe2929afc4d57ea23904c)
  (`fix(architecture): report entry point truncation`) records the total and a
  truncation flag for the capped entry-point list. It does **not** by itself
  settle route ordering/capping or the unanchored `%test%` predicate.

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

### Upstream resources

- **Related, open:** [#2107](https://github.com/DeusData/codebase-memory-mcp/issues/2107)
  documents an orphaned Unix socket yielding the same misleading 30-second
  daemon timeout. Its one user comment says the markerless-socket recovery
  commit [`8e70590d`](https://github.com/DeusData/codebase-memory-mcp/commit/8e70590d)
  is already on upstream `main`; that fix addresses stale sockets, not an
  `EPERM` connect.
- **Related, closed:** [#2046](https://github.com/DeusData/codebase-memory-mcp/issues/2046)
  was a cache-cohort teardown race, fixed by merged [#2047](https://github.com/DeusData/codebase-memory-mcp/pull/2047).
  It establishes the bootstrap/lifetime-lock context but does not cover sandbox
  access denial.
- **Local-history mitigation:** current `main` contains `2e2dfc8c`
  (`fix(cli): support read-only sandboxed clients`), which added the documented
  Codex profile and integration coverage. The remaining `EPERM` diagnostic and
  retry defect has no exact upstream issue found in the search above.

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

### Upstream resources

- **Closest, open:** [#2178](https://github.com/DeusData/codebase-memory-mcp/issues/2178)
  reports a live daemon that permanently poisons workers after its cohort marker
  is lost. The reporter supplied a macOS-specific cause: `tmp_cleaner` deletes
  old held lock files, allowing a new unlocked path to be created. This is not
  the same as a stalled identity handshake, but it supports treating a live
  holder as potentially unhealthy rather than retrying indefinitely.
- **Related, closed:** [#2162](https://github.com/DeusData/codebase-memory-mcp/issues/2162)
  and merged [#2179](https://github.com/DeusData/codebase-memory-mcp/pull/2179)
  fix a different mid-HELLO retirement race. [Open PR #2207](https://github.com/DeusData/codebase-memory-mcp/pull/2207)
  fixes a separate POSIX lock-probe bug that can drop a process-held lifetime
  lock. Neither PR claims to add the requested watchdog/takeover path.
- **Exact-report status:** no exact public report of “accept succeeds but HELLO
  never completes” was found; preserve the two dated reproductions above when
  filing one, and link it to #2178, #2162, and #2207.

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

### Upstream resources

- **Already present locally:** current `main` contains `459be8bf`
  (`fix(lsp): build empty cross-registries for zero-def corpora`). Its Java
  change keeps `jvm` NULL when `def_count == 0` and only indexes it inside the
  positive-count loop, directly addressing this expression.
- **Related upstream precedent:** merged [#2083](https://github.com/DeusData/codebase-memory-mcp/pull/2083)
  guards a different zero-count NULL UB (`qsort(NULL, 0)`) in semantic-edge
  processing. It demonstrates the intended sanitizer-backed regression style,
  but is not this Java fix. No exact upstream issue was found.

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

### Upstream resources

- **Exact and open:** [#2053](https://github.com/DeusData/codebase-memory-mcp/issues/2053)
  is the same Rust failure class: standard-library receivers bind to same-named
  project methods and fabricate `CALLS` edges. It includes a minimal repro and
  reports 37.6% stdlib-shaped edges on its corpus.
- **Complementary open design defects:** [#2127](https://github.com/DeusData/codebase-memory-mcp/issues/2127)
  (common-name import-map collisions), [#2126](https://github.com/DeusData/codebase-memory-mcp/issues/2126)
  (name-only guesses emitted as `CALLS` rather than `CALL_REFERENCE`), and
  [#2035](https://github.com/DeusData/codebase-memory-mcp/issues/2035)
  (missing arity) each cover a necessary guard. None has an attached PR or
  maintainer comment as of the check date.

## Rust trait impls are named after the implementing-for type

The impl `From<WriterIdentity> for String` produces the method node
`$P.crates.pm-port.src.writer_identity.String.from`. Three files contain such
impls, so three `String.from` nodes exist (`query_graph`: `MATCH (n) WHERE
n.name = 'from' AND n.qualified_name CONTAINS 'String'`). Every
`String::from(...)` call in the same crate then resolves to that impl, which
gives 30 callers at confidence 0.06, including `canonical_store.adr_id` and
`req_id`. A trait impl on a foreign type should be keyed by the trait and the
source type, not presented as a method of `String`.

### Upstream resources

- **Related, open:** use [#2053](https://github.com/DeusData/codebase-memory-mcp/issues/2053)
  and [#2127](https://github.com/DeusData/codebase-memory-mcp/issues/2127) for
  resolution consequences. They do not describe the wrong `String.from`
  ownership/QN construction, so this remains a separately reproducible Rust
  extractor defect. No exact upstream issue or PR was found.

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

### Upstream resources

- **Root-cause resources:** [#2053](https://github.com/DeusData/codebase-memory-mcp/issues/2053),
  [#2127](https://github.com/DeusData/codebase-memory-mcp/issues/2127), and
  [#2126](https://github.com/DeusData/codebase-memory-mcp/issues/2126) cover
  the false-edge and confidence/provenance portions. Closed [#1276](https://github.com/DeusData/codebase-memory-mcp/issues/1276)
  is an earlier Python example of unresolved member calls fabricating edges;
  useful precedent, not a Rust fix.
- No listed PR addresses the important 0.9 heuristic back-edge, so a confidence
  floor must not be represented as a complete solution.

## Calls through a trait object are not attributed to the trait method

`trace_path --function-name
$P.crates.pm-cli-grammar.src.context.RepositoryContinuity.reserve_for_mutation
--direction inbound` returns `callers_total: 0`. The trait is called through
`Rc<RefCell<dyn context::RepositoryContinuity>>` (`crates/pm-cli/src/lib.rs:1830`),
and codegraph reports a call site at `crates/pm-cli-grammar/src/context.rs:691`.
CBM credits that call to the concrete impl in `pm-cli` instead. So "who calls
this trait method" gets a false zero, and inbound impact analysis on a port
trait misses its callers.

### Upstream resources

- **Related, open:** [#2152](https://github.com/DeusData/codebase-memory-mcp/issues/2152)
  proposes owner `USAGE` hints where a method has no static callers, but targets
  framework dispatch. [#1114](https://github.com/DeusData/codebase-memory-mcp/issues/1114)
  is the analogous Go interface-dispatch gap. Neither proves a Rust trait-object
  attribution fix; no exact upstream Rust report was found.

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

### Upstream resources

- No exact upstream report was found. [#875](https://github.com/DeusData/codebase-memory-mcp/issues/875)
  (closed) is a Python import-alias call-resolution precedent only; it does not
  cover Rust `IMPLEMENTS` linking. Keep the exact alias, source file, query,
  and zero-row result when filing.

## Semantic search ranks unrelated symbols first

`search_graph --semantic-query '["resolve database path","linked worktree","git
common dir"]' --limit 10` ranks `advice_rules.contains_uuid_like_token` first
(score 0.072), then two more uuid-token helpers, then shell and Python
experiment functions. None of the top 19 results relate to the query. The
relevant functions (`db_path::resolve_db_path`, `git_common_dir`) do not
appear, although `search_code --pattern git-common-dir` and `--query` BM25 both
find the right area. Top scores are below 0.08 and most are negative, so the
result set is noise. It is still returned as ranked results.

### Upstream resources

- **Exact class, open:** [#1417](https://github.com/DeusData/codebase-memory-mcp/issues/1417)
  documents semantic ranking noise and includes maintainer acknowledgement that
  a nonsense token outranking a relevant term is a ranking-correctness failure.
  [#2149](https://github.com/DeusData/codebase-memory-mcp/issues/2149) identifies
  declared-but-unused per-field semantic weights, a plausible implementation
  lead rather than proof of this ranking failure.
- [Open PR #2227](https://github.com/DeusData/codebase-memory-mcp/pull/2227)
  improves BM25 label filtering and several graph-quality probes, but its PR
  description does **not** claim a semantic-vector ranking fix.

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

### Upstream resources

- No exact upstream report was found. Closed [#283](https://github.com/DeusData/codebase-memory-mcp/issues/283)
  fixed invalid-regex false empties and closed [#282](https://github.com/DeusData/codebase-memory-mcp/issues/282)
  documented literal-versus-regex confusion; both are relevant false-empty
  precedents, not fixes for a valid multiword regex.

## Cypher `WITH` aggregation corrupts carried node properties

`query_graph --query "MATCH (f)<-[:CALLS]-(c) WITH f, count(c) AS n ORDER BY
n DESC LIMIT 3 RETURN f.name, f.qualified_name, f.file_path, n"` returns rows
where `f.qualified_name` is the literal string `f`, while `f.name` and
`f.file_path` are populated (e.g. `new f crates/pm-cli-grammar/src/commands/mutation.rs
"3030"`). With only `f.qualified_name, f.label, n` projected, every row's qn is
`f`. Grouped results cannot be identified.

Related: `RETURN f` for a whole node returns only its name (`encode_hex`), not
the node.

### Upstream resources

- **Exact and open:** [#2208](https://github.com/DeusData/codebase-memory-mcp/issues/2208)
  reports wrong properties on a `WITH`-carried in-scope node. It has no comments
  or PR as of the check date. Closed [#1983](https://github.com/DeusData/codebase-memory-mcp/issues/1983)
  and [#1919](https://github.com/DeusData/codebase-memory-mcp/issues/1919) fixed
  related `WITH` scope/projection bugs, but did not close #2208.

## Cypher parse errors are internal token numbers

- `MATCH (n RETURN n` → `expected token type 67, got 2 at pos 9`
- `MATCH p=(a)-[:CALLS*1..2]->(b) ...` → `expected token type 66, got 85 at pos 6`

The second one is an unsupported feature (named paths or variable-length
relationships), but the message looks like a syntax error. Errors should name
the expected token and say when a construct is unsupported. By contrast,
`type(r)` in `WHERE` gets a clear unsupported-function message.

### Upstream resources

- No exact upstream report was found. Closed [#239](https://github.com/DeusData/codebase-memory-mcp/issues/239)
  is a direct precedent for opaque internal-token parse errors on an unsupported
  Cypher feature. Preserve both example queries because the requested remedy is
  diagnostic quality, not merely parser support.

## String literals and TOML tables become Routes, HTTP calls, and Classes

- All 3 `Route` nodes (`/tests/`, `/fakes/`, `/nonexistent/junit.rev`) and
  all 5 `HTTP_CALLS` edges come from path-substring checks in lint code and a
  test fixture path. This workspace has no HTTP surface.
- 134 of 142 `Class` nodes come from `.toml` files: `package`,
  `dependencies`, `features`, `dev-dependencies`, `lints` in each
  `Cargo.toml`. Label counts, architecture summaries, and `--label Class`
  searches are polluted by this.

### Upstream resources

- **Exact route-pollution class, open:** [#598](https://github.com/DeusData/codebase-memory-mcp/issues/598)
  covers route extraction from non-route text; the maintainer explicitly calls
  for source/kind filters and focused fixtures. Closed [#455](https://github.com/DeusData/codebase-memory-mcp/issues/455)
  and [#999](https://github.com/DeusData/codebase-memory-mcp/issues/999) are
  narrower filesystem/config-path precedents. None covers TOML tables becoming
  `Class` nodes, so retain that separately.

## TESTS edges link unrelated symbols

There are only 23 `TESTS` edges, and a sample of 6 is mostly nonsense:
`spec_traceability.test_files` TESTS `rust-toolchain.components`, and
`test_db_isolation_lint.rs.__file__` TESTS `git-safety db_path.clone` and
`TreeDirectory.insert`. Meanwhile, the workspace has thousands of `#[test]`
functions, which `is_test` flags correctly. The TESTS edge set is too sparse
to use and too wrong to trust.

### Upstream resources

- No exact upstream report was found. [#1556](https://github.com/DeusData/codebase-memory-mcp/issues/1556)
  is the opposite JVM test-edge failure (missing `TESTS_FILE` edges); its
  maintainer analysis identifies a missing language mapping. Closed [#1000](https://github.com/DeusData/codebase-memory-mcp/issues/1000)
  is another TESTS-edge regression precedent, not a fix for false links.

## Architecture packages and dependencies carry no dependency data

`get_architecture --aspects packages` reports `fan_in 0 fan_out 0` for every
package. It also lists `src` (2695 nodes) and `tests` (821) as packages
alongside real crates, which looks like directory-name grouping rather than
Cargo package grouping. `--aspects dependencies` returns only the edge-type
histogram, with no package or module dependency rows. For a Cargo workspace,
crate dependencies are readable directly from `Cargo.toml`, which CBM already
indexes.

### Upstream resources

- **Related architecture work:** [#271](https://github.com/DeusData/codebase-memory-mcp/issues/271)
  is the open first-class workspace proposal and explicitly includes Cargo;
  [#1479](https://github.com/DeusData/codebase-memory-mcp/issues/1479) requests
  manifest `DEPENDS_ON` edges. They support the expected model but do not claim
  the present Cargo package/dependency output works. No exact issue was found.

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

### Upstream resources

- **Exact and open:** [#2150](https://github.com/DeusData/codebase-memory-mcp/issues/2150)
  reports the JSON-array flag inconsistency. [Open PR #2160](https://github.com/DeusData/codebase-memory-mcp/pull/2160)
  proposes accepting non-empty JSON string arrays for array-typed flags and has
  a regression test, but is not merged. The local behavior described above is
  therefore still authoritative for this checkout.

## Ambiguous-name results ignore output budgets and exit 0

- `trace_path --function-name run` returns `{"status":"ambiguous",...}` as
  JSON even under the default tree format, and exits 0. The not-found case
  exits 1.
- `get_code_snippet --qualified-name run --max-output-tokens 200` prints
  11,870 bytes: all 64 suggestions, ignoring the budget.

Scripts cannot tell an ambiguous lookup from a successful one by exit code.

### Upstream resources

- No exact upstream report was found. [#1460](https://github.com/DeusData/codebase-memory-mcp/issues/1460)
  is the open umbrella request for caller-controlled budgets, paging, and
  explicit truncation. It is relevant to `get_code_snippet` budget violation,
  not to the ambiguous-name exit-status contract; keep both repros together in
  a new report if filing.

## Path formats differ between tools

`get_code_snippet` returns an absolute `file_path`
(`/Users/viktor/Projects/project-management/crates/pm-port/src/writer_identity.rs`).
`get_file_outline` rejects that value: `file_path must be a
repository-relative path without '..'`. `search_graph` and `trace_path` return
relative paths. Output from one tool should be valid input for the next.

### Upstream resources

- No exact upstream report was found. The historical [file-outline feature PR](https://github.com/DeusData/codebase-memory-mcp/pull/1868)
  introduced the consumer named here but does not promise round-trippable path
  formats. File the absolute output plus the rejecting `get_file_outline` call
  as one interoperability contract.

## Project registry keeps deleted roots and duplicates

`list_projects` lists 43 projects. For 29 of them the `root_path` no longer
exists (removed worktrees, `/private/tmp/*`, `/var/folders/*` test repos).
Nothing in the output marks them as stale. Three projects
(`...writer-20283`, `issue-20283-verifier`, `issue-20283-verifier-full`) share
one root. The `project not found` error then prints all 43 names in its
`available_projects` field. The registry should mark or prune projects whose
root is gone, and the not-found hint should rank near matches instead of
dumping everything.

### Upstream resources

- **Direct operational aid, open:** [PR #2214](https://github.com/DeusData/codebase-memory-mcp/pull/2214)
  adds a dry-run-first `prune_projects` tool for confirmed missing roots. Its
  linked issue [#1431](https://github.com/DeusData/codebase-memory-mcp/issues/1431)
  is the stale-root report. The PR is queued and unmerged; it does not claim to
  deduplicate roots or improve the huge not-found hint.

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

### Upstream resources

- **Partially fixed locally:** see `8eebc00e` in “Locally fixed in this fork.”
  It adds an entry-point total and truncation flag. This issue still requires
  deterministic ordering and equivalent truthful reporting for routes/hotspots.
- No exact upstream issue was found. Closed [#280](https://github.com/DeusData/codebase-memory-mcp/issues/280)
  introduced architecture aspects generally, but is not a truncation fix.

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

### Upstream resources

- No exact upstream report or PR was found. The nearby closed [#252](https://github.com/DeusData/codebase-memory-mcp/issues/252)
  concerns other Cypher property/type conversion failures, so cite it only as
  parser/evaluator precedent, not a boolean fix.

## arch_entry_points and arch_hotspots exclude production paths via unanchored LIKE

`arch_entry_points` (`src/store/store.c:6422`) and `arch_hotspots`
(`src/store/store.c:6591`) exclude test material with `file_path NOT LIKE
'%test%'`, an unanchored substring match that also drops production paths
containing the token anywhere (`latest/`, `contest/`, `testbed/`). The companion
`is_test` property check on the same rows is the precise test; the `LIKE` is
redundant where `is_test` is set and wrong where it is not.

### Upstream resources

- No exact upstream report or PR was found. This is intentionally separate from
  the local entry-point truncation fix: correcting counts does not restore
  production paths excluded by the unanchored predicate. Preserve the three
  concrete path-token examples when filing.
