# Rust relationship accuracy: current state

Semantic index version: **13**. The broader Rust accuracy objective remains unfinished.
The current supported subset, its measurement against rust-analyzer and its
verification are recorded in [RUST_REPAIR_EVIDENCE.md](RUST_REPAIR_EVIDENCE.md).
Commit history owns prior plans, investigation transcripts and replay history.

## Supported behavior

- Import extraction and both Rust resolver entry points share the AST use-tree
  expander. Nested groups, aliases, grouped self, globs and comments retain exact
  paths and bindings.
- Supported test attributes are classified by path, not documentation substrings.
- HashMap and HashSet retain their std owners; BTreeMap retains its alloc owner.
- Module paths map onto file-derived qualified names: `crate::` resolves to the
  crate's `<project>.<member dirs>.<src|tests|benches|examples>` prefix,
  `super::` to the parent directory (`mod.rs`, `lib.rs` and `main.rs` stand for
  their directory), a `mod child;` head to the child file, and a workspace crate
  name (`codex_core::x`, matched against each member manifest's package and lib
  names) to that member's prefix. Every mapped name is probed against the
  registry in the layouts a path can denote (`a/b.rs`, `a/b/mod.rs`, `lib.rs`,
  `main.rs`, an inline `mod`, a `#[cfg]` twin) and resolves only when exactly one
  registered item answers.
- A name that no file declares is followed through that file's own `use`
  (`pub use inner::Item;` re-exports, and a parent module's imports seen
  through `super::*`) using a pipeline-wide table of raw Rust import paths.
- Glob imports no longer close a file: an item of the current module shadows
  every glob, an explicit `use` is next, and a bare name is bound through a glob
  only when exactly one glob source defines it.
- Struct fields reach the shared registry, and a receiver type spelled in another
  file's module is recovered through this file's imports, module, globs, or a
  unique same-crate type of that name, so `self.field.method()` and
  let-bound method results resolve across files. `Arc<dyn Trait>` and the other
  std smart pointers dispatch to the trait method through the deref chain.
- Call sites inside `impl Trait for Type` methods and `#[cfg]`-gated free
  functions carry the published node identity (`Type.method[Trait]`,
  `name#cfg(...)]`), so their calls attach to the method or function node instead
  of the File node. A `let x = x(..)` initializer resolves before its binding
  shadows the callee.
- Both publication modes admit an exact, source-site-matched call from any
  function or method declared in the file to an existing graph node when the
  resolver strategy is one measured at 98% or better site precision against
  rust-analyzer on the codex-rs corpus: `lsp_direct`, `lsp_import_alias`,
  `lsp_glob_import`, `lsp_cross_crate`, `lsp_method_dispatch`,
  `lsp_trait_dispatch`, `lsp_deref_dispatch`, `lsp_constructor`, `lsp_ufcs`,
  `lsp_operator_trait`. `Type::trait_method()` on a concrete type is `lsp_ufcs`;
  `Trait::method()` on a trait head (`Default::default()`, 66% measured) stays
  `lsp_trait_ufcs` and closed, as do prelude guesses, macro carriers, bound
  dispatch and callable aliases. Missing, name-only and ambiguous targets emit
  nothing.
- Version 13 invalidates earlier semantic indexes. All three Rust partial-coverage
  flags remain set. An omitted relationship is not proof that none exists.

## Remaining valuable work, in order

Method, proven on every class above: index the corpus with
`CBM_RUST_RESOLVE_DUMP`, score it with `scripts/scip-site-join.py`, take the
largest miss class, find its first incorrect transition, fix it, re-score; gate on
`make scip-rust` (fabricated=0 is unconditional), the focused suites, and the
semantic-epoch contract. Site-level recall on internal function calls in
codex-rs stands at 43.6% (rust-analyzer 1.98.1, 2026-09-25); the remaining
misses, largest first:

1. **Receiver typing.** 44k internal call sites have a receiver whose type the
   resolver cannot infer: locals bound from external-crate calls, iterator and
   `Result` chains, generic parameters, closure parameters, tuple and pattern
   bindings, `Arc<dyn Trait>` fields of cross-crate traits. Each needs the
   first lost type traced through `rust_eval_expr_type`.
2. **Associated functions on unrecovered types.** 19k `Type::assoc()` sites where
   the type head itself does not resolve (module trees mounted with `#[path]`,
   `mod tests;` siblings named `*_tests.rs`, macro-generated types, external
   re-export chains longer than three hops).
3. **Methods missing on known receivers.** 14k sites where the receiver resolves
   but the method is not registered under it: trait methods provided through
   blanket impls or supertraits, methods behind type aliases, cfg-gated impls.
4. **Free functions through `#[path]` module trees and integration-test crate
   roots.** 11k bare calls whose module identity is not file-derived.
5. **Calls without a parsed carrier.** 3k `Default::default()`-style sites need
   expected-type inference; 2k same-file sites sit in attributed statements the
   walker skips.
6. **IMPLEMENTS/OVERRIDE, expanded (macro) calls, dyn dispatch fan-out, corpus
   parity and performance, cfg evaluation.** Unchanged from the prior plan;
   IMPORTS edges for Rust are still produced by the generic name matcher and
   contain fabricated targets (a `Pin` import bound to an unrelated `pin` method),
   so they must not be used as a resolution source.

## Corpus and implementation constraints

Read-only corpora: /Users/viktor/Projects/project-management and
/Users/viktor/Projects/github/codex/codex-rs (156 workspace members, 4,698 Rust
files). Recheck their revisions before measuring; old edge counts are not a
current reproduction. Measure a copy of the corpus, never the live checkout.

Useful cases are encode_hex and From<WriterIdentity> for String in
crates/pm-port/src/writer_identity.rs; the private clone helper in
crates/git-safety/src/db_path.rs; RepositoryContinuityPort in
crates/pm-cli/src/repository_continuity.rs; and the wrapped trait in
crates/pm-cli-grammar/src/context.rs.

Retain tree-sitter and the incremental architecture. Syntax preserves the relevant
alias, trait-argument, receiver and wrapper distinctions; it does not establish
bindings or inferred types. Use rustc/rust-analyzer as independent development-time
oracles where useful. Runtime SCIP ingestion is not a selected design.

For each repair, identify the first incorrect transition from syntax through
extraction, inferred type, binding, graph lookup and publication. Require positive
exact-target controls alongside refusals in both publication modes. Update restore,
migration and coverage policies consistently.

Use private runtime/cache directories for tests and corpus experiments. Do not alter
the live corpus, its default index or a shared daemon. Source verification does not
establish that the installed tool or existing indexes contain these changes;
verify the build identity and reindex before relying on the repaired behavior.
