# Rust relationship accuracy: current state

Semantic index version: **10**. The broader Rust accuracy objective remains unfinished.
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
- Both publication modes admit exact, source-site-matched `::`-qualified calls
  from file-local free functions to the unique free function inside the Cargo
  workspace member the source path names. Ambiguous member matches, undeclared
  heads and missing manifests emit nothing. A bare call with an exact lexical
  import into that member follows the same fail-closed route.
- Lexical imports and local value declarations prevent the tested shadowed names
  from binding to outer functions. Active glob scopes conservatively leave bare
  calls unresolved. The project-wide unique-short-name fallback is removed.
- Live CALLS and derived TESTS are restored for this bounded subset. Current-epoch
  CALLS/TESTS survive both incremental restoration routes. IMPLEMENTS/OVERRIDE
  remain rejected.
- Version 10 invalidates earlier semantic indexes. All three Rust partial-coverage
  flags remain set. An omitted relationship is not proof that none exists.

Methods, other import-mediated cross-file calls, expanded calls and implementation
relationships are not restored. Exact spans alone cannot validate an incorrectly
inferred target.

## Remaining valuable work, in order

Method, proven on the cross-crate class (epoch 9): pin the missing capability
red on a branch; widen `cbm_pipeline_plain_call_admitted`
(src/pipeline/lsp_resolve.h) by exactly one named class; bump the semantic
epoch together with tests/semantic-epoch.expected; gate on `make scip-rust`
(fabricated=0 is unconditional) and `make mutation-rust`.

1. **Implementation identity.** Preserve trait arguments and receiver identity
   through extraction, caller naming, registries and linking. Distinguish two
   same-named trait methods and distinct From<T> implementations on one receiver
   before admitting methods or IMPLEMENTS/OVERRIDE. The admission predicate's
   shared floor already isolates the caller-side check, so the method-caller
   widening itself is small once identity is trustworthy.

   The first incorrect transition is extraction, before any cross-file layer can
   recover the distinction. `extract_rust_impl` currently strips generic arguments
   from both the receiver (`Wrapper<T>` to `Wrapper`) and the implemented trait
   (`From<Feet>` to `From`), then names every method only as
   `<receiver-qualified-name>.<method>`. Two legal implementations such as
   `From<Feet> for Meters` and `From<Inches> for Meters` therefore produce the same
   `Meters.from` definition and the same `impl_trait = "From"` inside one
   `CBMFileResult`. The later `receiver_type` and `trait_qn` fields have the right
   conceptual roles but only receive this already-lossy data.

   Qualified names are persisted graph identity, not presentation: the in-memory
   graph keys nodes by qualified name and SQLite enforces uniqueness on
   `(project, qualified_name)`. Publishing a richer method identity is consequently
   an epoch-changing graph migration. Compact/spill handling already carries the
   existing `CBMDefinition.impl_trait` and `CBMImplTrait` strings; any new identity
   field must be added to that walker explicitly.

   Keep the repair serial and atomic. No later layer begins until the focused tests
   for the preceding layer are green:

   1. Pin extraction loss directly with one fixture containing distinct
      `From<Feet>` and `From<Inches>` implementations on `Meters`. Assert the two
      exact trait spellings on both the impl records and their method definitions;
      do not use graph counts or another proxy as the oracle.
   2. Preserve the exact trait spelling through extraction only. Do not change
      method qualified names, registries, publication, admission or the semantic
      epoch in this step.
   3. Preserve exact receiver spelling while retaining the declaration-owner
      identity needed to find the underlying type. Prove the extraction and
      compact/spill round trip before registry changes.
   4. Define one canonical internal implementation identity from receiver plus
      instantiated trait. Use it in Rust registry lookup and prove exact selection
      plus fail-closed ambiguity for same-named methods. The Rust trait-method lookup
      and the generic resolved-call occurrence join are separate ambiguity gates;
      neither substitutes for the other.
   5. Propagate that identity into resolved caller/callee naming without yet
      admitting method `CALLS`.
   6. Publish distinct method nodes and implementation relationships identically in
      sequential and parallel modes. Bump the semantic epoch at this first persisted
      identity change, then prove both incremental restoration routes.
   7. Widen `cbm_pipeline_plain_call_admitted` for exactly the now-trustworthy method
      class. Keep missing, name-only and ambiguous targets closed.

   Prefer the existing extraction seam (`cbm_extract_file` in the focused extraction
   suite) over a standalone probe: it observes the direct mechanism without
   duplicating the production parser/link setup. Each test must carry an exact
   mechanism-level oracle, a fixture discriminator, and recorded red-before-green
   evidence. After every production edit, run its focused suite before adding the
   next layer; the final repair still owes both publication modes, both restoration
   routes, the Rust SCIP fabricated-zero gate, the Rust mutation lane and the
   semantic-epoch contract.

   Trait-implementation method nodes use the published qualified-name form
   `<receiver-qn>.<method>[<instantiated-trait>]`, for example
   `crate.units.Meters.from[From<Feet>]`. The bracketed suffix keeps the ordinary
   dot-delimited receiver and method prefix intact while carrying the exact trait
   spelling already preserved by extraction. Rust paths inside the suffix use `::`,
   not `.`, so last-dot consumers continue to see the complete method leaf. The
   colon-aware `pxc_qn_leaf` helper is confined to the separate receiver-level
   `RustImpl` record and does not parse method qualified names. The two call-join
   consumers that compare or index method leaves explicitly remove the bracketed
   provenance before matching it to source spelling.
2. **Cargo, module and lexical scope.** Derive per-member crate roots, module
   paths, renamed dependencies and import scope from actual workspace inputs.
   Distinguish same-leaf symbols in different modules/crates. Repair semantic
   target joins before widening cross-file call admission. Package names that
   differ from their directory basename currently miss the member-segment join.
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
