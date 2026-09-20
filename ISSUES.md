# Open defects and gaps

This is the current backlog. Resolved items and dated investigation transcripts
live in Git. Rust's supported subset and remaining implementation work are
maintained in the [Rust accuracy plan](docs/RUST_CODEGRAPH_TRUTH_PLAN.md).

The other reports below remain pending revalidation against a fresh index made
by the current source build. They are not claims about the currently installed
binary. Upstream links are research leads, not assertions of current PR, merge
or release status. Confirm the behavior before starting a repair.

## Rust semantic coverage remains partial

Exact file-local free-function CALLS and their derived TESTS are supported.
Methods, cross-file calls, expanded calls and IMPLEMENTS/OVERRIDE remain gated;
missing edges do not establish absence. The [accuracy plan](docs/RUST_CODEGRAPH_TRUTH_PLAN.md)
owns implementation identity, Cargo/module scope, wrapper-type propagation,
library seeds, cfg/macros and full/incremental corpus verification.

Important corpus cases include `From<WriterIdentity> for String`, the private
`git-safety` `clone` helper, aliased `RepositoryContinuityPort` implementations,
and calls through `Rc<RefCell<dyn RepositoryContinuity>>`. Cycle, impact and test
answers must be checked after binding changes; a confidence threshold alone
cannot establish target correctness. See the plan for source paths and scope.

Related reports: [#2053](https://github.com/DeusData/codebase-memory-mcp/issues/2053)
(same-name false calls), [#2127](https://github.com/DeusData/codebase-memory-mcp/issues/2127)
(import collisions), [#2126](https://github.com/DeusData/codebase-memory-mcp/issues/2126)
(call/reference distinction), [#2035](https://github.com/DeusData/codebase-memory-mcp/issues/2035)
(arity), [#2152](https://github.com/DeusData/codebase-memory-mcp/issues/2152)
and [#1114](https://github.com/DeusData/codebase-memory-mcp/issues/1114)
(dispatch), [#1556](https://github.com/DeusData/codebase-memory-mcp/issues/1556)
and [#1000](https://github.com/DeusData/codebase-memory-mcp/issues/1000)
(test relationships).

## Sandbox socket and lock denials lose the actionable cause

Reported behavior: an unallowed Unix socket connection fails with `EPERM`, but
the client retries for 30 seconds and blames an active/starting daemon. Denied
coordination writes can instead produce `CLI exact-build admission could not be
verified; retry after active CBM operations exit`.

Preserve the permission-denied details, stop retrying denied connections, and
identify required runtime/socket access. Revalidate separately from stale-socket
and lifetime-lock races. The relevant diagnostics originate in
`cbm_daemon_ipc_connect` and `cbm_daemon_bootstrap_classify_failed_connect`.

The scoped configuration and opt-in integration check are documented in
[Configuration](docs/CONFIGURATION.md) and `tests/test_cli_codex_sandbox.sh`;
full-access mode is not required.
Related resources: [#2107](https://github.com/DeusData/codebase-memory-mcp/issues/2107),
[#2046](https://github.com/DeusData/codebase-memory-mcp/issues/2046),
[#2047](https://github.com/DeusData/codebase-memory-mcp/pull/2047).

## Permanent daemon can accept a connection without completing identity verification

Ordinary CLI commands run locally; reproduce this through daemon-backed sessions,
not by assuming the old daemon-backed CLI path still exists.

Reported behavior: the socket accepts connections but the identity handshake
stalls; clients refuse an unverified live daemon and cannot recover. Investigate
the accept-to-verify deadline, detection of a non-serving daemon, and actionable
recovery diagnostics. A live lifetime-lock holder is not proof of responsiveness.
Revalidate this separately from a denied socket connection.

Related resources: [#2178](https://github.com/DeusData/codebase-memory-mcp/issues/2178),
[#2162](https://github.com/DeusData/codebase-memory-mcp/issues/2162),
[#2179](https://github.com/DeusData/codebase-memory-mcp/pull/2179),
[#2207](https://github.com/DeusData/codebase-memory-mcp/pull/2207).

## Semantic search relevance

Revalidate `search_graph --semantic-query '["resolve database path","linked
worktree","git common dir"]' --limit 10` against the read-only corpus named in
the Rust plan. Expected relevant symbols include `db_path::resolve_db_path` and
`git_common_dir`; the report instead found unrelated UUID helpers. Compare with
`search_code --pattern git-common-dir` and BM25 `--query`. Keep semantic-vector
ranking separate from BM25 fixes.

Related resources: [#1417](https://github.com/DeusData/codebase-memory-mcp/issues/1417),
[#2149](https://github.com/DeusData/codebase-memory-mcp/issues/2149),
[#2227](https://github.com/DeusData/codebase-memory-mcp/pull/2227).

## Cypher diagnostics and whole-node projection

- Revalidate `MATCH (n RETURN n` and
  `MATCH p=(a)-[:CALLS*1..2]->(b) RETURN p`. Diagnostics should name tokens and
  distinguish malformed syntax from unsupported constructs, rather than expose
  internal token numbers. Related: [#239](https://github.com/DeusData/codebase-memory-mcp/issues/239).
- Revalidate whether `RETURN f` returns only a node name instead of a usable node
  value. This is separate from property projection through `WITH` aggregation.
  Related scope/projection reports: [#2208](https://github.com/DeusData/codebase-memory-mcp/issues/2208),
  [#1983](https://github.com/DeusData/codebase-memory-mcp/issues/1983),
  [#1919](https://github.com/DeusData/codebase-memory-mcp/issues/1919).

## Non-HTTP strings and TOML tables receive misleading graph labels

Revalidate filesystem strings such as `/tests/`, `/fakes/` and
`/nonexistent/junit.rev` becoming Route nodes or HTTP_CALLS in a workspace with
no HTTP surface. Separately check Cargo TOML tables (`package`, `dependencies`,
`features`, `dev-dependencies`, `lints`) being labeled Class. Verify source/kind
filters without removing genuine route or type nodes.

Related route reports: [#598](https://github.com/DeusData/codebase-memory-mcp/issues/598),
[#455](https://github.com/DeusData/codebase-memory-mcp/issues/455),
[#999](https://github.com/DeusData/codebase-memory-mcp/issues/999).

## Cargo architecture lacks package dependency information

Revalidate `get_architecture --aspects packages` and `--aspects dependencies`
against Cargo manifests. Directory buckets such as `src` and `tests`, zeroed
fan-in/fan-out, and an edge-type histogram do not establish crate dependencies.
Coordinate any repair with the Cargo scope work in the Rust plan.

Related resources: [#271](https://github.com/DeusData/codebase-memory-mcp/issues/271),
[#1479](https://github.com/DeusData/codebase-memory-mcp/issues/1479).

## Array flags accept JSON inconsistently

Revalidate JSON-array values for `--semantic-query`, `check_index_coverage
--paths`, and `get_architecture --aspects`. A JSON-looking value must be parsed
or rejected, not silently treated as one literal path. Repeated flags such as
`--paths a --paths b` are the reported working form.

Related resources: [#2150](https://github.com/DeusData/codebase-memory-mcp/issues/2150),
[#2160](https://github.com/DeusData/codebase-memory-mcp/pull/2160).

## Ambiguous-name responses need consistent budgets, formatting and exit status

Revalidate `trace_path --function-name run` on a corpus with multiple matches:
the report returned ambiguous JSON under tree format with exit 0, whereas a
missing symbol exited 1. Also check `get_code_snippet --qualified-name run
--max-output-tokens 200`: ambiguity suggestions must respect the output budget.

Related budget resource: [#1460](https://github.com/DeusData/codebase-memory-mcp/issues/1460).

## Tool paths do not round-trip

Revalidate passing `get_code_snippet`'s `file_path` directly to
`get_file_outline`. The report returned an absolute path which the latter
rejected with `file_path must be a repository-relative path without '..'`.
`search_graph` and `trace_path` returned relative paths.

Related interface resource: [#1868](https://github.com/DeusData/codebase-memory-mcp/pull/1868).

## Project registry retains missing roots and duplicate root identities

Revalidate missing-root visibility and duplicate registrations in
`list_projects`. A missing root should be marked or safely prunable, duplicate
roots should have a defined identity policy, and project-not-found hints should
prefer near matches over dumping the registry. This backlog does not authorize
deleting live project indexes.

Related resources: [#1431](https://github.com/DeusData/codebase-memory-mcp/issues/1431),
[#2214](https://github.com/DeusData/codebase-memory-mcp/pull/2214).

## Architecture list completeness and ordering

Entry-point output includes total and truncation metadata. Remaining checks are
deterministic ordering for capped aspects and equivalent truthful totals and
truncation disclosure for routes/hotspots. Revalidate `get_architecture` on a
corpus larger than each cap; returned counts must not imply complete populations.

Related aspect resource: [#280](https://github.com/DeusData/codebase-memory-mcp/issues/280).

## Architecture test-path filters can exclude production code

Revalidate `arch_entry_points` and `arch_hotspots` filtering with
`file_path NOT LIKE '%test%'`: production paths `latest/`, `contest/` and
`testbed/` must not disappear merely because they contain that substring.
Use the precise test classification where available and a defined path policy
where it is absent. Count/truncation fixes do not establish correct filtering.
