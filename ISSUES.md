# ISSUES — open defects and gaps

This is the canonical backlog. Remove resolved items rather than retaining a
second history; commit history carries the old investigations.

## Open

No confirmed product defect remains from the 2026-07-29 P0/P1/P2 audit.

## Resolved in the current local worktree

- Structural `File`/`Module` CALLS rows no longer inflate caller/callee totals.
- Structural spill ownership leak is removed.
- Callable, structural, and port-mediated trace rows share one page budget and
  cursor stream.
- Both trace directions use distinct JSON keys; no duplicate last-wins keys.
- Cursor identity includes `edge_types`.
- Structural risk-label output matches the selected trace shape.
- Port discovery and BFS size caps fail loudly instead of silently omitting the
  ninth port or rows beyond an internal cap.
- `is_test` node metadata takes precedence over file-path heuristics, including
  inline Rust tests.
- Data-flow args come from the exact deterministic predecessor edge.
- The unused `parameter_name` contract was removed and explicitly rejected.
- Rust cfg names are balanced, collision-free, and consistent across extraction
  paths.
- Nested Rust scopes and trait default methods own their calls.
- Weak Rust receiver guesses and non-callable CALLS targets are rejected.
- Module-relative Rust type paths resolve across files without an O(types)
  fallback, restoring trait-method dispatch at large-repository scale.
- C++ `operator[]` calls are extracted from subscript expressions.
- macOS CLI fingerprinting no longer dominates cold and warm invocation time.
- Unexpected killed index workers enter bounded crash recovery; cancellations
  remain terminal.
- macOS crash and executable-image replacement fixtures no longer strand
  uninterruptible processes, and the full clean test gate completes.

## Confirmed behavior

- Ambiguous symbols return an ambiguity result rather than a guessed node.
- Positional JSON and flag-based CLI forms both work; raw positional JSON is
  deprecated but still accepted.
- `edge_types` reaches the traversal. OVERRIDE edges run implementation → trait,
  so an inbound walk from an implementation does not cross its outbound
  OVERRIDE edge.
- Module CALLS edges can be real: Rust static/const initializers and top-level
  Python/shell bodies execute outside a callable. They remain in the graph as
  structural evidence and are intentionally reported separately from callers.
