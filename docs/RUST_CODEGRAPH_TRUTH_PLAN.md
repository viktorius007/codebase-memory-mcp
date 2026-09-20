# Rust relationship accuracy and incremental performance

Updated 2026-09-20. Investigation baseline: `bbe753d30cd55babec4a8cce38692dadb97c1d40`.
This is the current task reference and next-session handoff. It supersedes the prior
R0–R8 suppression plan, its temporary run ledger, and saved agent briefs. The completed
repair stack and gates are recorded below. No installation or upstream publication is
part of this session. The session scope remains progressive repair of incorrect Rust
scanning and analysis, including newly reproduced defects beyond the initial findings.

## Resume here

0. Finish the **outstanding full repository gate** described below before declaring the
   stack fully verified or installing it. It was interrupted, not passed. This is new
   validation work; the completed investigations and targeted evidence remain reusable.
1. Reuse the committed regression/compiler evidence in
   [RUST_REPAIR_EVIDENCE.md](RUST_REPAIR_EVIDENCE.md). Do not repeat the raw-parser audit,
   use-tree investigation, attribute redesign or library-owner diagnosis on unchanged
   source. The gate ledger below identifies the verified trees. Upstream merges or
   changes to affected code require appropriate fresh validation, not a new investigation.
2. Next implement F1/F2: replay `encode_hex`, private free `clone`, and a real local-call
   control through resolution and both publication modes. The first confirmed source
   boundaries are `resolve_single_call`, `resolve_file_calls`,
   `rust_resolve_call_expression_inner`, and `cbm_pipeline_plain_call_admitted`.
   Instrumented end-to-end replays of these cases are still outstanding.
3. Repair terminal semantic-target handling and receiver/reachability rules before
   enabling positive Rust CALLS. A source-site-exact join cannot validate a target
   guessed upstream by `lsp_short_name_unique` or sole-implementation trait dispatch.
   Change publication, incremental restore and coverage disclosure consistently.
4. Then address impl identity, Cargo/module/import scope and wrapper type propagation,
   with positive and negative exact-target controls. The ordered worklist below owns
   details. Macro/cfg semantics and the additional stdlib API leads remain unresolved.
5. Measure full/incremental parity and warm update performance on an isolated index of
   the recorded corpus with builds idle. The historical 1–2 second latency is still an
   owner report, not a measurement from these repairs.

## Commit stack and upstream replay

| Order | Commit | Scope | Semantic version |
|---|---|---|---|
| 1 | `94a0874789b9decd09f645005a6917d16f769532` | Shared serializer only; prerequisite for attribute storage | 4 (unchanged) |
| 2 | `77f76c6a2ba38426fc39a193fcb5fcfc87d72805` | Exact Rust imports and local/cross resolver tests | 5 |
| 3 | `db6a47bbc05bc3830333947c90568244ebab35e4` | Attribute identity, compact marker and TESTS tests | 6 |
| 4 | `5f439852a724ecbb4b4a2dbfe53af31772dec446` | HashMap/HashSet owners and exact-target tests | 7 |

Baseline: `bbe753d30cd55babec4a8cce38692dadb97c1d40`. The documentation commit
following `5f439852` records this ledger. The stack is retained on local branch
`codex/rust-analysis-repairs` and applied to `main`. Nothing was pushed or installed.


Keep the logical patches separate. The serializer refactor is a prerequisite for the
test-attribute commit; imports and collection-owner fixes are otherwise separate causal
changes. Each behavior commit advances the semantic index version (5, 6, then 7) and
carries its regression tests. Intermediate versions are intentional: checking out or
applying any fix must invalidate the preceding index. Version 7 is the final stack value.

For an upstream sync, preserve this local branch, review upstream changes, then replay
only unapplied commits in the recorded order on the new upstream base. Do not blindly
resolve version conflicts by retaining 5/6/7: choose versions newer than upstream's
semantic epoch while preserving each applied behavior change's invalidation. If upstream
has an equivalent fix, drop that patch only after checking the exact regression behavior;
if it already shares the serializer, adapt the attribute patch to that owner. Avoid
reformatting surrounding files or squashing the three unrelated Rust fixes together.
The final documentation commit can remain local when submitting individual fixes upstream.
No remote fetch/rebase/push was performed merely to create this local stack.

To export the code stack, use
`git format-patch bbe753d3..5f439852` (the four code commits above). Replay on a branch/worktree; do not overwrite unrelated local edits.
The full per-commit gates were not completed: the owner explicitly requested committing
the reviewed changes and closing the session without further waiting. After an upstream
merge, validate the merged tree rather than claiming an old run verifies new source.

| Check | Result and scope |
|---|---|
| Targeted ASan/UBSan | **1,252 passed** on the complete behavioral diff before packaging, at semantic version 5. All six new regression fixtures and their executed discriminators are detailed in RUST_REPAIR_EVIDENCE.md. |
| Final behavioral diff static checks | Diff-scoped clang-tidy, formatting and `git diff --check` passed before packaging. Generated/LSP files follow the repository's lint target selection; this is not a whole-repository clang-tidy claim. |
| Independent source and split review | No remaining blocking finding in the scoped repair reviews. The pure refactor preserves serialization; each later snapshot has its declarations, consumers and tests. Final snapshot matches the reviewed source except per-fix versions 5/6/7 and a generalized header comment. |
| Isolated refactor gate | `make -j3 -f Makefile.cbm lint-ci lint-tidy-diff` and `scripts/security-audit.sh` passed on the staged tree committed as `94a08747`. |
| Full canonical test gate | **Incomplete.** First attempt stopped when the sandbox denied the local smoke fixture server's bind (`PermissionError`, errno 1). Authorized unsandboxed rerun progressed through contract checks, then was deliberately terminated (exit 143) on the owner's request to commit and close. No full-suite pass is claimed. |
| Intermediate/final packaged runtime checks | Not completed separately. Epoch 7 and each intermediate commit were not subjected to a new full runtime gate. No failing Rust repair regression was observed; this is an outstanding verification obligation. |
| Mutation, installation, corpus/performance | Not performed for this repair stack. The installed MCP and default corpus index must not be assumed to contain these changes. |

Temporary packaging logs: `/private/tmp/cbm-rust-repair-evidence/commit-stack/`.
`stage-1-lint.log`, `stage-1-security.log`, `stage-1-test-sandbox.log` and
`stage-1-test.log` distinguish passed checks, environment denial and interrupted work.
They are supplementary to this durable verdict and the retained evidence document.
Before calling this stack fully gated or installing/releasing it, complete the canonical
full test gate and applicable linters on the current tree with local fixture sockets
permitted and the isolated runtime/cache wrapper. Do not repeat already-settled source
investigations merely because temporary logs have expired.


## Required outcome

- Return correct, useful Rust callers/callees, trait implementations, override and
  test relationships; derive reliable cycles and change-impact results from them.
- Preserve the fast incremental workflow. The owner reports warm updates take about
  1–2 seconds; measure a comparable baseline before repairs. This is an owner-reported
  observation, not a benchmark performed here or a universal latency guarantee.
- Require both correct positive relationships and absence of incorrect relationships.
  Blanket Rust omission, even with a partial-coverage warning, fails the outcome.
- Distinguish an established absence from an unresolved result. Keep static trait-item
  binding, possible concrete dispatch targets, and observed runtime calls distinct.
- Trace wrong results to lost/misused information. A suppression condition explains
  today's missing output but does not diagnose the original resolution failure.
- Use the smallest causal repairs. Other ISSUES.md items and broad redesigns are not
  automatically included in this Rust investigation.

## Architecture and evidence decisions

- Retain tree-sitter and the existing fast incremental architecture while investigating
  targeted repairs. Runtime rust-analyzer/SCIP integration is not a selected solution.
- Use rustc/rust-analyzer as development-time comparison sources where useful. Repeated
  full SCIP generation on the normal update path is unacceptable: the owner reports
  it takes over a minute. No evidence yet proves a replacement analyser is necessary.
- Source files establish the text. Tree-sitter supplies syntax, not symbol binding or
  inferred types. Rust semantics under the selected Cargo/build configuration govern
  the expected answers; record that configuration for independent comparison results.
- Do not require SCIP or compiler-origin metadata merely to admit a correct relationship
  from a repaired local resolver. Demonstrate its resolution rules against independent
  expected results. A confidence score or matching leaf name alone is not proof.
- SCIP is not a complete Rust call graph. The exporter inspected on 2026-09-20 emits
  symbol occurrences but empty symbol relationships; calls need separate classification.
  See the [upstream exporter](https://github.com/rust-lang/rust-analyzer/blob/master/crates/rust-analyzer/src/cli/scip.rs).
- Parsing success on three files does not prove complete grammar coverage or correct
  semantics. Read [the actual parser evidence](RUST_PARSER_EVIDENCE.md) before attributing
  a failure to tree-sitter or claiming that it resolves calls.

## Load-bearing findings

Paths and line anchors below refer to the baseline; locate the named function after edits.
"Source-confirmed" establishes the mechanism, not an instrumented replay of every old edge.

| ID | Finding and consequence | Evidence / next discriminator |
|---|---|---|
| F1 | Source-confirmed: a resolved target absent from the graph can fall through into generic local name matching. Known external and unresolved targets are not preserved as distinct terminal outcomes. | `src/pipeline/pass_calls.c:490`, call resolution/target lookup; library entries for `String.push` and `str.len` in `internal/cbm/lsp/generated/rust_stdlib_data.c:178`. Replay `encode_hex` and record the resolved target before graph lookup and after fallback. |
| F2 | Source-confirmed: generic fallback receives name/module/imports, not receiver type, signature or caller Cargo context. Same-module suffix matching can select a free function for a method call. Import unreachability can lower confidence instead of excluding a target. | `src/pipeline/registry.c:815` (`resolve_same_module`), `:1010` (`resolve_name_lookup`), `:1064` (`registry_resolve_chain`); Rust method syntax is not assigned the generic `is_method` flag in `internal/cbm/extract_calls.c:3655`. Replay private `clone` and generic-store method examples. |
| F3 | Source-confirmed: impl extraction strips generic arguments from trait and receiver names, then names methods by receiver + method. Distinct impl identities can collapse. | `internal/cbm/extract_defs.c:5141`, `extract_rust_impl`; parser evidence preserves `From<WriterIdentity>` separately from `String`. Compare two same-named trait methods on one type and two `From<T>` impls. |
| F4 | Source-confirmed: two import representations disagree. General Rust import extraction stores grouped/aliased use text as one path; the private Rust resolver expands aliases. Semantic linking consumes the former and drops imports without a matching graph node. | `internal/cbm/extract_imports.c:590`, `parse_rust_imports`; `internal/cbm/lsp/rust_lsp.c:5399`, `rust_collect_uses`; `src/pipeline/pass_semantic.c:87`, `build_import_map`. Replay `RepositoryContinuityPort`; raw tree explicitly contains its alias. |
| F5 | Source-confirmed information gap: standard-library `Rc::clone`, `RefCell::borrow`, and `borrow_mut` signatures have unknown returns. Expression typing cannot reliably preserve the wrapped trait type. | `internal/cbm/lsp/generated/rust_stdlib_data.c:331`; `internal/cbm/lsp/rust_lsp.c:1911` and `rust_eval_member_access`. The first loss in the historical full trait-object chain has NOT been instrumented; distinguish this candidate cause from a replay-proved attribution. |
| F6 | Source-confirmed: crate-root resolution assumes dotted path positions; manifest loading reads the repository-root Cargo.toml. These do not establish per-member crate/module/dependency identity. | `internal/cbm/lsp/rust_lsp.c:633`, `rust_resolve_path_expr`; `src/pipeline/pass_lsp_cross.c:1523`, `cbm_pxc_build_rust_manifest`; `internal/cbm/lsp/rust_cargo.h`. Trace `crate::`, renamed dependencies and nested workspace modules against actual Cargo metadata. |
| F7 | Source-confirmed: TESTS derives from CALLS, then additionally requires a test-like function name even when `is_test` is true. Correct calls alone will not restore every Rust test link. | `src/pipeline/pass_tests.c:211`, `create_tests_edges`, and `:115`, `cbm_is_test_func_name`. Compare an attributed test named `rejects_empty_input` with a `test_`-prefixed equivalent. |

## Concrete corpus and outstanding work

Read-only live corpus: `/Users/viktor/Projects/project-management`, observed HEAD
`e8a08ab4c97cf835a2735d7011794f7dd1c586de`. Original issue counts belong to earlier
recorded revisions; do not describe them as freshly reproduced at this HEAD.

| Case | Source / expected distinction |
|---|---|
| `encode_hex` | `crates/pm-port/src/writer_identity.rs`: `encoded.push`, `value.len`, `char::from`; preserve library targets instead of unrelated project methods. |
| Private `clone` | `crates/git-safety/src/db_path.rs`: private free helper previously credited with 747 callers; reject unrelated `.clone()` calls while retaining its real callers. |
| Trait impl identity | `From<WriterIdentity> for String` in writer_identity.rs; retain trait argument and receiver identity. |
| Aliased implementation | `crates/pm-cli/src/repository_continuity.rs`: `RepositoryContinuityPort` aliases `pm_cli_grammar::context::RepositoryContinuity`. |
| Wrapped trait call | `crates/pm-cli-grammar/src/context.rs`: `Option<Rc<RefCell<dyn RepositoryContinuity>>>`, then clone/borrow/method calls. Attribute the static call to the trait item. |
| False cycles | Generic-store method calls in `pm-usecase` matched same-module free functions; library `File::open` matched an unrelated crate's method. Use ISSUES.md for exact original observations. |

1. Replay selected cases through raw tree → extracted records → receiver type → selected
   binding → graph-node lookup → fallback → published edge. Record the first incorrect
   transition and the actual source/binary versions. Isolated diagnostics may inspect
   candidates behind the suppression rule; removing the rule alone is not a repair.
2. Establish positive and negative expected results independently. Compare exact symbol
   identities and call sites, not only leaf names, counts, or absence of old bad edges.
3. Measure warm no-change and representative edit-to-query updates on the same corpus,
   hardware, configuration and cache state before/after. Separate cold indexing from
   incremental work. Report latency distribution, work invalidated and memory use;
   parser-only speed is not index-update speed.
4. Implement bounded causal repairs under the current session authorization. Preserve
   real relationships while preventing the recorded false matches. Do not weaken positive
   expectations to make suppression pass.
5. Verify sequential/parallel publication, full/incremental parity, old-index migration,
   dependent trace/cycle/impact/test answers and non-Rust controls. Change the suppression
   and restore policies consistently as verified Rust capabilities return.

## Active repair sequence

1. **Import syntax (F4):** share one AST use-tree expander between extraction and both
   Rust resolver entry points. Preserve each complete path, local alias, grouped `self`,
   nested group and glob. Assert exact output pairs and exact resolved call identities.
   Follow with lexical import scopes and Cargo-aware target joins; syntax repair alone
   does not establish correct trait linking.
2. **Test relationships (F7):** distinguish an attributed Rust test entry point from a
   helper merely located in a test file. Consume the stored attribute data and share
   attribute classification with definition extraction. Check attributed/unattributed
   functions with arbitrary names, test helpers, and calls between tests. CALLS restoration
   is a separate dependency before this produces live Rust TESTS output.
3. **Call resolution and publication (F1/F2):** replay `encode_hex`, private free `clone`,
   and a positive local-call control through both pipeline modes. Preserve a resolved
   external target as terminal; admit internal targets only after binding identity and
   source-site agreement. Remove receiver-free suffix fallback for Rust methods. Restore
   verified positive relationships with the corresponding restore and coverage policies.
4. **Implementation identity (F3):** retain the trait's arguments and receiver identity
   through definition extraction, caller naming, registries and relationship linking.
   Check two same-named trait methods and distinct `From<T>` implementations on one type
   before enabling IMPLEMENTS/OVERRIDE publication.
5. **Crate and lexical identity (F4/F6):** derive per-member crate roots, module paths,
   renamed dependencies and import scope from actual workspace inputs. Verify alternate
   files with duplicate leaf names; compare full and edit-triggered incremental results.
6. **Wrapper inference (F5):** instrument the first type loss in the wrapped-trait corpus
   case, repair that transition, and assert the static trait-item target separately from
   possible concrete implementations. Do not infer concrete dispatch from a sole indexed
   implementation.
7. **Broader corpus and performance:** use a private copy/index, exact symbol/call-site
   expectations, full/incremental parity, trace/cycle/impact/test queries, and non-Rust
   controls. Measure warm no-change and representative edit updates with builds idle.
   Report unresolved classes; passing the selected examples is not complete Rust coverage.

Completed repair behavior (commit identities above):

- F4 syntax repair: `cbm_extract_rust_use_tree` is shared by import extraction and the
  local/cross-file Rust resolver. Exact import fixtures cover nested groups, aliases,
  grouped `self`, globs and trivia; resolver fixtures assert complete target identities.
  Lexical scopes, Cargo identity and semantic target joins remain open.
- F7 classification repair: exact supported attribute paths replace substring matching.
  Nested comments, raw identifiers, absolute paths and Rust whitespace are handled;
  documentation mentioning `tokio::test` does not mark a function as a test. This is
  source-attribute classification, not cfg evaluation or macro identity resolution.
- F7 publication repair: Rust TESTS sources require the compact `rust_test_attribute`
  property; arbitrary attributed names qualify and unannotated test-file helpers do not.
  A shared sequential/parallel serializer preserves that property even when a large
  documentation attribute makes the optional decorators array exceed its storage budget.
  CALLS restoration remains a prerequisite for live Rust TESTS output.
- Library-owner repair: HashMap and HashSet now retain their valid `std` owners in
  local and cross-file resolution; BTreeMap remains under `alloc`. This validates the
  tested constructor/method identities, not the entire hand-maintained stdlib seed.
- Semantic index versions 5/6/7 invalidate the preceding index at each independent fix,
  even when source bytes are unchanged. The migration regression uses the immediately
  preceding version.
- Independent source review found and rejected the initial `is_test`-only bypass, then
  found the lossy decorators-array case and the Unicode whitespace omission. The current
  implementation addresses all three. No blanket Rust suppression policy was removed.

Verification record for this batch:

- Diff-scoped clang-tidy passes using an isolated temporary Git index/object store;
  the user's real index is unchanged. Formatting and `git diff --check` pass.
- The import/attribute post-review canonical targeted command passed **1,251 tests** across
  `extraction,extraction_imports,rust_lsp,pipeline,parallel` under ASan/UBSan:
  `scripts/test.sh --suites extraction,extraction_imports,rust_lsp,pipeline,parallel`.
  It used the isolated `scripts/test-runtime.sh` runtime/cache wrapper; output is in
  `post-review-targeted.log`. The later collection-owner repair brings this to **1,252
  passing tests** on the final restored source (`collection-owner-final.log`).
- Fixture discriminators produced the expected exact-binding and exact-target failures:
  `Map` → `OtherMap` failed the local-name assertion, and replacing `Text::new()` with
  `Sequence::new()` failed the String target assertion. The original realistic test-helper
  discriminator also exposed the rejected F7 candidate (three TESTS edges instead of two).
- Final F7 fixture discriminators completed with **627 passed / 3 expected failures**:
  changing a documentation-only function to `#[test]` failed its false `is_test` oracle;
  removing one source's compact marker produced one TESTS edge instead of two; replacing
  the large-doc fixture's `#[test]` with `#[allow(dead_code)]` failed the stored-marker
  oracle. All original test-file hashes were restored afterward. Output is in
  `attribute-discriminators.log`. The subsequent five-suite run restored all 1,251 prior
  tests to green and failed only the newly added collection-owner regression.
- rustc 1.94.0, edition 2021 independently compiled and ran the import oracle. Its test
  harness listed exactly the two attributed functions in a trivia/Unicode/docs fixture;
  the unannotated helper and documentation-only function were not tests.
- Temporary commands, logs, compiler oracles and SHA-256 source hashes are under
  `/private/tmp/cbm-rust-repair-evidence/`. Durable compiler inputs/outcomes and regression
  oracles are retained in RUST_REPAIR_EVIDENCE.md. Packaging gate results are recorded
  above. No mutation sweep, installed-binary validation or comparable incremental
  performance measurement has been completed for this batch.

Next call-resolution diagnosis now has a more precise source boundary: both materializers
fall through after an LSP target is absent from the graph (`resolve_single_call` and
`resolve_file_calls`). Additionally, the embedded resolver's `lsp_short_name_unique`
fallback uses the project prefix as if it were a crate boundary, and trait UFCS currently
prefers a sole concrete implementation. An exact graph join alone would not validate
those upstream targets. The next repair must distinguish binding evidence from guesses
before restoring positive CALLS, with source-site and local/cross-file controls.

Additional source/compiler-confirmed finding: the stdlib table and prelude map name
HashMap `alloc.collections.HashMap`. With rustc 1.94.0, edition 2021, importing
`alloc::collections::HashMap` fails with E0432; `std::collections::HashMap` in the import
oracle compiles and runs. The bounded production correction is now applied: HashMap/HashSet type and method owners
use `std.collections`, the erroneous re-export aliases are removed, and BTreeMap retains
its valid `alloc.collections` owner. The exact local/cross-resolver regression failed
before this repair (`alloc.collections.HashMap.len` versus `std.collections.HashMap.len`),
with the other 1,251 targeted tests green. Its corrected-production discriminator replaced the `Map` import with BTreeMap and
failed exactly on `alloc.collections.BTreeMap.len` versus `std.collections.HashMap.len`
(523 other Rust resolver tests passed). After restoring the fixture, the canonical five
suites passed **1,252 tests** under ASan/UBSan. Independent scoped source review,
diff-scoped clang-tidy and `git diff --check` also pass. Logs are
`collection-owner-red.log`, `collection-owner-discriminator.log` and
`collection-owner-final.log`. The independent compiler oracle prints
`std::collections::hash::map::HashMap`, `std::collections::hash::set::HashSet` and
`alloc::collections::btree::map::BTreeMap` as their type owners. Correcting these owners
will not validate every seeded method: review identified additional API-validity leads
(`HashMap.raw_entry`, `core.slice.windows_mut`, `Iterator.tuple_windows`) for separate
compiler checks.
The same prelude table also contains names that ordinarily require imports and consults
prelude entries before the same-module fallback; membership and shadowing require their
own compiler/resolver discriminators under F4/F6. Do not change exact expected targets
to bless invalid identities. Probe sources and output are
currently under `/private/tmp/cbm-rust-repair-evidence/` and may expire.

## Current suppression and prior evidence

- `src/pipeline/lsp_resolve.h:124`, `cbm_pipeline_plain_call_admitted`, rejects every
  Rust plain CALLS edge. `pass_semantic.c:581` and `pass_parallel.c:3235` skip Rust impl
  linking. Restore filters also omit old Rust edges. These are containment measures,
  not the desired final behaviour; identify all publication/restore checks before repair.
- Prior work through `bbe753d3` records 8,068 tests passed / 10 skipped and 70/70 audit
  predicates passed. It proved selected false-edge removal and partial disclosures,
  not correct positive Rust coverage, performance, or the currently installed binary.
- Historical counts 747/3/1 became 0/0/0 in two private indexing passes. The account-default
  corpus database was deliberately left unchanged. Installation/update state is unverified.
- Retain the query fixes: `4f24b468` (WITH/search), `c8050c7e` (literal types),
  `372b2f94` (scalar equality), and `ca3df9eb` (cycle coverage disclosure).
- R4 lacks historical independent-verifier/staged-diff artifacts. R6 lacks the requested
  separate failing-test/report history for combined WITH/search work. These are historical
  evidence gaps, not newly demonstrated product defects. Use fresh evidence for new work;
  do not fabricate old results or reconstruct history as a prerequisite to causal repair.
- Prior reports: `/private/tmp/codebase-memory-rust-truth/reports/r7-audit-v2.md`,
  `r7-fixes-verifier-v2.md`, `r8-closeout-verifier.md`, and `r1-oracle.md`. Read technical
  evidence selectively; their zero-coverage acceptance and mandatory-ingestion prescriptions
  are superseded. Full old plan/ledger are in that directory's `archive/`.

## Working safeguards

- Preserve unrelated changes: at this update ISSUES.md and graph-ui/tsconfig.tsbuildinfo
  were already modified. This note update does not authorise editing or staging them.
- `/Users/viktor/Projects/github/codex/` remains source-read-only. Use disposable fixtures
  for compiler experiments; do not build, generate, cache, install, or clean in that tree.
- Keep project-management source and its default index unchanged. Use copies/private
  `CBM_CACHE_DIR` and `CBM_RUNTIME_DIR` for index experiments; do not stop a shared daemon.
- Keep one build-active lane, measure disk before substantial builds, and clean only
  exact owned artifacts after preserving decisive evidence. Recheck actual worktrees and
  processes; archived agent assignments and disk snapshots are not current state.
- Graph access recovered during this session. The original workspace graph was refreshed
  and checked for each relied-on path; partial TLS/atomic/macro ranges were read directly.
  The isolated packaging trees were not indexed and were verified from source. At the
  next session, refresh graph readiness/coverage for new navigation; do not infer that the
  installed tool or existing corpus index contains these uninstalled repairs.
- If sandbox ccache writes fail, use `CCACHE_DISABLE=1`. Listener-dependent test failures
  require an environment diagnosis. Use `scripts/test-runtime.sh` for isolated runtime/cache;
  its prior invocation is retained in `reports/r7-fixes-verifier-v2.md`.
- The repository's diff lint consumes staged changes. Verification must cover the actual
  changed range; the saved `briefs/staged-diff-gate-protocol.md` describes an alternate-index
  procedure. Inspect current repository gates before using historical commands.

## Record maintenance

Update findings in place with source/replay evidence and a proved/falsified status.
When a repair has regression coverage, replace its diagnostic directions with the commit
and test pointers. Archive superseded task state; do not append another execution diary.
