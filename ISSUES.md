# ISSUES — open defects and gaps

This is the canonical backlog. Remove resolved items rather than retaining a
second history; commit history carries the old investigations.

## Open

## Resolved during investigation

- `search_graph` degree accounting once omitted `OVERRIDE` edges. The mechanism was
  `cbm_store_search`'s two correlated edge-count subqueries: their `IN` lists produced
  the reported `in_deg` and `out_deg` columns, and `search_apply_degree_filter` then
  filtered those same aliases. Omitting `OVERRIDE` therefore both reported trait
  implementations as degree zero and admitted them through `max_degree=0`. The
  surgical resolution is to include `OVERRIDE` in both the inbound and outbound
  subqueries. That production change was already present in baseline commit
  `eb12be98`; the observed project graph came from a stale/pre-fix server build, while
  current HEAD already contained the surgical fix. The focused store test now also
  locks the filtered and reported results.

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
