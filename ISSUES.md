# ISSUES — open defects and gaps

This is the canonical backlog. Remove resolved items rather than retaining a
second history; commit history carries the old investigations.

Verified 2026-08-27 against a from-source HEAD build (`1f4e74e8`, version
`dev build=e95e7503…`) serving a fresh index of project-management. The prior
round's "still open" verdicts were measured against a stale DeusData release
binary (`build=c0cc131b…`) and its stale index; those are void. See the
distribution note at the bottom.

## Open

### `macro_rules!` nested-repetition and derive-driven callables still omit

Single-level `$(...)<sep><kind>` repetition now binds and expands per iteration
(see the fixed ledger), so `impl_seq_addressed_transport_wire!`
(`crates/pm-core/src/transport/mod.rs:701`) emits all five `validate_decoded`
methods and their `refuse_zero_seq_id` edges. Two macro shapes remain:

- **Nested repetition.** `string_enum!`
  (`crates/pm-core/src/entity/string_enum.rs:45`) uses a repetition inside a
  repetition (`$( $variant => $canonical $( | $alias )* )+`) plus meta- and
  vis-repetitions. The expander deliberately does not support a `$(` inside a
  repetition inner pattern (`rust_lsp.c` "Repetitions inside repetitions are NOT
  supported"); the matcher rolls that rule back to a wildcard, so the generated
  `enum` and its `Display`/`FromStr` are not emitted. This degrades cleanly (no
  false parse-failure health), but the callables are still absent.
- **Derive-generated methods.** `string_enum!`'s `token()` comes from
  `#[derive(::pm_entity_derive::PmStringEnum)]`, a proc-macro expanded by rustc,
  not by `macro_rules!`. It is out of scope for the transcriber expander and
  needs a separate derive-aware pass.

Calls made inside an unsupported macro body still have no caller node, matching
the long-standing "calls from inside a macro invocation body have no caller
node" gap for the shapes above.

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
  methods). Nested repetition stays unsupported (see Open).

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
