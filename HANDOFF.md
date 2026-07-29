# HANDOFF — correctness mission

Repository: `/Users/viktor/Projects/github/codebase-memory-mcp`, branch `main`.
Baseline: `b8ffd3e1` (`origin/main`). The fixes described here are local,
uncommitted, and unpushed.

## Outcome

The P0/P1/P2 backlog recorded in the previous handoff and `ISSUES.md` has been
implemented:

- `trace_path` now partitions structural `File` and `Module` CALLS sources out
  of callable totals, frees them safely, and puts callable, structural, and
  port-mediated rows into one deterministic page/cursor budget.
- Directional JSON keys prevent the prior duplicate-key case. Cursor identity
  includes `edge_types`. Unknown modes, formats, and malformed edge-type arrays
  fail loudly.
- Rust cfg-qualified names are balanced and shared by definition, call, and LSP
  extraction. Nested functions and trait default methods retain their callable
  owner.
- Rust receiver resolution no longer guesses across explicit owner mismatches,
  ambiguous fuzzy matches, or Cargo-package boundaries. `CALLS` cannot target
  structural fields or variables.
- Module-relative Rust receiver types resolve through a hashed crate-root
  lookup, so cross-module trait dispatch works without an O(types) fallback on
  large repositories.
- C++ subscript expressions emit `operator[]` calls for normal LSP resolution.
- Data-flow argument display follows the exact predecessor edge and parses its
  JSON rather than scanning an arbitrary incident edge.
- The advertised but unimplemented `parameter_name` trace argument was removed;
  older callers supplying it receive an explicit error.
- CLI daemon fingerprinting preserves full executable-image integrity while
  avoiding the multi-second byte-at-a-time digest path on macOS.
- Supervised indexing treats an unexpected worker kill as a recoverable crash,
  while cancellation remains a non-retryable terminal state.
- Intentional macOS crash tests use a reliably terminating worker signal, and
  the executable-replacement fingerprint regression uses a test-only path
  seam instead of mutating a live macOS process image.

## Real-repository acceptance

A fresh full index of `/Users/viktor/Projects/agent` produced:

- 9,131 nodes and 32,048 edges at the first post-fix evidence run; the final
  database contains 2,244 Functions, 793 Methods, 747 Files, and 711 Modules.
- CALLS sources: 5,104 Function, 511 Method, 75 Module, **0 File**.
- CALLS targets labelled Field/Variable/File/Module: **0**.
- malformed cfg qualified names: **0**.

The 75 Module edges are genuine top-level execution: Rust `const`/`static`
initializers and Python/shell module bodies. They remain as evidence but do not
inflate callable totals. The original `error_body_truncated_message` repro now
returns `callers_total: 0` and one separately labelled
`unattributed_inbound` initializer row.

The index reported seven best-effort parse-partial files, all documentation,
patch, or script artifacts; it skipped no source files.

## Verification

The canonical clean gate, `scripts/test.sh`, completed successfully on macOS
arm64:

- 6,883 sanitizer tests passed, 0 failed, 4 intentional platform skips across
  all 121 suites.
- The focused Rust/C++ regression gate passed all 635 tests.
- The production binary built cleanly under `-Wall -Wextra -Werror`.
- Parent-death and worker/descendant watchdog regressions passed.
- Security-string allow-list regressions passed.

An independent adversarial review found two additional P1 paths: a null
dereference while cleaning up a failed port-page allocation, and a per-port
rather than aggregate 5,000-row dynamic-dispatch ceiling. Both are fixed; the
new aggregate-ceiling regression passes.

## Workspace state

Do not touch or push `/Users/viktor/Projects/agent`. At final observation it was
clean and 55 commits ahead of its upstream; the earlier TEMP probe commits were
no longer at its tip.

No external state was changed. Nothing was pushed, published, or deployed.
