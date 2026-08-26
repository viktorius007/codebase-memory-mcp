# ISSUES — open defects and gaps

This is the canonical backlog. Remove resolved items rather than retaining a
second history; commit history carries the old investigations.

## Open

## Resolved during investigation

### Rust authority gaps discard valid local Cargo dependency imports

For a cross-crate Rust `use`, the pipeline's authority pass can fail to map the
source spelling to a graph qualified name when the graph includes workspace
package-directory segments. Directory `mod.rs` endpoints and public re-exports
through one exposed this mismatch in project-management. The pass wrote the
same authoritative empty string used for a private or ambiguous path. That
empty value suppressed the syntactic import, while the Rust LSP Cargo fallback
only handled scoped `crate::item()` calls, not an imported bare call.

Authority resolution now reports access denial separately from an unresolved
graph route. It preserves the exact source import only when there is one import
declaration, no visibility denial, and exactly one caller-scoped local Cargo
dependency route. The LSP then resolves that address within the routed target
package by a segment-bounded package prefix and the complete normalized source
suffix; zero or multiple matches remain unresolved. Regression controls cover
private/empty authority, conflicting routes, package-prefix decoys, and
same-suffix ambiguity, while the end-to-end fixture protects both the direct
`mod.rs` endpoint and its public re-export shape.

The original project-management graph also came from the stale account daemon
build `c0cc131b...`, while the worktree binary was `349ad6d...`. Replacing that
daemon and reindexing is still required for an existing graph to receive this
source fix.

### Rust `macro_rules!` item expansions omitted callable nodes and calls

Source-visible `macro_rules!` invocations that expand to item declarations such
as `impl` blocks or free functions produced neither callable graph nodes nor
joinable `CALLS` edges. The omission was not reported as partial coverage.

Root cause: `rust_expand_user_macro` wrapped every substituted transcriber in a
synthetic function and only walked that function body for calls. Module-scope
macro invocations terminated by `;` also arrive as `expression_statement`
nodes, which the item-list dispatcher ignored. Consequently generated items
never reached the callable-item processor, and recovered calls had neither a
generated caller definition nor a syntactic carrier.

Resolution: item-shaped transcribers are now parsed as source-file item lists;
their generated functions/methods are emitted at the invocation's source lines,
their bodies are resolved under those callable qualified names, and synthetic
call carriers make the semantic resolutions joinable by the graph pipeline.
Expression-statement macro invocations now enter macro expansion as well. The
focused Rust LSP regression asserts the generated node, exact resolved caller,
and the carrier-to-resolution join.

### `search_graph` degree accounting omitted `OVERRIDE`

The mechanism was `cbm_store_search`'s two correlated edge-count subqueries:
their `IN` lists produced the reported `in_deg` and `out_deg` columns, and
`search_apply_degree_filter` then filtered those same aliases. Omitting
`OVERRIDE` therefore both reported trait implementations as degree zero and
admitted them through `max_degree=0`.

The surgical resolution is to include `OVERRIDE` in both the inbound and
outbound subqueries. That production change was already present in baseline
commit `eb12be98`; the observed project graph came from a stale/pre-fix server
build, while current HEAD already contained the fix. The focused store test now
also locks the filtered and reported results.

### `get_architecture` hides entry-point truncation

Root cause: `arch_entry_points` in `src/store/store.c` applies `LIMIT 20` to its
only qualifying-node query. The store result therefore carries only the sliced
row count, and both MCP encodings present that count without an exact population
total or truncation signal. Large projects consequently make 20 returned entry
points indistinguishable from exactly 20 matches.

Surgical resolution: retain the bounded 20-row payload, derive the exact total
in the same SQLite snapshot with `COUNT(*) OVER()`, and carry `entry_point_total`
plus `entry_points_truncated` through the store result into both tree and JSON
MCP responses. A 21-entry store fixture pins `shown = 20`, `total = 21`, and
`truncated = true`; the existing two-entry fixture pins the non-truncated case.

### `query_graph` hid `max_rows` truncation

`query_graph` treated `max_rows` as an executor projection limit. The Cypher
projection freed every match beyond N before `handle_query_graph` received the
result, so N+1 matches and exactly N matches both serialized as `total: N` with
no completeness signal.

The handler now executes for one sentinel row beyond a positive `max_rows`,
serializes no more than N rows, and emits `truncated: true` only when that
sentinel proves additional matches exist. This preserves `total` as the
returned-row count while making the result's completeness truthful.

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
