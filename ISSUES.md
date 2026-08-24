# ISSUES — open defects and gaps

This is the canonical backlog. Remove resolved items rather than retaining a
second history; commit history carries the old investigations.

## Open

- `trace_path` and the CALLS/USAGE edge set miss callers reached through a
  fn-pointer table or `dyn` dispatch. Observed 2026-08-24 in the
  project-management repo: a control probe showed 0 callers for a symbol with
  a plain direct call while a sibling in the same generation resolved; methods
  reached only through `dyn` (`run_from_matches` x8) or a fn-pointer table
  (xtask `which_sccache` family) have no inbound edge at all, so any
  zero-caller list built from the graph reports them as dead. Candidate fix:
  emit a USAGE edge for each fn-pointer-table entry and for trait-method
  implementations reachable through `dyn`. Until then consumers must re-probe
  zero-caller rows by grep before acting on them.

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
