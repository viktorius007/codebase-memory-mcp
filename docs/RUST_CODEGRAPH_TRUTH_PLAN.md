# Rust relationship accuracy: current state

Semantic index version: **8**. The broader Rust accuracy objective remains unfinished.
The current supported subset and its verification are recorded in
[RUST_REPAIR_EVIDENCE.md](RUST_REPAIR_EVIDENCE.md). Commit history owns prior plans,
investigation transcripts, intermediate results and replay history.

## Supported behavior

- Import extraction and both Rust resolver entry points share the AST use-tree
  expander. Nested groups, aliases, grouped self, globs and comments retain exact
  paths and bindings.
- Supported test attributes are classified by path, not documentation substrings.
  A compact attribute marker survives large documentation and is shared by both
  definition serializers. Attributed tests may have arbitrary names; unannotated
  helpers do not qualify solely by being in a test file.
- HashMap and HashSet retain their std owners; BTreeMap retains its alloc owner.
- Both publication modes admit exact, source-site-matched bare calls between
  file-local free functions. Missing Rust semantic targets are terminal:
  project-prefix, leaf-name and generic registry fallbacks cannot invent an edge.
- Lexical imports and local value declarations prevent the tested shadowed names
  from binding to outer functions. Active glob scopes conservatively leave bare
  calls unresolved. The project-wide unique-short-name fallback is removed.
- Live CALLS and derived TESTS are restored for this bounded subset. Current-epoch
  CALLS/TESTS survive both incremental restoration routes. IMPLEMENTS/OVERRIDE
  remain rejected.
- Version 8 invalidates earlier semantic indexes. All three Rust partial-coverage
  flags remain set. An omitted relationship is not proof that none exists.

Methods, cross-file calls, expanded calls and implementation relationships are not
restored. Exact spans alone cannot validate an incorrectly inferred target.

## Remaining valuable work, in order

1. **Implementation identity.** Preserve trait arguments and receiver identity
   through extraction, caller naming, registries and linking. Distinguish two
   same-named trait methods and distinct From<T> implementations on one receiver
   before admitting methods or IMPLEMENTS/OVERRIDE.
2. **Cargo, module and lexical scope.** Derive per-member crate roots, module
   paths, renamed dependencies and import scope from actual workspace inputs.
   Distinguish same-leaf symbols in different modules/crates. Repair semantic
   target joins before widening cross-file call admission.
3. **Wrapper-type propagation.** Trace the first lost type in
   Option<Rc<RefCell<dyn Trait>>> through clone/borrow/method calls. Bind the static
   call to the trait item; report possible concrete dispatch separately.
4. **Library seeds and prelude.** Independently check additional API-validity leads:
   HashMap.raw_entry, core.slice.windows_mut and Iterator.tuple_windows. Verify
   prelude membership and local shadowing. Do not bless a seed by copying it into
   a test expectation.
5. **Corpus parity and performance.** Compare full and edit-triggered incremental
   indexes, exact call-site targets, trace/cycle/impact/test answers and non-Rust
   controls. Measure warm no-change and representative edit-to-query latency with
   builds idle. The reported 1–2 second warm latency is not a measured guarantee.
6. **cfg and macros.** Validate configuration-dependent and expanded identities
   before broadening semantic completeness claims.

## Corpus and implementation constraints

Read-only corpus: /Users/viktor/Projects/project-management. Recheck its revision
before measuring; old edge counts are not a current reproduction.

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
