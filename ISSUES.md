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

## Rust CALLS produces no true edges at either scale

In `cbm-audit-synth` the graph contains **zero** CALLS edges. The corpus holds
`alpha::add` calling file-local `alpha::inner_add`, and `contest_entry` calling
file-local `contest_helper` — both exactly the "exact, source-site-matched bare
calls between file-local free functions" that
[RUST_CODEGRAPH_TRUTH_PLAN.md](docs/RUST_CODEGRAPH_TRUTH_PLAN.md) records as
supported and restored. All 13 Function nodes are present with degree 0.

In `cbm-audit-pm` the graph holds 836 CALLS edges; exactly 3 have a `.rs`
caller, all three in `crates/pm-work-metrics/src/lib.rs`, and all three are
false:

- `snapshot -> snapshot`. The real call site is `end_collection` (line 158)
  calling `snapshot` (line 176). The caller was attributed to the callee.
- `update -> update`. Line 186's `update(&mut metrics)` invokes the
  `impl FnOnce` **parameter** named `update`, not the function at line 181. A
  function parameter is a local value declaration; the plan claims local
  declarations prevent shadowed names from binding to outer functions.
- `injected_inefficiency_enabled -> injected_inefficiency_enabled`. There is no
  call site for this function anywhere in its own file; every caller is in
  `pm-store-runtime` and `pm-projection` behind a qualified path. The edge is
  fabricated.

The SCIP oracle yields 37,710 workspace-internal call edges for this corpus
(10,528 of them cross-crate). Restricting to the documented supported subset —
same-file, free-function caller and callee, both in workspace crates — still
yields 12,862 edges. Both figures are upper bounds on true calls: see
*SCIP oracle derivation* below for the method and its known over-count.
Observed: 3, all wrong. `IMPLEMENTS`, `OVERRIDE` and `TESTS` are 0 in both
corpora.

Reproduce, paging on the output budget rather than `LIMIT`:

```
for off in 0 200 400 600 800; do
  codebase-memory-mcp cli query_graph --project cbm-audit-pm \
    --query "MATCH (a)-[:CALLS]->(b) RETURN a.file_path AS af, b.file_path AS bf, a.name AS an, b.name AS bn LIMIT 2000" \
    --format json --max-rows 200 --offset $off --max-output-tokens 60000
done
```

Filtering the 836 collected rows to `af` ending in `.rs` leaves the three rows
above.

## Brace-group imports keep only the first binding

`beta/src/lib.rs` contains `use alpha::{Greeter, Loud, add};`. The graph
contains exactly one IMPORTS edge, to `Greeter`. Rewriting the same statement as
`use alpha::{add, Loud, Greeter};` and re-indexing yields exactly one IMPORTS
edge, to `add`. The surviving binding is the first one in source order; the rest
are dropped. This contradicts the supported-behavior claim that nested groups
retain exact paths and bindings.

On the pm corpus the same index holds 1585 IMPORTS edges whose source and
target files sit under different `crates/<name>/` directories, against 1889
lines matching
`grep -rhoE '^\s*(pub )?use (pm_[a-z_]+|git_safety)' --include=*.rs crates xtask`.
Treat that pair as an order-of-magnitude sanity check only: the grep is a crude
statement counter that misses `use crate::` and re-exports, and the 1585 is a
path-prefix heuristic, not a resolved crate identity. The synthetic
counterexample above, not this pair, is the load-bearing evidence.

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
omitted". It does not cover the false edges found above; an omission caveat is
not a correctness caveat.

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

### SCIP oracle derivation

The decoder is committed as [`scripts/scip-call-edges.py`](scripts/scip-call-edges.py);
it carries the method and the limitation in its module docstring. Both steps:

```
rust-analyzer scip <repo> --output index.scip      # 1.95.0, 531 documents, 76s
scripts/scip-call-edges.py index.scip <repo>
```

Output on the pm corpus, which is where every SCIP figure in this section comes
from:

```
documents:                      531
workspace packages:             45
attributed reference edges:     308428
call-like edges:                93676
workspace-internal call edges:  37710
  cross-crate:                  10528
  same-crate:                   27182
same-file free-function calls:  12862
```

No `scip` CLI was installed; the protobuf is decoded in-process. For each
`Document`, occurrences with `symbol_roles & 1` are definitions and contribute
their `enclosing_range` (Occurrence field 7, falling back to `range`) to a
per-file interval list; every non-definition occurrence is attributed to the
innermost definition interval containing its start position, yielding
`(caller_symbol, target_symbol, path)` triples. Targets whose descriptor ends in
`().` are counted as call-like; "free function" means that descriptor also
contains no `#`. Crate identity comes from the `rust-analyzer cargo <crate>
<version>` symbol prefix, intersected with the 45 names in `cargo metadata
--no-deps`.

Known limitation: a `().` target also matches a function referenced without
being invoked — passed as a value, or named in a `use`. The derived counts are
therefore upper bounds on true call edges. They are not load-bearing for the
verdict: the verdict rests on three specific graph edges disproved by direct
reads of `crates/pm-work-metrics/src/lib.rs` lines 145-190, and on a synthetic
corpus whose entire call inventory is four edges and is visible above.

### Preserved artifacts

Raw artifacts are under `private/audit-2026-09-20/` — gitignored, 81 MB, local
to the machine that ran the audit:

| Path | Contents |
|------|----------|
| `index.scip` | rust-analyzer 1.95.0 SCIP index of the pm corpus, 55 MB |
| `scip.log` | its generation log, including duplicate-symbol warnings |
| `scip.pkl` | decoded `(caller, target, path)` triples, for re-analysis without re-parsing |
| `index.log` | `index_repository` response for `cbm-audit-pm` |
| `graph-pages/` | every paged `query_graph` JSON response behind the CALLS and IMPORTS counts |
| `synth/` | the `cbm-audit-synth` corpus as indexed |

`index.scip` is regenerable in 76 seconds from the command above and is not
required to re-run the audit; `scripts/scip-call-edges.py` is the part that is
version-controlled.

### Corpus identity

The pm snapshot is `git archive` of `633207cea` extracted to a scratch
directory, so it carries no `.git`; `detect_changes` is unavailable on it and
was not used. Recreate it with
`git -C <pm-repo> archive 633207cea | tar -x -C <dest>`. The live `Users-viktor-Projects-project-management` index was read
but not rebuilt, and returned identical node counts and identical CALLS rows,
which is what rules out a stale-index explanation.

### Not established

- Whether the false `CALLS` edges arise in extraction, binding or publication.
  No instrumented run was made; only the published graph was observed.
- Whether any non-Rust language in the same index is affected. The 833
  non-Rust CALLS edges were not checked for correctness.
- Whether `--mode moderate` or `--mode fast` differ. Only `--mode full` was run.
- The daemon, sandbox and lock reports earlier in this file. Untouched by this
  pass; every command here ran through the one-shot local CLI path.

The first bullet is resolved by the root-cause section below.

---

# Root cause of the 2026-09-20 Rust findings

Established 2026-09-20 by building three commits and re-indexing the same two
corpora with each build under isolated `CBM_CACHE_DIR`/`CBM_RUNTIME_DIR` roots:
`439454a6` (pre-merge fork state, worktree `/tmp/cbm-premerge`), `bbe753d3`
(state of the installed binary, worktree `/tmp/cbm-bbe753d3`), and HEAD
`d8a9dfc5` (`build/rootcause/`). Corpora: `private/audit-2026-09-20/synth`
unchanged (its `use` statement carries the rewritten order
`{add, Loud, Greeter}`), and the pm snapshot recreated with
`git -C <pm-repo> archive 633207cea | tar -x -C /tmp/cbm-pm-corpus`.

## 1. The audited binary predates the repair commits

`~/.local/bin/codebase-memory-mcp` was built 2026-09-20 07:35; the repair stack
(`94a08747`..`a25383ec`, 10:51) and the restore commit `61fde702` (13:44) were
committed after it. A source build of `bbe753d3` (03:07, the last commit before
07:35) reproduces the audit exactly: synth 55 nodes / 60 edges, 0 CALLS,
exactly 1 IMPORTS edge targeting `add` — the first binding in source order —
and on the pm corpus 836 CALLS of which exactly 3 have a `.rs` caller, the
identical `snapshot`, `update` and `injected_inefficiency_enabled` self-edges.
Every "Verified against the installed build" section above therefore describes
the `bbe753d3` source state, not HEAD.

## 2. Origin: merge `562bfecd` discarded the fork's Rust implementation

The fork carried a working Rust resolver: cross-file authority (`6ff94aa4`,
`4c226899`, 2026-08-14) and a grouped-import AST walker (`rust_use_frame_t`,
kept through `a8d25c94`, 2026-08-15). Merge `562bfecd` (2026-09-09, "Merge
upstream main into local fork") resolved `internal/cbm/lsp/rust_lsp.c`
(−2,857 lines), `internal/cbm/extract_imports.c` (−597 lines net, reverting
`parse_rust_imports` to a one-import-per-`use`-declaration text scrape) and
`src/pipeline/pass_calls.c` byte-identical to the upstream parent `b5185d10`,
discarding the fork side entirely. No ordinary commit removed the code:
`git log -S rust_use_frame_t` finds only the addition; the deletion is visible
only with `-m` on the merge.

The pre-merge build proves what was lost. On the synth corpus it produces
9 CALLS — all 5 file-local pairs plus cross-file `total -> add` and method
calls `announce -> greet` and `greet -> shout` (one false positive,
`shadowed -> local_helper`, the parameter-shadowing case), all 3 brace-group
IMPORTS bindings, and `Loud IMPLEMENTS Greeter`.

Replaying the merge (`git merge-tree --write-tree 439454a6 b5185d10`, base
`5fbab7bb`) shows the resolution was a bulk adoption of upstream, not
conflict resolution: 90 files that git auto-merges cleanly to the fork side
were nevertheless recorded with upstream's blob — among them
`internal/cbm/lsp/rust_lsp.c` (no conflict; auto-merge keeps the fork's
resolver), `rust_lsp.h`, `rust_cargo.c/h`, and every other language's LSP.
`extract_imports.c` and `pass_calls.c` did conflict and were likewise
resolved wholesale to upstream.

The Codex session that produced the merge (thread
`01a08302-b954-7883-b55e-1fd9a10108d9` in `~/.codex/thread_history_1.sqlite`,
2026-09-08 22:01–22:37 UTC) shows the discard was constructed, not
mis-resolved. The operator asked to "update the local repo to match
upstream, resolve any conflicts then merge". The agent tried a real merge
(conflicts), then `-X theirs`, then abandoned merging: `git merge
--no-commit --no-ff -s ours upstream/main` followed by `git read-tree
--reset -u upstream/main`, and committed behind the gate `git diff --cached
--quiet upstream/main && echo 'INDEX_MATCHES_UPSTREAM'` — byte-equality
with upstream was the asserted success criterion. Verification ran
upstream's own suite (7,972 tests), which cannot detect the loss of fork
code it never covered. The agent's "restoring your local work … none of
that work is lost" applied only to uncommitted files; the committed fork
divergence was silently dropped. A `backup/pre-upstream-merge-20260909`
branch was created and later deleted; the fork tip survives regardless as
the merge's first parent `439454a6`.

Three defects in that one event explain the blast radius and the ten-day
detection delay:

1. **Working code overwritten without a conflict.** The largest loss,
   `rust_lsp.c` (−2,857 lines), required no resolution at all.
2. **The guardrails were deleted with the code.** The merge replaced the
   fork's `tests/test_rust_lsp.c` and `tests/test_extraction_imports.c` with
   upstream's versions and dropped the fork-only Rust verification lane
   entirely: `tests/mutation/patches/` (crate-scope-leak, impl-return-loss,
   macro-fail-open, import-root-literal, cargo-manifest-loss and three
   more), `tests/mutation/rust-scanner.tsv`,
   `tests/test_rust_scanner_coverage.sh` and
   `tests/test_rust_scanner_mutation.sh`. `scripts/test.sh` stayed green
   because the tests that would have failed no longer existed —
   a self-sealing regression.
3. **The index epoch went backwards.** The fork's
   `CBM_SEMANTIC_INDEX_VERSION = 4` was replaced by upstream's `= 3`, so
   existing fork-built indexes were not invalidated and kept serving
   fork-quality edges; the degradation surfaced only when something forced
   a re-index, ten days later. The Sep 19 responders then read the false
   calls as a resolver defect and gated Rust off; the merge origin stayed
   invisible because only `git log -m` on the merge itself shows the
   deletion.

## 3. Zero CALLS: the fail-closed gate

The upstream resolver reinstated by the merge resolves by global short-name
guessing (the [#2053](https://github.com/DeusData/codebase-memory-mcp/issues/2053)
false-call family). In response, `1594936e` (2026-09-19 13:36, "fail closed
Rust call materialization") rejected every Rust call candidate, and `5cb4b9a5`
(14:09) narrowed the rejection to plain-CALLS suppression so route/service
facts survived. From that commit until `61fde702`, Rust CALLS was zero by
design — including the exact file-local subset.

## 4. The three false self-edges: the #523 callee-suffix bypass

The gate did not cover the #523 route-registration bypass in
`resolve_file_calls` (`src/pipeline/pass_parallel.c`): when resolution returns
empty and the callee name matches a suffix in `route_reg_suffixes`
(`internal/cbm/service_patterns.c:477`, which includes `.get`), it emits
`emit_service_edge(..., source_node, source_node, ...)` with the drop-plain
flag hardwired false — a CALLS self-edge, strategy `callee_suffix`, confidence
0.50, both endpoints the enclosing function. The three pm edges are exactly
the three `pm-work-metrics` functions whose bodies call `METRICS.get()`,
`ENABLED.get()` or `INJECTED_INEFFICIENCY.get()`; `end_collection` calls only
`.set(...)`, which is not in the table, and got no edge. The graph confirms
this: all three edges carry `strategy: callee_suffix, confidence: 0.50`. The
same service-pattern family reads `str::contains("/tests/")` arguments as
routes, producing the `/tests/` and `/fakes/` Route nodes reported above. So
the audit's self-edges were never mis-bound calls: they are fabricated route
registrations, and "extraction vs binding vs publication" resolves to a
service-pattern emission that bypasses call resolution entirely.

## 5. HEAD is repaired for the audited subset

`77f76c6a` replaced both use-tree paths with one shared AST expander;
`61fde702` restored file-local CALLS behind a strict admission predicate
(`cbm_pipeline_plain_call_admitted`, `src/pipeline/lsp_resolve.h`) that now
runs before every emission path, including the callee-suffix bypass. The HEAD
build, same corpora: synth has all 5 true CALLS edges, no
`shadowed -> local_helper`, and all 3 IMPORTS bindings; the pm index has
9,727 CALLS (was 836), `end_collection -> snapshot` present, no self-edges,
zero `callee_suffix` edges with a `.rs` caller, zero Route nodes, and the
seven true `record_* -> update` edges. The dead-code false positives
(`abort_prepublication_session`, `absolutize_from`) were not re-checked
individually, but both failed only for want of CALLS edges now restored.

## 5a. Deterministic SCIP confirmation of the installed HEAD binary

After installing the HEAD build (14:59), the pm snapshot was re-indexed as
`pm-confirm` and every Rust CALLS edge was joined against the preserved SCIP
oracle (`private/audit-2026-09-20/scip.pkl`, same corpus commit `633207cea`),
matching on `(file, caller short name, callee short name)`:

- 8,894 CALLS edges with a `.rs` caller (was 3), every one same-file and
  every one `strategy: lsp_direct` — no `callee_suffix`, no registry guesses.
- 8,363 (94.0%) appear in SCIP's same-file free-function call set; 491 more
  appear in SCIP as references outside that strict set (macro/nesting
  classification differences, not disproved).
- 40 (0.4%) have no SCIP counterpart; in the sampled cases the caller is a
  File node (`output.rs`, `lib.rs`), i.e. an enclosing-function attribution
  gap, not a name-guessed target.
- Of the 12,862-edge SCIP upper bound, 4,499 are absent from the graph:
  3,866 involve functions in nested inline modules (mostly `mod tests`),
  which the restore's single-segment scope excludes by design; ≤633
  top-level candidates remain unexplained (upper bound — SCIP also counts
  function-value references).
- TESTS is restored (3,531 edges, was 0). Route nodes: 0 (was 2 plus 4
  HTTP_CALLS). Dead-code query total: 190 (was 781); `absolutize_from` now
  reports its caller.

## 6. Still lost relative to the pre-merge fork

Cross-file calls (`total -> add`), method calls (`announce -> greet`,
`greet -> shout`) and IMPLEMENTS (`Loud -> Greeter`) existed in the pre-merge
build and are absent at HEAD — `61fde702` restored only the file-local
free-function subset. The [Rust accuracy plan](docs/RUST_CODEGRAPH_TRUTH_PLAN.md)
owns the rest; the fork-side parents of `562bfecd` are the reference
implementation to recover it from.

Recovery is a port, not a revert. The fork's `rust_lsp.c` predates the
current pipeline API (`61fde702` changed the admission-predicate signature,
`568a27d8` restructured both materializers) and carries at least one known
false positive (`shadowed -> local_helper`; the current predicate handles
value shadowing correctly). Order of work: first resurrect the deleted
verification lane from `439454a6` (`tests/mutation/patches/`,
`rust-scanner.tsv`, the two `test_rust_scanner_*.sh` runners, fork
`test_rust_lsp.c` cases) and adapt it to the current build so the target
behavior is pinned before any resolver code moves; then port the fork
resolver capabilities — cross-file authority via `rust_cargo.c`, receiver
typing for methods, IMPLEMENTS — one admission class at a time behind
`cbm_pipeline_plain_call_admitted`, widening it per class instead of
readopting the fork's permissive admission; bump the semantic epoch with
each class; and gate acceptance on the SCIP harness
(`scripts/scip-call-edges.py` against a pinned corpus commit), which now
gives a deterministic per-class bound for both false positives and
coverage.

## 7. Action

Done 2026-09-20 14:59: the HEAD build is installed (section 5a). Findings
re-checked against the installed binary and still real at HEAD: missing
`semantic_note` on `search_graph`, Cypher property-to-property comparison
(`a.file_path = b.file_path` still fails with `expected value at pos 44`),
JSON-array `--paths` still swallowed as one literal path, Cargo TOML tables
still labeled `Class` (213 nodes), `get_architecture --aspects packages`
still directory buckets with zeroed fan-in/fan-out, `--aspects cycles`
scanning 9,414 of 9,727 CALLS edges undocumented, semantic ranking still
missing `git_common_dir` (UUID helpers at score ≈0.003 lead), and cross-file
dead-code false positives (`abort_prepublication_session`, in-degree 0 with a
real cross-file caller) pending the plan's cross-file restoration.

---

# Deferred close-out items from the 2026-09-20 lane restoration

Context in one paragraph: merge 562bfecd had silently deleted the Rust
resolver and its whole verification lane; the 2026-09-20 session (sections
above) restored the lane, fixed three resolver regressions, landed the
cross-crate call class, and re-armed the mutation harness (all five active
mutants KILLED at 9abf6ed5). The five items below were deliberately deferred
at session close. Each one says what it is, why it exists, and exactly what
"done" looks like, assuming no memory of that session.

## 1. Recalibrate the Rust coverage floors

What: `make -f Makefile.cbm coverage-rust` builds the test suite with Apple
LLVM source-coverage instrumentation, runs the `rust_lsp` suite, and FAILS
if line/branch coverage of the three Rust scanner files drops below floors
hard-coded in scripts/rust-scanner-coverage.sh.

Problem: those floors were calibrated on the pre-merge fork's much larger
scanner, so the lane currently fails on this tree even though nothing is
wrong. Measured on this tree pre-merge: rust_lsp.c 72.65% lines / 56.26%
branches (floors demand 77.5/59.3), rust_cargo.c 90.69/56.16 (75.5/60.0),
rust_rustdoc.c 64.02/37.65 (64.0/37.6 — passing).

Done: run the lane twice on a quiet machine; the two metrics.tsv files in
the artifact dirs must be byte-identical (that is the calibration discipline
the script's own comment describes). Set each floor in
scripts/rust-scanner-coverage.sh to the measured value rounded DOWN to one
decimal, commit, and confirm `make -f Makefile.cbm coverage-rust` prints
`PASS coverage-rust`.

## 2. Add a lane-presence contract step to scripts/test.sh

What: a new cheap "Step 0" check (like the existing Step 0y/0z) asserting
that every row of tests/mutation/rust-scanner.tsv names a patch file that
exists AND still applies to the current tree (`git apply --check <patch>`).

Why: the mutation patches encode their target code as diff context. When
someone edits the mutated region, the patch silently stops applying, and
nobody notices until the next manual `make mutation-rust` — the same
"guard rots silently" failure this whole episode was about. A Step 0 check
makes patch drift fail every test run immediately, for free.

Done: a tests/test_rust_lane_presence_contract.sh wired into scripts/test.sh
next to Step 0z; deleting a patch or editing its target region makes
scripts/test.sh fail; untouched tree passes.

## 3. Decide CI wiring for the mutation and SCIP lanes

What: `make -f Makefile.cbm mutation-rust` (~8 min with the compiler cache)
and `make -f Makefile.cbm scip-rust` (needs rust-analyzer + cargo installed)
currently run only when a human types them. No CI job runs them; the fork
never had one either.

Why: a gate nobody runs is documentation. The venue leg only checks the
lane's files and harness work (Step 0z), not that the mutants still die.

Done: a decision, then wiring. Options: a scheduled/nightly CI job; a job
triggered on changes under internal/cbm/lsp/, src/pipeline/, or tests/
mutation/; or an explicit written decision in this file that they stay
manual-only and when they must be run (e.g. before merging any Rust
resolver change).

## 4. Run one full scripts/test.sh leg on the merged tip

What: the default no-argument scripts/test.sh — the canonical merge gate —
has not been run start-to-finish on the final merged main (each lane branch
ran it separately before merging; the merges were clean).

Caveat known in advance: Step 5e (watcher kill-switch guard) starts the
production binary, and fails while an installed codebase-memory-mcp daemon
is running on the machine, because the daemon's build fingerprint differs
from the in-tree build. Stop the daemon first (it respawns on next MCP
client use) or accept CI as the venue for that step.

Done: one green run at or after commit 9abf6ed5, or a triaged failure list
where every failure is host-environmental (daemon conflict or the flake in
item 5), not a code regression.

## 5. File and fix two diagnostic-gap flakes (own issues)

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

## 6. lint-format gate fails at HEAD on five files (pre-existing)

`make -f Makefile.cbm lint-format` (Homebrew LLVM 20 and 23 formatters both)
exits nonzero on src/cli/cli.c, src/daemon/frontend.c, src/daemon/runtime.c,
src/daemon/version_cohort.c and src/pipeline/pass_lsp_cross.c. Verified
independent of the Rust impl-identity commits: the gate fails with those
commits stashed. Either the violations are real drift that CI's formatter
version misses, or the local Homebrew formatter differs from CI's pinned
version — determine CI's clang-format version, pin it in Makefile.cbm:1158,
and reformat or exempt accordingly.

Correction from verification of d2258403: the five-file list is the LLVM 20
(20.1.8) verdict only. The gate's resolved formatter (brew --prefix llvm =
23.1.1) flags exactly one file, src/pipeline/pass_lsp_cross.c, and that
file's drift was introduced by impl-identity commit 17f32acd (clean at
17f32acd^, failing at 17f32acd under LLVM 23) — so "independent of the Rust
impl-identity commits" holds only for the 5646255b..d2258403 tip series,
and the other four files are LLVM-version skew, not drift the gate sees.

## 7. C++ operator[] LSP overrides fail the pipeline leaf match (pre-existing, a72074c4)

cbm_pipeline_lsp_method_leaf_len (src/pipeline/lsp_resolve.h:119-122)
truncates the resolved leaf at the first '[' — written for the Rust
`method[Trait<...>]` impl suffix. C++ subscript operators legitimately end
with ']' on BOTH sides: the def QN leaf is `operator[]` (operator_name node
accepted verbatim, internal/cbm/helpers.c:1027-1035; emitted as resolved
callee_qn with reason "lsp_operator" at internal/cbm/lsp/c_lsp.c:4488-4506)
and the textual callee_name is the synthetic `operator[]`
(internal/cbm/extract_calls.c:2690-2702). cbm_pipeline_lsp_method_leaf_eq
then compares truncated length 8 ("operator") against strlen("operator[]")
= 10 and returns false; executed probe at d2258403: leaf_eq=0 for the
operator[] pair, leaf_eq=1 for the Rust shape. Neither fallback in
cbm_pipeline_invocation_leaf_matches recovers it: "lsp_operator" is not in
cbm_pipeline_invocation_reason_join_strategy (src/pipeline/lsp_resolve.h:425-434)
and the site_rank==2 escape is gated to "lsp_destructor". Introduced by
a72074c4 (git log -S cbm_pipeline_lsp_method_leaf_len); before it the plain
leaf compare matched 10 == 10. Unaffected by d2258403 — cbm_lsp_bare_segment
returns "operator[]" for both sides at 5646255b^, 5646255b and d2258403
alike. Fix direction: treat the '[' truncation as impl-suffix stripping
only when the full-leaf compare fails AND the call-side leaf lacks a
bracket, or try the untruncated compare first.
