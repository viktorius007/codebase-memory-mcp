# Open defects and gaps

This is the current backlog. Resolved items and dated investigation transcripts
live in Git. Rust graph accuracy, its measurement and the planned work are
maintained only in [docs/RUST_GRAPH_STATUS.md](docs/RUST_GRAPH_STATUS.md).

The other reports below remain pending revalidation against a fresh index made
by the current source build. They are not claims about the currently installed
binary. Upstream links are research leads, not assertions of current PR, merge
or release status. Confirm the behavior before starting a repair.

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
worktree","git common dir"]' --limit 10` against a read-only corpus. Expected relevant symbols include `db_path::resolve_db_path` and
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

---

# Verified against the installed build, 2026-09-20

Binary: `~/.local/bin/codebase-memory-mcp` (`dev`, built 2026-09-20 07:35).
Corpus: `/Users/viktor/Projects/project-management` at `633207cea`, re-indexed
fresh as project `cbm-audit-pm` (`--mode full`, 25772 nodes / 102365 edges) from
a `git archive` snapshot, so none of the findings below are stale-index
artifacts. Independent oracles: `rust-analyzer 1.95.0 scip` (531 documents) and
`cargo metadata`. Every graph claim was re-checked by direct file read.

A second minimal corpus, `cbm-audit-synth`, was built for isolation: a two-crate
Cargo workspace (`alpha`, `beta`), 5 `.rs` files, 13 functions, one trait with
one impl, one path dependency, and textbook file-local free-function calls.

## Degree-based queries report live functions as dead, with no caveat

`search_graph --label Function --max-degree 0 --exclude-entry-points true`
returns `total: 781` on the pm corpus. The first two results are
`pm-git-sync::reconcile::abort_prepublication_session` (called at
`crates/pm-cli/src/reconcile_runtime.rs:590`) and
`git-safety::db_path::absolutize_from` (called at `db_path.rs:598` and `:614`,
in the same file, from a free function, by a bare call).

`search_graph`, `trace_path` and `get_code_snippet` emit no `semantic_note`.
`query_graph`, `get_architecture` and `check_index_coverage` do. A client that
follows the documented dead-code recipe therefore receives an unqualified list
of 781 false positives. `get_code_snippet` likewise prints bare
`"callers": 0, "callees": 0` for `absolutize_from`.

The partial-coverage note that the disclosing tools do emit says facts "may be
omitted". An omission caveat is not a correctness caveat: name-matched Rust
USAGE and IMPORTS links are wrong at the rates in
[docs/RUST_GRAPH_STATUS.md](docs/RUST_GRAPH_STATUS.md).

## Cypher cannot compare two properties, and says so unusably

All three of `a.file_path = b.file_path`, `a.file_path <> b.file_path` and
`a.file_path != b.file_path` fail with `expected value at pos 44` / `pos 45`.
The parser accepts only a literal on the right-hand side. The message gives a
byte offset, names no token, and does not say the construct is unsupported.

Related previously-reported diagnostics still reproduce verbatim:
`MATCH (n RETURN n` gives `expected token type 67, got 2 at pos 9`, and
`MATCH p=(a)-[:CALLS*1..2]->(b) RETURN p` gives `expected token type 66, got 85
at pos 6` — internal token numbers in both. `MATCH (f:Function) RETURN f LIMIT
2` returns `[["metadata"],["uuid"]]`: node names as strings, not node values.

Aggregates return strings: `RETURN count(r) AS c` yields `"836"`, while
`get_architecture` returns `total_edges` as an integer. Projecting a property
the node does not carry returns `""` silently — `RETURN a.line` produced empty
strings for all rows rather than null or an error.

## get_architecture derives no Cargo package or dependency facts

On `cbm-audit-synth` — a two-crate Cargo workspace with `beta` declaring a path
dependency on `alpha` — `--aspects packages` returns two rows, `src` (17 nodes)
and `Cargo` (4 nodes). Neither `alpha` nor `beta` appears. `--aspects
dependencies` emits no section. So do `--aspects entry_points` and `--aspects
hotspots`.

On `cbm-audit-pm` (45 workspace packages, 330 intra-workspace dependency edges
per `cargo metadata`) `--aspects packages` returns 15 rows mixing directory
buckets (`src`, `tests`) with crate names, every `fan_in` and `fan_out` zero,
and no `total` or truncation key in the JSON. `routes`, `hotspots` and
`entry_points` sections likewise carry only `cols` and `rows`. The backlog entry
asserting that entry-point output already includes total and truncation metadata
does not hold for this build.

`--aspects cycles` reports `call_edges_scanned: 553` while the same index holds
836 CALLS edges; the 283-edge gap is undocumented.

## Cargo manifest tables and `str::contains` arguments get code labels

Reproduced unchanged. `package`, `dependencies`, `dev-dependencies`, `features`,
`lints`, `lib` and `bin` tables become `Class` nodes — 230 of them in the pm
corpus, 4 in a 5-file synthetic workspace.

The pm corpus has no HTTP surface, yet it holds two `Route` nodes, `/tests/` and
`/fakes/`, with empty `file_path`, and four HTTP_CALLS edges into them. The
source is `xtask/tests/static_safety_policy.rs:1481-1486`, where
`is_test_source_label` calls `label.contains("/tests/")` and
`label.contains("/fakes/")`. A string literal passed to `str::contains` is being
read as a route.

## Array flags are still inconsistent

`--aspects '["packages"]'` is correctly rejected with the valid-aspect list.
`check_index_coverage --project cbm-audit-pm --paths
'["crates/pm-work-metrics/src/lib.rs","crates/git-safety/src/db_path.rs"]'` is
silently accepted as one literal path: the response echoes the whole JSON string
as `requested_path` and `path`, and reports `status: no_recorded_issue`
alongside `freshness: missing` and `recommended_action:
read_source_and_reindex`. A path that does not exist should not report "no
recorded issue". Passing the same two paths as repeated `--paths` flags works
and returns two rows.

## Semantic ranking does not surface the symbols it is asked for

`search_graph --semantic-query "resolve database path" --semantic-query "linked
worktree" --semantic-query "git common dir"` returns 50 results led by
`unsafe_boundary_policy` test functions and `contains_uuid_like_token`; 41 of the
50 score exactly 0 or negative. `git_common_dir` is in the graph
(`crates/git-safety/src/db_path.rs:208`, found by `--name-pattern`) and does not
appear. Unchanged from the earlier report.

## Interface defects

- `index_status` never renders its file lists. Both corpora print
  `parse_partial: files: 0 / count: 2 / truncated: true / omitted: 2` and
  `not_indexed: dirs: 0 / dirs_count: 13 / files: 0 / files_count: 28`. The same
  build's `index_repository` response does print the paths and reasons, so the
  data exists and only the `index_status` view drops it.
- Path round-trip still fails. `get_code_snippet` returns
  `"file_path": "/Users/viktor/Projects/project-management/crates/git-safety/src/db_path.rs"`;
  passing that to `get_file_outline` gives `file_path must be a
  repository-relative path without '..'`.
- `trace_path --function-name run` on the pm corpus prints all 63 suggestions —
  roughly 8 KB on one line, including `Field` and `Method` matches for a
  function-name query — and exits 0, while an unknown symbol exits 1.

## Evidence and reproduction

### The synthetic corpus

`cbm-audit-synth` is five files. Recreate it anywhere, then
`index_repository --repo-path <dir> --mode full --name cbm-audit-synth`.

`Cargo.toml`:

```toml
[workspace]
members = ["alpha","beta"]
resolver = "2"
```

`alpha/Cargo.toml` declares `name = "alpha"`, `edition = "2021"`.
`beta/Cargo.toml` declares `name = "beta"`, `edition = "2021"` and
`[dependencies] alpha = { path = "../alpha" }`.

`alpha/src/lib.rs`:

```rust
pub trait Greeter {
    fn greet(&self) -> String;
}

pub struct Loud;

impl Greeter for Loud {
    fn greet(&self) -> String {
        shout("hi")
    }
}

pub fn shout(word: &str) -> String {
    word.to_uppercase()
}

pub fn add(a: i32, b: i32) -> i32 {
    inner_add(a, b)
}

fn inner_add(a: i32, b: i32) -> i32 {
    a + b
}
```

`beta/src/lib.rs`:

```rust
pub mod contest;
pub mod latest;
pub mod testbed;

use alpha::{Greeter, Loud, add};

pub fn total() -> i32 {
    add(1, 2) + local_helper()
}

fn local_helper() -> i32 {
    3
}

pub fn announce() -> String {
    let l = Loud;
    l.greet()
}

pub fn shadowed(local_helper: impl Fn() -> i32) -> i32 {
    local_helper()
}
```

`beta/src/{contest,latest,testbed}/mod.rs`, each with its directory name
substituted for `NAME`:

```rust
pub fn NAME_entry() -> i32 {
    NAME_helper()
}

fn NAME_helper() -> i32 {
    7
}
```

Indexed result: 55 nodes, 60 edges, 13 Function nodes, 0 CALLS, 0 IMPLEMENTS,
1 IMPORTS, 3 USAGE, 2 DEFINES_METHOD, 4 `Class` nodes (the Cargo tables).
`search_graph --label Function` shows every function at degree 0 except
`announce`, which has one outbound USAGE edge to `Loud`.

The `contest/`, `latest/` and `testbed/` directories were included to exercise
the `file_path NOT LIKE '%test%'` filter reported further up this file. That
check could not be completed: `--aspects entry_points` and `--aspects hotspots`
return no section at all on this corpus, so there is no filtered output to
inspect.

### Preserved artifacts

Raw artifacts are under `private/audit-2026-09-20/`:

| Path | Contents |
|------|----------|
| `scip.log` | the SCIP generation log, including duplicate-symbol warnings |
| `index.log` | `index_repository` response for `cbm-audit-pm` |
| `graph-pages/` | every paged `query_graph` JSON response behind the CALLS and IMPORTS counts |
| `synth/` | the `cbm-audit-synth` corpus as indexed |

The 55 MB SCIP index of the pm corpus is not kept. Regenerate it on the snapshot
below with `rust-analyzer scip <repo> --output index.scip` (1.95.0, 531
documents, 76 s).

### Corpus identity

The pm snapshot is `git archive` of `633207cea` extracted to a scratch
directory, so it carries no `.git`; `detect_changes` is unavailable on it and
was not used. Recreate it with
`git -C <pm-repo> archive 633207cea | tar -x -C <dest>`. The live `Users-viktor-Projects-project-management` index was read
but not rebuilt, and returned identical node counts and identical CALLS rows,
which is what rules out a stale-index explanation.

### Not established

- Whether any non-Rust language in the same index is affected. The 833
  non-Rust CALLS edges were not checked for correctness.
- Whether `--mode moderate` or `--mode fast` differ. Only `--mode full` was run.
- The daemon, sandbox and lock reports earlier in this file. Untouched by this
  pass; every command here ran through the one-shot local CLI path.

---

# Deferred maintenance items

## File and fix two diagnostic-gap flakes (own issues)

a) tool_detect_changes_impact_shape (tests/test_incremental.c:1988) fails
rarely under 16-job parallel load: the detect_changes MCP tool reports
"git merge-base failed: the contained command could not complete". Root:
src/mcp/mcp.c:16018-16030 uses one error message both when git itself exits
nonzero and when the sandboxed subprocess runner fails to spawn under load,
so a scheduling hiccup is indistinguishable from a git semantics error.
Fix direction: separate the two branches' messages; consider retrying the
spawn once.

b) CBM_RUNTIME_DIR (the directory the CLI uses for its coordination socket)
longer than ~50 bytes makes every CLI command fail with "secure CLI
coordination could not be created (endpoint)" and an EMPTY detail string
(src/main.c:2838): the macOS unix-socket path limit is the real cause and
the diagnostic never names it. Measured: 49-byte parent works, 67 fails;
the default macOS TMPDIR is already too long. Fix direction: detect the
sun_path overflow and say so, naming the path and the limit.

## Pin the CI clang-format version in Makefile.cbm

`Makefile.cbm:1158` resolves whatever Homebrew LLVM is installed, so
formatter verdicts drift across LLVM majors: 20.1.8 and 23.1.1 disagree on
four files (src/cli/cli.c, src/daemon/frontend.c, src/daemon/runtime.c,
src/daemon/version_cohort.c) that only LLVM 20 flags. The one file the
resolved formatter (LLVM 23) flagged, src/pipeline/pass_lsp_cross.c —
drift introduced by 17f32acd — is reformatted and the gate is green
locally. Determine CI's pinned clang-format version, pin the same in
Makefile.cbm:1158, and drop this entry.
