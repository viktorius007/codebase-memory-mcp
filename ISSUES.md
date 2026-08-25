# ISSUES — open defects and gaps

This is the canonical backlog. Remove resolved items rather than retaining a
second history; commit history carries the old investigations.

## Open

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
