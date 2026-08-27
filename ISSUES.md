# ISSUES — open defects and gaps

This is the canonical backlog. Remove resolved items rather than retaining a
second history; commit history carries the old investigations.

Verified 2026-08-27 against a from-source HEAD build (`1f4e74e8`, version
`dev build=e95e7503…`) serving a fresh index of project-management. The prior
round's "still open" verdicts were measured against a stale DeusData release
binary (`build=c0cc131b…`) and its stale index; those are void. See the
distribution note at the bottom.

## Open

### Cross-file `#[macro_export] macro_rules!` invocations do not expand

Verified 2026-08-27 against a from-source HEAD build (`--version` fingerprint
`e76c243b0d31e54998492f3197341ca0364648ff2a98e6818ca8ee19eb286ee2`, compiled
`TEST_SEAMS=1` and run against a private `CBM_TEST_DAEMON_RUNTIME_PARENT`
namespace so the account-wide daemon of live MCP clients stayed untouched)
reindexing project-management full (`index_repository --mode full`: 18268 nodes,
115993 edges, `rust_analysis` verdict `complete`, 358/358 Rust files complete)
into a fresh index.

The nested-repetition expander fix (see the fixed ledger) is confirmed live for
the same-file case: `DemoStatus`, the `string_enum!` invocation inside the
`#[cfg(test)] mod tests` of the very file that defines the macro
(`crates/pm-core/src/entity/string_enum.rs:207`, macro defined at
`string_enum.rs:45`), now has an `Enum` node
(`query_graph … n.label = 'Enum' AND n.file_path CONTAINS 'entity/string_enum.rs'`
→ rows `Bare`, `DemoStatus`).

Every production entity enum invoked cross-file still has no node
(`search_graph --name-pattern 'AdrStatus'` → `total: 0`; likewise absent from a
`MATCH (n) WHERE n.label='Enum' AND n.file_path CONTAINS 'entity/'` sweep, which
returns only hand-written enums plus the same-file `DemoStatus`/`Bare`). The
affected invocation sites, each `use crate::string_enum;`-importing the macro:

- `crates/pm-core/src/entity/adr.rs:25` → `pub enum AdrStatus` (`adr.rs:27`)
- `crates/pm-core/src/entity/issue.rs:7` → `IssueType` (`issue.rs:9`),
  `issue.rs:18` → `IssueStatus` (`issue.rs:20`), `issue.rs:31` →
  `IssueTargetKind` (`issue.rs:33`)
- `crates/pm-core/src/entity/goal.rs:6` → `GoalStatus` (`goal.rs:8`)
- `crates/pm-core/src/entity/reason.rs:6` → `ReasonStatus` (`reason.rs:8`)
- `crates/pm-core/src/entity/task.rs:10` → `Complexity` (`task.rs:12`)
- `crates/pm-core/src/entity/quality_attribute.rs:3` → `QualityAttribute`
  (`quality_attribute.rs:5`)
- `crates/pm-core/src/entity/external_ref.rs:23` → `Scheme`
  (`external_ref.rs:27`), `external_ref.rs:475` → `ExternalRefStateReason`
  (`external_ref.rs:486`)

(`crates/pm-infra/src/sqlite/helpers/mod.rs:845` mentions `string_enum!` only in
a doc comment; it is not an invocation site.)

Root cause, traced in source: Rust extraction is per-file — `cbm_extract_file`
(`internal/cbm/cbm.c:1161`) is called once per file by the pipeline passes
(e.g. `src/pipeline/pass_semantic.c:497`), and `rust_lsp_process_file`
(`internal/cbm/lsp/rust_lsp.c:7499`) seeds the macro table with
`rust_collect_macro_rules` (`rust_lsp.c:3797`), which walks only the root of the
file under extraction. The macro is `#[macro_export]`ed at `string_enum.rs:44`
and `use crate::string_enum`-imported at each site (e.g. `adr.rs:4`), but that
definition text is absent from `adr.rs`'s tree. At the invocation,
`rust_expand_user_macro` calls `rust_visible_macro_definition`
(`rust_lsp.c:4923`), which finds no rule of that name in this file's
`macro_rules_arr` and returns NULL, so the expander returns at `rust_lsp.c:4925`
before any pattern match, expansion, or health record — the cross-file miss is
silent, identical to genuinely-dead code. The only corpus-wide macro table,
`cbm_build_macro_table_from_files` (`src/pipeline/pass_parallel.c:900`, built at
`pass_parallel.c:983`), is ObjectScript-`.inc`-only and carries no Rust macros.

A fix needs a crate-scoped Rust macro registry collected in a pre-pass and
threaded into per-file extraction so a `#[macro_export]` (or otherwise
textually-in-scope) macro is visible at every invocation site — a new cross-file
subsystem, not a local expander change. Reindex under a fresh
`delete_project` + `index_repository --mode full` after any such change: an
unchanged tree takes the incremental no-op path and will not re-exercise a
graph-construction fix (see the distribution hazard below).

### `macro_rules!` derive-driven callables still omit

`string_enum!`'s `token()` also comes from
`#[derive(::pm_entity_derive::PmStringEnum)]`, a proc-macro expanded by rustc,
not by `macro_rules!`. It is out of scope for the transcriber expander and needs
a separate derive-aware pass. The `token()` written literally in the
`string_enum!` transcriber body IS emitted (confirmed for the same-file
`DemoStatus`); only derive-generated methods are absent. Calls made inside a
still-unsupported macro body have no caller node, matching the long-standing
"calls from inside a macro invocation body have no caller node" gap.

## Confirmed behavior

Deliberate designs that have been mistaken for defects before. These are not
bugs; do not "fix" them without changing the contract first.

- Ambiguous symbols return an ambiguity result rather than a guessed node.
- Positional JSON and flag-based CLI forms both work; raw positional JSON is
  deprecated but still accepted.
- `edge_types` reaches the traversal. OVERRIDE edges run implementation → trait,
  so an inbound walk from an implementation does not cross its outbound
  OVERRIDE edge.
- Module CALLS edges can be real: Rust static/const initializers and top-level
  Python/shell bodies execute outside a callable. They remain in the graph as
  structural evidence and are intentionally reported separately from callers.
- The bare project-root `.__file__` name is not resolvable
  (`get_code_snippet … .__file__` returns `symbol not found`, exit 1); per-file
  `__file__` nodes exist internally as `DEFINES` sources.

## Fixed and verified on HEAD (2026-08-27)

Removed from Open after live confirmation on the HEAD build; kept here only as a
short ledger, to be pruned once the release ships them.

- **Entry-point truncation disclosure** (`8eebc00e`): `entry_points_total` /
  `entry_points_truncated` present and firing.
- **Test-target entry points retained** (`629c89a9`): test `main` targets appear
  in `entry_points`.
- **`query_graph --max-rows` truncation disclosure** (`e21e48d5`): `truncated:
  true` emitted when matches exceed the cap; absent otherwise.
- **`search_graph` degree counts `OVERRIDE`** (`eb12be98` / `eb743fa0`): trait
  implementations no longer report degree-0; degree-0 hits are genuine zero-edge
  stubs.
- **`trace_path`/`detect_changes` reject depth > 15** (`abbf1030`): `depth must
  be at most 15 (given N)` instead of silent clamping.
- **Exact build identifier** (`5d1e21a1`): `--version` prints `dev
  build=<64-hex>` fingerprint.
- **Route slice truncation disclosure** (`21d39bca`): `get_architecture` routes
  aspect emits `routes_total` / `routes_truncated`, mirroring the entry-point
  plumbing. Truncation is measured against the pre-LIMIT `COUNT(*) OVER()`
  population read before the in-C test-path filter, so the test-path `continue`
  cannot spoof a false negative.
- **Re-exported bare and module-alias cross-crate CALLS** (`6fdb6efe`): a bare
  call of a symbol `pub use`-re-exported through a directory module now resolves
  via re-export-ancestor matching (`produce_show_frame_dot`), and a
  module-alias-qualified associated call `alias::fn()` resolves by rebuilding the
  import target from the aliased module (`ext_shape::append_event_sql`). Existing
  decoy rejections (target-package prefix, ambiguous source suffix, conflicting
  routes) still hold.
- **Single-level macro repetition binding/expansion** (`b0852424`): a
  `$(...)<sep><kind>` repetition binds each metavar's per-iteration values and
  expands the transcriber body once per iteration, so item-producing repetition
  macros emit every generated callable and the calls inside them
  (`impl_seq_addressed_transport_wire!` now yields all five `validate_decoded`
  methods).
- **Nested-repetition item macros expand (`string_enum!`)** (pending commit):
  five separate expander gaps blocked the `string_enum!` shape
  (`crates/pm-core/src/entity/string_enum.rs:45`), each independently fatal, all
  in the clean-room `macro_rules!` expander (`internal/cbm/lsp/rust_lsp.c`):
  (1) the `$vis:vis` fragment was unsupported, so the `pub enum` header never
  matched; (2) a separator-less outer `+` iterated once (the variant list
  separates via an inner `$(,)?`, not an outer sep); (3) a `$(` inside a
  repetition inner pattern was rolled back to a wildcard, dropping the whole
  variant list; (4) `$crate` was left literal, so `$crate::…` failed the item
  parse; (5) `macro_consume_fragment` sent a `:literal` string through the
  bracket-only balancer, which never advanced past the opening quote. The fix
  adds `vis` and string-literal fragment consumption, greedy separator-less
  outer iteration, a nested-`$(` matcher that consumes the span and seeds inner
  metavars as empty (so the transcriber drops them), `$crate`→`crate`
  substitution, and macro-expansion-time emission of the generated
  `enum`/`struct` type node. Verified by
  `rustlsp_macro_string_enum_nested_repetition_emits_enum_and_methods`
  (`tests/test_rust_lsp.c`): the generated `Status` enum node, its `token`
  method, and the `sink` call edge inside that method all appear. Full C suite
  green (7752 passed). Live-index reconfirmed on a from-source HEAD build
  (`e76c243b…`, compiled `TEST_SEAMS=1` and run against a private
  `CBM_TEST_DAEMON_RUNTIME_PARENT` namespace so the account-wide daemon of live
  MCP clients stayed untouched): a fresh full reindex of project-management now
  carries an `Enum` node for `DemoStatus`, the same-file `string_enum!`
  invocation in `string_enum.rs`. Scope limit: this fix covers only same-file
  invocations; cross-file `#[macro_export]` invocations remain unexpanded for a
  separate reason (see Open — cross-file macro visibility).

## Distribution hazard (not a code defect)

The binary a session actually runs can lag fork HEAD by a full fix wave. The
shipped DeusData release trailed the `viktorius007` fork, and the stale account
daemon kept serving the old in-memory image (`build=c0cc131b…`) even after fixes
landed in source — `daemon stop` refuses while committed MCP clients from other
sessions are live. An index built by the stale binary also cannot exercise a
graph-construction fix after the binary is swapped; a reindex is required.
`index_repository` alone is not enough: when git state is unchanged it takes the
incremental path and returns the cached graph as a sub-second no-op (identical
node/edge counts), so a graph-construction fix is not re-exercised. To verify one
on an unchanged tree, `delete_project` then `index_repository --mode full`, and
confirm the node/edge count moved. Before recording any documented fix as broken,
confirm the running binary carries it (`--version`, or grep its string table for
a fix marker) and that the graph under test was freshly built by that binary.
